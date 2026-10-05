/* nv_typed.h - unboxed numbers for the code the compiler has typed. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_TYPED_H
#define NV_TYPED_H

/* ------------------------------------------------------------------ */
/* Declared numbers read out of a field                                */
/* ------------------------------------------------------------------ */

/* A field declared `float` or `integer` holds a number of that kind (every
 * way of storing into one converts, see nv_coerce_numeric_field), and the
 * compiler computes with it unboxed. A value that is not what the
 * declaration says is a type error, reported here instead of being mistaken
 * for a number. */
static NV_NOINLINE double nv_unbox_float_slow(nv v) {
    if (nv_type_of(v) == NV_FLOAT) {
        return nv_fval(v);
    }
    nv_error("expected a float but found %s", nv_type_name(v));
    return 0.0;
}

static inline double nv_unbox_float(nv v) {
    return nv_is_imm_float(v) ? nv_fval(v) : nv_unbox_float_slow(v);
}

static NV_NOINLINE long long nv_unbox_int_slow(nv v) {
    if (nv_type_of(v) == NV_INT) {
        return nv_ival(v);
    }
    nv_error("expected an integer but found %s", nv_type_name(v));
    return 0;
}

static inline long long nv_unbox_int(nv v) {
    return nv_is_tagged(v) ? nv_ival(v) : nv_unbox_int_slow(v);
}

/* `target.name` where the compiler guessed the class of `target`: an object
 * of exactly that class has the field in the slot the compiler worked out;
 * any other value (a subclass, a different class) is asked the way a call
 * without a guess asks it. */
static inline nv nv_get_field(NvIc *ic, nv t, NvClass *cls, int slot, const char *name) {
    if (nv_is_object(t) && t->o->cls == cls) {
        return nv_fields(t->o)[slot];
    }
    return nv_ic0(ic, t, name);
}

/* ------------------------------------------------------------------ */
/* A float on one side, any value on the other                         */
/* ------------------------------------------------------------------ */

/* `x - v` with x a double and v anything: a number gives the same double the
 * general operator would box, anything else is the error the general
 * operator reports. (`+` is not here: with a string on one side it
 * concatenates, so its result is not always a number.) */
static inline int nv_numeric_double(nv v, double *d) {
    if (nv_is_imm_float(v)) {
        *d = nv_fval(v);
        return 1;
    }
    if (nv_is_tagged(v)) {
        *d = (double)nv_ival(v);
        return 1;
    }
    if (!nv_is_num(v)) {
        return 0;
    }
    *d = nv_as_double(v);
    return 1;
}

static NV_NOINLINE double nv_mixed_fail_right(double a, nv r, const char *op) {
    nv_arith_check(nv_float(a), r, op);
    return 0.0;
}

static NV_NOINLINE double nv_mixed_fail_left(nv l, double b, const char *op) {
    nv_arith_check(l, nv_float(b), op);
    return 0.0;
}

#define NV_MIXED_OPERATOR(name, op_text, expr)                              \
    static inline double nv_f##name##_du(double a, nv r) {                  \
        double b;                                                           \
        if (nv_numeric_double(r, &b)) {                                     \
            return expr;                                                    \
        }                                                                   \
        return nv_mixed_fail_right(a, r, op_text);                          \
    }                                                                       \
    static inline double nv_f##name##_ud(nv l, double b) {                  \
        double a;                                                           \
        if (nv_numeric_double(l, &a)) {                                     \
            return expr;                                                    \
        }                                                                   \
        return nv_mixed_fail_left(l, b, op_text);                           \
    }

NV_MIXED_OPERATOR(sub, "-", a - b)
NV_MIXED_OPERATOR(mul, "*", a * b)
NV_MIXED_OPERATOR(div, "/", nv_fdiv(a, b))
NV_MIXED_OPERATOR(mod, "%", nv_fmod_(a, b))

/* ------------------------------------------------------------------ */
/* The numeric natives of the math module on unboxed numbers           */
/* ------------------------------------------------------------------ */

static inline double nv_to_float32(double x) { return (double)(float)x; }
static inline long long nv_floor_int(double x) { return nv_d2i(floor(x)); }
static inline long long nv_ceil_int(double x) { return nv_d2i(ceil(x)); }
static inline long long nv_round_int(double x) { return nv_d2i(floor(x + 0.5)); }

#endif /* NV_TYPED_H */
