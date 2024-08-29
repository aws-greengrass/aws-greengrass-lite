// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/json_decode.h"
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Parses JSON using a parser-combinator strategy
// Parsers take a buffer, and if they match a prefix of the buffer, they consume
// that prefix and return true. Otherwise, they return false without modifying
// the buffer.
// Combinators generate a parser by combining other parsers.

typedef enum {
    JSON_TYPE_STR,
    JSON_TYPE_NUMBER,
    JSON_TYPE_OBJECT,
    JSON_TYPE_ARRAY,
    JSON_TYPE_TRUE,
    JSON_TYPE_FALSE,
    JSON_TYPE_NULL,
} JsonType;

typedef void (*ParseValueHandler)(
    void *ctx, JsonType type, GglBuffer content, size_t count
);

typedef struct {
    JsonType json_type;
    GglBuffer content;
    size_t count;
} ParseResult;

typedef struct {
    bool (*fn)(const void *parser_ctx, GglBuffer *buf, ParseResult *output);
    const void *parser_ctx;
} Parser;

typedef void (*ParseOutputHandler)(GglBuffer match, ParseResult *output);

static bool parser_call(
    const Parser *parser, GglBuffer *buf, ParseResult *output
) {
    return parser->fn(parser->parser_ctx, buf, output);
}

static bool comb_one_of_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const Parser *const *parsers = parser_ctx;

    while (*parsers != NULL) {
        if (parser_call(*parsers, buf, output)) {
            return true;
        }
        parsers = &parsers[1];
    }

    return false;
}

#define COMB_ONE_OF(...) \
    (Parser) { \
        .fn = comb_one_of_fn, \
        .parser_ctx = (const Parser *[]) { __VA_ARGS__, NULL }, \
    }

static bool comb_sequence_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const Parser *const *parsers = parser_ctx;
    GglBuffer buf_copy = *buf;

    while (*parsers != NULL) {
        if (!parser_call(*parsers, &buf_copy, output)) {
            return false;
        }
        parsers = &parsers[1];
    }

    *buf = buf_copy;
    return true;
}

#define COMB_SEQUENCE(...) \
    (Parser) { \
        .fn = comb_sequence_fn, \
        .parser_ctx = (const Parser *[]) { __VA_ARGS__, NULL }, \
    }

static bool comb_zero_or_more_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const Parser *parser = parser_ctx;
    while (parser_call(parser, buf, output)) { }
    return true;
}

#define COMB_ZERO_OR_MORE(parser) \
    (Parser) { \
        .fn = comb_zero_or_more_fn, .parser_ctx = (parser), \
    }

static bool comb_maybe_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const Parser *parser = parser_ctx;
    (void) parser_call(parser, buf, output);
    return true;
}

#define COMB_MAYBE(parser) \
    (Parser) { \
        .fn = comb_maybe_fn, .parser_ctx = (parser), \
    }

typedef struct {
    const Parser *parser;
    ParseOutputHandler callback;
} CombCallbackCtx;

static bool comb_nested_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const Parser *parser = parser_ctx;
    (void) output;
    return parser_call(parser, buf, NULL);
}

typedef struct {
    const Parser *parser;
    JsonType type;
} CombResultValCtx;

static bool comb_result_val_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const CombResultValCtx *ctx = parser_ctx;

    GglBuffer match = *buf;
    bool matches = parser_call(ctx->parser, buf, output);
    match.len = (size_t) (buf->data - match.data);

    if (matches && (output != NULL)) {
        output->json_type = ctx->type;
        output->content = match;
    }
    return matches;
}

#define COMB_RESULT_VAL(type, parser) \
    (Parser) { \
        .fn = comb_result_val_fn, \
        .parser_ctx = &(CombResultValCtx) { parser, type }, \
    }

/// Need to disable manipulating return val while in nested objects.
#define COMB_NESTED(parser) \
    (Parser) { \
        .fn = comb_nested_fn, .parser_ctx = (parser), \
    }

static bool parser_char_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const uint8_t *c = parser_ctx;
    (void) output;

    if (buf->len < 1) {
        return false;
    }
    if (buf->data[0] != *c) {
        return false;
    }
    *buf = ggl_buffer_substr(*buf, 1, SIZE_MAX);
    return true;
}

