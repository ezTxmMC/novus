/* nv_json_cursor.h - the cursor over JSON text and its lexical helpers. Part of the Novus runtime; included by nv_json_scan.h and nv_json_parse.h.
 * Depends on: nv_display.h (nv_display). */
#ifndef NV_JSON_CURSOR_H
#define NV_JSON_CURSOR_H

/* ------------------------------------------------------------------ */
/* json cursor                                                         */
/* ------------------------------------------------------------------ */

#define NV_JSON_HEX_DIGITS 4            /* the XXXX of \uXXXX */
#define NV_JSON_HEX_RADIX_SHIFT 4
#define NV_JSON_DECIMAL_DIGITS 10       /* value of the letter a in hex */
#define NV_JSON_UNICODE_ESCAPE_LENGTH 6 /* backslash, u and the four digits */

/* The text is always NUL terminated at `len`, so reading at `pos == len` is
 * safe; a NUL before `len` is part of the text and no grammar accepts it. */
typedef struct NvJsonCursor {
    const char *text;
    int len;
    int pos;
    int depth;
} NvJsonCursor;

static void nv_json_open(NvJsonCursor *cursor, nv text) {
    cursor->text = nv_display(text);
    cursor->len = nv_type_of(text) == NV_STR ? text->slen : (int)strlen(cursor->text);
    cursor->pos = 0;
    cursor->depth = 0;
}

static char nv_json_peek(const NvJsonCursor *cursor) { return cursor->text[cursor->pos]; }

static int nv_json_at_end(const NvJsonCursor *cursor) { return cursor->pos >= cursor->len; }

/* The four whitespace characters of RFC 8259 - not isspace(), which also
 * takes \f, \v and whatever the locale adds. */
static int nv_json_is_space(char byte) { return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r'; }

static void nv_json_skip_space(NvJsonCursor *cursor) {
    while (!nv_json_at_end(cursor) && nv_json_is_space(nv_json_peek(cursor))) {
        cursor->pos++;
    }
}

static void nv_json_skip_digits(NvJsonCursor *cursor) {
    while (isdigit((unsigned char)nv_json_peek(cursor))) {
        cursor->pos++;
    }
}

static int nv_json_at_word(const NvJsonCursor *cursor, const char *word) {
    return strncmp(cursor->text + cursor->pos, word, strlen(word)) == 0;
}

/* Value of a hex digit, -1 for any other character. */
static int nv_json_hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + NV_JSON_DECIMAL_DIGITS;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + NV_JSON_DECIMAL_DIGITS;
    }
    return -1;
}

/* The four hex digits at `digits` as a number; 0 when they are not four digits. */
static int nv_json_hex4(const char *digits, unsigned *code) {
    int k;
    *code = 0;
    for (k = 0; k < NV_JSON_HEX_DIGITS; k++) {
        int value = nv_json_hex_digit(digits[k]);
        if (value < 0) {
            return 0;
        }
        *code = (*code << NV_JSON_HEX_RADIX_SHIFT) | (unsigned)value;
    }
    return 1;
}

#endif /* NV_JSON_CURSOR_H */
