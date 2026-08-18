// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Thompson NFA regex engine for platform attribute matching.
// This engine uses only a caller-provided arena and is fully reentrant.
//
// DESIGN: Two-phase compile+simulate.
//   Phase 1: Recursive-descent parser compiles the pattern into a bytecode
//            program stored in an arena-allocated array.
//   Phase 2: Classic two-list NFA simulation (Thompson 1968) executes the
//            program against the subject string. Time is O(pattern * subject)
//            with NO backtracking, making catastrophic backtracking
//            structurally impossible.
//
// MATCHING SEMANTICS: Always whole-string. The simulation accepts only if
// the MATCH instruction is reached after consuming the entire subject.
//
// FAIL-CLOSED: Any compilation error (pattern too long, program overflow,
// nesting overflow, parse error) returns the appropriate GgError.
// A malformed pattern must NEVER cause a manifest to be selected.
// ============================================================================

/// Maximum pattern interior length.
#define GGL_REGEX_MAX_PATTERN 256
/// Maximum number of NFA instructions in the compiled program.
#define GGL_REGEX_MAX_PROG 512
/// Maximum nesting depth for parentheses during parsing.
#define GGL_REGEX_MAX_NESTING 16

// --- NFA instruction set ---

enum NfaOp {
    NFA_CHAR, // Match one literal byte. arg1 = the byte.
    NFA_ANY, // Match any single byte (dot).
    NFA_CLASS, // Match a bracket expression. arg1 = offset into pattern,
               //   arg2 = length of bracket content, arg3 = negated flag.
    NFA_SPLIT, // Epsilon split. arg1 = index A, arg2 = index B.
    NFA_JMP, // Epsilon jump. arg1 = target index.
    NFA_MATCH // Successful match.
};

struct NfaInst {
    enum NfaOp op;
    uint16_t arg1;
    uint16_t arg2;
    uint16_t arg3;
};

// --- Compiler state (arena-backed, passed by pointer) ---
//
// Arena footprint per call.  Breakdown:
//   NfaInst prog[512] * 12 bytes           = 6144 B
//   nfa_simulate: 4 arrays of uint16_t[512] = 4096 B
//   Total                                   = 10240 B (+ alignment padding)
// This is allocated from the caller-provided arena.  No stack arrays are used
// for the large buffers, keeping per-call stack use minimal.

struct NfaCompiler {
    const uint8_t *pat; // Pattern bytes (bare, no surrounding slashes).
    size_t pat_len; // Length of pattern.
    size_t pos; // Current parse position.
    struct NfaInst *prog; // Arena-allocated program array.
    uint16_t prog_len; // Number of instructions emitted.
    int depth; // Current parenthesis nesting depth.
    bool error; // Set on any parse/compile error.
    bool range_error; // Set when nesting cap exceeded (=> GG_ERR_RANGE).
};

static bool nfa_emit(struct NfaCompiler *c, struct NfaInst inst) {
    if (c->prog_len >= GGL_REGEX_MAX_PROG) {
        c->error = true;
        return false;
    }
    c->prog[c->prog_len++] = inst;
    return true;
}

// Forward declarations for recursive-descent parser.
static uint16_t nfa_parse_alt(struct NfaCompiler *c);
static uint16_t nfa_parse_concat(struct NfaCompiler *c);
static uint16_t nfa_parse_repeat(struct NfaCompiler *c);
static uint16_t nfa_parse_atom(struct NfaCompiler *c);

// Helper: shift instructions [from, prog_len) up by 1 and fix internal targets.
static bool nfa_shift_up(struct NfaCompiler *c, uint16_t from) {
    if (c->prog_len + 1 >= GGL_REGEX_MAX_PROG) {
        c->error = true;
        return false;
    }
    for (uint16_t i = c->prog_len; i > from; i--) {
        c->prog[i] = c->prog[i - 1];
    }
    c->prog_len++;
    // Fix SPLIT/JMP targets that reference positions >= from.
    for (uint16_t i = (uint16_t) (from + 1); i < c->prog_len; i++) {
        if (c->prog[i].op == NFA_SPLIT) {
            if (c->prog[i].arg1 >= from) {
                c->prog[i].arg1++;
            }
            if (c->prog[i].arg2 >= from) {
                c->prog[i].arg2++;
            }
        } else if (c->prog[i].op == NFA_JMP) {
            if (c->prog[i].arg1 >= from) {
                c->prog[i].arg1++;
            }
        }
    }
    return true;
}

