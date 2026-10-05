/* nv_values.h - the value representation (NvVal, arrays, maps, objects, classes). Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_VALUES_H
#define NV_VALUES_H

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

enum { NV_NULL = 0, NV_INT, NV_FLOAT, NV_BOOL, NV_STR, NV_ARR, NV_MAP, NV_OBJ };

/* NV_F_STABLE: the bytes of this string are NUL terminated, live as long as
 * the program and nothing will ever write at or past their end. A map can
 * point its key straight at them instead of copying - which is most of what
 * a program that builds maps out of literals used to spend on keys. */
#define NV_F_STABLE 1u

typedef struct NvVal NvVal;
typedef NvVal *nv;

typedef struct NvArr {
    nv *items;
    int len;
    int cap;
} NvArr;

typedef struct NvEntry {
    const char *key;
    nv val;
} NvEntry;

/* Entries live in insertion order with an open addressing index on top, so
 * lookup and insert are O(1). Iteration (keys, values, for..in, display,
 * json) always sorts first - the compiler depends on that order, and the
 * bootstrap fixpoint depends on the compiler.
 *
 * Two ways to be in key order: `sorted` says the items themselves are, or
 * `ord` (when present) is a block whose first int counts the items it
 * covers and whose following ints list their positions in key order; the
 * items after those are new and not yet placed. The second form is what a
 * map gets when entries are added out of order after it had been sorted, so
 * that adding one key and iterating again costs a binary search instead of
 * a sort of the whole map. The count lives in the block, not in the map:
 * only maps of NV_MAP_ORD_MIN entries or more ever have one, and every
 * other map stays a cell smaller. */
typedef struct NvMap {
    NvEntry *items;
    int len;
    int cap;
    int *index;   /* slot -> position + 1 in items, 0 is empty */
    int mask;     /* index capacity - 1 (a power of two), 0 when absent */
    int sorted;   /* whether items are currently in key order (ord absent) */
    int *ord;     /* [count, positions in key order...] or 0, see above */
} NvMap;

typedef struct NvClass NvClass;

/* Only an enum constant has a name, and enum constants are a handful per
 * program while class instances are millions - so the name is not a field
 * here. It sits in one extra slot behind the fields of enum objects, which
 * takes eight bytes off every object a program allocates. */
typedef struct NvObj {
    NvClass *cls;
    /* the field slots follow this header directly - see nv_fields() */
} NvObj;

/* One slot per field, in class order (see nv_field_index). */
static inline nv *nv_fields(NvObj *o) { return (nv *)(o + 1); }

/* A value is 16 bytes. Small integers (62 bit) are not allocated at all:
 * they are encoded in the pointer itself (lowest bit set) - always go
 * through nv_type_of() / nv_ival() instead of touching the fields. */
struct NvVal {
    unsigned char type;
    unsigned char owns;  /* NV_STR: s has a capacity header, see nv_buf_cap */
    unsigned short flags; /* NV_F_* */
    int slen; /* NV_STR: length of s in bytes */
    union {
        long long i;   /* NV_INT (heap fallback) and NV_BOOL */
        double f;      /* NV_FLOAT */
        const char *s; /* NV_STR: NUL terminated unless a later concatenation
                          appended into the shared buffer (see nv_cstr) */
        NvArr *a;
        NvMap *m;
        NvObj *o;
    };
};

#define NV_TAG_LIMIT ((long long)1 << 61)

/* The low bits of an nv tell what it is. xx1: a small integer. 010: a float
 * that lives in the word itself (see nv_float). 000: a pointer to an NvVal
 * (blocks are 8 byte aligned at least). Code that must not follow the
 * pointer asks nv_is_ptr(), never "is it not an integer". */
#if UINTPTR_MAX > 0xffffffffu
#define NV_IMM_FLOAT 1
#endif
#define NV_FLOAT_TAG 2u

static int nv_is_tagged(nv v) { return ((uintptr_t)v & 1) != 0; }

static inline int nv_is_imm_float(nv v) {
#ifdef NV_IMM_FLOAT
    return ((uintptr_t)v & 7) == NV_FLOAT_TAG;
#else
    (void)v;
    return 0;
#endif
}

static inline int nv_is_ptr(nv v) { return ((uintptr_t)v & 7) == 0; }

static inline int nv_type_of(nv v) {
    if (nv_is_tagged(v)) {
        return NV_INT;
    }
    return nv_is_imm_float(v) ? NV_FLOAT : v->type;
}

