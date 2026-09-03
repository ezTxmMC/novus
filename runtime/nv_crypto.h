/* nv_crypto.h - SHA-1, MD5, AES-128-CFB8 and random bytes. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_CRYPTO_H
#define NV_CRYPTO_H

/* ------------------------------------------------------------------ */
/* Crypto: SHA-1, MD5, AES-128-CFB8, random bytes                      */
/* ------------------------------------------------------------------ */

/* Only what a Minecraft style handshake needs: SHA-1 for the session server
 * digest, MD5 for offline UUIDs (version 3 of "OfflinePlayer:<name>") and
 * AES-128 in CFB-8 for the encrypted stream. All produce raw bytes. */

/* ---- SHA-1 ---- */

typedef struct {
    unsigned int h[5];
    unsigned char block[64];
    size_t len;
    int fill;
} NvSha1;

static unsigned int nv_rotl32(unsigned int v, int n) { return (v << n) | (v >> (32 - n)); }

static void nv_sha1_block(NvSha1 *s, const unsigned char *p) {
    unsigned int w[80];
    unsigned int a, b, c, d, e;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) |
               ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = nv_rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = s->h[0];
    b = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    for (i = 0; i < 80; i++) {
        unsigned int f;
        unsigned int k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        {
            unsigned int t = nv_rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = nv_rotl32(b, 30);
            b = a;
            a = t;
        }
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

static void nv_sha1(const unsigned char *data, size_t len, unsigned char out[20]) {
    NvSha1 s;
    size_t i;
    unsigned long long bits = (unsigned long long)len * 8;
    s.h[0] = 0x67452301u;
    s.h[1] = 0xefcdab89u;
    s.h[2] = 0x98badcfeu;
    s.h[3] = 0x10325476u;
    s.h[4] = 0xc3d2e1f0u;
    s.fill = 0;
    for (i = 0; i < len; i++) {
        s.block[s.fill++] = data[i];
        if (s.fill == 64) {
            nv_sha1_block(&s, s.block);
            s.fill = 0;
        }
    }
    s.block[s.fill++] = 0x80;
    if (s.fill > 56) {
        while (s.fill < 64) {
            s.block[s.fill++] = 0;
        }
        nv_sha1_block(&s, s.block);
        s.fill = 0;
    }
    while (s.fill < 56) {
        s.block[s.fill++] = 0;
    }
    for (i = 0; i < 8; i++) {
        s.block[56 + i] = (unsigned char)((bits >> ((7 - i) * 8)) & 0xff);
    }
    nv_sha1_block(&s, s.block);
    for (i = 0; i < 5; i++) {
        out[i * 4] = (unsigned char)((s.h[i] >> 24) & 0xff);
        out[i * 4 + 1] = (unsigned char)((s.h[i] >> 16) & 0xff);
        out[i * 4 + 2] = (unsigned char)((s.h[i] >> 8) & 0xff);
        out[i * 4 + 3] = (unsigned char)(s.h[i] & 0xff);
    }
}

/* ---- MD5 ---- */

static const unsigned int nv_md5_k[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u,
    0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u,
    0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du,
    0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
    0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u,
    0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu,
    0xeb86d391u};

static const int nv_md5_r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void nv_md5_block(unsigned int h[4], const unsigned char *p) {
    unsigned int w[16];
    unsigned int a = h[0];
    unsigned int b = h[1];
    unsigned int c = h[2];
    unsigned int d = h[3];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = (unsigned int)p[i * 4] | ((unsigned int)p[i * 4 + 1] << 8) |
               ((unsigned int)p[i * 4 + 2] << 16) | ((unsigned int)p[i * 4 + 3] << 24);
    }
    for (i = 0; i < 64; i++) {
        unsigned int f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        {
            unsigned int t = d;
            unsigned int sum = a + f + nv_md5_k[i] + w[g];
            d = c;
            c = b;
            b = b + nv_rotl32(sum, nv_md5_r[i]);
            a = t;
        }
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

static void nv_md5(const unsigned char *data, size_t len, unsigned char out[16]) {
    unsigned int h[4];
    unsigned char block[64];
    size_t i;
    int fill = 0;
    unsigned long long bits = (unsigned long long)len * 8;
    h[0] = 0x67452301u;
    h[1] = 0xefcdab89u;
    h[2] = 0x98badcfeu;
    h[3] = 0x10325476u;
    for (i = 0; i < len; i++) {
        block[fill++] = data[i];
        if (fill == 64) {
            nv_md5_block(h, block);
            fill = 0;
        }
    }
    block[fill++] = 0x80;
    if (fill > 56) {
        while (fill < 64) {
            block[fill++] = 0;
        }
        nv_md5_block(h, block);
        fill = 0;
    }
    while (fill < 56) {
        block[fill++] = 0;
    }
    for (i = 0; i < 8; i++) {
        block[56 + i] = (unsigned char)((bits >> (i * 8)) & 0xff);
    }
    nv_md5_block(h, block);
    for (i = 0; i < 4; i++) {
        out[i * 4] = (unsigned char)(h[i] & 0xff);
        out[i * 4 + 1] = (unsigned char)((h[i] >> 8) & 0xff);
        out[i * 4 + 2] = (unsigned char)((h[i] >> 16) & 0xff);
        out[i * 4 + 3] = (unsigned char)((h[i] >> 24) & 0xff);
    }
}

/* ---- AES-128 ---- */

static const unsigned char nv_aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static unsigned char nv_aes_xtime(unsigned char x) {
    return (unsigned char)((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}

/* AES-128: 11 round keys of 16 bytes. */
static void nv_aes_expand(const unsigned char key[16], unsigned char rk[176]) {
    static const unsigned char rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                           0x20, 0x40, 0x80, 0x1b, 0x36};
    int i;
    memcpy(rk, key, 16);
    for (i = 4; i < 44; i++) {
        unsigned char t[4];
        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % 4 == 0) {
            unsigned char tmp = t[0];
            t[0] = (unsigned char)(nv_aes_sbox[t[1]] ^ rcon[i / 4 - 1]);
            t[1] = nv_aes_sbox[t[2]];
            t[2] = nv_aes_sbox[t[3]];
            t[3] = nv_aes_sbox[tmp];
        }
        rk[i * 4] = (unsigned char)(rk[(i - 4) * 4] ^ t[0]);
        rk[i * 4 + 1] = (unsigned char)(rk[(i - 4) * 4 + 1] ^ t[1]);
        rk[i * 4 + 2] = (unsigned char)(rk[(i - 4) * 4 + 2] ^ t[2]);
        rk[i * 4 + 3] = (unsigned char)(rk[(i - 4) * 4 + 3] ^ t[3]);
    }
}

/* CFB-8 only ever encrypts blocks, in both directions - no inverse cipher. */
static void nv_aes_encrypt_block(const unsigned char rk[176], const unsigned char in[16],
                                 unsigned char out[16]) {
    unsigned char s[16];
    int round;
    int i;
    for (i = 0; i < 16; i++) {
        s[i] = (unsigned char)(in[i] ^ rk[i]);
    }
    for (round = 1; round <= 10; round++) {
        unsigned char t[16];
        for (i = 0; i < 16; i++) {
            s[i] = nv_aes_sbox[s[i]];
        }
        /* ShiftRows on the column major state */
        t[0] = s[0];   t[4] = s[4];   t[8] = s[8];    t[12] = s[12];
        t[1] = s[5];   t[5] = s[9];   t[9] = s[13];   t[13] = s[1];
        t[2] = s[10];  t[6] = s[14];  t[10] = s[2];   t[14] = s[6];
        t[3] = s[15];  t[7] = s[3];   t[11] = s[7];   t[15] = s[11];
        memcpy(s, t, 16);
        if (round != 10) {
            for (i = 0; i < 16; i += 4) {
                unsigned char a0 = s[i];
                unsigned char a1 = s[i + 1];
                unsigned char a2 = s[i + 2];
                unsigned char a3 = s[i + 3];
                unsigned char all = (unsigned char)(a0 ^ a1 ^ a2 ^ a3);
                s[i] = (unsigned char)(a0 ^ all ^ nv_aes_xtime((unsigned char)(a0 ^ a1)));
                s[i + 1] = (unsigned char)(a1 ^ all ^ nv_aes_xtime((unsigned char)(a1 ^ a2)));
                s[i + 2] = (unsigned char)(a2 ^ all ^ nv_aes_xtime((unsigned char)(a2 ^ a3)));
                s[i + 3] = (unsigned char)(a3 ^ all ^ nv_aes_xtime((unsigned char)(a3 ^ a0)));
            }
        }
        for (i = 0; i < 16; i++) {
            s[i] = (unsigned char)(s[i] ^ rk[round * 16 + i]);
        }
    }
    memcpy(out, s, 16);
}

/* A CFB-8 stream keeps its shift register between calls, so one handle per
 * direction per connection. */
typedef struct {
    unsigned char rk[176];
    unsigned char iv[16];
    int encrypt;
    int used;
} NvCipher;

#define NV_CIPHER_MAX 4096
static NvCipher nv_ciphers[NV_CIPHER_MAX];
static int nv_cipher_count = 0;

static nv nv_crypto_cipher_new(nv key, nv iv, nv encrypt) {
    int klen;
    int ivlen;
    const char *k = nv_bin(key, &klen);
    const char *v = nv_bin(iv, &ivlen);
    int slot;
    if (klen != 16 || ivlen != 16) {
        return nv_int(-1);
    }
    for (slot = 0; slot < nv_cipher_count; slot++) {
        if (!nv_ciphers[slot].used) {
            break;
        }
    }
    if (slot == nv_cipher_count) {
        if (nv_cipher_count >= NV_CIPHER_MAX) {
            return nv_int(-1);
        }
        nv_cipher_count++;
    }
    nv_aes_expand((const unsigned char *)k, nv_ciphers[slot].rk);
    memcpy(nv_ciphers[slot].iv, v, 16);
    nv_ciphers[slot].encrypt = nv_truthy(encrypt);
    nv_ciphers[slot].used = 1;
    return nv_int(slot);
}

static nv nv_crypto_cipher_free(nv handle) {
    long long h = nv_as_int(handle);
    if (h >= 0 && h < nv_cipher_count) {
        nv_ciphers[h].used = 0;
    }
    return nv_nil;
}

/* CFB-8: encrypt the register, XOR one byte, shift the ciphertext byte in. */
static nv nv_crypto_cipher_update(nv handle, nv data) {
    long long h = nv_as_int(handle);
    int len;
    const char *in = nv_bin(data, &len);
    NvCipher *c;
    char *out;
    int i;
    if (h < 0 || h >= nv_cipher_count || !nv_ciphers[h].used) {
        return nv_str("");
    }
    c = &nv_ciphers[h];
    out = (char *)nv_alloc_atomic((size_t)len + 1);
    for (i = 0; i < len; i++) {
        unsigned char keystream[16];
        unsigned char plain = (unsigned char)in[i];
        unsigned char cipher;
        nv_aes_encrypt_block(c->rk, c->iv, keystream);
        cipher = (unsigned char)(plain ^ keystream[0]);
        memmove(c->iv, c->iv + 1, 15);
        c->iv[15] = c->encrypt ? cipher : plain;
        out[i] = (char)cipher;
    }
    out[len] = 0;
    return nv_str_own(out, len);
}

/* SHA-256, as FIPS 180-4 defines it. Written out here rather than linked
 * against a library, so that a Novus program still needs nothing but a C
 * compiler. */

static const uint32_t nv_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t nv_sha256_rotr(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

static void nv_sha256_block(uint32_t *h, const unsigned char *p) {
    uint32_t w[64], a, b, c, d, e, f, g, hh;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = nv_sha256_rotr(w[i - 15], 7) ^ nv_sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = nv_sha256_rotr(w[i - 2], 17) ^ nv_sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        uint32_t s1 = nv_sha256_rotr(e, 6) ^ nv_sha256_rotr(e, 11) ^ nv_sha256_rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + s1 + ch + nv_sha256_k[i] + w[i];
        uint32_t s0 = nv_sha256_rotr(a, 2) ^ nv_sha256_rotr(a, 13) ^ nv_sha256_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void nv_sha256(const unsigned char *data, size_t len, unsigned char *out) {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    unsigned char tail[128];
    size_t i, rest, total;
    uint64_t bits = (uint64_t)len * 8;
    for (i = 0; i + 64 <= len; i += 64) {
        nv_sha256_block(h, data + i);
    }
    rest = len - i;
    memcpy(tail, data + i, rest);
    tail[rest++] = 0x80;
    total = (rest > 56) ? 128 : 64;
    memset(tail + rest, 0, total - rest);
    for (i = 0; i < 8; i++) {
        tail[total - 1 - i] = (unsigned char)(bits >> (i * 8));
    }
    nv_sha256_block(h, tail);
    if (total == 128) {
        nv_sha256_block(h, tail + 64);
    }
    for (i = 0; i < 8; i++) {
        out[i * 4] = (unsigned char)(h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)h[i];
    }
}

/* ---- hashes and randomness for Novus ---- */

static nv nv_crypto_sha256(nv data) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char digest[32];
    nv_sha256((const unsigned char *)bytes, (size_t)len, digest);
    return nv_strn((const char *)digest, 32);
}

static nv nv_crypto_sha1(nv data) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char digest[20];
    nv_sha1((const unsigned char *)bytes, (size_t)len, digest);
    return nv_strn((const char *)digest, 20);
}

static nv nv_crypto_md5(nv data) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char digest[16];
    nv_md5((const unsigned char *)bytes, (size_t)len, digest);
    return nv_strn((const char *)digest, 16);
}

/* Minecraft's session digest: SHA-1 read as a signed big endian number and
 * printed in hex, negative values as the two's complement with a leading "-". */
static nv nv_crypto_mc_digest(nv data) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char digest[20];
    int negative;
    int i;
    char hex[48];
    int at = 0;
    int leading = 1;
    nv_sha1((const unsigned char *)bytes, (size_t)len, digest);
    negative = (digest[0] & 0x80) != 0;
    if (negative) {
        int carry = 1;
        for (i = 19; i >= 0; i--) {
            int value = (~digest[i] & 0xff) + carry;
            digest[i] = (unsigned char)(value & 0xff);
            carry = value >> 8;
        }
        hex[at++] = '-';
    }
    for (i = 0; i < 20; i++) {
        static const char *digits = "0123456789abcdef";
        int hi = digest[i] >> 4;
        int lo = digest[i] & 0xf;
        if (leading && hi == 0) {
            /* skip */
        } else {
            hex[at++] = digits[hi];
            leading = 0;
        }
        if (leading && lo == 0) {
            continue;
        }
        hex[at++] = digits[lo];
        leading = 0;
    }
    if (at == 0 || (at == 1 && hex[0] == '-')) {
        hex[at++] = '0';
    }
    return nv_strn(hex, at);
}