/// Parse alternation: concat ('|' concat)*
// NOLINTNEXTLINE(misc-no-recursion)
static uint16_t nfa_parse_alt(struct NfaCompiler *c) {
    uint16_t start = nfa_parse_concat(c);
    if (c->error) {
        return start;
    }
    while (c->pos < c->pat_len && c->pat[c->pos] == '|') {
        c->pos++; // consume '|'
        // Insert SPLIT before first branch (at 'start'), shift existing code.
        if (!nfa_shift_up(c, start)) {
            return start;
        }
        // Emit JMP at end of first branch (will jump past second branch).
        uint16_t jmp_slot = c->prog_len;
        struct NfaInst jmp_inst;
        jmp_inst.op = NFA_JMP;
        jmp_inst.arg1 = 0;
        jmp_inst.arg2 = 0;
        jmp_inst.arg3 = 0;
        nfa_emit(c, jmp_inst);
        if (c->error) {
            return start;
        }
        uint16_t branch2_start = c->prog_len;
        nfa_parse_concat(c);
        if (c->error) {
            return start;
        }
        // Patch SPLIT at 'start': arg1 -> first branch (start+1),
        //                          arg2 -> second branch.
        c->prog[start].op = NFA_SPLIT;
        c->prog[start].arg1 = (uint16_t) (start + 1);
        c->prog[start].arg2 = branch2_start;
        c->prog[start].arg3 = 0;
        // Patch JMP to jump past second branch.
        c->prog[jmp_slot].arg1 = c->prog_len;
    }
    return start;
}

/// Parse concatenation: repeat*
// NOLINTNEXTLINE(misc-no-recursion)
static uint16_t nfa_parse_concat(struct NfaCompiler *c) {
    uint16_t start = c->prog_len;
    while (c->pos < c->pat_len) {
        uint8_t ch = c->pat[c->pos];
        // Stop at alternation or close-paren.
        if (ch == '|' || ch == ')') {
            break;
        }
        nfa_parse_repeat(c);
        if (c->error) {
            return start;
        }
    }
    return start;
}

/// Parse repeat: atom ('*' | '+' | '?')?
// NOLINTNEXTLINE(misc-no-recursion)
static uint16_t nfa_parse_repeat(struct NfaCompiler *c) {
    uint16_t atom_start = c->prog_len;
    nfa_parse_atom(c);
    if (c->error) {
        return atom_start;
    }
    if (c->pos >= c->pat_len) {
        return atom_start;
    }
    uint8_t quant = c->pat[c->pos];
    if (quant != '*' && quant != '+' && quant != '?') {
        return atom_start;
    }
    c->pos++; // consume quantifier

    if (quant == '?') {
        // a? => SPLIT(atom_start+1, after_atom) then atom
        if (!nfa_shift_up(c, atom_start)) {
            return atom_start;
        }
        c->prog[atom_start].op = NFA_SPLIT;
        c->prog[atom_start].arg1 = (uint16_t) (atom_start + 1);
        c->prog[atom_start].arg2 = c->prog_len;
        c->prog[atom_start].arg3 = 0;
    } else if (quant == '*') {
        // a* => SPLIT(atom_start+1, after_jmp) then atom then JMP(split)
        if (!nfa_shift_up(c, atom_start)) {
            return atom_start;
        }
        struct NfaInst jmp_inst;
        jmp_inst.op = NFA_JMP;
        jmp_inst.arg1 = atom_start;
        jmp_inst.arg2 = 0;
        jmp_inst.arg3 = 0;
        nfa_emit(c, jmp_inst);
        if (c->error) {
            return atom_start;
        }
        c->prog[atom_start].op = NFA_SPLIT;
        c->prog[atom_start].arg1 = (uint16_t) (atom_start + 1);
        c->prog[atom_start].arg2 = c->prog_len;
        c->prog[atom_start].arg3 = 0;
    } else if (quant == '+') {
        // a+ => atom then SPLIT(atom_start, after_split)
        struct NfaInst split_inst;
        split_inst.op = NFA_SPLIT;
        split_inst.arg1 = atom_start;
        split_inst.arg2 = (uint16_t) (c->prog_len + 1);
        split_inst.arg3 = 0;
        nfa_emit(c, split_inst);
        if (c->error) {
            return atom_start;
        }
    }
    return atom_start;
}

