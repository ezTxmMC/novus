/* nv_unicode.h - UTF-8 text and UTF-16 positions: the unicode module. Part of the Novus runtime; included by novus_rt.h.
 * Depends on: nv_utf8.h (decoding), nv_bytes.h (nv_bin, declared below). */
#ifndef NV_UNICODE_H
#define NV_UNICODE_H

#include "nv_utf8.h"

/* ------------------------------------------------------------------ */
/* unicode module                                                      */
/* ------------------------------------------------------------------ */

/* A Novus string is a byte buffer; these natives read it as UTF-8. Protocols
 * such as LSP count positions in UTF-16 code units, so the mapping between
 * the two has to be cheap: every function is one pass over the bytes with a
 * fast lane for ASCII. */

static const char *nv_bin(nv v, int *len); /* nv_bytes.h: the bytes of a string, NUL safe */

static int nv_utf16_units_of(unsigned code) { return code >= NV_UNICODE_ASTRAL_FIRST ? 2 : 1; }

/* Number of ASCII bytes in a row from `pos`, not passing `limit`. */
static int nv_ascii_run(const unsigned char *bytes, int pos, int limit) {
    int start = pos;
    while (pos < limit && bytes[pos] < NV_UTF8_ASCII_LIMIT) {
        pos++;
    }
    return pos - start;
}

static long long nv_unicode_clamp(long long value, long long low, long long high) {
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static long long nv_utf16_count(const unsigned char *bytes, int len) {
    long long units = 0;
    int pos = 0;
    while (pos < len) {
        unsigned code;
        int run = nv_ascii_run(bytes, pos, len);
        units += run;
        pos += run;
        if (pos < len) {
            pos += nv_utf8_decode(bytes, len, pos, &code);
            units += nv_utf16_units_of(code);
        }
    }
    return units;
}

/* UTF-16 offset of the character that contains byte `target`. */
static long long nv_utf16_offset_of_byte(const unsigned char *bytes, int len, int target) {
    long long units = 0;
    int pos = 0;
    while (pos < target) {
        unsigned code;
        int width;
        int run = nv_ascii_run(bytes, pos, target);
        units += run;
        pos += run;
        if (pos >= target) {
            break;
        }
        width = nv_utf8_decode(bytes, len, pos, &code);
        if (pos + width > target) {
            break;
        }
        units += nv_utf16_units_of(code);
        pos += width;
    }
    return units;
}

/* Byte offset of the character that contains UTF-16 offset `target`. */
static int nv_utf16_to_byte_offset(const unsigned char *bytes, int len, long long target) {
    long long units = 0;
    int pos = 0;
    while (pos < len) {
        unsigned code;
        int width;
        int run = nv_ascii_run(bytes, pos, len);
        if (units + run > target) {
            return pos + (int)(target - units);
        }
        units += run;
        pos += run;
        if (pos >= len) {
            break;
        }
        width = nv_utf8_decode(bytes, len, pos, &code);
        if (units + nv_utf16_units_of(code) > target) {
            return pos;
        }
        units += nv_utf16_units_of(code);
        pos += width;
    }
    return len;
}

/* Start of the character holding byte `index`. */
static int nv_utf8_char_start(const unsigned char *bytes, int len, int index) {
    int start = index;
    unsigned code;
    while (start > 0 && index - start < NV_UTF8_MAX_WIDTH - 1 && nv_utf8_is_continuation(bytes[start])) {
        start--;
    }
    if (start + nv_utf8_decode(bytes, len, start, &code) > index) {
        return start;
    }
    return index;
}

static nv nv_unicode_utf16_length(nv text) {
    int len;
    const unsigned char *bytes = (const unsigned char *)nv_bin(text, &len);
    return nv_int(nv_utf16_count(bytes, len));
}

/* UTF-16 offset of a byte offset; clamped to the string, and an offset inside
 * a character maps to that character's start. */
static nv nv_unicode_byte_to_utf16(nv text, nv byte_index) {
    int len;
    const unsigned char *bytes = (const unsigned char *)nv_bin(text, &len);
    int target = (int)nv_unicode_clamp(nv_as_int(byte_index), 0, len);
    return nv_int(nv_utf16_offset_of_byte(bytes, len, target));
}

/* Byte offset of a UTF-16 offset; clamped, and an offset inside a surrogate
 * pair maps to the start of the character. */
static nv nv_unicode_utf16_to_byte(nv text, nv utf16_index) {
    int len;
    const unsigned char *bytes = (const unsigned char *)nv_bin(text, &len);
    long long target = nv_as_int(utf16_index);
    if (target <= 0) {
        return nv_int(0);
    }
    return nv_int(nv_utf16_to_byte_offset(bytes, len, target));
}

/* Code point of the character holding the byte; -1 outside the string. */
static nv nv_unicode_code_point_at(nv text, nv byte_index) {
    int len;
    const unsigned char *bytes = (const unsigned char *)nv_bin(text, &len);
    long long index = nv_as_int(byte_index);
    unsigned code;
    if (index < 0 || index >= len) {
        return nv_int(-1);
    }
    nv_utf8_decode(bytes, len, nv_utf8_char_start(bytes, len, (int)index), &code);
    return nv_int((long long)code);
}

static nv nv_unicode_char_length(nv text) {
    int len;
    const unsigned char *bytes = (const unsigned char *)nv_bin(text, &len);
    long long count = 0;
    int pos = 0;
    while (pos < len) {
        unsigned code;
        int run = nv_ascii_run(bytes, pos, len);
        count += run;
        pos += run;
        if (pos < len) {
            pos += nv_utf8_decode(bytes, len, pos, &code);
            count++;
        }
    }
    return nv_int(count);
}

static nv nv_unicode_is_valid_utf8(nv text) {
    int len;
    const unsigned char *bytes = (const unsigned char *)nv_bin(text, &len);
    int pos = 0;
    while (pos < len) {
        unsigned code;
        int width;
        pos += nv_ascii_run(bytes, pos, len);
        if (pos >= len) {
            break;
        }
        width = nv_utf8_decode(bytes, len, pos, &code);
        if (width == 1) {
            return nv_bool(0);
        }
        pos += width;
    }
    return nv_bool(1);
}

/* The UTF-8 bytes of a code point; U+FFFD for what is no Unicode scalar value. */
static nv nv_unicode_from_code_point(nv number) {
    long long wanted = nv_as_int(number);
    char out[NV_UTF8_MAX_WIDTH];
    unsigned code = nv_unicode_is_scalar(wanted) ? (unsigned)wanted : NV_UNICODE_REPLACEMENT;
    return nv_strn(out, nv_utf8_encode(code, out));
}

#endif /* NV_UNICODE_H */
