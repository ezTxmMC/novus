/* nv_bits.h - bitwise operations. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_BITS_H
#define NV_BITS_H

/* ------------------------------------------------------------------ */
/* Bitwise operations                                                  */
/* ------------------------------------------------------------------ */

/* The operators &, |, ^, << and >> on 64 bit integers. A float operand is
 * truncated first, the way an integer declaration truncates one. */
static long long nv_bit_operand(nv v, const char *op) {
    int type = nv_type_of(v);
    if (type != NV_INT && type != NV_FLOAT) {
        nv_error("cannot apply '%s' to %s", op, nv_type_name(v));
    }
    return nv_as_int(v);
}

static nv nv_band(nv l, nv r) {
    return nv_int(nv_bit_operand(l, "&") & nv_bit_operand(r, "&"));
}

static nv nv_bor(nv l, nv r) {
    return nv_int(nv_bit_operand(l, "|") | nv_bit_operand(r, "|"));
}

static nv nv_bxor(nv l, nv r) {
    return nv_int(nv_bit_operand(l, "^") ^ nv_bit_operand(r, "^"));
}

/* A shift of 64 or more is undefined in C; it yields 0 here, and a negative
 * count shifts the other way, so that neither can be a source of surprise. */
static nv nv_shl(nv l, nv r) {
    long long value = nv_bit_operand(l, "<<");
    long long count = nv_bit_operand(r, "<<");
    if (count < 0) {
        count = -count;
        if (count >= 64) {
            return nv_int(value < 0 ? -1 : 0);
        }
        return nv_int(value >> count);
    }
    if (count >= 64) {
        return nv_int(0);
    }
    return nv_int((long long)((unsigned long long)value << count));
}

/* Arithmetic shift: the sign bit is kept, as it is in Rust and Java's >>. */
static nv nv_shr(nv l, nv r) {
    long long value = nv_bit_operand(l, ">>");
    long long count = nv_bit_operand(r, ">>");
    if (count < 0) {
        count = -count;
        if (count >= 64) {
            return nv_int(0);
        }
        return nv_int((long long)((unsigned long long)value << count));
    }
    if (count >= 64) {
        return nv_int(value < 0 ? -1 : 0);
    }
    return nv_int(value >> count);
}

/* The operations that have no operator. */
static nv nv_bits_not(nv v) { return nv_int(~nv_bit_operand(v, "not")); }

/* Logical right shift: zeros are shifted in, Java's >>>. */
static nv nv_bits_ushr(nv l, nv r) {
    unsigned long long value = (unsigned long long)nv_bit_operand(l, "ushr");
    long long count = nv_bit_operand(r, "ushr");
    if (count <= 0 || count >= 64) {
        return nv_int(count == 0 ? (long long)value : 0);
    }
    return nv_int((long long)(value >> count));
}

static nv nv_bits_rotl(nv l, nv r) {
    unsigned long long value = (unsigned long long)nv_bit_operand(l, "rotl");
    long long count = nv_bit_operand(r, "rotl") & 63;
    if (count == 0) {
        return nv_int((long long)value);
    }
    return nv_int((long long)((value << count) | (value >> (64 - count))));
}

static nv nv_bits_rotr(nv l, nv r) {
    unsigned long long value = (unsigned long long)nv_bit_operand(l, "rotr");
    long long count = nv_bit_operand(r, "rotr") & 63;
    if (count == 0) {
        return nv_int((long long)value);
    }
    return nv_int((long long)((value >> count) | (value << (64 - count))));
}

/* Addition and multiplication that wrap at 64 bits instead of overflowing.
 *
 * Signed overflow is undefined in C, so a compiler is free to assume it never
 * happens - which is exactly what an algorithm built on wrapping arithmetic
 * relies on. Doing it unsigned makes the wrap the defined behaviour it has to
 * be. Anything reproducing another language's random numbers needs these. */
static nv nv_bits_wrapping_add(nv a, nv b) {
    unsigned long long x = (unsigned long long)nv_bit_operand(a, "wrappingAdd");
    unsigned long long y = (unsigned long long)nv_bit_operand(b, "wrappingAdd");
    return nv_int((long long)(x + y));
}

static nv nv_bits_wrapping_sub(nv a, nv b) {
    unsigned long long x = (unsigned long long)nv_bit_operand(a, "wrappingSub");
    unsigned long long y = (unsigned long long)nv_bit_operand(b, "wrappingSub");
    return nv_int((long long)(x - y));
}

static nv nv_bits_wrapping_mul(nv a, nv b) {
    unsigned long long x = (unsigned long long)nv_bit_operand(a, "wrappingMul");
    unsigned long long y = (unsigned long long)nv_bit_operand(b, "wrappingMul");
    return nv_int((long long)(x * y));
}

/* The low 32 bits, read as a signed 32 bit number. */
static nv nv_bits_to_i32(nv v) {
    return nv_int((int)(unsigned int)nv_bit_operand(v, "toI32"));
}

/* An unsigned 64 bit value as a double, which a signed cast would get wrong
 * for anything with the top bit set. */
static nv nv_bits_to_unsigned_float(nv v) {
    unsigned long long x = (unsigned long long)nv_bit_operand(v, "toUnsignedFloat");
    return nv_float((double)x);
}

/* The low `count` bits of `value`. */
static nv nv_bits_mask(nv value, nv count) {
    long long bits = nv_bit_operand(count, "mask");
    if (bits <= 0) {
        return nv_int(0);
    }
    if (bits >= 64) {
        return nv_int(nv_bit_operand(value, "mask"));
    }
    return nv_int(nv_bit_operand(value, "mask") & (((long long)1 << bits) - 1));
}

/* How many bits are set. */
static nv nv_bits_count_ones(nv v) {
    unsigned long long value = (unsigned long long)nv_bit_operand(v, "countOnes");
    int n = 0;
    while (value) {
        n += (int)(value & 1);
        value >>= 1;
    }
    return nv_int(n);
}

/* The position of the highest set bit, -1 for zero. */
static nv nv_bits_highest(nv v) {
    unsigned long long value = (unsigned long long)nv_bit_operand(v, "highestBit");
    int n = -1;
    while (value) {
        value >>= 1;
        n++;
    }
    return nv_int(n);
}


#endif /* NV_BITS_H */