/// Parse atom: '(' alt ')' | '[' bracket ']' | '.' | '\\' char | literal
// NOLINTNEXTLINE(misc-no-recursion,readability-function-cognitive-complexity)
static uint16_t nfa_parse_atom(struct NfaCompiler *c) {
    uint16_t start = c->prog_len;
    if (c->pos >= c->pat_len) {
        c->error = true;
        return start;
    }
    uint8_t ch = c->pat[c->pos];

    if (ch == '(') {
        c->pos++;
        c->depth++;
        if (c->depth > GGL_REGEX_MAX_NESTING) {
            // Nesting too deep - fail closed.
            c->error = true;
            c->range_error = true;
            return start;
        }
        nfa_parse_alt(c);
        if (c->error) {
            return start;
        }
        if (c->pos >= c->pat_len || c->pat[c->pos] != ')') {
            // Unbalanced parenthesis - fail closed.
            c->error = true;
            return start;
        }
        c->pos++; // consume ')'
        c->depth--;
    } else if (ch == '[') {
        // Bracket expression: store offset+length into pattern.
        c->pos++; // consume '['
        uint16_t bracket_start = (uint16_t) c->pos;
        uint16_t negated = 0;
        if (c->pos < c->pat_len && c->pat[c->pos] == '^') {
            negated = 1;
            c->pos++;
        }
        // A ']' immediately after '[' or '[^' is treated as a literal.
        if (c->pos < c->pat_len && c->pat[c->pos] == ']') {
            c->pos++;
        }
        while (c->pos < c->pat_len && c->pat[c->pos] != ']') {
            c->pos++;
        }
        if (c->pos >= c->pat_len) {
            // Unterminated bracket expression - fail closed.
            c->error = true;
            return start;
        }
        uint16_t bracket_len = (uint16_t) (c->pos - bracket_start);
        c->pos++; // consume ']'
        struct NfaInst class_inst;
        class_inst.op = NFA_CLASS;
        class_inst.arg1 = bracket_start;
        class_inst.arg2 = bracket_len;
        class_inst.arg3 = negated;
        nfa_emit(c, class_inst);
    } else if (ch == '.') {
        c->pos++;
        struct NfaInst any_inst;
        any_inst.op = NFA_ANY;
        any_inst.arg1 = 0;
        any_inst.arg2 = 0;
        any_inst.arg3 = 0;
        nfa_emit(c, any_inst);
    } else if (ch == '\\') {
        c->pos++;
        if (c->pos >= c->pat_len) {
            // Trailing backslash - fail closed.
            c->error = true;
            return start;
        }
        uint8_t escaped = c->pat[c->pos];
        // Reject escapes of alphanumeric characters. Java assigns special
        // meaning to \d, \w, \s, etc., which this engine does not support.
        // Silently emitting the bare letter would change the pattern's
        // semantics. Non-alphanumeric escapes (\., \(, \{, etc.) are fine.
        if ((escaped >= 'A' && escaped <= 'Z')
            || (escaped >= 'a' && escaped <= 'z')
            || (escaped >= '0' && escaped <= '9')) {
            c->error = true;
            return start;
        }
        c->pos++;
        struct NfaInst char_inst;
        char_inst.op = NFA_CHAR;
        char_inst.arg1 = escaped;
        char_inst.arg2 = 0;
        char_inst.arg3 = 0;
        nfa_emit(c, char_inst);
    } else if (ch == '^') {
        // '^' at position 0 in pattern is a redundant anchor (whole-string
        // match is already inherent). Elsewhere treat as literal.
        if (c->pos == 0) {
            c->pos++; // skip redundant anchor
            // If caret IS the entire pattern, compile as empty (matches
            // only the empty string), matching Java nucleus behavior.
            if (c->pos >= c->pat_len) {
                return start;
            }
            return nfa_parse_atom(c); // parse the real first atom
        }
        c->pos++;
        struct NfaInst char_inst;
        char_inst.op = NFA_CHAR;
        char_inst.arg1 = ch;
        char_inst.arg2 = 0;
        char_inst.arg3 = 0;
        nfa_emit(c, char_inst);
    } else if (ch == '$') {
        // '$' at the last position in pattern is a redundant anchor.
        // Elsewhere treat as literal.
        if (c->pos == c->pat_len - 1) {
            c->pos++; // skip redundant anchor
            return start;
        }
        c->pos++;
        struct NfaInst char_inst;
        char_inst.op = NFA_CHAR;
        char_inst.arg1 = ch;
        char_inst.arg2 = 0;
        char_inst.arg3 = 0;
        nfa_emit(c, char_inst);
    } else if (ch == '*' || ch == '+' || ch == '?' || ch == '{') {
        // Quantifier with nothing to repeat - fail closed.
        // '{' is included: Java treats {n,m} as a counted quantifier, but
        // this engine has no repetition-count support, so the pattern would
        // silently change meaning. Fail closed.
        // Note: an unmatched '}' is left as a literal because Java also treats
        // an unmatched '}' as a literal, so there is no divergence.
        c->error = true;
        return start;
    } else {
        // Literal byte.
        c->pos++;
        struct NfaInst char_inst;
        char_inst.op = NFA_CHAR;
        char_inst.arg1 = ch;
        char_inst.arg2 = 0;
        char_inst.arg3 = 0;
        nfa_emit(c, char_inst);
    }
    return start;
}

