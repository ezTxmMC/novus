/* nv_zlib.h - zlib and deflate. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_ZLIB_H
#define NV_ZLIB_H

/* ------------------------------------------------------------------ */
/* zlib (RFC 1950) and deflate (RFC 1951)                              */
/* ------------------------------------------------------------------ */

/* Self contained on purpose: a Novus program only ever needs a C compiler,
 * so linking against the system zlib is not an option. Inflate handles all
 * three block types; deflate emits fixed Huffman blocks with an LZ77 hash
 * chain, which is what a game protocol wants (fast, good enough ratio). */

static unsigned long nv_adler32(const unsigned char *data, size_t len) {
    unsigned long a = 1;
    unsigned long b = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

/* ---- bit reader ---- */

typedef struct {
    const unsigned char *in;
    size_t inlen;
    size_t inpos;
    int bitbuf;
    int bitcnt;
    unsigned char *out;
    size_t outlen;
    size_t outcap;
    int error;
} NvInfl;

static void nv_infl_put(NvInfl *s, unsigned char c) {
    if (s->outlen == s->outcap) {
        size_t cap = s->outcap ? s->outcap * 2 : 1024;
        unsigned char *grown = (unsigned char *)realloc(s->out, cap);
        if (!grown) {
            s->error = 1;
            return;
        }
        s->out = grown;
        s->outcap = cap;
    }
    s->out[s->outlen++] = c;
}

static int nv_infl_bits(NvInfl *s, int need) {
    long value = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) {
            s->error = 1;
            return 0;
        }
        value |= (long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(value >> need);
    s->bitcnt -= need;
    return (int)(value & ((1L << need) - 1));
}

/* ---- canonical Huffman ---- */

typedef struct {
    short *count;  /* number of codes of each length 0..15 */
    short *symbol; /* symbols in canonical order */
} NvHuff;

static int nv_huff_decode(NvInfl *s, const NvHuff *h) {
    int len;
    int code = 0;
    int first = 0;
    int index = 0;
    for (len = 1; len <= 15; len++) {
        int count;
        code |= nv_infl_bits(s, 1);
        if (s->error) {
            return -1;
        }
        count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int nv_huff_build(NvHuff *h, const short *lengths, int n) {
    int symbol;
    int len;
    int left;
    short offs[16];
    for (len = 0; len <= 15; len++) {
        h->count[len] = 0;
    }
    for (symbol = 0; symbol < n; symbol++) {
        h->count[lengths[symbol]]++;
    }
    if (h->count[0] == n) {
        return 0; /* no codes at all - an empty (but legal) table */
    }
    left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return -1; /* over-subscribed */
        }
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) {
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    }
    for (symbol = 0; symbol < n; symbol++) {
        if (lengths[symbol] != 0) {
            h->symbol[offs[lengths[symbol]]++] = (short)symbol;
        }
    }
    return 0;
}

static const short nv_len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                      15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short nv_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short nv_dist_base[30] = {1,    2,    3,    4,    5,    7,     9,    13,
                                       17,   25,   33,   49,   65,   97,    129,  193,
                                       257,  385,  513,  769,  1025, 1537,  2049, 3073,
                                       4097, 6145, 8193, 12289, 16385, 24577};
static const short nv_dist_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                        4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                        9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int nv_infl_codes(NvInfl *s, const NvHuff *lencode, const NvHuff *distcode) {
    for (;;) {
        int symbol = nv_huff_decode(s, lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 256) {
            nv_infl_put(s, (unsigned char)symbol);
            if (s->error) {
                return -1;
            }
            continue;
        }
        if (symbol == 256) {
            return 0;
        }
        symbol -= 257;
        if (symbol >= 29) {
            return -1;
        }
        {
            int length = nv_len_base[symbol] + nv_infl_bits(s, nv_len_extra[symbol]);
            int dsym = nv_huff_decode(s, distcode);
            size_t dist;
            int i;
            if (dsym < 0 || dsym >= 30) {
                return -1;
            }
            dist = (size_t)(nv_dist_base[dsym] + nv_infl_bits(s, nv_dist_extra[dsym]));
            if (s->error || dist > s->outlen) {
                return -1;
            }
            for (i = 0; i < length; i++) {
                nv_infl_put(s, s->out[s->outlen - dist]);
                if (s->error) {
                    return -1;
                }
            }
        }
    }
}

static int nv_infl_stored(NvInfl *s) {
    unsigned len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) {
        return -1;
    }
    len = (unsigned)s->in[s->inpos] | ((unsigned)s->in[s->inpos + 1] << 8);
    s->inpos += 4; /* LEN and its complement NLEN */
    if (s->inpos + len > s->inlen) {
        return -1;
    }
    while (len--) {
        nv_infl_put(s, s->in[s->inpos++]);
        if (s->error) {
            return -1;
        }
    }
    return 0;
}

