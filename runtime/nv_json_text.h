/* nv_json_text.h - the JSON nesting limit and the text of strings and numbers. Part of the Novus runtime; included by nv_json_write.h.
 * Depends on: nv_utf8.h, nv_data.h (NvSb). */
#ifndef NV_JSON_TEXT_H
#define NV_JSON_TEXT_H

#include "nv_utf8.h"

/* ------------------------------------------------------------------ */
/* json limits and text primitives                                     */
/* ------------------------------------------------------------------ */

/* Nesting limit of the writer, the parser and the validator alike: deep
 * enough for any real document, shallow enough that a 128 KB virtual thread
 * stack (about 210 bytes per writer level) leaves room for the caller's own
 * frames. Past it they raise an error (or answer "invalid") instead of
 * overflowing the C stack. */
#define NV_JSON_MAX_DEPTH 256

#define NV_JSON_ESCAPE_SIZE 8 /* "\u00XX" and the terminator */
#define NV_JSON_FLOAT_SIZE 64
#define NV_JSON_FLOAT_MIN_PRECISION 15 /* the shortest text that reads back as the same double wins */
#define NV_JSON_FLOAT_MAX_PRECISION 17

static int nv_json_needs_escape(unsigned char byte) { return byte < ' ' || byte == '"' || byte == '\\'; }

static void nv_json_escape(NvSb *sb, unsigned char byte) {
    char text[NV_JSON_ESCAPE_SIZE];
    switch (byte) {
    case '"':
        nv_sb_add(sb, "\\\"");
        return;
    case '\\':
        nv_sb_add(sb, "\\\\");
        return;
    case '\n':
        nv_sb_add(sb, "\\n");
        return;
    case '\r':
        nv_sb_add(sb, "\\r");
        return;
    case '\t':
        nv_sb_add(sb, "\\t");
        return;
    case '\b':
        nv_sb_add(sb, "\\b");
        return;
    case '\f':
        nv_sb_add(sb, "\\f");
        return;
    default:
        snprintf(text, sizeof(text), "\\u%04x", byte);
        nv_sb_add(sb, text);
    }
}

/* The end of the run of bytes that go out unchanged. */
static int nv_json_plain_run(const unsigned char *bytes, int pos, int len) {
    while (pos < len && bytes[pos] < NV_UTF8_ASCII_LIMIT && !nv_json_needs_escape(bytes[pos])) {
        pos++;
    }
    return pos;
}

/* A character beyond ASCII: valid UTF-8 is copied, an ill-formed byte
 * becomes U+FFFD so the output is always valid JSON text. Returns the width. */
static int nv_json_write_multibyte(NvSb *sb, const unsigned char *bytes, int len, int pos) {
    char replacement[NV_UTF8_MAX_WIDTH];
    unsigned code;
    int width = nv_utf8_decode(bytes, len, pos, &code);
    if (width > 1) {
        nv_sb_addn(sb, (const char *)bytes + pos, width);
        return width;
    }
    nv_sb_addn(sb, replacement, nv_utf8_encode(NV_UNICODE_REPLACEMENT, replacement));
    return 1;
}

/* By length, not to the first NUL: a NUL byte becomes \u0000. */
static void nv_json_string_n(NvSb *sb, const char *text, int len) {
    const unsigned char *bytes = (const unsigned char *)text;
    int pos = 0;
    nv_sb_addc(sb, '"');
    while (pos < len) {
        int end = nv_json_plain_run(bytes, pos, len);
        nv_sb_addn(sb, text + pos, end - pos);
        pos = end;
        if (pos >= len) {
            break;
        }
        if (bytes[pos] < NV_UTF8_ASCII_LIMIT) {
            nv_json_escape(sb, bytes[pos]);
            pos++;
            continue;
        }
        pos += nv_json_write_multibyte(sb, bytes, len, pos);
    }
    nv_sb_addc(sb, '"');
}

static void nv_json_string(NvSb *sb, const char *text) { nv_json_string_n(sb, text, (int)strlen(text)); }

/* JSON has no NaN or Infinity: they are written as null. */
static void nv_json_float(NvSb *sb, double value) {
    char text[NV_JSON_FLOAT_SIZE];
    int precision;
    if (!isfinite(value)) {
        nv_sb_add(sb, "null");
        return;
    }
    for (precision = NV_JSON_FLOAT_MIN_PRECISION; precision <= NV_JSON_FLOAT_MAX_PRECISION; precision++) {
        snprintf(text, sizeof(text), "%.*g", precision, value);
        if (atof(text) == value) {
            break;
        }
    }
    nv_sb_add(sb, text);
    if (!strchr(text, '.') && !strchr(text, 'e')) {
        nv_sb_add(sb, ".0");
    }
}

#endif /* NV_JSON_TEXT_H */