/* An immediate float is the double's bits rotated left by one (sign to the
 * bottom, the exponent on top), with the exponent rebased so that the
 * exponents 897..1150 (magnitudes 2^-126 .. 2^127) become 1..254: those fit
 * eight bits, three bits are dropped from the top and the tag takes their
 * place. Everything outside - NaN, infinities, subnormals, -0.0, huge and
 * tiny magnitudes - stays a heap cell, and +0.0 is the bare tag. Rebasing
 * to 1..254 keeps every encoding other than +0.0 away from the word that
 * is 0.0. Both forms are the same value to every operation. */
#define NV_FLOAT_EXP_BASE ((uint64_t)896 << 53)
#define NV_FLOAT_EXP_MIN 897u
#define NV_FLOAT_EXP_SPAN 254u

static inline uint64_t nv_double_bits(double d) {
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    return bits;
}

static inline double nv_bits_double(uint64_t bits) {
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

/* The word for `d`, or 0 when it has to be a heap cell. */
static inline nv nv_float_immediate(double d) {
#ifdef NV_IMM_FLOAT
    uint64_t bits = nv_double_bits(d);
    uint64_t rotated;
    if (bits == 0) {
        return (nv)(uintptr_t)NV_FLOAT_TAG;
    }
    rotated = (bits << 1) | (bits >> 63);
    if (((rotated >> 53) - NV_FLOAT_EXP_MIN) < NV_FLOAT_EXP_SPAN) {
        return (nv)(uintptr_t)(((rotated - NV_FLOAT_EXP_BASE) << 3) | NV_FLOAT_TAG);
    }
#else
    (void)d;
#endif
    return 0;
}

/* The double of an nv of type NV_FLOAT, whichever form it has. */
static inline double nv_fval(nv v) {
#ifdef NV_IMM_FLOAT
    if (nv_is_imm_float(v)) {
        uint64_t word = (uint64_t)(uintptr_t)v;
        uint64_t rotated;
        if (word == NV_FLOAT_TAG) {
            return 0.0;
        }
        rotated = (word >> 3) + NV_FLOAT_EXP_BASE;
        return nv_bits_double((rotated >> 1) | (rotated << 63));
    }
#endif
    return v->f;
}

/* An integer as a double. A function, because gcc folds `0.0 - (double)n` to
 * `-(double)n` as soon as it sees the conversion - which is -0.0 where IEEE
 * says +0.0 for n == 0. The call hides the conversion from that folding and
 * costs nothing once inlined. */
static inline double nv_i2d(long long n) { return (double)n; }

/* A double as an integer: truncated toward zero. NaN and what does not fit give
 * the lowest integer, which is what x86 gives; C leaves it open, and other
 * processors saturate or give 0 - so the same program would not print the
 * same everywhere. */
static inline long long nv_d2i(double d) {
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
        return (long long)d;
    }
    return -9223372036854775807LL - 1;
}

static long long nv_ival(nv v) { return nv_is_tagged(v) ? (long long)(((intptr_t)v) >> 1) : v->i; }

static const char *nv_display(nv v);
static const char *nv_bin(nv v, int *len);
static void nv_conc_init(void); /* see "Threads, virtual threads, tasks" */

typedef nv (*NvMethodFn)(nv self, nv *args, int n);

typedef struct NvMethod {
    const char *name;
    int arity;
    NvMethodFn fn;
} NvMethod;

struct NvClass {
    const char *name;
    const char *base;
    int isAbstract;
    int isEnum;
    const char **fieldNames;
    const char **fieldTypes;
    int nfields;
    int fieldCap;
    NvMethod *methods;
    int nmethods;
    int methodCap;
    NvMethodFn ctor;
    NvMethodFn resolvedCtor; /* ctor of this class or the nearest base, cached */
    int ctorResolved;
    int ctorArity;
    NvMap *constants; /* enum constants */
    NvArr *constantOrder;
    int *order;       /* field indices sorted by name, built on demand */
    const char **flatTypes;  /* field types in slot order, built on demand */
    signed char *flatKinds;  /* 1 integer, 2 float, 3 string, 0 anything else */
    int totalFields;  /* fields of this class and its bases, -1 until counted */
    nv *defaults;     /* default value per field, built with totalFields */
};

static int nv_class_field_count(NvClass *c);
static const char *nv_field_name_at(NvClass *c, int index, const char **type);
static int nv_field_index(NvClass *c, const char *name);
static nv *nv_class_defaults(NvClass *c);
/* Field indices in name order - display and JSON keep the sorted output the
 * language always had (and the golden tests rely on). */
static int *nv_field_order(NvClass *c, int count);

/* The constant name of an enum object, NULL for anything else. */
static const char *nv_obj_name(NvObj *o) {
    if (!o->cls->isEnum) {
        return 0;
    }
    return *(const char **)(nv_fields(o) + nv_class_field_count(o->cls));
}


#endif /* NV_VALUES_H */