/* Fills `out` with `count` strong random bytes, falling back to rand() only
 * where /dev/urandom is not there. */
static void nv_random_bytes_into(unsigned char *out, int count) {
    if (count <= 0) {
        return;
    }
#ifndef _WIN32
    {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            size_t got = fread(out, 1, (size_t)count, f);
            fclose(f);
            if (got == (size_t)count) {
                return;
            }
        }
    }
#endif
    {
        static int seeded = 0;
        int i;
        if (!seeded) {
            srand((unsigned)time(0) ^ (unsigned)NV_GETPID());
            seeded = 1;
        }
        for (i = 0; i < count; i++) {
            out[i] = (unsigned char)(rand() & 0xff);
        }
    }
}

static nv nv_crypto_random(nv count) {
    long long n = nv_as_int(count);
    char *buf;
    if (n <= 0) {
        return nv_str("");
    }
    buf = (char *)nv_alloc_atomic((size_t)n + 1);
    nv_random_bytes_into((unsigned char *)buf, (int)n);
    buf[n] = 0;
    return nv_str_own(buf, (int)n);
}

/* Canonical UUID text ("xxxxxxxx-xxxx-...") of 16 raw bytes. */
static nv nv_crypto_uuid_text(nv data) {
    static const char *digits = "0123456789abcdef";
    int len;
    const char *bytes = nv_bin(data, &len);
    char out[36];
    int at = 0;
    int i;
    if (len < 16) {
        return nv_str("");
    }
    for (i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out[at++] = '-';
        }
        out[at++] = digits[((unsigned char)bytes[i]) >> 4];
        out[at++] = digits[((unsigned char)bytes[i]) & 0xf];
    }
    return nv_strn(out, at);
}


#endif /* NV_CRYPTO_H */
