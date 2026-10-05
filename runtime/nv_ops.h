/* nv_ops.h - operators, integer fast paths, indexing and members. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_OPS_H
#define NV_OPS_H

/* ------------------------------------------------------------------ */
/* Operators                                                           */
/* ------------------------------------------------------------------ */

/* Concatenation. The result gets spare capacity, and when the left
 * operand still owns the end of its buffer the right side is appended in
 * place, so `s = s + x` loops run in amortized linear time. Older values
 * sharing the buffer keep their length; nv_cstr() copies them on demand. */
/* How much room a concatenation asks for.
 *
 * Most concatenations are one-shot - `"a" + b` in an argument, a key, a
 * message - and never grow again, so they get exactly what they need. A left
 * side that already owns a buffer is the second or later step of `s = s + x`,
 * and that one doubles: the copies stay amortized O(1) and the spare room
 * stays a bounded multiple of the string. Telling the two apart is what
 * `owns` is for. */
#define NV_STR_EXACT(need) ((need) + 1)
#define NV_STR_GROW(need) ((need) * 2 + 16)
#define NV_STR_CAP(l, need) \
    (nv_type_of(l) == NV_STR && (l)->owns ? NV_STR_GROW(need) : NV_STR_EXACT(need))

static nv nv_concat(nv l, nv r) {
    const char *ls;
    const char *rs;
    size_t ll, rl, cap;
    char *buf;
    nv v;
    if (nv_type_of(l) == NV_STR) {
        ls = l->s;
        ll = (size_t)l->slen;
    } else {
        ls = nv_display(l);
        ll = strlen(ls);
    }
    if (nv_type_of(r) == NV_STR) {
        rs = r->s;
        rl = (size_t)r->slen;
    } else {
        rs = nv_display(r);
        rl = strlen(rs);
    }
    if (rl == 0 && nv_type_of(l) == NV_STR) {
        return l;
    }
    nv_check_text_length(ll + rl);
    if (nv_type_of(l) == NV_STR && l->owns && l->s[l->slen] == 0 && rl > 0 && rs[0] != 0 &&
        nv_buf_cap(l->s) > ll + rl) {
        buf = (char *)l->s;
        memmove(buf + ll, rs, rl);
        buf[ll + rl] = 0;
        v = nv_new(NV_STR);
        v->s = buf;
        v->slen = (int)(ll + rl);
        v->owns = 1;
        return v;
    }
    cap = NV_STR_CAP(l, ll + rl);
    buf = nv_buf_alloc(cap);
    memcpy(buf, ls, ll);
    memcpy(buf + ll, rs, rl);
    buf[ll + rl] = 0;
    v = nv_new(NV_STR);
    v->s = buf;
    v->slen = (int)(ll + rl);
    v->owns = 1;
    return v;
}

static nv nv_add(nv l, nv r);

/* `a + b + c + ...` as a single call. The chain is left associative, so the
 * operands before the first string are added arithmetically and everything
 * from there on goes into one buffer - instead of one string value, one
 * length walk and one copy per step. */
static nv nv_add_chain(int n, ...) {
    nv parts[16];
    const char *strs[16];
    size_t lens[16];
    va_list ap;
    int i, j, first = -1, m = 0;
    nv acc;
    size_t total = 0, at, cap;
    char *buf;
    nv v;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        parts[i] = va_arg(ap, nv);
    }
    va_end(ap);
    for (i = 0; i < n; i++) {
        if (nv_type_of(parts[i]) == NV_STR) {
            first = i;
            break;
        }
    }
    acc = parts[0];
    if (first < 0) {
        for (i = 1; i < n; i++) {
            acc = nv_add(acc, parts[i]);
        }
        return acc;
    }
    for (i = 1; i <= first; i++) {
        acc = nv_add(acc, parts[i]);
    }
    if (nv_type_of(acc) != NV_STR) {
        for (i = first + 1; i < n; i++) {
            acc = nv_add(acc, parts[i]);
        }
        return acc;
    }
    for (i = first + 1; i < n; i++) {
        if (nv_type_of(parts[i]) == NV_STR) {
            strs[m] = parts[i]->s;
            lens[m] = (size_t)parts[i]->slen;
        } else {
            strs[m] = nv_display(parts[i]);
            lens[m] = strlen(strs[m]);
        }
        total += lens[m];
        m++;
    }
    if (total == 0) {
        return acc;
    }
    at = (size_t)acc->slen;
    nv_check_text_length(at + total);
    if (acc->owns && acc->s[acc->slen] == 0 && nv_buf_cap(acc->s) > at + total) {
        buf = (char *)acc->s;             /* still owns the end of its buffer */
    } else {
        cap = NV_STR_CAP(acc, at + total);
        buf = nv_buf_alloc(cap);
        memcpy(buf, acc->s, at);
    }
    (void)cap;
    for (j = 0; j < m; j++) {
        memmove(buf + at, strs[j], lens[j]);   /* a part may live in buf */
        at += lens[j];
    }
    buf[at] = 0;
    v = nv_new(NV_STR);
    v->s = buf;
    v->slen = (int)at;
    v->owns = 1;
    return v;
}

