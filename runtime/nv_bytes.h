/* nv_bytes.h - binary buffers (byte reads and writes, varints, hex). Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_BYTES_H
#define NV_BYTES_H

/* ------------------------------------------------------------------ */
/* Binary buffers                                                      */
/* ------------------------------------------------------------------ */

/* Novus strings carry an explicit length (slen) and charAt/substring work on
 * bytes, so a string is also the natural byte buffer: it survives NUL bytes
 * and every value in 0..255. Only the NUL terminated views (nv_cstr,
 * nv_display) would truncate, so everything below goes through nv_bin(). */
static const char *nv_bin(nv v, int *len) {
    if (nv_type_of(v) == NV_STR) {
        *len = v->slen;
        return v->s;
    }
    {
        const char *s = nv_display(v);
        *len = (int)strlen(s);
        return s;
    }
}

/* Reads past the end of a buffer set this flag instead of aborting: a server
 * has to survive a truncated or hostile packet. bytes.failed() reads it. */
static NV_TLS int nv_bytes_err = 0;

static nv nv_bytes_failed(void) { return nv_bool(nv_bytes_err != 0); }
static nv nv_bytes_clear_error(void) {
    nv_bytes_err = 0;
    return nv_nil;
}

/* Unsigned big endian read of `n` bytes at `off`; 0 and the error flag when
 * the buffer is too short. */
static unsigned long long nv_bytes_raw(nv b, long long off, int n) {
    int len;
    const char *s = nv_bin(b, &len);
    unsigned long long v = 0;
    int i;
    if (off < 0 || off + n > len) {
        nv_bytes_err = 1;
        return 0;
    }
    for (i = 0; i < n; i++) {
        v = (v << 8) | (unsigned char)s[off + i];
    }
    return v;
}

static nv nv_bytes_u8(nv b, nv off) { return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 1)); }
static nv nv_bytes_u16(nv b, nv off) { return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 2)); }
static nv nv_bytes_u32(nv b, nv off) { return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 4)); }

static nv nv_bytes_i8(nv b, nv off) {
    return nv_int((signed char)(unsigned char)nv_bytes_raw(b, nv_as_int(off), 1));
}
static nv nv_bytes_i16(nv b, nv off) {
    return nv_int((short)(unsigned short)nv_bytes_raw(b, nv_as_int(off), 2));
}
static nv nv_bytes_i32(nv b, nv off) {
    return nv_int((int)(unsigned int)nv_bytes_raw(b, nv_as_int(off), 4));
}
static nv nv_bytes_i64(nv b, nv off) {
    return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 8));
}

static nv nv_bytes_f32(nv b, nv off) {
    unsigned int bits = (unsigned int)nv_bytes_raw(b, nv_as_int(off), 4);
    float f;
    memcpy(&f, &bits, 4);
    return nv_float((double)f);
}

static nv nv_bytes_f64(nv b, nv off) {
    unsigned long long bits = nv_bytes_raw(b, nv_as_int(off), 8);
    double d;
    memcpy(&d, &bits, 8);
    return nv_float(d);
}

/* VarInt/VarLong: seven bits per byte, high bit continues. varIntSize()
 * returns how many bytes the value at `off` occupies, or 0 when the buffer
 * ends inside it (an incomplete read, not an error) and -1 when it is longer
 * than the protocol allows. */
static int nv_varint_len(const char *s, int len, long long off, int maxBytes) {
    int i;
    for (i = 0; i < maxBytes; i++) {
        if (off + i >= len) {
            return 0;
        }
        if (((unsigned char)s[off + i] & 0x80) == 0) {
            return i + 1;
        }
    }
    return -1;
}

static nv nv_bytes_varint_size(nv b, nv off) {
    int len;
    const char *s = nv_bin(b, &len);
    return nv_int(nv_varint_len(s, len, nv_as_int(off), 5));
}

static nv nv_bytes_varlong_size(nv b, nv off) {
    int len;
    const char *s = nv_bin(b, &len);
    return nv_int(nv_varint_len(s, len, nv_as_int(off), 10));
}

