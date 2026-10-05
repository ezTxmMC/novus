/* nv_json_string.h - reading a JSON string: escapes and surrogate pairs. Part of the Novus runtime; included by nv_json_parse.h.
 * Depends on: nv_json_cursor.h, nv_utf8.h (encoding), nv_data.h (NvSb, strings, errors). */
#ifndef NV_JSON_STRING_H
#define NV_JSON_STRING_H

#include "nv_json_cursor.h"
#include "nv_utf8.h"

/* ------------------------------------------------------------------ */
/* json strings                                                        */
/* ------------------------------------------------------------------ */

static void nv_json_fail(const NvJsonCursor *cursor, const char *what) {
    nv_error("json parse error at position %d: %s", cursor->pos, what);
}

static void nv_json_add_code_point(NvSb *sb, unsigned code) {
    char bytes[NV_UTF8_MAX_WIDTH];
    nv_sb_addn(sb, bytes, nv_utf8_encode(code, bytes));
}

/* A string failure frees the half built text first: the error unwinds
 * without returning, and the builder is not collected memory. */
static void nv_json_string_fail(const NvJsonCursor *cursor, NvSb *sb, const char *what) {
    free(sb->buf);
    nv_json_fail(cursor, what);
}

/* A high surrogate escape followed by a low one is one code point; any other
 * surrogate is alone and becomes U+FFFD, so the result is always valid UTF-8.
 * `first` has been read; on return pos is at the last digit that was used. */
static unsigned nv_json_combine_surrogates(NvJsonCursor *cursor, unsigned first) {
    unsigned low;
    const char *next = cursor->text + cursor->pos + 1;
    if (nv_is_low_surrogate(first)) {
        return NV_UNICODE_REPLACEMENT;
    }
    if (!nv_is_high_surrogate(first)) {
        return first;
    }
    if (next[0] != '\\' || next[1] != 'u' || !nv_json_hex4(next + 2, &low) || !nv_is_low_surrogate(low)) {
        return NV_UNICODE_REPLACEMENT;
    }
    cursor->pos += NV_JSON_UNICODE_ESCAPE_LENGTH;
    return NV_UNICODE_ASTRAL_FIRST + ((first - NV_UNICODE_HIGH_SURROGATE_FIRST) << NV_UTF16_SURROGATE_SHIFT) +
           (low - NV_UNICODE_LOW_SURROGATE_FIRST);
}

/* pos is at the 'u'; leaves it at the last digit. */
static void nv_json_unicode_escape(NvJsonCursor *cursor, NvSb *sb) {
    unsigned first;
    if (!nv_json_hex4(cursor->text + cursor->pos + 1, &first)) {
        nv_json_string_fail(cursor, sb, "bad unicode escape");
    }
    cursor->pos += NV_JSON_HEX_DIGITS;
    nv_json_add_code_point(sb, nv_json_combine_surrogates(cursor, first));
}

static char nv_json_simple_escape(char escape) {
    switch (escape) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    default:
        return escape;
    }
}

/* pos is at the backslash; leaves it at the last character of the escape. */
static void nv_json_escape_sequence(NvJsonCursor *cursor, NvSb *sb) {
    char escape = cursor->text[++cursor->pos];
    if (escape == 'u') {
        nv_json_unicode_escape(cursor, sb);
        return;
    }
    if (escape == 0) {
        nv_json_string_fail(cursor, sb, nv_json_at_end(cursor) ? "unterminated string" : "NUL byte in string");
    }
    nv_sb_addc(sb, nv_json_simple_escape(escape));
}

/* One character of the string body, or the whole escape sequence that starts
 * at a backslash. */
static void nv_json_string_step(NvJsonCursor *cursor, NvSb *sb) {
    if (nv_json_peek(cursor) == '\\') {
        nv_json_escape_sequence(cursor, sb);
        return;
    }
    nv_sb_addc(sb, nv_json_peek(cursor));
}

static nv nv_json_parse_string(NvJsonCursor *cursor) {
    NvSb sb;
    int len;
    nv_sb_init(&sb);
    cursor->pos++; /* opening quote */
    while (nv_json_peek(cursor) && nv_json_peek(cursor) != '"') {
        nv_json_string_step(cursor, &sb);
        cursor->pos++;
    }
    if (nv_json_peek(cursor) != '"') {
        nv_json_string_fail(cursor, &sb, nv_json_at_end(cursor) ? "unterminated string" : "NUL byte in string");
    }
    cursor->pos++;
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

#endif /* NV_JSON_STRING_H */
