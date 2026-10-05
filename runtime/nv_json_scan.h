/* nv_json_scan.h - json.isValid, the strict RFC 8259 check. Part of the Novus runtime; included by nv_json.h.
 * Depends on: nv_json_cursor.h, nv_json_text.h (NV_JSON_MAX_DEPTH). */
#ifndef NV_JSON_SCAN_H
#define NV_JSON_SCAN_H

#include "nv_json_cursor.h"
#include "nv_json_text.h"

/* ------------------------------------------------------------------ */
/* json validator                                                      */
/* ------------------------------------------------------------------ */

/* Never aborts: every function answers 0 for text that is not JSON. */

static int nv_json_scan(NvJsonCursor *cursor);

/* pos is after the backslash. */
static int nv_json_scan_escape(NvJsonCursor *cursor) {
    unsigned ignored;
    char escape = nv_json_peek(cursor);
    if (escape == 'u') {
        if (!nv_json_hex4(cursor->text + cursor->pos + 1, &ignored)) {
            return 0;
        }
        cursor->pos += 1 + NV_JSON_HEX_DIGITS;
        return 1;
    }
    if (escape == 0 || !strchr("\"\\/bfnrt", escape)) {
        return 0;
    }
    cursor->pos++;
    return 1;
}

static int nv_json_scan_string(NvJsonCursor *cursor) {
    cursor->pos++;
    while (nv_json_peek(cursor) != '"') {
        unsigned char byte = (unsigned char)nv_json_peek(cursor);
        if (byte < ' ') {
            return 0; /* raw control characters, a NUL and the end of the text */
        }
        cursor->pos++;
        if (byte == '\\' && !nv_json_scan_escape(cursor)) {
            return 0;
        }
    }
    cursor->pos++;
    return 1;
}

static int nv_json_scan_digits(NvJsonCursor *cursor) {
    int start = cursor->pos;
    nv_json_skip_digits(cursor);
    return cursor->pos > start;
}

/* 0 | [1-9][0-9]* - no leading zeros */
static int nv_json_scan_integer_part(NvJsonCursor *cursor) {
    if (nv_json_peek(cursor) == '0') {
        cursor->pos++;
        return 1;
    }
    return nv_json_scan_digits(cursor);
}

static int nv_json_scan_fraction(NvJsonCursor *cursor) {
    if (nv_json_peek(cursor) != '.') {
        return 1;
    }
    cursor->pos++;
    return nv_json_scan_digits(cursor);
}

static int nv_json_scan_exponent(NvJsonCursor *cursor) {
    if (nv_json_peek(cursor) != 'e' && nv_json_peek(cursor) != 'E') {
        return 1;
    }
    cursor->pos++;
    if (nv_json_peek(cursor) == '+' || nv_json_peek(cursor) == '-') {
        cursor->pos++;
    }
    return nv_json_scan_digits(cursor);
}

/* -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)? */
static int nv_json_scan_number(NvJsonCursor *cursor) {
    if (nv_json_peek(cursor) == '-') {
        cursor->pos++;
    }
    if (!nv_json_scan_integer_part(cursor)) {
        return 0;
    }
    if (!nv_json_scan_fraction(cursor)) {
        return 0;
    }
    return nv_json_scan_exponent(cursor);
}

static int nv_json_scan_word(NvJsonCursor *cursor, const char *word) {
    if (!nv_json_at_word(cursor, word)) {
        return 0;
    }
    cursor->pos += (int)strlen(word);
    return 1;
}

/* After a member: the comma (more = 1) or the closing character (more = 0);
 * anything else is invalid. */
static int nv_json_scan_separator(NvJsonCursor *cursor, char closing, int *more) {
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) == ',') {
        cursor->pos++;
        *more = 1;
        return 1;
    }
    if (nv_json_peek(cursor) != closing) {
        return 0;
    }
    cursor->pos++;
    *more = 0;
    return 1;
}

static int nv_json_scan_array_items(NvJsonCursor *cursor) {
    int more = 1;
    while (more) {
        if (!nv_json_scan(cursor) || !nv_json_scan_separator(cursor, ']', &more)) {
            return 0;
        }
    }
    return 1;
}

static int nv_json_scan_member(NvJsonCursor *cursor) {
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) != '"' || !nv_json_scan_string(cursor)) {
        return 0;
    }
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) != ':') {
        return 0;
    }
    cursor->pos++;
    return nv_json_scan(cursor);
}

static int nv_json_scan_object_members(NvJsonCursor *cursor) {
    int more = 1;
    while (more) {
        if (!nv_json_scan_member(cursor) || !nv_json_scan_separator(cursor, '}', &more)) {
            return 0;
        }
    }
    return 1;
}

/* pos is at the opening bracket. */
static int nv_json_scan_array(NvJsonCursor *cursor) {
    cursor->pos++;
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) == ']') {
        cursor->pos++;
        return 1;
    }
    return nv_json_scan_array_items(cursor);
}

/* pos is at the opening brace. */
static int nv_json_scan_object(NvJsonCursor *cursor) {
    cursor->pos++;
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) == '}') {
        cursor->pos++;
        return 1;
    }
    return nv_json_scan_object_members(cursor);
}

static int nv_json_scan_container(NvJsonCursor *cursor) {
    int valid;
    if (++cursor->depth > NV_JSON_MAX_DEPTH) {
        return 0;
    }
    valid = nv_json_peek(cursor) == '{' ? nv_json_scan_object(cursor) : nv_json_scan_array(cursor);
    cursor->depth--;
    return valid;
}

static int nv_json_scan(NvJsonCursor *cursor) {
    char first;
    nv_json_skip_space(cursor);
    first = nv_json_peek(cursor);
    if (first == '{' || first == '[') {
        return nv_json_scan_container(cursor);
    }
    if (first == '"') {
        return nv_json_scan_string(cursor);
    }
    if (first == '-' || isdigit((unsigned char)first)) {
        return nv_json_scan_number(cursor);
    }
    return nv_json_scan_word(cursor, "true") || nv_json_scan_word(cursor, "false") || nv_json_scan_word(cursor, "null");
}

/* The whole text must be one value: anything after it - a NUL and what
 * follows included - makes it invalid. */
static nv nv_json_is_valid(nv text) {
    NvJsonCursor cursor;
    nv_json_open(&cursor, text);
    if (!nv_json_scan(&cursor)) {
        return nv_bool(0);
    }
    nv_json_skip_space(&cursor);
    return nv_bool(nv_json_at_end(&cursor));
}

#endif /* NV_JSON_SCAN_H */