static unsigned long long nv_varint_value(nv b, long long off, int maxBytes) {
    int len;
    const char *s = nv_bin(b, &len);
    unsigned long long v = 0;
    int i;
    for (i = 0; i < maxBytes; i++) {
        unsigned char byte;
        if (off + i >= len) {
            nv_bytes_err = 1;
            return 0;
        }
        byte = (unsigned char)s[off + i];
        v |= (unsigned long long)(byte & 0x7f) << (i * 7);
        if ((byte & 0x80) == 0) {
            return v;
        }
    }
    nv_bytes_err = 1;
    return 0;
}

static nv nv_bytes_varint(nv b, nv off) {
    return nv_int((int)(unsigned int)nv_varint_value(b, nv_as_int(off), 5));
}

static nv nv_bytes_varlong(nv b, nv off) {
    return nv_int((long long)nv_varint_value(b, nv_as_int(off), 10));
}

/* Writers: every one returns the encoded bytes as a fresh string. */
static nv nv_bytes_put_raw(unsigned long long v, int n) {
    char buf[8];
    int i;
    for (i = 0; i < n; i++) {
        buf[i] = (char)((v >> ((n - 1 - i) * 8)) & 0xff);
    }
    return nv_strn(buf, n);
}

static nv nv_bytes_put_u8(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 1); }
static nv nv_bytes_put_i16(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 2); }
static nv nv_bytes_put_i32(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 4); }
static nv nv_bytes_put_i64(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 8); }

static nv nv_bytes_put_f32(nv v) {
    float f = (float)nv_as_double(v);
    unsigned int bits;
    memcpy(&bits, &f, 4);
    return nv_bytes_put_raw(bits, 4);
}

static nv nv_bytes_put_f64(nv v) {
    double d = nv_as_double(v);
    unsigned long long bits;
    memcpy(&bits, &d, 8);
    return nv_bytes_put_raw(bits, 8);
}

static nv nv_bytes_put_varnum(unsigned long long v, int maxBytes) {
    char buf[10];
    int n = 0;
    while (n < maxBytes) {
        unsigned char byte = (unsigned char)(v & 0x7f);
        v >>= 7;
        if (v == 0) {
            buf[n++] = (char)byte;
            break;
        }
        buf[n++] = (char)(byte | 0x80);
    }
    return nv_strn(buf, n);
}

static nv nv_bytes_put_varint(nv v) {
    return nv_bytes_put_varnum((unsigned long long)(unsigned int)(int)nv_as_int(v), 5);
}

static nv nv_bytes_put_varlong(nv v) {
    return nv_bytes_put_varnum((unsigned long long)nv_as_int(v), 10);
}

/* The number of bytes writeVarInt() would produce - the protocol needs it to
 * reserve the length prefix before the body is known. */
static nv nv_bytes_varint_written(nv v) {
    unsigned int val = (unsigned int)(int)nv_as_int(v);
    int n = 1;
    while (val >= 0x80) {
        val >>= 7;
        n++;
    }
    return nv_int(n);
}

static nv nv_bytes_slice(nv b, nv from, nv to) {
    int len;
    const char *s = nv_bin(b, &len);
    long long a = nv_as_int(from);
    long long z = nv_as_int(to);
    if (a < 0) {
        a = 0;
    }
    if (z > len) {
        z = len;
    }
    if (z <= a) {
        return nv_str("");
    }
    return nv_strn(s + a, (int)(z - a));
}

static nv nv_bytes_size(nv b) {
    int len;
    nv_bin(b, &len);
    return nv_int(len);
}

/* Joins an array of byte strings in one pass - the packet writer builds its
 * payload as a list of pieces and flattens it once. */