/* Integer arithmetic wraps around at 64 bits, like the boxed operators do in
 * practice; plain C `+` on signed values would let the C compiler assume it
 * never overflows. Division by zero is 0 in Novus, not a trap. */
static inline long long nv_iadd(long long a, long long b) { return (long long)((unsigned long long)a + (unsigned long long)b); }
static inline long long nv_isub(long long a, long long b) { return (long long)((unsigned long long)a - (unsigned long long)b); }
static inline long long nv_imul(long long a, long long b) { return (long long)((unsigned long long)a * (unsigned long long)b); }
static inline long long nv_ineg(long long a) { return (long long)(0ULL - (unsigned long long)a); }

/* Unboxed integer helpers: division by zero is 0 in Novus, not a trap. */
static inline long long nv_idiv(long long a, long long b) {
    if (b == -1) {
        return nv_ineg(a);   /* the lowest integer / -1 would trap on some processors */
    }
    return b ? a / b : 0;
}
static inline long long nv_imod(long long a, long long b) {
    if (b == -1) {
        return 0;
    }
    return b ? a % b : 0;
}

static void nv_arith_check(nv l, nv r, const char *op) {
    if (!nv_is_num(l) || !nv_is_num(r)) {
        nv_error("cannot apply '%s' to %s and %s", op, nv_type_name(l), nv_type_name(r));
    }
}

static nv nv_add(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return nv_int(nv_iadd(nv_ival(l), nv_ival(r)));
    }
    if (nv_type_of(l) == NV_STR || nv_type_of(r) == NV_STR) {
        return nv_concat(l, r);
    }
    nv_arith_check(l, r, "+");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(nv_as_double(l) + nv_as_double(r));
    }
    return nv_int(nv_iadd(nv_ival(l), nv_ival(r)));
}

static nv nv_sub(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return nv_int(nv_isub(nv_ival(l), nv_ival(r)));
    }
    nv_arith_check(l, r, "-");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(nv_as_double(l) - nv_as_double(r));
    }
    return nv_int(nv_isub(nv_ival(l), nv_ival(r)));
}

static nv nv_mul(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return nv_int(nv_imul(nv_ival(l), nv_ival(r)));
    }
    nv_arith_check(l, r, "*");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(nv_as_double(l) * nv_as_double(r));
    }
    return nv_int(nv_imul(nv_ival(l), nv_ival(r)));
}

static nv nv_div(nv l, nv r) {
    nv_arith_check(l, r, "/");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        double d = nv_as_double(r);
        return nv_float(d != 0.0 ? nv_as_double(l) / d : 0.0);
    }
    return nv_int(nv_idiv(nv_ival(l), nv_ival(r)));
}

static nv nv_mod(nv l, nv r) {
    nv_arith_check(l, r, "%");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        double d = nv_as_double(r);
        return nv_float(d != 0.0 ? fmod(nv_as_double(l), d) : 0.0);   /* x % 0.0 is 0.0, like x / 0.0 */
    }
    return nv_int(nv_imod(nv_ival(l), nv_ival(r)));
}

static nv nv_neg(nv v) {
    if (nv_type_of(v) == NV_FLOAT) {
        return nv_float(-nv_fval(v));
    }
    if (nv_type_of(v) == NV_INT) {
        return nv_int(nv_ineg(nv_ival(v)));
    }
    nv_error("cannot negate %s", nv_type_name(v));
    return nv_nil;
}

static nv nv_not(nv v) { return nv_bool(!nv_truthy(v)); }

/* Strings are equal when their lengths and their bytes are: the length is
 * compared first, so most unequal strings never touch their bytes, and the
 * comparison does not stop at a NUL byte. */
static inline int nv_str_equal(nv l, nv r) {
    if (l->slen != r->slen) {
        return 0;
    }
    if (l->slen == 0 || l->s == r->s) {
        return 1;
    }
    return l->s[0] == r->s[0] && memcmp(l->s, r->s, (size_t)l->slen) == 0;
}

