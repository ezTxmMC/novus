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

static void nv_arith_check(nv l, nv r, const char *op) {
    if (!nv_is_num(l) || !nv_is_num(r)) {
        nv_error("cannot apply '%s' to %s and %s", op, nv_type_name(l), nv_type_name(r));
    }
}

static nv nv_add(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return nv_int(nv_ival(l) + nv_ival(r));
    }
    if (nv_type_of(l) == NV_STR || nv_type_of(r) == NV_STR) {
        return nv_concat(l, r);
    }
    nv_arith_check(l, r, "+");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(nv_as_double(l) + nv_as_double(r));
    }
    return nv_int(nv_ival(l) + nv_ival(r));
}

static nv nv_sub(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return nv_int(nv_ival(l) - nv_ival(r));
    }
    nv_arith_check(l, r, "-");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(nv_as_double(l) - nv_as_double(r));
    }
    return nv_int(nv_ival(l) - nv_ival(r));
}

static nv nv_mul(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return nv_int(nv_ival(l) * nv_ival(r));
    }
    nv_arith_check(l, r, "*");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(nv_as_double(l) * nv_as_double(r));
    }
    return nv_int(nv_ival(l) * nv_ival(r));
}

static nv nv_div(nv l, nv r) {
    nv_arith_check(l, r, "/");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        double d = nv_as_double(r);
        return nv_float(d != 0.0 ? nv_as_double(l) / d : 0.0);
    }
    return nv_int(nv_ival(r) != 0 ? nv_ival(l) / nv_ival(r) : 0);
}

static nv nv_mod(nv l, nv r) {
    nv_arith_check(l, r, "%");
    if (nv_type_of(l) == NV_FLOAT || nv_type_of(r) == NV_FLOAT) {
        return nv_float(fmod(nv_as_double(l), nv_as_double(r)));
    }
    return nv_int(nv_ival(r) != 0 ? nv_ival(l) % nv_ival(r) : 0);
}

static nv nv_neg(nv v) {
    if (nv_type_of(v) == NV_FLOAT) {
        return nv_float(-v->f);
    }
    if (nv_type_of(v) == NV_INT) {
        return nv_int(-nv_ival(v));
    }
    nv_error("cannot negate %s", nv_type_name(v));
    return nv_nil;
}

static nv nv_not(nv v) { return nv_bool(!nv_truthy(v)); }

static int nv_equals(nv l, nv r) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        return l == r;
    }
    if (nv_type_of(l) == NV_STR || nv_type_of(r) == NV_STR) {
        return strcmp(nv_data(l), nv_data(r)) == 0;
    }
    if (nv_type_of(l) == NV_BOOL || nv_type_of(r) == NV_BOOL) {
        return strcmp(nv_data(l), nv_data(r)) == 0;
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

static int nv_compare(nv l, nv r, const char *op) {
    if (nv_is_tagged(l) && nv_is_tagged(r)) {
        long long a = nv_ival(l), b = nv_ival(r);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (nv_type_of(l) == NV_STR || nv_type_of(r) == NV_STR) {
        return strcmp(nv_data(l), nv_data(r));
    }
    if (nv_is_num(l) && nv_is_num(r)) {
        double a = nv_as_double(l), b = nv_as_double(r);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    nv_error("cannot compare %s and %s with '%s'", nv_type_name(l), nv_type_name(r), op);
    return 0;
}

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

/* Unboxed integer helpers: division by zero is 0 in Novus, not a trap. */
static inline long long nv_idiv(long long a, long long b) { return b ? a / b : 0; }
static inline long long nv_imod(long long a, long long b) { return b ? a % b : 0; }

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

static inline nv nv_add_fast(nv l, nv r) {
    if (nv_both_tagged(l, r)) {
        return nv_tag(nv_ival(l) + nv_ival(r));
    }
    return nv_add(l, r);
}

static inline nv nv_sub_fast(nv l, nv r) {
    if (nv_both_tagged(l, r)) {
        return nv_tag(nv_ival(l) - nv_ival(r));
    }
    return nv_sub(l, r);
}

static inline nv nv_mul_fast(nv l, nv r) {
    if (nv_both_tagged(l, r)) {
        long long a = nv_ival(l), b = nv_ival(r);
        if (a > -0x40000000LL && a < 0x40000000LL && b > -0x40000000LL && b < 0x40000000LL) {
            return nv_tag(a * b);
        }
    }
    return nv_mul(l, r);
}

/* Conditions want a C int, not a boxed bool - these skip the boxing. */
static inline int nv_lt_bool(nv l, nv r) {
    if (nv_both_tagged(l, r)) return nv_ival(l) < nv_ival(r);
    return nv_compare(l, r, "<") < 0;
}
static inline int nv_gt_bool(nv l, nv r) {
    if (nv_both_tagged(l, r)) return nv_ival(l) > nv_ival(r);
    return nv_compare(l, r, ">") > 0;
}
static inline int nv_le_bool(nv l, nv r) {
    if (nv_both_tagged(l, r)) return nv_ival(l) <= nv_ival(r);
    return nv_compare(l, r, "<=") <= 0;
}
static inline int nv_ge_bool(nv l, nv r) {
    if (nv_both_tagged(l, r)) return nv_ival(l) >= nv_ival(r);
    return nv_compare(l, r, ">=") >= 0;
}
static inline int nv_eq_bool(nv l, nv r) {
    if (nv_both_tagged(l, r)) return l == r;
    return nv_equals(l, r);
}
static inline int nv_ne_bool(nv l, nv r) { return !nv_eq_bool(l, r); }

static nv nv_eq(nv l, nv r) { return nv_bool(nv_equals(l, r)); }
static nv nv_ne(nv l, nv r) { return nv_bool(!nv_equals(l, r)); }
static nv nv_lt(nv l, nv r) { return nv_bool(nv_compare(l, r, "<") < 0); }
static nv nv_gt(nv l, nv r) { return nv_bool(nv_compare(l, r, ">") > 0); }
static nv nv_le(nv l, nv r) { return nv_bool(nv_compare(l, r, "<=") <= 0); }
static nv nv_ge(nv l, nv r) { return nv_bool(nv_compare(l, r, ">=") >= 0); }

/* ------------------------------------------------------------------ */
/* Indexing and members                                                */
/* ------------------------------------------------------------------ */

static nv nv_index(nv t, nv k) {
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

static void nv_index_set(nv t, nv k, nv v) {
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
        nv_fields(t->o)[at] = v;
        return;
    }
    if (nv_type_of(t) == NV_MAP) {
        nv_map_set(t->m, name, v);
        return;
    }
    nv_error("cannot set property '%s' on value of type %s", name, nv_type_name(t));
}


#endif /* NV_OPS_H */
