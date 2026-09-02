/* nv_rsa.h - RSA key pairs, DER and PKCS#1. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_RSA_H
#define NV_RSA_H

/* ------------------------------------------------------------------ */
/* RSA                                                                 */
/* ------------------------------------------------------------------ */

/* Enough RSA for a key exchange: generate a key pair, hand out the public
 * key as DER, and decrypt what was sent to it with PKCS#1 v1.5 padding.
 *
 * Big integers are fixed length arrays of 32 bit limbs, least significant
 * first. Modular arithmetic goes through Montgomery multiplication, which
 * needs an odd modulus - an RSA modulus always is - and replaces the
 * division a modexp would otherwise do per step. */

#define NV_BN_LIMBS 64 /* 2048 bits, twice what a 1024 bit key needs */

typedef struct {
    unsigned int limb[NV_BN_LIMBS];
    int len; /* limbs in use */
} NvBn;

static void nv_bn_zero(NvBn *a) {
    memset(a->limb, 0, sizeof(a->limb));
    a->len = 0;
}

static void nv_bn_trim(NvBn *a) {
    while (a->len > 0 && a->limb[a->len - 1] == 0) {
        a->len--;
    }
}

static void nv_bn_from_u32(NvBn *a, unsigned int v) {
    nv_bn_zero(a);
    a->limb[0] = v;
    a->len = v ? 1 : 0;
}

/* Big endian bytes in, which is how every RSA value travels. */
static void nv_bn_from_bytes(NvBn *a, const unsigned char *bytes, int len) {
    int i;
    nv_bn_zero(a);
    for (i = 0; i < len; i++) {
        int index = (len - 1 - i) / 4;
        int shift = ((len - 1 - i) % 4) * 8;
        if (index >= NV_BN_LIMBS) {
            break;
        }
        a->limb[index] |= (unsigned int)bytes[i] << shift;
        if (index + 1 > a->len) {
            a->len = index + 1;
        }
    }
    nv_bn_trim(a);
}

/* Big endian bytes out, left padded to `len`. */
static void nv_bn_to_bytes(const NvBn *a, unsigned char *out, int len) {
    int i;
    for (i = 0; i < len; i++) {
        int index = (len - 1 - i) / 4;
        int shift = ((len - 1 - i) % 4) * 8;
        out[i] = (unsigned char)(index < NV_BN_LIMBS ? (a->limb[index] >> shift) & 0xff : 0);
    }
}