/* strcmp's order (unsigned bytes, a prefix is smaller) for texts of known
 * length; the result is -1, 0 or 1. */
static int nv_text_compare(const char *a, size_t al, const char *b, size_t bl) {
    size_t common = al < bl ? al : bl;
    int c = common ? memcmp(a, b, common) : 0;
    if (c != 0) {
        return c < 0 ? -1 : 1;
    }
    return al < bl ? -1 : (al > bl ? 1 : 0);
}

/* Comparison of two values of which at least one is a string: the data of
 * both sides, byte for byte. */
static int nv_data_compare(nv l, nv r) {
    size_t ll, rl;
    const char *lp, *rp;
    if (nv_type_of(l) == NV_STR && nv_type_of(r) == NV_STR) {
        return nv_text_compare(l->s, (size_t)l->slen, r->s, (size_t)r->slen);
    }
    lp = nv_data_text(l, &ll);
    rp = nv_data_text(r, &rl);
    return nv_text_compare(lp, ll, rp, rl);
}

static int nv_equals(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return l == r;
    }
    if (nv_type_of(l) == NV_STR && nv_type_of(r) == NV_STR) {
        return nv_str_equal(l, r);
    }
    if (nv_type_of(l) == NV_STR || nv_type_of(r) == NV_STR) {
        return nv_data_compare(l, r) == 0;
    }
    if (nv_type_of(l) == NV_BOOL || nv_type_of(r) == NV_BOOL) {
        return nv_data_compare(l, r) == 0;
    }
    if (nv_type_of(l) == NV_INT && nv_type_of(r) == NV_INT) {
        return nv_ival(l) == nv_ival(r);   /* beyond 2^53 a double cannot tell integers apart */
    }
    if (nv_is_num(l) && nv_is_num(r)) {
        return nv_as_double(l) == nv_as_double(r);
    }
    if (nv_type_of(l) == NV_OBJ && nv_type_of(r) == NV_OBJ) {
        const char *ln = nv_obj_name(l->o);
        const char *rn = nv_obj_name(r->o);
        if (ln && rn) {
            return strcmp(ln, rn) == 0 && l->o->cls == r->o->cls;
        }
        return l->o == r->o;
    }
    if (nv_type_of(l) != nv_type_of(r)) {
        return 0;
    }
    if (nv_type_of(l) == NV_ARR) {
        return l->a == r->a;
    }
    if (nv_type_of(l) == NV_MAP) {
        return l->m == r->m;
    }
    return 1; /* nil == nil */
}

/* nv_compare is -1, 0 or 1 - or this, for two numbers of which one is a NaN:
 * a NaN is neither smaller, nor larger, nor equal (see nv_order_le). */
#define NV_UNORDERED 2