#define PARSER_CHAR(c) \
    (Parser) { \
        .fn = parser_char_fn, .parser_ctx = &(char) { c }, \
    }

static bool parser_str_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const GglBuffer *str = parser_ctx;
    (void) output;

    if (buf->len < str->len) {
        return false;
    }
    if (memcmp(str->data, buf->data, str->len) != 0) {
        return false;
    }
    *buf = ggl_buffer_substr(*buf, str->len, SIZE_MAX);
    return true;
}

#define PARSER_STR(str) \
    (Parser) { \
        .fn = parser_str_fn, .parser_ctx = &GGL_STR(str), \
    }

typedef struct {
    char start;
    char end;
} CharRange;

static bool parser_char_range_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const CharRange *range = parser_ctx;
    (void) output;

    if (buf->len < 1) {
        return false;
    }

    char c = (char) buf->data[0];

    if ((c < range->start) || (c > range->end)) {
        return false;
    }

    *buf = ggl_buffer_substr(*buf, 1, SIZE_MAX);
    return true;
}

#define PARSER_CHAR_RANGE(start, end) \
    (Parser) { \
        .fn = parser_char_range_fn, .parser_ctx = &(CharRange) { start, end }, \
    }

static const Parser PARSER_DIGIT = PARSER_CHAR_RANGE('0', '9');

static const Parser PARSER_HEX_DIGIT = COMB_ONE_OF(
    &PARSER_DIGIT, &PARSER_CHAR_RANGE('A', 'F'), &PARSER_CHAR_RANGE('a', 'f')
);

static const Parser PARSER_JSON_STR_ESCAPE = COMB_SEQUENCE(
    &PARSER_CHAR('\\'),
    &COMB_ONE_OF(
        &PARSER_CHAR('"'),
        &PARSER_CHAR('\\'),
        &PARSER_CHAR('/'),
        &PARSER_CHAR('b'),
        &PARSER_CHAR('f'),
        &PARSER_CHAR('n'),
        &PARSER_CHAR('r'),
        &PARSER_CHAR('t'),
        &COMB_SEQUENCE(
            &PARSER_CHAR('u'),
            &PARSER_HEX_DIGIT,
            &PARSER_HEX_DIGIT,
            &PARSER_HEX_DIGIT,
            &PARSER_HEX_DIGIT
        )
    )
);

static bool parser_json_str_codepoint_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    (void) parser_ctx;
    (void) output;

    if (buf->len < 1) {
        return false;
    }
    if (buf->data[0] <= 0x1F) {
        // control character
        return false;
    }
    if ((char) buf->data[0] == '"') {
        return false;
    }
    if ((char) buf->data[0] == '\\') {
        return false;
    }
    if ((buf->data[0] & 0b11000000) == 0b10000000) {
        // UTF-8 continuation byte
        return false;
    }

    size_t utf8_len = 0;
    if ((buf->data[0] & 0b10000000) == 0) {
        utf8_len = 1;
    } else if ((buf->data[0] & 0b11100000) == 0b11000000) {
        utf8_len = 2;
    } else if ((buf->data[0] & 0b11110000) == 0b11100000) {
        utf8_len = 3;
    } else if ((buf->data[0] & 0b11111000) == 0b11110000) {
        utf8_len = 4;
    } else {
        return false;
    }

    if (buf->len < utf8_len) {
        return false;
    }

    for (size_t i = 1; i < utf8_len; i++) {
        if ((buf->data[0] & 0b11000000) != 0b10000000) {
            // Not a UTF-8 continuation byte
            return false;
        }
    }

    *buf = ggl_buffer_substr(*buf, utf8_len, SIZE_MAX);
    return true;
}

static const Parser PARSER_JSON_STR_CODEPOINT = {
    .fn = parser_json_str_codepoint_fn,
};

static const Parser PARSER_JSON_WHITESPACE = COMB_ZERO_OR_MORE(&COMB_ONE_OF(
    &PARSER_CHAR(' '),
    &PARSER_CHAR('\n'),
    &PARSER_CHAR('\r'),
    &PARSER_CHAR('\t')
));

