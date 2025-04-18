#include "ggl/uri.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <uriparser/Uri.h>
#include <uriparser/UriBase.h>
#include <stdalign.h>
#include <stdint.h>

static GglBuffer buffer_from_text_range(UriTextRangeA range) {
    return (GglBuffer) { .data = (uint8_t *) range.first,
                         .len = (size_t) (range.afterLast - range.first) };
}

static GglBuffer buffer_from_linked_list(
    UriPathSegmentA *head, UriPathSegmentA *tail
) {
    if (head == NULL) {
        return GGL_STR("");
    }
    return (GglBuffer) { .data = (uint8_t *) head->text.first,
                         .len
                         = (size_t) (tail->text.afterLast - head->text.first) };
}

static void *ggl_uri_malloc(struct UriMemoryManagerStruct *mem, size_t size) {
    return ggl_arena_alloc((GglArena *) mem->userData, size, alignof(size_t));
}

static void *ggl_uri_calloc(
    struct UriMemoryManagerStruct *mem, size_t count, size_t size_per_element
) {
    void *block = ggl_uri_malloc(mem, count * size_per_element);
    if (block != NULL) {
        memset(block, 0, count * size_per_element);
    }
    return block;
}

static void *ggl_uri_realloc(
    struct UriMemoryManagerStruct *mem, void *block, size_t size
) {
    (void) mem;
    (void) block;
    (void) size;
    return NULL;
}

static void *ggl_uri_reallocarray(
    struct UriMemoryManagerStruct *mem,
    void *block,
    size_t count,
    size_t size_per_element
) {
    (void) mem;
    (void) block;
    (void) count;
    (void) size_per_element;
    return NULL;
}

static void ggl_uri_free(struct UriMemoryManagerStruct *mem, void *block) {
    (void) mem;
    (void) block;
}

static GglError convert_uriparser_error(int error) {
    switch (error) {
    case URI_SUCCESS:
        return GGL_ERR_OK;
    case URI_ERROR_SYNTAX:
        return GGL_ERR_PARSE;
    case URI_ERROR_NULL:
        return GGL_ERR_INVALID;
    case URI_ERROR_MALLOC:
        return GGL_ERR_NOMEM;
    default:
        return GGL_ERR_FAILURE;
    }
}

GglError gg_uri_parse(GglArena *arena, GglBuffer uri, GglUriInfo *info) {
    UriUriA result;

    // TODO: can we patch uriparser to not need to allocate memory for a linked
    // list?
    UriMemoryManager mem = { .calloc = ggl_uri_calloc,
                             .realloc = ggl_uri_realloc,
                             .reallocarray = ggl_uri_reallocarray,
                             .malloc = ggl_uri_malloc,
                             .free = ggl_uri_free,
                             .userData = arena };
    const char *error_pos = NULL;

    int uri_error = uriParseSingleUriExMmA(
        &result,
        (const char *) uri.data,
        (const char *) (&uri.data[uri.len]),
        &error_pos,
        &mem
    );
    if (uri_error != URI_SUCCESS) {
        GGL_LOGE("Failed to parse URI");
        return convert_uriparser_error(uri_error);
    }

    info->scheme = buffer_from_text_range(result.scheme);
    info->userinfo = buffer_from_text_range(result.userInfo);
    info->host = buffer_from_text_range(result.hostText);
    info->port = buffer_from_text_range(result.portText);
    info->path = buffer_from_linked_list(result.pathHead, result.pathTail);
    if (result.pathTail != NULL) {
        info->file = buffer_from_text_range(result.pathTail->text);
    } else {
        info->file = GGL_STR("");
    }
    uriFreeUriMembersMmA(&result, &mem);

    if (info->scheme.len > 0) {
        GGL_LOGD("Scheme: %.*s", (int) info->scheme.len, info->scheme.data);
    }
    if (info->userinfo.len > 0) {
        GGL_LOGD("UserInfo: Present");
    }
    if (info->host.len > 0) {
        GGL_LOGD("Host: %.*s", (int) info->host.len, info->host.data);
    }
    if (info->port.len > 0) {
        GGL_LOGD("Port: %.*s", (int) info->port.len, info->port.data);
    }
    if (info->path.len > 0) {
        GGL_LOGD("Path: %.*s", (int) info->path.len, info->path.data);
    }

    return GGL_ERR_OK;
}