static int nv_compare(nv l, nv r, const char *op) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        long long a = nv_ival(l), b = nv_ival(r);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (nv_type_of(l) == NV_STR || nv_type_of(r) == NV_STR) {
        return nv_data_compare(l, r);
    }
    if (nv_type_of(l) == NV_INT && nv_type_of(r) == NV_INT) {
        long long a = nv_ival(l), b = nv_ival(r);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (nv_is_num(l) && nv_is_num(r)) {
        double a = nv_as_double(l), b = nv_as_double(r);
        if (a != a || b != b) {
            return NV_UNORDERED;
        }
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    nv_error("cannot compare %s and %s with '%s'", nv_type_name(l), nv_type_name(r), op);
    return 0;
}

/* The four orderings, false for a NaN like C's operators on doubles. */
static inline int nv_order_lt(int order) { return order == -1; }
static inline int nv_order_gt(int order) { return order == 1; }
static inline int nv_order_le(int order) { return order == -1 || order == 0; }
static inline int nv_order_ge(int order) { return order == 1 || order == 0; }

/* ---------------------------------------------------------------- */
/* Fast paths: small-integer arithmetic and comparisons without a call */
/* ---------------------------------------------------------------- */

static inline nv nv_tag(long long v) {
    if (v > -NV_TAG_LIMIT && v < NV_TAG_LIMIT) {
        return (nv)(uintptr_t)(((uintptr_t)v << 1) | 1u);
    }
    return nv_int(v);
}

static inline int nv_both_tagged(nv l, nv r) { return ((uintptr_t)l & (uintptr_t)r & 1u) != 0; }

/* Unboxed float helpers: division by zero is 0.0 in Novus, not infinity. */
static inline double nv_fdiv(double a, double b) { return b != 0.0 ? a / b : 0.0; }
static inline double nv_fmod_(double a, double b) { return b != 0.0 ? fmod(a, b) : 0.0; }

/* Shifts on unboxed integers. A count of 64 or more, or a negative one, is
 * undefined in C but not here - see nv_shl/nv_shr, which these mirror. */
static inline long long nv_ishl(long long v, long long n) {
    if (n < 0) {
        return n <= -64 ? (v < 0 ? -1 : 0) : (v >> -n);
    }
    return n >= 64 ? 0 : (long long)((unsigned long long)v << n);
}

static inline long long nv_ishr(long long v, long long n) {
    if (n < 0) {
        return n <= -64 ? 0 : (long long)((unsigned long long)v << -n);
    }
    return n >= 64 ? (v < 0 ? -1 : 0) : (v >> n);
}

/* Both operands as doubles, when each is an immediate float or a small
 * integer and at least one is a float: the case where the result is a float
 * and neither operand needs a call to be read. Operands that are anything
 * else (heap floats, strings, ...) take the general functions. */
static inline int nv_float_operands(nv l, nv r, double *a, double *b) {
    if (nv_is_imm_float(l)) {
        *a = nv_fval(l);
        if (nv_is_imm_float(r)) {
            *b = nv_fval(r);
            return 1;
        }
        if (nv_is_tagged(r)) {
            *b = (double)nv_ival(r);
            return 1;
        }
        return 0;
    }
    if (nv_is_imm_float(r) && nv_is_tagged(l)) {
        *a = (double)nv_ival(l);
        *b = nv_fval(r);
        return 1;
    }
    return 0;
}

static inline nv nv_add_fast(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) {
        return nv_tag(nv_ival(l) + nv_ival(r));
    }
    if (nv_float_operands(l, r, &a, &b)) {
        return nv_float(a + b);
    }
    return nv_add(l, r);
}

static inline nv nv_sub_fast(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) {
        return nv_tag(nv_ival(l) - nv_ival(r));
    }
    if (nv_float_operands(l, r, &a, &b)) {
        return nv_float(a - b);
    }
    return nv_sub(l, r);
}

static inline nv nv_mul_fast(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) {
        long long x = nv_ival(l), y = nv_ival(r);
        if (x > -0x40000000LL && x < 0x40000000LL && y > -0x40000000LL && y < 0x40000000LL) {
            return nv_tag(x * y);
        }
    }
    if (nv_float_operands(l, r, &a, &b)) {
        return nv_float(a * b);
    }
    return nv_mul(l, r);
}

static inline nv nv_div_fast(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) {
        long long y = nv_ival(r);
        return y != 0 ? nv_tag(nv_ival(l) / y) : nv_tag(0);
    }
    if (nv_float_operands(l, r, &a, &b)) {
        return nv_float(nv_fdiv(a, b));
    }
    return nv_div(l, r);
}

static inline nv nv_mod_fast(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) {
        long long y = nv_ival(r);
        return y != 0 ? nv_tag(nv_ival(l) % y) : nv_tag(0);
    }
    if (nv_float_operands(l, r, &a, &b)) {
        return nv_float(nv_fmod_(a, b));
    }
    return nv_mod(l, r);
}

/* Conditions want a C int, not a boxed bool - these skip the boxing. */
static inline int nv_lt_bool(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) return nv_ival(l) < nv_ival(r);
    if (nv_float_operands(l, r, &a, &b)) return a < b;
    return nv_order_lt(nv_compare(l, r, "<"));
}
static inline int nv_gt_bool(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) return nv_ival(l) > nv_ival(r);
    if (nv_float_operands(l, r, &a, &b)) return a > b;
    return nv_order_gt(nv_compare(l, r, ">"));
}
static inline int nv_le_bool(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) return nv_ival(l) <= nv_ival(r);
    if (nv_float_operands(l, r, &a, &b)) return a <= b;
    return nv_order_le(nv_compare(l, r, "<="));
}
static inline int nv_ge_bool(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) return nv_ival(l) >= nv_ival(r);
    if (nv_float_operands(l, r, &a, &b)) return a >= b;
    return nv_order_ge(nv_compare(l, r, ">="));
}
static inline int nv_eq_bool(nv l, nv r) {
    double a, b;
    if (nv_both_tagged(l, r)) return l == r;
    if (nv_float_operands(l, r, &a, &b)) return a == b;
    if (nv_is_ptr(l) && nv_is_ptr(r) && l->type == NV_STR && r->type == NV_STR) return nv_str_equal(l, r);
    return nv_equals(l, r);
}
static inline int nv_ne_bool(nv l, nv r) { return !nv_eq_bool(l, r); }