static const Parser PARSER_JSON_STR = COMB_SEQUENCE(
    &PARSER_CHAR('"'),
    &COMB_RESULT_VAL(
        JSON_TYPE_STR,
        &COMB_ZERO_OR_MORE(
            &COMB_ONE_OF(&PARSER_JSON_STR_CODEPOINT, &PARSER_JSON_STR_ESCAPE)
        )
    ),
    &PARSER_CHAR('"')
);

static const Parser PARSER_INT_PART = COMB_SEQUENCE(
    &COMB_MAYBE(&PARSER_CHAR('-')),
    &COMB_ONE_OF(
        &PARSER_CHAR('0'),
        &COMB_SEQUENCE(
            &PARSER_CHAR_RANGE('1', '9'), &COMB_ZERO_OR_MORE(&PARSER_DIGIT)
        )
    )
);

static const Parser PARSER_FRAC_PART
    = COMB_SEQUENCE(&PARSER_CHAR('.'), &COMB_ZERO_OR_MORE(&PARSER_DIGIT));

static const Parser PARSER_EXPONENT = COMB_SEQUENCE(
    &COMB_ONE_OF(&PARSER_CHAR('e'), &PARSER_CHAR('E')),
    &COMB_MAYBE(&COMB_ONE_OF(&PARSER_CHAR('+'), &PARSER_CHAR('-'))),
    &PARSER_DIGIT,
    &COMB_ZERO_OR_MORE(&PARSER_DIGIT)
);

static const Parser PARSER_JSON_NUMBER = COMB_RESULT_VAL(
    JSON_TYPE_NUMBER,
    &COMB_SEQUENCE(
        &PARSER_INT_PART,
        &COMB_MAYBE(&PARSER_FRAC_PART),
        &COMB_MAYBE(&PARSER_EXPONENT)
    )
);

static bool comb_increment_count_fn(
    const void *parser_ctx, GglBuffer *buf, ParseResult *output
) {
    const Parser *parser = parser_ctx;

    bool matches = parser_call(parser, buf, output);

    if (matches && (output != NULL)) {
        output->count += 1;
    }
    return matches;
}

#define COMB_INCREMENT_COUNT(parser) \
    (Parser) { \
        .fn = comb_increment_count_fn, .parser_ctx = (parser), \
    }

static const Parser PARSER_JSON_VALUE;

static const Parser PARSER_JSON_OBJECT_KV = COMB_NESTED(&COMB_SEQUENCE(
    &PARSER_JSON_STR,
    &PARSER_JSON_WHITESPACE,
    &PARSER_CHAR(':'),
    &PARSER_JSON_VALUE
));

static const Parser PARSER_JSON_OBJECT = COMB_SEQUENCE(
    &PARSER_CHAR('{'),
    &PARSER_JSON_WHITESPACE,
    &COMB_RESULT_VAL(
        JSON_TYPE_OBJECT,
        &COMB_MAYBE(&COMB_SEQUENCE(
            &COMB_ZERO_OR_MORE(&COMB_INCREMENT_COUNT(&COMB_SEQUENCE(
                &PARSER_JSON_OBJECT_KV,
                &PARSER_CHAR(','),
                &PARSER_JSON_WHITESPACE
            ))),
            &COMB_INCREMENT_COUNT(&PARSER_JSON_OBJECT_KV)
        ))
    ),
    &PARSER_CHAR('}')
);

static const Parser PARSER_JSON_ARRAY_ELEM = COMB_NESTED(&PARSER_JSON_VALUE);

static const Parser PARSER_JSON_ARRAY = COMB_SEQUENCE(
    &PARSER_CHAR('['),
    &PARSER_JSON_WHITESPACE,
    &COMB_RESULT_VAL(
        JSON_TYPE_ARRAY,
        &COMB_MAYBE(&COMB_SEQUENCE(
            &COMB_ZERO_OR_MORE(&COMB_INCREMENT_COUNT(
                &COMB_SEQUENCE(&PARSER_JSON_ARRAY_ELEM, &PARSER_CHAR(','))
            )),
            &COMB_INCREMENT_COUNT(&PARSER_JSON_ARRAY_ELEM)
        ))
    ),
    &PARSER_CHAR(']')
);