static nv nv_bytes_join(nv parts) {
    NvArr *a;
    int total = 0;
    int i;
    char *buf;
    int at = 0;
    if (nv_type_of(parts) != NV_ARR) {
        return nv_str("");
    }
    a = parts->a;
    for (i = 0; i < a->len; i++) {
        int len;
        nv_bin(a->items[i], &len);
        total += len;
    }
    buf = (char *)nv_alloc_atomic((size_t)total + 1);
    for (i = 0; i < a->len; i++) {
        int len;
        const char *s = nv_bin(a->items[i], &len);
        memcpy(buf + at, s, (size_t)len);
        at += len;
    }
    buf[total] = 0;
    return nv_str_own(buf, total);
}

/* n zero bytes. */
static nv nv_bytes_zeros(nv count) {
    long long n = nv_as_int(count);
    char *buf;
    if (n <= 0) {
        return nv_str("");
    }
    buf = (char *)nv_alloc_atomic((size_t)n + 1);
    memset(buf, 0, (size_t)n + 1);
    return nv_str_own(buf, (int)n);
}

static nv nv_bytes_hex(nv b) {
    static const char *digits = "0123456789abcdef";
    int len;
    const char *s = nv_bin(b, &len);
    char *buf = (char *)nv_alloc_atomic((size_t)len * 2 + 1);
    int i;
    for (i = 0; i < len; i++) {
        buf[i * 2] = digits[((unsigned char)s[i]) >> 4];
        buf[i * 2 + 1] = digits[((unsigned char)s[i]) & 0xf];
    }
    buf[len * 2] = 0;
    return nv_str_own(buf, len * 2);
}

static int nv_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static nv nv_bytes_from_hex(nv text) {
    int len;
    const char *s = nv_bin(text, &len);
    char *buf = (char *)nv_alloc_atomic((size_t)len / 2 + 1);
    int n = 0;
    int i = 0;
    while (i + 1 < len) {
        int hi = nv_hex_digit(s[i]);
        int lo = nv_hex_digit(s[i + 1]);
        if (hi < 0 || lo < 0) {
            break;
        }
        buf[n++] = (char)((hi << 4) | lo);
        i += 2;
    }
    buf[n] = 0;
    return nv_str_own(buf, n);
}

/* array<integer> of the byte values, and back. */
static nv nv_bytes_to_array(nv b) {
    int len;
    const char *s = nv_bin(b, &len);
    nv out = nv_arr();
    int i;
    for (i = 0; i < len; i++) {
        nv_arr_push(out->a, nv_int((unsigned char)s[i]));
    }
    return out;
}

static nv nv_bytes_of_array(nv values) {
    NvArr *a;
    char *buf;
    int i;
    if (nv_type_of(values) != NV_ARR) {
        return nv_str("");
    }
    a = values->a;
    buf = (char *)nv_alloc_atomic((size_t)a->len + 1);
    for (i = 0; i < a->len; i++) {
        buf[i] = (char)(nv_as_int(a->items[i]) & 0xff);
    }
    buf[a->len] = 0;
    return nv_str_own(buf, a->len);
}

/* Index of `needle` in `haystack` at or after `from`, byte exact (-1 when
 * absent). The string builtin stops at a NUL byte; this one does not. */
static nv nv_bytes_index_of(nv haystack, nv needle, nv from) {
    int hlen;
    int nlen;
    const char *h = nv_bin(haystack, &hlen);
    const char *n = nv_bin(needle, &nlen);
    long long start = nv_as_int(from);
    long long i;
    if (start < 0) {
        start = 0;
    }
    if (nlen == 0) {
        return nv_int(start <= hlen ? start : -1);
    }
    for (i = start; i + nlen <= hlen; i++) {
        if (memcmp(h + i, n, (size_t)nlen) == 0) {
            return nv_int(i);
        }
    }
    return nv_int(-1);
}

/* Byte exact equality (the == operator compares NUL terminated views). */
static nv nv_bytes_equal(nv a, nv b) {
    int alen;
    int blen;
    const char *x = nv_bin(a, &alen);
    const char *y = nv_bin(b, &blen);
    return nv_bool(alen == blen && memcmp(x, y, (size_t)alen) == 0);
}


#endif /* NV_BYTES_H */