static int nv_infl_fixed(NvInfl *s) {
    static short lencnt[16];
    static short lensym[288];
    static short distcnt[16];
    static short distsym[30];
    static NvHuff lencode;
    static NvHuff distcode;
    static int built = 0;
    if (!built) {
        short lengths[288];
        int symbol;
        for (symbol = 0; symbol < 144; symbol++) {
            lengths[symbol] = 8;
        }
        for (; symbol < 256; symbol++) {
            lengths[symbol] = 9;
        }
        for (; symbol < 280; symbol++) {
            lengths[symbol] = 7;
        }
        for (; symbol < 288; symbol++) {
            lengths[symbol] = 8;
        }
        lencode.count = lencnt;
        lencode.symbol = lensym;
        nv_huff_build(&lencode, lengths, 288);
        for (symbol = 0; symbol < 30; symbol++) {
            lengths[symbol] = 5;
        }
        distcode.count = distcnt;
        distcode.symbol = distsym;
        nv_huff_build(&distcode, lengths, 30);
        built = 1;
    }
    return nv_infl_codes(s, &lencode, &distcode);
}

static int nv_infl_dynamic(NvInfl *s) {
    static const short order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
    short lencnt[16];
    short lensym[288];
    short distcnt[16];
    short distsym[30];
    NvHuff lencode;
    NvHuff distcode;
    short lengths[320];
    int nlen;
    int ndist;
    int ncode;
    int index;
    nlen = nv_infl_bits(s, 5) + 257;
    ndist = nv_infl_bits(s, 5) + 1;
    ncode = nv_infl_bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30) {
        return -1;
    }
    for (index = 0; index < ncode; index++) {
        lengths[order[index]] = (short)nv_infl_bits(s, 3);
    }
    for (; index < 19; index++) {
        lengths[order[index]] = 0;
    }
    lencode.count = lencnt;
    lencode.symbol = lensym;
    if (nv_huff_build(&lencode, lengths, 19) != 0) {
        return -1;
    }
    index = 0;
    while (index < nlen + ndist) {
        int symbol = nv_huff_decode(s, &lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
            continue;
        }
        {
            short len = 0;
            int repeat;
            if (symbol == 16) {
                if (index == 0) {
                    return -1;
                }
                len = lengths[index - 1];
                repeat = 3 + nv_infl_bits(s, 2);
            } else if (symbol == 17) {
                repeat = 3 + nv_infl_bits(s, 3);
            } else {
                repeat = 11 + nv_infl_bits(s, 7);
            }
            if (index + repeat > nlen + ndist) {
                return -1;
            }
            while (repeat--) {
                lengths[index++] = len;
            }
        }
    }
    if (lengths[256] == 0) {
        return -1; /* no end-of-block code */
    }
    lencode.count = lencnt;
    lencode.symbol = lensym;
    if (nv_huff_build(&lencode, lengths, nlen) != 0) {
        return -1;
    }
    distcode.count = distcnt;
    distcode.symbol = distsym;
    if (nv_huff_build(&distcode, lengths + nlen, ndist) != 0) {
        return -1;
    }
    return nv_infl_codes(s, &lencode, &distcode);
}

/* Raw deflate stream -> bytes. `limit` (when > 0) caps the output so a tiny
 * hostile packet cannot expand into gigabytes. */
static int nv_inflate_raw(const unsigned char *in, size_t inlen, size_t limit, unsigned char **out,
                          size_t *outlen) {
    NvInfl s;
    int last;
    memset(&s, 0, sizeof(s));
    s.in = in;
    s.inlen = inlen;
    do {
        int type;
        int rc;
        last = nv_infl_bits(&s, 1);
        type = nv_infl_bits(&s, 2);
        if (s.error) {
            free(s.out);
            return -1;
        }
        if (type == 0) {
            rc = nv_infl_stored(&s);
        } else if (type == 1) {
            rc = nv_infl_fixed(&s);
        } else if (type == 2) {
            rc = nv_infl_dynamic(&s);
        } else {
            rc = -1;
        }
        if (rc != 0 || s.error) {
            free(s.out);
            return -1;
        }
        if (limit > 0 && s.outlen > limit) {
            free(s.out);
            return -1;
        }
    } while (!last);
    *out = s.out;
    *outlen = s.outlen;
    return 0;
}