static const Parser PARSER_JSON_TRUE
    = COMB_RESULT_VAL(JSON_TYPE_TRUE, &PARSER_STR("true"));

static const Parser PARSER_JSON_FALSE
    = COMB_RESULT_VAL(JSON_TYPE_FALSE, &PARSER_STR("false"));

static const Parser PARSER_JSON_NULL
    = COMB_RESULT_VAL(JSON_TYPE_NULL, &PARSER_STR("null"));

static const Parser PARSER_JSON_VALUE = COMB_SEQUENCE(
    &PARSER_JSON_WHITESPACE,
    &COMB_ONE_OF(
        &PARSER_JSON_STR,
        &PARSER_JSON_NUMBER,
        &PARSER_JSON_OBJECT,
        &PARSER_JSON_ARRAY,
        &PARSER_JSON_TRUE,
        &PARSER_JSON_FALSE,
        &PARSER_JSON_NULL
    ),
    &PARSER_JSON_WHITESPACE
);

static bool hex_char_to_byte(uint8_t *c) {
    if ((*c >= '0') && (*c <= '9')) {
        *c -= '0';
        return true;
    }
    if ((*c >= 'A') && (*c <= 'F')) {
        *c = (uint8_t) (*c - 'A' + 10);
        return true;
    }
    if ((*c >= 'a') && (*c <= 'f')) {
        *c = (uint8_t) (*c - 'a' + 10);
        return true;
    }
    return false;
}

static bool get_uint16_from_hex4(uint8_t *hex_bytes, uint16_t *out) {
    uint8_t bytes[4];
    memcpy(bytes, hex_bytes, 4);
    for (size_t i = 0; i < 4; i++) {
        bool ret = hex_char_to_byte(&bytes[i]);
        if (!ret) {
            return false;
        }
    }

    // unsigned to avoid int promotion
    *out = (uint16_t) (((unsigned) bytes[0] << 12) & ((unsigned) bytes[1] << 8)
                       & ((unsigned) bytes[2] << 4) & ((unsigned) bytes[3]));
    return true;
}

static bool write_codepoint_utf8(uint32_t code_point, uint8_t **write_ptr) {
    uint8_t buf[4] = { 0 };

    if (code_point <= 0x7F) {
        **write_ptr = (uint8_t) code_point;
        *write_ptr = &(*write_ptr)[1];
        return true;
    }
    if (code_point <= 0x7FF) {
        buf[0] = 0b11000000 + (uint8_t) (code_point >> 6);
        buf[1] = 0b10000000 + (uint8_t) (code_point & 0b00111111);
        memcpy(*write_ptr, buf, 2);
        *write_ptr = &(*write_ptr)[2];
        return true;
    }
    if (code_point <= 0xFFFF) {
        buf[0] = 0b11100000 + (uint8_t) (code_point >> 12);
        buf[1] = 0b10000000 + (uint8_t) ((code_point >> 6) & 0b00111111);
        buf[2] = 0b10000000 + (uint8_t) (code_point & 0b00111111);
        memcpy(*write_ptr, buf, 3);
        *write_ptr = &(*write_ptr)[3];
        return true;
    }
    if (code_point <= 0x1FFFFF) {
        buf[0] = 0b11110000 + (uint8_t) (code_point >> 18);
        buf[1] = 0b10000000 + (uint8_t) ((code_point >> 12) & 0b00111111);
        buf[2] = 0b10000000 + (uint8_t) ((code_point >> 6) & 0b00111111);
        buf[3] = 0b10000000 + (uint8_t) (code_point & 0b00111111);
        memcpy(*write_ptr, buf, 4);
        *write_ptr = &(*write_ptr)[4];
        return true;
    }
    return false;
}

