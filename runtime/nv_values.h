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
 * bootstrap fixpoint depends on the compiler. */
typedef struct NvMap {
    NvEntry *items;
    int len;
    int cap;
    int *index;   /* slot -> position + 1 in items, 0 is empty */
    int mask;     /* index capacity - 1 (a power of two), 0 when absent */
    int sorted;   /* whether items are currently in key order */
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

static int nv_is_tagged(nv v) { return ((uintptr_t)v & 1) != 0; }

static int nv_type_of(nv v) { return nv_is_tagged(v) ? NV_INT : v->type; }

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
