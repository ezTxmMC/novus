/* nv_utf8.h - decoding and encoding of UTF-8 characters. Part of the Novus runtime; included by novus_rt.h.
 * Depends on: nothing (plain C). Used by: nv_unicode.h, nv_json_write.h, nv_json_parse.h. */
#ifndef NV_UTF8_H
#define NV_UTF8_H

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */

/* Ill-formed input never aborts: each byte that does not belong to a valid
 * sequence (stray continuation byte, overlong form, surrogate, beyond
 * U+10FFFF, truncated sequence) counts as one character, U+FFFD. */

#define NV_UNICODE_REPLACEMENT 0xFFFD
#define NV_UNICODE_MAX 0x10FFFF
#define NV_UNICODE_ASTRAL_FIRST 0x10000
#define NV_UNICODE_HIGH_SURROGATE_FIRST 0xD800
#define NV_UNICODE_HIGH_SURROGATE_LAST 0xDBFF
#define NV_UNICODE_LOW_SURROGATE_FIRST 0xDC00
#define NV_UNICODE_LOW_SURROGATE_LAST 0xDFFF
#define NV_UTF16_SURROGATE_SHIFT 10 /* a high surrogate carries the upper 10 bits of the offset */

#define NV_UTF8_ASCII_LIMIT 0x80
#define NV_UTF8_MAX_WIDTH 4
#define NV_UTF8_TWO_BYTE_MIN 0x80    /* smallest code point that needs 2 bytes */
#define NV_UTF8_THREE_BYTE_MIN 0x800 /* ... 3 bytes */
#define NV_UTF8_LEAD_TWO_FIRST 0xC2  /* 0xC0 and 0xC1 could only start overlong forms */
#define NV_UTF8_LEAD_THREE_FIRST 0xE0
#define NV_UTF8_LEAD_FOUR_FIRST 0xF0
#define NV_UTF8_LEAD_LIMIT 0xF5      /* 0xF5 and up would pass U+10FFFF */
#define NV_UTF8_CONTINUATION_TAG 0x80
#define NV_UTF8_CONTINUATION_MASK 0xC0
#define NV_UTF8_PAYLOAD_MASK 0x3F
#define NV_UTF8_PAYLOAD_BITS 6
#define NV_BYTE_MAX 0xFFu

static int nv_utf8_is_continuation(unsigned char byte) {
    return (byte & NV_UTF8_CONTINUATION_MASK) == NV_UTF8_CONTINUATION_TAG;
}

static int nv_is_high_surrogate(unsigned code) {
    return code >= NV_UNICODE_HIGH_SURROGATE_FIRST && code <= NV_UNICODE_HIGH_SURROGATE_LAST;
}

static int nv_is_low_surrogate(unsigned code) {
    return code >= NV_UNICODE_LOW_SURROGATE_FIRST && code <= NV_UNICODE_LOW_SURROGATE_LAST;
}

/* Whether the number is a Unicode scalar value: in range and not a surrogate. */
static int nv_unicode_is_scalar(long long code) {
    if (code < 0 || code > NV_UNICODE_MAX) {
        return 0;
    }
    return !nv_is_high_surrogate((unsigned)code) && !nv_is_low_surrogate((unsigned)code);
}

/* Length a lead byte announces; 0 for bytes that can never start a sequence. */
static int nv_utf8_lead_width(unsigned char lead) {
    if (lead < NV_UTF8_ASCII_LIMIT) {
        return 1;
    }
    if (lead < NV_UTF8_LEAD_TWO_FIRST) {
        return 0;
    }
    if (lead < NV_UTF8_LEAD_THREE_FIRST) {
        return 2;
    }
    if (lead < NV_UTF8_LEAD_FOUR_FIRST) {
        return 3;
    }
    if (lead < NV_UTF8_LEAD_LIMIT) {
        return 4;
    }
    return 0;
}

/* Rejects overlong forms, surrogates and values beyond U+10FFFF. */
static int nv_utf8_in_range(unsigned value, int width) {
    static const unsigned smallest[NV_UTF8_MAX_WIDTH + 1] = {
        0, 0, NV_UTF8_TWO_BYTE_MIN, NV_UTF8_THREE_BYTE_MIN, NV_UNICODE_ASTRAL_FIRST};
    if (value < smallest[width]) {
        return 0;
    }
    return nv_unicode_is_scalar((long long)value);
}

/* The character at `pos`: its width in bytes (at least 1) and code point. */
static int nv_utf8_decode(const unsigned char *bytes, int len, int pos, unsigned *code) {
    unsigned char lead = bytes[pos];
    int width = nv_utf8_lead_width(lead);
    unsigned value;
    int k;
    *code = NV_UNICODE_REPLACEMENT;
    if (width == 1) {
        *code = lead;
        return 1;
    }
    if (width == 0 || pos + width > len) {
        return 1;
    }
    value = lead & (NV_BYTE_MAX >> (width + 1));
    for (k = 1; k < width; k++) {
        if (!nv_utf8_is_continuation(bytes[pos + k])) {
            return 1;
        }
        value = (value << NV_UTF8_PAYLOAD_BITS) | (bytes[pos + k] & NV_UTF8_PAYLOAD_MASK);
    }
    if (!nv_utf8_in_range(value, width)) {
        return 1;
    }
    *code = value;
    return width;
}

static int nv_utf8_width_of(unsigned code) {
    if (code < NV_UTF8_ASCII_LIMIT) {
        return 1;
    }
    if (code < NV_UTF8_THREE_BYTE_MIN) {
        return 2;
    }
    return code < NV_UNICODE_ASTRAL_FIRST ? 3 : 4;
}

/* Writes the UTF-8 form of a code point (at most 4 bytes) and returns its length. */
static int nv_utf8_encode(unsigned code, char *out) {
    static const unsigned char lead_tag[NV_UTF8_MAX_WIDTH + 1] = {
        0, 0, NV_UTF8_CONTINUATION_MASK, NV_UTF8_LEAD_THREE_FIRST, NV_UTF8_LEAD_FOUR_FIRST};
    int width = nv_utf8_width_of(code);
    int k;
    if (width == 1) {
        out[0] = (char)code;
        return 1;
    }
    for (k = width - 1; k > 0; k--) {
        out[k] = (char)(NV_UTF8_CONTINUATION_TAG | (code & NV_UTF8_PAYLOAD_MASK));
        code >>= NV_UTF8_PAYLOAD_BITS;
    }
    out[0] = (char)(lead_tag[width] | code);
    return width;
}

#endif /* NV_UTF8_H */