static bool str_conv_handle_utf16_escape(GglBuffer *buf, uint8_t **write_ptr) {
    if ((buf->len < 6) || (buf->data[0] != '\\') || (buf->data[1] != 'u')) {
        return false;
    }

    uint16_t code_value;
    bool ret = get_uint16_from_hex4(&(*write_ptr)[2], &code_value);
    if (!ret) {
        return false;
    }

    if ((code_value >= 0xD800) && (code_value <= 0xDBFF)) {
        // high surrogates
        if ((buf->len < 12) || (buf->data[6] != '\\')
            || (buf->data[7] != 'u')) {
            return false;
        }
        uint16_t low_surrogate;
        ret = get_uint16_from_hex4(&(*write_ptr)[8], &low_surrogate);
        if (!ret || (low_surrogate < 0xDC00) || (low_surrogate > 0xDFFF)) {
            return false;
        }

        uint32_t code_point = ((((uint32_t) code_value - 0xD800) << 10)
                               + (low_surrogate - 0xDC00))
            + 0x10000;

        return write_codepoint_utf8(code_point, write_ptr);
    }

    if ((code_value >= 0xDC00) && (code_value <= 0xDFFF)) {
        // low surrogates
        return false;
    }

    return write_codepoint_utf8(code_value, write_ptr);
}

static bool str_conv_handle_escape(GglBuffer *buf, uint8_t **write_ptr) {
    if ((buf->len < 2) || (buf->data[0] != '\\')) {
        return false;
    }
    if (buf->data[1] == 'u') {
        return str_conv_handle_utf16_escape(buf, write_ptr);
    }

    uint8_t c;
    switch ((char) buf->data[1]) {
    case '"':
    case '\\':
    case '/':
        c = buf->data[1];
        break;
    case 'b':
        c = '\b';
        break;
    case 'f':
        c = '\f';
        break;
    case 'n':
        c = '\n';
        break;
    case 'r':
        c = '\r';
        break;
    case 't':
        c = '\t';
        break;
    default:
        return false;
    }

    **write_ptr = c;
    *write_ptr = &(*write_ptr)[1];
    *buf = ggl_buffer_substr(*buf, 2, SIZE_MAX);
    return true;
}

static bool unescape_string(GglBuffer *str) {
    uint8_t *write_ptr = str->data;
    GglBuffer buf = *str;
    while (buf.len > 0) {
        if (buf.data[0] == '\\') {
            bool ret = str_conv_handle_escape(&buf, &write_ptr);
            if (!ret) {
                return false;
            }
        } else {
            *write_ptr = buf.data[0];
            write_ptr = &write_ptr[1];
            buf = ggl_buffer_substr(buf, 1, SIZE_MAX);
        }
    }
    str->len = (size_t) (write_ptr - str->data);
    return true;
}

static GglError decode_json_str(GglBuffer content, GglObject *obj) {
    GglBuffer str = content;
    bool ret = unescape_string(&str);
    if (!ret) {
        GGL_LOGE("json", "Error decoding JSON string.");
        return GGL_ERR_PARSE;
    }
    *obj = GGL_OBJ(str);
    return GGL_ERR_OK;
}

static GglError decode_json_number(GglBuffer content, GglObject *obj) {
    GglBuffer buf = content;

    bool result = parser_call(&PARSER_INT_PART, &buf, NULL);
    if (!result) {
        GGL_LOGE("json", "Failed to parse JSON number.");
        return GGL_ERR_PARSE;
    }

    bool has_frac_part = parser_call(&PARSER_FRAC_PART, &buf, NULL);
    bool has_exp_part = parser_call(&PARSER_EXPONENT, &buf, NULL);

    if (!has_frac_part && !has_exp_part) {
        int64_t val;
        GglError parse_ret = ggl_str_to_int64(content, &val);
        if (parse_ret != GGL_ERR_OK) {
            GGL_LOGE("json", "JSON integer out of range of int64_t.");
            return parse_ret;
        }
        *obj = GGL_OBJ_I64(val);
        return GGL_ERR_OK;
    }

    errno = 0;
    double val = strtod((char *) content.data, NULL);
    if (errno == ERANGE) {
        GGL_LOGE("json", "JSON float out of range of double.");
        return GGL_ERR_RANGE;
    }
    *obj = GGL_OBJ_F64(val);
    return GGL_ERR_OK;
}