// --- Bracket expression matching helper ---

/// Check if byte 'ch' matches the bracket expression content at
/// pat[offset..offset+len]. The 'negated' flag inverts the result.
static bool nfa_class_match(
    const uint8_t *pat,
    uint16_t offset,
    uint16_t len,
    uint16_t negated,
    uint8_t ch
) {
    bool matched = false;
    uint16_t i = offset;
    uint16_t end = (uint16_t) (offset + len);
    // Skip '^' if present (already accounted for in negated flag).
    if (i < end && pat[i] == '^') {
        i++;
    }
    while (i < end) {
        uint8_t lo = pat[i];
        // Check for range: lo-hi
        if (i + 2 < end && pat[i + 1] == '-') {
            uint8_t hi = pat[i + 2];
            if (ch >= lo && ch <= hi) {
                matched = true;
                break;
            }
            i += 3;
        } else {
            if (ch == lo) {
                matched = true;
                break;
            }
            i++;
        }
    }
    return negated ? !matched : matched;
}

// --- NFA simulation (two-list Thompson algorithm) ---

/// Simulate the compiled NFA against subject. Returns true on full-string
/// match. Uses generation stamps to avoid adding duplicates to a list.
/// Epsilon closure is computed iteratively using a bounded stack.
static bool nfa_simulate(
    const struct NfaInst *prog,
    uint16_t prog_len,
    const uint8_t *pat, // original pattern for CLASS offset lookups
    const uint8_t *subject,
    size_t subject_len,
    uint16_t *list_a, // arena-allocated simulation arrays
    uint16_t *list_b,
    uint16_t *gen,
    uint16_t *estack
) {
    uint16_t *clist = list_a;
    uint16_t *nlist = list_b;
    uint16_t clen = 0;
    uint16_t nlen = 0;
    uint16_t current_gen = 1;

    // Zero out generation stamps.
    for (uint16_t i = 0; i < prog_len; i++) {
        gen[i] = 0;
    }

// Macro: add a state plus its epsilon closure to a list (iterative).
#define NFA_ADD_STATE(list, list_len, state_idx, cur_gen) \
    do { \
        uint16_t _es_top = 0; \
        estack[_es_top++] = (state_idx); \
        while (_es_top > 0) { \
            uint16_t _s = estack[--_es_top]; \
            if (_s >= prog_len || gen[_s] == (cur_gen)) { \
                continue; \
            } \
            gen[_s] = (cur_gen); \
            if (prog[_s].op == NFA_SPLIT) { \
                /* Defense in depth: overflow is believed structurally */ \
                /* unreachable (compiler cannot emit enough chained */ \
                /* epsilon states), but guard locally to enforce the */ \
                /* invariant without a whole-program argument. */ \
                if (_es_top < GGL_REGEX_MAX_PROG) { \
                    estack[_es_top++] = prog[_s].arg1; \
                } \
                if (_es_top < GGL_REGEX_MAX_PROG) { \
                    estack[_es_top++] = prog[_s].arg2; \
                } \
            } else if (prog[_s].op == NFA_JMP) { \
                if (_es_top < GGL_REGEX_MAX_PROG) { \
                    estack[_es_top++] = prog[_s].arg1; \
                } \
            } else { \
                (list)[(list_len)++] = _s; \
            } \
        } \
    } while (0)

    // Seed the current list with epsilon closure of state 0.
    NFA_ADD_STATE(clist, clen, 0, current_gen);

    // Process each byte of the subject.
    for (size_t si = 0; si < subject_len; si++) {
        uint8_t byte = subject[si];
        nlen = 0;
        current_gen++;
        // Handle generation counter wrap-around (16-bit).
        if (current_gen == 0) {
            current_gen = 1;
            for (uint16_t i = 0; i < prog_len; i++) {
                gen[i] = 0;
            }
        }

        for (uint16_t ci = 0; ci < clen; ci++) {
            uint16_t pc = clist[ci];
            const struct NfaInst *inst = &prog[pc];
            bool advance = false;
            switch (inst->op) {
            case NFA_CHAR:
                advance = (byte == (uint8_t) inst->arg1);
                break;
            case NFA_ANY:
                advance = true;
                break;
            case NFA_CLASS:
                advance = nfa_class_match(
                    pat, inst->arg1, inst->arg2, inst->arg3, byte
                );
                break;
            case NFA_MATCH: // MATCH before end of subject: not a whole-string
                            // match yet.
            case NFA_SPLIT:
            case NFA_JMP:
                // Should not appear in clist (epsilon states are expanded).
                break;
            }
            if (advance) {
                NFA_ADD_STATE(nlist, nlen, (uint16_t) (pc + 1), current_gen);
            }
        }

        // Swap lists.
        uint16_t *tmp = clist;
        clist = nlist;
        nlist = tmp;
        clen = nlen;
    }

#undef NFA_ADD_STATE

    // Whole-string match: accept only if MATCH is reachable after all input.
    for (uint16_t ci = 0; ci < clen; ci++) {
        if (prog[clist[ci]].op == NFA_MATCH) {
            return true;
        }
    }
    return false;
}