static int nv_bn_cmp(const NvBn *a, const NvBn *b) {
    int i = a->len > b->len ? a->len : b->len;
    while (i-- > 0) {
        unsigned int x = i < a->len ? a->limb[i] : 0;
        unsigned int y = i < b->len ? b->limb[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

static int nv_bn_is_zero(const NvBn *a) { return a->len == 0; }

static int nv_bn_bit(const NvBn *a, int index) {
    int limb = index / 32;
    if (limb >= a->len) {
        return 0;
    }
    return (int)((a->limb[limb] >> (index % 32)) & 1);
}

static int nv_bn_bits(const NvBn *a) {
    int top;
    unsigned int word;
    int n = 0;
    if (a->len == 0) {
        return 0;
    }
    top = a->len - 1;
    word = a->limb[top];
    while (word) {
        word >>= 1;
        n++;
    }
    return top * 32 + n;
}

/* out = a + b */
static void nv_bn_add(NvBn *out, const NvBn *a, const NvBn *b) {
    unsigned long long carry = 0;
    int n = a->len > b->len ? a->len : b->len;
    int i;
    for (i = 0; i < n || carry; i++) {
        unsigned long long sum = carry;
        if (i < a->len) {
            sum += a->limb[i];
        }
        if (i < b->len) {
            sum += b->limb[i];
        }
        if (i >= NV_BN_LIMBS) {
            break;
        }
        out->limb[i] = (unsigned int)(sum & 0xffffffffu);
        carry = sum >> 32;
    }
    {
        int j;
        for (j = i; j < NV_BN_LIMBS; j++) {
            out->limb[j] = 0;
        }
    }
    out->len = i;
    nv_bn_trim(out);
}

/* out = a - b, assuming a >= b */
static void nv_bn_sub(NvBn *out, const NvBn *a, const NvBn *b) {
    long long borrow = 0;
    int i;
    for (i = 0; i < NV_BN_LIMBS; i++) {
        long long diff = (long long)(i < a->len ? a->limb[i] : 0) - borrow -
                         (long long)(i < b->len ? b->limb[i] : 0);
        if (diff < 0) {
            diff += 0x100000000LL;
            borrow = 1;
        } else {
            borrow = 0;
        }
        out->limb[i] = (unsigned int)diff;
    }
    out->len = a->len;
    nv_bn_trim(out);
}

/* out = a * b, schoolbook. */
static void nv_bn_mul(NvBn *out, const NvBn *a, const NvBn *b) {
    NvBn result;
    int i;
    nv_bn_zero(&result);
    for (i = 0; i < a->len; i++) {
        unsigned long long carry = 0;
        int j;
        for (j = 0; j < b->len || carry; j++) {
            unsigned long long cur;
            if (i + j >= NV_BN_LIMBS) {
                break;
            }
            cur = result.limb[i + j] + carry;
            if (j < b->len) {
                cur += (unsigned long long)a->limb[i] * b->limb[j];
            }
            result.limb[i + j] = (unsigned int)(cur & 0xffffffffu);
            carry = cur >> 32;
        }
    }
    result.len = a->len + b->len;
    if (result.len > NV_BN_LIMBS) {
        result.len = NV_BN_LIMBS;
    }
    nv_bn_trim(&result);
    *out = result;
}

/* out = a >> 1 */
static void nv_bn_shr1(NvBn *out, const NvBn *a) {
    int i;
    for (i = 0; i < NV_BN_LIMBS - 1; i++) {
        out->limb[i] = (a->limb[i] >> 1) | (a->limb[i + 1] << 31);
    }
    out->limb[NV_BN_LIMBS - 1] = a->limb[NV_BN_LIMBS - 1] >> 1;
    out->len = a->len;
    nv_bn_trim(out);
}

/* out = a << 1 */
static void nv_bn_shl1(NvBn *out, const NvBn *a) {
    int i;
    unsigned int carry = 0;
    for (i = 0; i < NV_BN_LIMBS; i++) {
        unsigned int next = a->limb[i] >> 31;
        out->limb[i] = (a->limb[i] << 1) | carry;
        carry = next;
    }
    out->len = a->len + 1 > NV_BN_LIMBS ? NV_BN_LIMBS : a->len + 1;
    nv_bn_trim(out);
}

/* Bit by bit long division. Used only where speed does not matter: reducing
 * a value once, and the extended gcd. */
static void nv_bn_divmod(NvBn *quotient, NvBn *remainder, const NvBn *a, const NvBn *m) {
    NvBn q;
    NvBn r;
    int i;
    nv_bn_zero(&q);
    nv_bn_zero(&r);
    if (nv_bn_is_zero(m)) {
        *quotient = q;
        *remainder = r;
        return;
    }
    for (i = nv_bn_bits(a) - 1; i >= 0; i--) {
        NvBn shifted;
        nv_bn_shl1(&shifted, &r);
        r = shifted;
        if (nv_bn_bit(a, i)) {
            r.limb[0] |= 1;
            if (r.len == 0) {
                r.len = 1;
            }
        }
        if (nv_bn_cmp(&r, m) >= 0) {
            NvBn reduced;
            nv_bn_sub(&reduced, &r, m);
            r = reduced;
            q.limb[i / 32] |= 1u << (i % 32);
            if (i / 32 + 1 > q.len) {
                q.len = i / 32 + 1;
            }
        }
    }
    nv_bn_trim(&q);
    nv_bn_trim(&r);
    if (quotient) {
        *quotient = q;
    }
    if (remainder) {
        *remainder = r;
    }
}

static void nv_bn_mod(NvBn *out, const NvBn *a, const NvBn *m) {
    nv_bn_divmod(0, out, a, m);
}

/* ---- Montgomery arithmetic ---- */

typedef struct {
    NvBn n;         /* the modulus, odd */
    NvBn rr;        /* R^2 mod n, for entering the domain */
    unsigned int n0inv; /* -n^-1 mod 2^32 */
    int len;        /* limbs of n */
} NvMont;

/* -n^-1 mod 2^32 by Newton iteration; exact after five steps for 32 bits. */
static unsigned int nv_mont_n0inv(unsigned int n0) {
    unsigned int x = 1;
    int i;
    for (i = 0; i < 5; i++) {
        x = x * (2u - n0 * x);
    }
    return (unsigned int)(0u - x);
}

static int nv_mont_init(NvMont *mont, const NvBn *n) {
    NvBn r;
    int i;
    if (n->len == 0 || (n->limb[0] & 1) == 0) {
        return -1; /* Montgomery needs an odd modulus */
    }
    mont->n = *n;
    mont->len = n->len;
    mont->n0inv = nv_mont_n0inv(n->limb[0]);
    /* R^2 mod n: start at 1 and double 2 * len * 32 times */
    nv_bn_from_u32(&r, 1);
    for (i = 0; i < 2 * mont->len * 32; i++) {
        NvBn doubled;
        nv_bn_shl1(&doubled, &r);
        if (nv_bn_cmp(&doubled, n) >= 0) {
            NvBn reduced;
            nv_bn_sub(&reduced, &doubled, n);
            r = reduced;
        } else {
            r = doubled;
        }
    }
    mont->rr = r;
    return 0;
}

/* out = a * b * R^-1 mod n (CIOS). */
static void nv_mont_mul(NvBn *out, const NvBn *a, const NvBn *b, const NvMont *mont) {
    unsigned int t[NV_BN_LIMBS + 2];
    int len = mont->len;
    int i;
    memset(t, 0, sizeof(unsigned int) * (size_t)(len + 2));
    for (i = 0; i < len; i++) {
        unsigned long long carry = 0;
        unsigned int m;
        int j;
        unsigned int ai = i < a->len ? a->limb[i] : 0;
        for (j = 0; j < len; j++) {
            unsigned long long cur = (unsigned long long)t[j] + carry +
                                     (unsigned long long)ai * (j < b->len ? b->limb[j] : 0);
            t[j] = (unsigned int)(cur & 0xffffffffu);
            carry = cur >> 32;
        }
        {
            unsigned long long cur = (unsigned long long)t[len] + carry;
            t[len] = (unsigned int)(cur & 0xffffffffu);
            t[len + 1] = (unsigned int)(cur >> 32);
        }
        m = (unsigned int)((unsigned long long)t[0] * mont->n0inv);
        carry = 0;
        for (j = 0; j < len; j++) {
            unsigned long long cur =
                (unsigned long long)t[j] + carry + (unsigned long long)m * mont->n.limb[j];
            t[j] = (unsigned int)(cur & 0xffffffffu);
            carry = cur >> 32;
        }
        {
            unsigned long long cur = (unsigned long long)t[len] + carry;
            t[len] = (unsigned int)(cur & 0xffffffffu);
            t[len + 1] += (unsigned int)(cur >> 32);
        }
        /* shift down by one limb */
        for (j = 0; j <= len; j++) {
            t[j] = t[j + 1];
        }
        t[len + 1] = 0;
    }
    nv_bn_zero(out);
    for (i = 0; i < len + 1 && i < NV_BN_LIMBS; i++) {
        out->limb[i] = t[i];
    }
    out->len = len + 1 > NV_BN_LIMBS ? NV_BN_LIMBS : len + 1;
    nv_bn_trim(out);
    if (nv_bn_cmp(out, &mont->n) >= 0) {
        NvBn reduced;
        nv_bn_sub(&reduced, out, &mont->n);
        *out = reduced;
    }
}

/* out = base^exponent mod n, square and multiply in the Montgomery domain. */
static void nv_bn_modexp(NvBn *out, const NvBn *base, const NvBn *exponent, const NvBn *n) {
    NvMont mont;
    NvBn reduced;
    NvBn one;
    NvBn acc;
    NvBn factor;
    int i;
    if (nv_mont_init(&mont, n) != 0) {
        nv_bn_from_u32(out, 0);
        return;
    }
    nv_bn_mod(&reduced, base, n);
    nv_bn_from_u32(&one, 1);
    nv_mont_mul(&acc, &one, &mont.rr, &mont);     /* 1 in the domain */
    nv_mont_mul(&factor, &reduced, &mont.rr, &mont); /* base in the domain */
    for (i = 0; i < nv_bn_bits(exponent); i++) {
        if (nv_bn_bit(exponent, i)) {
            NvBn product;
            nv_mont_mul(&product, &acc, &factor, &mont);
            acc = product;
        }
        {
            NvBn squared;
            nv_mont_mul(&squared, &factor, &factor, &mont);
            factor = squared;
        }
    }
    nv_mont_mul(out, &acc, &one, &mont); /* back out of the domain */
}

/* ---- primes ---- */

static const unsigned int nv_small_primes[] = {
    3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,
    71,  73,  79,  83,  89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157,
    163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251};

/* a mod m for a small m, by folding the limbs. */
static unsigned int nv_bn_mod_u32(const NvBn *a, unsigned int m) {
    unsigned long long rest = 0;
    int i;
    for (i = a->len - 1; i >= 0; i--) {
        rest = ((rest << 32) | a->limb[i]) % m;
    }
    return (unsigned int)rest;
}

/* Miller-Rabin with `rounds` random bases. */
static int nv_bn_is_probable_prime(const NvBn *n, int rounds) {
    NvBn nMinusOne;
    NvBn d;
    NvBn one;
    NvBn two;
    int s = 0;
    int i;
    size_t p;
    if (n->len == 0 || (n->limb[0] & 1) == 0) {
        return 0;
    }
    for (p = 0; p < sizeof(nv_small_primes) / sizeof(nv_small_primes[0]); p++) {
        if (nv_bn_mod_u32(n, nv_small_primes[p]) == 0) {
            return 0;
        }
    }
    nv_bn_from_u32(&one, 1);
    nv_bn_from_u32(&two, 2);
    nv_bn_sub(&nMinusOne, n, &one);
    d = nMinusOne;
    while (nv_bn_bit(&d, 0) == 0 && !nv_bn_is_zero(&d)) {
        NvBn half;
        nv_bn_shr1(&half, &d);
        d = half;
        s++;
    }
    for (i = 0; i < rounds; i++) {
        NvBn a;
        NvBn x;
        int j;
        int witness;
        nv_bn_from_u32(&a, nv_small_primes[i % (int)(sizeof(nv_small_primes) / sizeof(nv_small_primes[0]))]);
        nv_bn_modexp(&x, &a, &d, n);
        if (nv_bn_cmp(&x, &one) == 0 || nv_bn_cmp(&x, &nMinusOne) == 0) {
            continue;
        }
        witness = 1;
        for (j = 0; j < s - 1; j++) {
            NvBn squared;
            nv_bn_modexp(&squared, &x, &two, n);
            x = squared;
            if (nv_bn_cmp(&x, &nMinusOne) == 0) {
                witness = 0;
                break;
            }
        }
        if (witness) {
            return 0;
        }
    }
    return 1;
}

/* A random prime of `bits` bits, with the top two bits set so that the
 * product of two of them has exactly twice the length. */
static void nv_bn_random_prime(NvBn *out, int bits) {
    unsigned char buffer[256];
    int bytes = bits / 8;
    for (;;) {
        nv_random_bytes_into(buffer, bytes);
        buffer[0] |= 0xc0;        /* the top two bits */
        buffer[bytes - 1] |= 1;   /* odd */
        nv_bn_from_bytes(out, buffer, bytes);
        if (nv_bn_is_probable_prime(out, 8)) {
            return;
        }
    }
}

/* d with d * e = 1 mod m, by the extended Euclidean algorithm on
 * non-negative values (the classic formulation with a sign flag). */
static int nv_bn_modinv(NvBn *out, const NvBn *e, const NvBn *m) {
    NvBn r0 = *m;
    NvBn r1 = *e;
    NvBn t0;
    NvBn t1;
    int t0neg = 0;
    int t1neg = 0;
    nv_bn_from_u32(&t0, 0);
    nv_bn_from_u32(&t1, 1);
    while (!nv_bn_is_zero(&r1)) {
        NvBn q;
        NvBn rest;
        NvBn product;
        NvBn next;
        int nextNeg;
        nv_bn_divmod(&q, &rest, &r0, &r1);
        r0 = r1;
        r1 = rest;
        nv_bn_mul(&product, &q, &t1);
        /* next = t0 - q * t1, tracking the sign by hand */
        if (t0neg == t1neg) {
            if (nv_bn_cmp(&t0, &product) >= 0) {
                nv_bn_sub(&next, &t0, &product);
                nextNeg = t0neg;
            } else {
                nv_bn_sub(&next, &product, &t0);
                nextNeg = !t0neg;
            }
        } else {
            nv_bn_add(&next, &t0, &product);
            nextNeg = t0neg;
        }
        t0 = t1;
        t0neg = t1neg;
        t1 = next;
        t1neg = nextNeg;
    }
    {
        NvBn one;
        nv_bn_from_u32(&one, 1);
        if (nv_bn_cmp(&r0, &one) != 0) {
            return -1; /* not invertible */
        }
    }
    if (t0neg) {
        NvBn positive;
        NvBn reduced;
        nv_bn_mod(&reduced, &t0, m);
        nv_bn_sub(&positive, m, &reduced);
        nv_bn_mod(out, &positive, m);
        return 0;
    }
    nv_bn_mod(out, &t0, m);
    return 0;
}

/* ---- key pairs ---- */

typedef struct {
    NvBn n;
    NvBn e;
    NvBn d;
    int bits;
    int used;
} NvRsaKey;

#define NV_RSA_MAX 8
static NvRsaKey nv_rsa_keys[NV_RSA_MAX];
static int nv_rsa_count = 0;

static nv nv_crypto_rsa_generate(nv bitsValue) {
    int bits = (int)nv_as_int(bitsValue);
    int slot;
    NvBn p;
    NvBn q;
    NvBn one;
    NvBn pMinus;
    NvBn qMinus;
    NvBn phi;
    NvRsaKey *key;
    if (bits != 1024 && bits != 512 && bits != 2048) {
        return nv_int(-1);
    }
    for (slot = 0; slot < nv_rsa_count; slot++) {
        if (!nv_rsa_keys[slot].used) {
            break;
        }
    }
    if (slot == nv_rsa_count) {
        if (nv_rsa_count >= NV_RSA_MAX) {
            return nv_int(-1);
        }
        nv_rsa_count++;
    }
    key = &nv_rsa_keys[slot];
    nv_bn_from_u32(&one, 1);
    nv_bn_from_u32(&key->e, 65537);
    for (;;) {
        nv_bn_random_prime(&p, bits / 2);
        nv_bn_random_prime(&q, bits / 2);
        if (nv_bn_cmp(&p, &q) == 0) {
            continue;
        }
        nv_bn_mul(&key->n, &p, &q);
        nv_bn_sub(&pMinus, &p, &one);
        nv_bn_sub(&qMinus, &q, &one);
        nv_bn_mul(&phi, &pMinus, &qMinus);
        if (nv_bn_modinv(&key->d, &key->e, &phi) == 0) {
            break;
        }
    }
    key->bits = bits;
    key->used = 1;
    return nv_int(slot);
}

static nv nv_crypto_rsa_free(nv handle) {
    long long h = nv_as_int(handle);
    if (h >= 0 && h < nv_rsa_count) {
        nv_rsa_keys[h].used = 0;
    }
    return nv_nil;
}

/* ---- DER ---- */

/* A DER length: short form below 128, else the byte count then the bytes. */
static int nv_der_len(unsigned char *out, int length) {
    if (length < 128) {
        out[0] = (unsigned char)length;
        return 1;
    }
    if (length < 256) {
        out[0] = 0x81;
        out[1] = (unsigned char)length;
        return 2;
    }
    out[0] = 0x82;
    out[1] = (unsigned char)((length >> 8) & 0xff);
    out[2] = (unsigned char)(length & 0xff);
    return 3;
}

/* An INTEGER, with the leading zero DER wants when the top bit is set. */
static int nv_der_integer(unsigned char *out, const NvBn *value) {
    unsigned char raw[NV_BN_LIMBS * 4];
    int bytes = (nv_bn_bits(value) + 7) / 8;
    int at = 0;
    int pad;
    if (bytes == 0) {
        bytes = 1;
    }
    nv_bn_to_bytes(value, raw, bytes);
    pad = (raw[0] & 0x80) ? 1 : 0;
    out[at++] = 0x02;
    at += nv_der_len(out + at, bytes + pad);
    if (pad) {
        out[at++] = 0x00;
    }
    memcpy(out + at, raw, (size_t)bytes);
    return at + bytes;
}

/* The public key as a SubjectPublicKeyInfo, which is what the handshake
 * expects: an AlgorithmIdentifier of rsaEncryption plus a BIT STRING holding
 * the RSAPublicKey SEQUENCE of modulus and exponent. */
static nv nv_crypto_rsa_public_der(nv handle) {
    long long h = nv_as_int(handle);
    unsigned char inner[NV_BN_LIMBS * 4 + 64];
    unsigned char rsaKey[NV_BN_LIMBS * 4 + 96];
    unsigned char out[NV_BN_LIMBS * 4 + 160];
    static const unsigned char algorithm[] = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
                                              0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    NvRsaKey *key;
    int innerLen = 0;
    int rsaLen = 0;
    int at = 0;
    int bodyLen;
    unsigned char lengthBytes[3];
    int lengthSize;
    if (h < 0 || h >= nv_rsa_count || !nv_rsa_keys[h].used) {
        return nv_str("");
    }
    key = &nv_rsa_keys[h];
    innerLen += nv_der_integer(inner + innerLen, &key->n);
    innerLen += nv_der_integer(inner + innerLen, &key->e);

    /* SEQUENCE { modulus, exponent } */
    rsaKey[rsaLen++] = 0x30;
    rsaLen += nv_der_len(rsaKey + rsaLen, innerLen);
    memcpy(rsaKey + rsaLen, inner, (size_t)innerLen);
    rsaLen += innerLen;

    /* the outer SEQUENCE { algorithm, BIT STRING { rsaKey } } */
    /* algorithm, then the BIT STRING: its tag, its length, the unused-bits
     * byte and the key itself. */
    bodyLen = (int)sizeof(algorithm) + 1 + nv_der_len(lengthBytes, rsaLen + 1) + 1 + rsaLen;
    out[at++] = 0x30;
    at += nv_der_len(out + at, bodyLen);
    memcpy(out + at, algorithm, sizeof(algorithm));
    at += (int)sizeof(algorithm);
    out[at++] = 0x03; /* BIT STRING */
    lengthSize = nv_der_len(lengthBytes, rsaLen + 1);
    memcpy(out + at, lengthBytes, (size_t)lengthSize);
    at += lengthSize;
    out[at++] = 0x00; /* no unused bits */
    memcpy(out + at, rsaKey, (size_t)rsaLen);
    at += rsaLen;
    return nv_strn((const char *)out, at);
}

/* Decrypts a PKCS#1 v1.5 block and returns the payload, "" when the padding
 * is not what it must be. */
static nv nv_crypto_rsa_decrypt(nv handle, nv data) {
    long long h = nv_as_int(handle);
    int len;
    const char *bytes = nv_bin(data, &len);
    NvBn cipher;
    NvBn plain;
    unsigned char block[NV_BN_LIMBS * 4];
    int size;
    int at;
    NvRsaKey *key;
    if (h < 0 || h >= nv_rsa_count || !nv_rsa_keys[h].used) {
        return nv_str("");
    }
    key = &nv_rsa_keys[h];
    size = key->bits / 8;
    if (len > size) {
        return nv_str("");
    }
    nv_bn_from_bytes(&cipher, (const unsigned char *)bytes, len);
    if (nv_bn_cmp(&cipher, &key->n) >= 0) {
        return nv_str("");
    }
    nv_bn_modexp(&plain, &cipher, &key->d, &key->n);
    nv_bn_to_bytes(&plain, block, size);
    /* 00 02 <at least eight non-zero bytes> 00 <payload> */
    if (block[0] != 0x00 || block[1] != 0x02) {
        return nv_str("");
    }
    at = 2;
    while (at < size && block[at] != 0x00) {
        at++;
    }
    if (at >= size || at < 10) {
        return nv_str("");
    }
    at++;
    return nv_strn((const char *)block + at, size - at);
}

/* Encrypting is only needed to test the pair; the client does the real one. */
static nv nv_crypto_rsa_encrypt(nv handle, nv data) {
    long long h = nv_as_int(handle);
    int len;
    const char *bytes = nv_bin(data, &len);
    NvBn plain;
    NvBn cipher;
    unsigned char block[NV_BN_LIMBS * 4];
    unsigned char out[NV_BN_LIMBS * 4];
    int size;
    int at;
    NvRsaKey *key;
    if (h < 0 || h >= nv_rsa_count || !nv_rsa_keys[h].used) {
        return nv_str("");
    }
    key = &nv_rsa_keys[h];
    size = key->bits / 8;
    if (len > size - 11) {
        return nv_str("");
    }
    block[0] = 0x00;
    block[1] = 0x02;
    for (at = 2; at < size - len - 1; at++) {
        unsigned char filler;
        do {
            nv_random_bytes_into(&filler, 1);
        } while (filler == 0);
        block[at] = filler;
    }
    block[at++] = 0x00;
    memcpy(block + at, bytes, (size_t)len);
    nv_bn_from_bytes(&plain, block, size);
    nv_bn_modexp(&cipher, &plain, &key->e, &key->n);
    nv_bn_to_bytes(&cipher, out, size);
    return nv_strn((const char *)out, size);
}


#endif /* NV_RSA_H */