static GglError take_json_val(GglBuffer *buf, GglAlloc *alloc, GglObject *obj);

// NOLINTNEXTLINE(misc-no-recursion)
static GglError decode_json_array(
    GglBuffer content, size_t count, GglAlloc *alloc, GglObject *obj
) {
    GglObject *items = GGL_ALLOCN(alloc, GglObject, count);
    if (items == NULL) {
        GGL_LOGE("json", "Insufficent memory to decode JSON.");
        return GGL_ERR_NOMEM;
    }

    GglBuffer buf_copy = content;

    for (size_t i = 0; i < count; i++) {
        GglError ret = take_json_val(&buf_copy, alloc, &items[i]);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (i != count - 1) {
            bool matches = parser_call(&PARSER_CHAR(','), &buf_copy, NULL);
            if (!matches) {
                GGL_LOGE("json", "Failed to match comma while decoding array.");
                return GGL_ERR_PARSE;
            }
        }
    }

    *obj = GGL_OBJ((GglList) { .items = items, .len = count });
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError decode_json_object(
    GglBuffer content, size_t count, GglAlloc *alloc, GglObject *obj
) {
    GglKV *pairs = GGL_ALLOCN(alloc, GglKV, count);
    if (pairs == NULL) {
        GGL_LOGE("json", "Insufficent memory to decode JSON.");
        return GGL_ERR_NOMEM;
    }

    GglBuffer buf_copy = content;

    for (size_t i = 0; i < count; i++) {
        GglObject key_obj;
        GglError ret = take_json_val(&buf_copy, alloc, &key_obj);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (key_obj.type != GGL_TYPE_BUF) {
            GGL_LOGE("json", "Non-string key type when decoding object.");
            return GGL_ERR_PARSE;
        }
        pairs[i].key = key_obj.buf;

        bool matches = parser_call(&PARSER_CHAR(':'), &buf_copy, NULL);
        if (!matches) {
            GGL_LOGE("json", "Failed to match comma while decoding object.");
            return GGL_ERR_PARSE;
        }

        ret = take_json_val(&buf_copy, alloc, &pairs[i].val);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (i != count - 1) {
            matches = parser_call(&PARSER_CHAR(','), &buf_copy, NULL);
            if (!matches) {
                GGL_LOGE(
                    "json", "Failed to match comma while decoding object."
                );
                return GGL_ERR_PARSE;
            }
        }
    }

    *obj = GGL_OBJ((GglMap) { .pairs = pairs, .len = count });
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError take_json_val(GglBuffer *buf, GglAlloc *alloc, GglObject *obj) {
    ParseResult output = { 0 };
    bool matches = parser_call(&PARSER_JSON_VALUE, buf, &output);
    if (!matches) {
        GGL_LOGE("json", "Failed to parse buffer.");
        return GGL_ERR_PARSE;
    }

    switch (output.json_type) {
    case JSON_TYPE_STR:
        return decode_json_str(output.content, obj);
    case JSON_TYPE_NUMBER:
        return decode_json_number(output.content, obj);
    case JSON_TYPE_TRUE:
        *obj = GGL_OBJ_BOOL(true);
        return GGL_ERR_OK;
    case JSON_TYPE_FALSE:
        *obj = GGL_OBJ_BOOL(false);
        return GGL_ERR_OK;
    case JSON_TYPE_NULL:
        *obj = GGL_OBJ_NULL();
        return GGL_ERR_OK;
    case JSON_TYPE_ARRAY:
        return decode_json_array(output.content, output.count, alloc, obj);
    case JSON_TYPE_OBJECT:
        return decode_json_object(output.content, output.count, alloc, obj);
    }

    assert(false);
    return GGL_ERR_FAILURE;
}

GglError ggl_json_decode_destructive(
    GglBuffer buf, GglAlloc *alloc, GglObject *obj
) {
    GglBuffer buf_copy = buf;

    GglError ret = take_json_val(&buf_copy, alloc, obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (buf_copy.len > 0) {
        GGL_LOGE("json", "Trailing buffer content when decoding.");
        return GGL_ERR_PARSE;
    }

    return GGL_ERR_OK;
}