GgError ggl_regex_match(
    GgBuffer pattern, GgBuffer subject, GgArena *arena, bool *matches
) {
    *matches = false;

    // Fail closed if pattern exceeds the fixed-size buffer.
    if (pattern.len > GGL_REGEX_MAX_PATTERN) {
        GG_LOGW(
            "Regex pattern too long (%zu bytes), rejecting match.", pattern.len
        );
        return GG_ERR_RANGE;
    }

    // Allocate program array from arena.
    struct NfaInst *prog
        = GG_ARENA_ALLOCN(arena, struct NfaInst, GGL_REGEX_MAX_PROG);
    if (prog == NULL) {
        return GG_ERR_NOMEM;
    }

    // Compile pattern to NFA bytecode.
    struct NfaCompiler compiler;
    compiler.pat = pattern.data;
    compiler.pat_len = pattern.len;
    compiler.pos = 0;
    compiler.prog = prog;
    compiler.prog_len = 0;
    compiler.depth = 0;
    compiler.error = false;
    compiler.range_error = false;

    nfa_parse_alt(&compiler);
    if (compiler.error || compiler.pos != pattern.len) {
        // Parse error or trailing garbage - fail closed.
        GG_LOGW("Regex pattern failed to compile, rejecting match.");
        return compiler.range_error ? GG_ERR_RANGE : GG_ERR_INVALID;
    }
    // Emit MATCH instruction at the end of the program.
    struct NfaInst match_inst;
    match_inst.op = NFA_MATCH;
    match_inst.arg1 = 0;
    match_inst.arg2 = 0;
    match_inst.arg3 = 0;
    if (!nfa_emit(&compiler, match_inst)) {
        GG_LOGW("Regex program too large, rejecting match.");
        return GG_ERR_RANGE;
    }

    // Allocate simulation arrays from arena.
    uint16_t *list_a = GG_ARENA_ALLOCN(arena, uint16_t, GGL_REGEX_MAX_PROG);
    uint16_t *list_b = GG_ARENA_ALLOCN(arena, uint16_t, GGL_REGEX_MAX_PROG);
    uint16_t *gen = GG_ARENA_ALLOCN(arena, uint16_t, GGL_REGEX_MAX_PROG);
    uint16_t *estack = GG_ARENA_ALLOCN(arena, uint16_t, GGL_REGEX_MAX_PROG);
    if (list_a == NULL || list_b == NULL || gen == NULL || estack == NULL) {
        return GG_ERR_NOMEM;
    }

    // Simulate the NFA against the subject (whole-string match).
    *matches = nfa_simulate(
        compiler.prog,
        compiler.prog_len,
        pattern.data,
        subject.data,
        subject.len,
        list_a,
        list_b,
        gen,
        estack
    );

    return GG_ERR_OK;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <unity.h>

// Helper: run ggl_regex_match with a stack-backed arena.
static GgError test_regex_match(GgBuffer pattern, GgBuffer subject, bool *m) {
    uint8_t arena_mem[GGL_REGEX_MIN_ARENA_SIZE];
    GgArena arena = gg_arena_init(GG_BUF(arena_mem));
    return ggl_regex_match(pattern, subject, &arena, m);
}

// --- Tests moved from recipe.c (bare patterns, no slashes) ---

GG_TEST_DEFINE(regex_alternation_true) {
    bool m = false;
    GG_TEST_ASSERT_OK(
        test_regex_match(GG_STR("windows|linux"), GG_STR("linux"), &m)
    );
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_alternation_false) {
    bool m = false;
    GG_TEST_ASSERT_OK(
        test_regex_match(GG_STR("windows|linux"), GG_STR("darwin"), &m)
    );
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_anchored_prefix_no_match) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("lin"), GG_STR("linux"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_anchored_infix_no_match) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("inu"), GG_STR("linux"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_anchored_alternation_true) {
    bool m = false;
    GG_TEST_ASSERT_OK(
        test_regex_match(GG_STR("aarch64|amd64"), GG_STR("amd64"), &m)
    );
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_anchored_alternation_no_prefix) {
    bool m = false;
    GG_TEST_ASSERT_OK(
        test_regex_match(GG_STR("aarch64|amd64"), GG_STR("amd64x"), &m)
    );
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_dotplus_matches_nonempty) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR(".+"), GG_STR("aarch64"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_dotplus_no_match_empty) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR(".+"), GG_STR(""), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_dotstar_matches_empty) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR(".*"), GG_STR(""), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_malformed_bracket_fails_closed) {
    bool m = true;
    GgError err = test_regex_match(GG_STR("["), GG_STR("linux"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_malformed_paren_fails_closed) {
    bool m = true;
    GgError err = test_regex_match(GG_STR("("), GG_STR("linux"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_nested_group_quantifier_true) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("(12)+3"), GG_STR("12123"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_nested_group_quantifier_false) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("(12)+3"), GG_STR("1213"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_alternation_in_group) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("(a|b)c"), GG_STR("bc"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_bracket_range_true) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[a-z]+"), GG_STR("linux"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_bracket_range_false) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[a-z]+"), GG_STR("linux9"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_negated_bracket_true) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[^0-9]+"), GG_STR("arm"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_negated_bracket_false) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[^0-9]+"), GG_STR("arm64"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_escaped_metachar_true) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("a\\.b"), GG_STR("a.b"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_escaped_metachar_false) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("a\\.b"), GG_STR("axb"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_redundant_anchors) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("^linux$"), GG_STR("linux"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_quantified_optional_absent) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("ab?c"), GG_STR("ac"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_quantified_optional_present) {
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("ab?c"), GG_STR("abc"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_unbalanced_paren_fails) {
    bool m = true;
    GgError err = test_regex_match(GG_STR("(a"), GG_STR("a"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_unterminated_bracket_fails) {
    bool m = true;
    GgError err = test_regex_match(GG_STR("[a"), GG_STR("a"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_trailing_backslash_fails) {
    bool m = true;
    GgError err = test_regex_match(GG_STR("a\\"), GG_STR("a"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_deep_nesting_fails) {
    // Build a pattern with nesting deeper than GGL_REGEX_MAX_NESTING.
    uint8_t deep[36];
    for (int i = 0; i < 17; i++) {
        deep[i] = '(';
    }
    deep[17] = 'a';
    for (int i = 0; i < 17; i++) {
        deep[18 + i] = ')';
    }
    GgBuffer deep_buf = { .data = deep, .len = 35 };
    bool m = true;
    GgError err = test_regex_match(deep_buf, GG_STR("a"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_RANGE, err);
}

GG_TEST_DEFINE(regex_catastrophic_backtrack_linear) {
    // Pattern (a+)+b against 30 a's with no b. A backtracking engine would
    // take exponential time. Our Thompson NFA returns promptly (linear time).
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(
        GG_STR("(a+)+b"), GG_STR("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), &m
    ));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_star_matches_empty) {
    // a* against empty string should be true (zero repetitions).
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("a*"), GG_STR(""), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_reversed_range_no_match) {
    // A reversed range [z-a] never matches any character.
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[z-a]"), GG_STR("a"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_bracket_literal_close) {
    // A literal ] as the first character in a bracket expression is consumed
    // as a literal, so []a]+ matches ] and a characters.
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[]a]+"), GG_STR("]a"), &m));
    TEST_ASSERT_TRUE(m);
    m = true;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("[]a]+"), GG_STR("b"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_lone_caret_matches_empty) {
    // A lone ^ compiles to an empty pattern matching only the empty string.
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("^"), GG_STR(""), &m));
    TEST_ASSERT_TRUE(m);
    m = true;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("^"), GG_STR("a"), &m));
    TEST_ASSERT_FALSE(m);
}

GG_TEST_DEFINE(regex_overlong_pattern_returns_range) {
    // Build a pattern longer than the 256 cap
    uint8_t big[300];
    for (size_t i = 0; i < 297; i++) {
        big[i] = 'a';
    }
    GgBuffer overlong = { .data = big, .len = 297 };
    bool m = true;
    GgError err = test_regex_match(overlong, GG_STR("aaa"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_RANGE, err);
}

GG_TEST_DEFINE(regex_unescaped_brace_rejected) {
    // Unescaped '{' is rejected: Java reads {2} as a counted quantifier.
    bool m = true;
    GgError err = test_regex_match(GG_STR("a{2}"), GG_STR("a{2}"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_brace_open_range_rejected) {
    // Java reads {2,} as "two or more"; this engine has no counted repetition.
    bool m = true;
    GgError err = test_regex_match(GG_STR("a{2,}"), GG_STR("aa"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_leading_brace_rejected) {
    // '{' in atom position, with nothing preceding it to repeat.
    bool m = true;
    GgError err = test_regex_match(GG_STR("{2}"), GG_STR("{2}"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_escaped_brace_literal) {
    // Escaped \{ produces a literal brace.
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("a\\{2}"), GG_STR("a{2}"), &m));
    TEST_ASSERT_TRUE(m);
}

GG_TEST_DEFINE(regex_escape_digit_class_rejected) {
    // \d is rejected: Java matches digits, this engine would match 'd'.
    bool m = true;
    GgError err = test_regex_match(GG_STR("\\d+"), GG_STR("123"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_escape_word_class_rejected) {
    // \w is rejected: Java matches word chars, this engine would match 'w'.
    bool m = true;
    GgError err = test_regex_match(GG_STR("\\w"), GG_STR("w"), &m);
    TEST_ASSERT_EQUAL(GG_ERR_INVALID, err);
}

GG_TEST_DEFINE(regex_escape_nonalpha_still_works) {
    // Non-alphanumeric escape \. still works as literal dot.
    bool m = false;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("a\\.b"), GG_STR("a.b"), &m));
    TEST_ASSERT_TRUE(m);
    m = true;
    GG_TEST_ASSERT_OK(test_regex_match(GG_STR("a\\.b"), GG_STR("axb"), &m));
    TEST_ASSERT_FALSE(m);
}

#endif