static nv nv_eq(nv l, nv r) { return nv_bool(nv_equals(l, r)); }
static nv nv_ne(nv l, nv r) { return nv_bool(!nv_equals(l, r)); }
static nv nv_lt(nv l, nv r) { return nv_bool(nv_order_lt(nv_compare(l, r, "<"))); }
static nv nv_gt(nv l, nv r) { return nv_bool(nv_order_gt(nv_compare(l, r, ">"))); }
static nv nv_le(nv l, nv r) { return nv_bool(nv_order_le(nv_compare(l, r, "<="))); }
static nv nv_ge(nv l, nv r) { return nv_bool(nv_order_ge(nv_compare(l, r, ">="))); }

/* ------------------------------------------------------------------ */
/* Indexing and members                                                */
/* ------------------------------------------------------------------ */

static nv nv_coerce_numeric_field(NvClass *c, int at, nv v);

static nv nv_index_general(nv t, nv k) {
    if (nv_type_of(t) == NV_MAP) {
        const char *key = nv_display(k);
        nv v = nv_map_get(t->m, key);
        if (!v) {
            nv_error("key '%s' not found in map", key);
        }
        return v;
    }
    if (nv_type_of(t) == NV_ARR) {
        long long i = nv_as_int(k);
        if (i < 0 || i >= t->a->len) {
            nv_error("array index %lld out of bounds (size %d)", i, t->a->len);
        }
        return t->a->items[i];
    }
    if (nv_type_of(t) == NV_STR) {
        long long i = nv_as_int(k);
        if (i < 0 || i >= t->slen) {
            nv_error("string index %lld out of bounds (length %d)", i, t->slen);
        }
        return nv_strn(t->s + i, 1);
    }
    nv_error("cannot index into a value of type %s", nv_type_name(t));
    return nv_nil;
}

static void nv_index_set_general(nv t, nv k, nv v) {
    if (nv_type_of(t) == NV_MAP) {
        nv_map_set_key(t->m, k, v);
        return;
    }
    if (nv_type_of(t) == NV_ARR) {
        long long i = nv_as_int(k);
        if (i < 0 || i >= t->a->len) {
            nv_error("array index %lld out of bounds (size %d)", i, t->a->len);
        }
        t->a->items[i] = v;
        return;
    }
    nv_error("cannot assign by index into a value of type %s", nv_type_name(t));
}

/* Indexing an array with a small integer in range - the loops of numeric
 * code - is a bounds check and a load; everything else, including the
 * errors, goes through the general functions. */
static inline nv nv_index(nv t, nv k) {
    if (nv_is_ptr(t) && t->type == NV_ARR && nv_is_tagged(k)) {
        long long i = nv_ival(k);
        if (i >= 0 && i < t->a->len) {
            return t->a->items[i];
        }
    }
    return nv_index_general(t, k);
}

static inline void nv_index_set(nv t, nv k, nv v) {
    if (nv_is_ptr(t) && t->type == NV_ARR && nv_is_tagged(k)) {
        long long i = nv_ival(k);
        if (i >= 0 && i < t->a->len) {
            t->a->items[i] = v;
            return;
        }
    }
    nv_index_set_general(t, k, v);
}

static nv nv_get_member(nv t, const char *name) {
    nv v = 0;
    if (nv_type_of(t) == NV_OBJ) {
        int at = nv_field_index(t->o->cls, name);
        if (at < 0) {
            nv_error("no property '%s' on value of type %s", name, t->o->cls->name);
        }
        return nv_fields(t->o)[at];
    }
    if (nv_type_of(t) == NV_MAP) {
        v = nv_map_get(t->m, name);
        if (!v) {
            nv_error("no property '%s' in map", name);
        }
        return v;
    }
    nv_error("no property '%s' on value of type %s", name, nv_type_name(t));
    return nv_nil;
}

static void nv_set_member(nv t, const char *name, nv v) {
    if (nv_type_of(t) == NV_OBJ) {
        int at = nv_field_index(t->o->cls, name);
        if (at < 0) {
            nv_error("no field '%s' on %s", name, t->o->cls->name);
        }
        nv_fields(t->o)[at] = nv_coerce_numeric_field(t->o->cls, at, v);
        return;
    }
    if (nv_type_of(t) == NV_MAP) {
        nv_map_set(t->m, name, v);
        return;
    }
    nv_error("cannot set property '%s' on value of type %s", name, nv_type_name(t));
}


#endif /* NV_OPS_H */