/* ---- deflate ---- */

typedef struct {
    unsigned char *out;
    size_t len;
    size_t cap;
    int bitbuf;
    int bitcnt;
    int error;
} NvDefl;

static void nv_defl_byte(NvDefl *s, unsigned char c) {
    if (s->len == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 1024;
        unsigned char *grown = (unsigned char *)realloc(s->out, cap);
        if (!grown) {
            s->error = 1;
            return;
        }
        s->out = grown;
        s->cap = cap;
    }
    s->out[s->len++] = c;
}

/* Deflate is a little endian bit stream: values go in LSB first. */
static void nv_defl_bits(NvDefl *s, int value, int count) {
    s->bitbuf |= value << s->bitcnt;
    s->bitcnt += count;
    while (s->bitcnt >= 8) {
        nv_defl_byte(s, (unsigned char)(s->bitbuf & 0xff));
        s->bitbuf >>= 8;
        s->bitcnt -= 8;
    }
}

/* Huffman codes travel MSB first, so they are reversed into the stream. */
static void nv_defl_huff(NvDefl *s, int code, int count) {
    int i;
    for (i = count - 1; i >= 0; i--) {
        nv_defl_bits(s, (code >> i) & 1, 1);
    }
}

static void nv_defl_literal(NvDefl *s, int symbol) {
    if (symbol < 144) {
        nv_defl_huff(s, 0x30 + symbol, 8);
    } else if (symbol < 256) {
        nv_defl_huff(s, 0x190 + symbol - 144, 9);
    } else if (symbol < 280) {
        nv_defl_huff(s, symbol - 256, 7);
    } else {
        nv_defl_huff(s, 0xc0 + symbol - 280, 8);
    }
}

#define NV_DEFL_WBITS 15
#define NV_DEFL_WSIZE (1 << NV_DEFL_WBITS)
#define NV_DEFL_HBITS 15
#define NV_DEFL_HSIZE (1 << NV_DEFL_HBITS)
#define NV_DEFL_MAXMATCH 258
#define NV_DEFL_MINMATCH 3
#define NV_DEFL_CHAIN 32

static int nv_defl_length_code(int length) {
    int i;
    for (i = 28; i >= 0; i--) {
        if (length >= nv_len_base[i]) {
            return i;
        }
    }
    return 0;
}

static int nv_defl_dist_code(int dist) {
    int i;
    for (i = 29; i >= 0; i--) {
        if (dist >= nv_dist_base[i]) {
            return i;
        }
    }
    return 0;
}

/* Raw deflate: one fixed Huffman block with LZ77 matches found through a
 * hash of three bytes and a chain of previous positions. */
static int nv_deflate_raw(const unsigned char *in, size_t inlen, unsigned char **out,
                          size_t *outlen) {
    NvDefl s;
    int *head = 0;
    int *prev = 0;
    size_t pos = 0;
    size_t i;
    memset(&s, 0, sizeof(s));
    head = (int *)malloc(sizeof(int) * NV_DEFL_HSIZE);
    prev = (int *)malloc(sizeof(int) * (inlen > 0 ? inlen : 1));
    if (!head || !prev) {
        free(head);
        free(prev);
        return -1;
    }
    for (i = 0; i < NV_DEFL_HSIZE; i++) {
        head[i] = -1;
    }
    nv_defl_bits(&s, 1, 1); /* BFINAL */
    nv_defl_bits(&s, 1, 2); /* fixed Huffman */
    while (pos < inlen) {
        int bestLen = 0;
        size_t bestDist = 0;
        unsigned hash = 0;
        if (pos + NV_DEFL_MINMATCH <= inlen) {
            hash = ((unsigned)in[pos] << 10) ^ ((unsigned)in[pos + 1] << 5) ^ (unsigned)in[pos + 2];
            hash &= NV_DEFL_HSIZE - 1;
            {
                int candidate = head[hash];
                int tries = NV_DEFL_CHAIN;
                while (candidate >= 0 && tries-- > 0) {
                    size_t dist = pos - (size_t)candidate;
                    int len = 0;
                    if (dist == 0 || dist > NV_DEFL_WSIZE) {
                        break;
                    }
                    while (len < NV_DEFL_MAXMATCH && pos + len < inlen &&
                           in[(size_t)candidate + len] == in[pos + len]) {
                        len++;
                    }
                    if (len > bestLen) {
                        bestLen = len;
                        bestDist = dist;
                        if (len >= NV_DEFL_MAXMATCH) {
                            break;
                        }
                    }
                    candidate = prev[candidate];
                }
            }
            prev[pos] = head[hash];
            head[hash] = (int)pos;
        }
        if (bestLen >= NV_DEFL_MINMATCH) {
            int lc = nv_defl_length_code(bestLen);
            int dc = nv_defl_dist_code((int)bestDist);
            nv_defl_literal(&s, 257 + lc);
            nv_defl_bits(&s, bestLen - nv_len_base[lc], nv_len_extra[lc]);
            nv_defl_huff(&s, dc, 5);
            nv_defl_bits(&s, (int)bestDist - nv_dist_base[dc], nv_dist_extra[dc]);
            /* every skipped position still has to enter the hash chain */
            for (i = 1; i < (size_t)bestLen; i++) {
                size_t at = pos + i;
                if (at + NV_DEFL_MINMATCH <= inlen) {
                    unsigned h = ((unsigned)in[at] << 10) ^ ((unsigned)in[at + 1] << 5) ^
                                 (unsigned)in[at + 2];
                    h &= NV_DEFL_HSIZE - 1;
                    prev[at] = head[h];
                    head[h] = (int)at;
                }
            }
            pos += (size_t)bestLen;
            continue;
        }
        nv_defl_literal(&s, in[pos]);
        pos++;
    }
    nv_defl_literal(&s, 256); /* end of block */
    if (s.bitcnt > 0) {
        nv_defl_byte(&s, (unsigned char)(s.bitbuf & 0xff));
    }
    free(head);
    free(prev);
    if (s.error) {
        free(s.out);
        return -1;
    }
    *out = s.out;
    *outlen = s.len;
    return 0;
}

/* ---- the Novus facing calls ---- */

/* zlib container: 0x78 0x9C, deflate data, big endian Adler-32. */
static nv nv_zlib_compress(nv data) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char *raw = 0;
    size_t rawlen = 0;
    unsigned long adler;
    char *result;
    size_t total;
    if (nv_deflate_raw((const unsigned char *)bytes, (size_t)len, &raw, &rawlen) != 0) {
        free(raw);
        return nv_str("");
    }
    adler = nv_adler32((const unsigned char *)bytes, (size_t)len);
    total = rawlen + 6;
    result = (char *)nv_alloc_atomic(total + 1);
    result[0] = (char)0x78;
    result[1] = (char)0x9c;
    memcpy(result + 2, raw, rawlen);
    result[rawlen + 2] = (char)((adler >> 24) & 0xff);
    result[rawlen + 3] = (char)((adler >> 16) & 0xff);
    result[rawlen + 4] = (char)((adler >> 8) & 0xff);
    result[rawlen + 5] = (char)(adler & 0xff);
    result[total] = 0;
    free(raw);
    return nv_str_own(result, (int)total);
}

/* `limit` > 0 rejects a stream that expands beyond it. "" on any failure -
 * zlib.failed() tells the two apart from an empty input. */
static NV_TLS int nv_zlib_err = 0;

static nv nv_zlib_decompress(nv data, nv limit) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char *raw = 0;
    size_t rawlen = 0;
    size_t cap = (size_t)nv_as_int(limit);
    char *result;
    nv_zlib_err = 0;
    if (len < 2) {
        nv_zlib_err = 1;
        return nv_str("");
    }
    /* skip the 2 byte zlib header; FDICT (bit 5 of FLG) is not supported */
    if (((unsigned char)bytes[1] & 0x20) != 0) {
        nv_zlib_err = 1;
        return nv_str("");
    }
    if (nv_inflate_raw((const unsigned char *)bytes + 2, (size_t)len - 2, cap, &raw, &rawlen) != 0) {
        free(raw);
        nv_zlib_err = 1;
        return nv_str("");
    }
    result = (char *)nv_alloc_atomic(rawlen + 1);
    memcpy(result, raw, rawlen);
    result[rawlen] = 0;
    free(raw);
    return nv_str_own(result, (int)rawlen);
}

static nv nv_zlib_failed(void) { return nv_bool(nv_zlib_err != 0); }


#endif /* NV_ZLIB_H */
