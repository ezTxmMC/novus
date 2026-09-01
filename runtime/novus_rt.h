/*
 * novus_rt.h - the C runtime embedded into every program that novusc emits.
 *
 * Values are dynamically typed (NvVal): integers, floats, bools, strings,
 * arrays, maps, class instances and enum constants all flow through the
 * same variables. Integers up to 62 bits are encoded in the pointer itself
 * (no allocation); everything else is arena allocated and never freed
 * (bootstrap-style runtime).
 *
 * Portable C11 (anonymous unions): builds with gcc, clang, zig cc and
 * mingw on 64-bit Linux, macOS and Windows.
 */
#ifndef NOVUS_RT_H
#define NOVUS_RT_H

/* POSIX extensions (popen, setenv, gettimeofday, nanosleep, dirent) */
#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#endif

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define NV_GETCWD _getcwd
#define NV_CHDIR _chdir
#define NV_RMDIR _rmdir
#define NV_MKDIR(p) _mkdir(p)
#define NV_POPEN _popen
#define NV_PCLOSE _pclose
#define NV_GETPID _getpid
#else
#include <dirent.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#define NV_GETCWD getcwd
#define NV_CHDIR chdir
#define NV_RMDIR rmdir
#define NV_MKDIR(p) mkdir(p, 0755)
#define NV_POPEN popen
#define NV_PCLOSE pclose
#define NV_GETPID getpid
#endif

#ifdef __GNUC__
#define NV_UNUSED __attribute__((unused))
#else
#define NV_UNUSED
#endif

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

enum { NV_NULL = 0, NV_INT, NV_FLOAT, NV_BOOL, NV_STR, NV_ARR, NV_MAP, NV_OBJ };

typedef struct NvVal NvVal;
typedef NvVal *nv;

typedef struct NvArr {
    nv *items;
    int len;
    int cap;
    int heap;  /* items came from malloc: grow with realloc instead of copying */
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

typedef struct NvObj {
    NvClass *cls;
    const char *name; /* enum constant name, NULL for class instances */
    /* the field slots follow this header directly - see nv_fields() */
} NvObj;

/* One slot per field, in class order (see nv_field_index). */
static inline nv *nv_fields(NvObj *o) { return (nv *)(o + 1); }

/* A value is 24 bytes. Small integers (62 bit) are not allocated at all:
 * they are encoded in the pointer itself (lowest bit set) - always go
 * through nv_type_of() / nv_ival() instead of touching the fields. */
struct NvVal {
    int type;
    int slen; /* NV_STR: length of s in bytes */
    int scap; /* NV_STR: writable capacity of s (0: read-only / not owned) */
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

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

static char *nv_arena_ptr = 0;
static size_t nv_arena_left = 0;

static void *nv_alloc(size_t n) {
    void *p;
    n = (n + 7) & ~(size_t)7;   /* every value in the runtime is 8-aligned */
    if (n > 256 * 1024) {
        p = malloc(n);
        if (!p) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        return p;
    }
    if (n > nv_arena_left) {
        size_t chunk = 1024 * 1024;
        nv_arena_ptr = (char *)malloc(chunk);
        if (!nv_arena_ptr) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        nv_arena_left = chunk;
    }
    p = nv_arena_ptr;
    nv_arena_ptr += n;
    nv_arena_left -= n;
    return p;
}

static char *nv_strndup(const char *s, size_t n) {
    char *r = (char *)nv_alloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* ------------------------------------------------------------------ */
/* String builder                                                      */
/* ------------------------------------------------------------------ */

typedef struct NvSb {
    char *buf;
    int len;
    int cap;
} NvSb;

static void nv_sb_init(NvSb *sb) {
    sb->cap = 64;
    sb->len = 0;
    sb->buf = (char *)malloc((size_t)sb->cap);
    sb->buf[0] = 0;
}

static void nv_sb_addn(NvSb *sb, const char *s, int n) {
    if (sb->len + n + 1 > sb->cap) {
        while (sb->len + n + 1 > sb->cap) {
            sb->cap *= 2;
        }
        sb->buf = (char *)realloc(sb->buf, (size_t)sb->cap);
    }
    memcpy(sb->buf + sb->len, s, (size_t)n);
    sb->len += n;
    sb->buf[sb->len] = 0;
}

static void nv_sb_add(NvSb *sb, const char *s) { nv_sb_addn(sb, s, (int)strlen(s)); }

static void nv_sb_addc(NvSb *sb, char c) { nv_sb_addn(sb, &c, 1); }

static const char *nv_sb_finish(NvSb *sb) {
    const char *r = nv_strndup(sb->buf, (size_t)sb->len);
    free(sb->buf);
    sb->buf = 0;
    return r;
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

static void nv_error(const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

static NvVal nv_nil_val = {NV_NULL, 0, 0, {0}};
static NvVal nv_true_val = {NV_BOOL, 0, 0, {1}};
static NvVal nv_false_val = {NV_BOOL, 0, 0, {0}};
static NvVal nv_empty_str_val = {NV_STR, 0, 0, {0}};
static nv nv_nil = &nv_nil_val;
static nv nv_char_table[256];

static nv nv_new(int type) {
    nv v = (nv)nv_alloc(sizeof(NvVal));
    memset(v, 0, sizeof(NvVal));
    v->type = type;
    return v;
}

static nv nv_int(long long i) {
    nv v;
    if (i > -NV_TAG_LIMIT && i < NV_TAG_LIMIT) {
        return (nv)(uintptr_t)(((uintptr_t)i << 1) | 1u);
    }
    v = nv_new(NV_INT);
    v->i = i;
    return v;
}

static int nv_exit_code(nv v) { return nv_type_of(v) == NV_INT ? (int)nv_ival(v) : 0; }

static nv nv_float(double f) {
    nv v = nv_new(NV_FLOAT);
    v->f = f;
    return v;
}

static nv nv_bool(int b) { return b ? &nv_true_val : &nv_false_val; }

static nv nv_strn(const char *s, int n) {
    nv v;
    if (n == 1 && nv_char_table[(unsigned char)s[0]]) {
        return nv_char_table[(unsigned char)s[0]];
    }
    v = nv_new(NV_STR);
    v->s = nv_strndup(s, (size_t)n);
    v->slen = n;
    v->scap = n + 1;
    return v;
}

static nv nv_str(const char *s) { return nv_strn(s, (int)strlen(s)); }

/* String literal from generated code: points at the (immutable) literal. */
static nv nv_lit(const char *s) {
    nv v = nv_new(NV_STR);
    v->s = s;
    v->slen = (int)strlen(s);
    v->scap = 0;
    return v;
}

static nv nv_str_own(const char *s, int n) { /* s already arena allocated */
    nv v = nv_new(NV_STR);
    v->s = s;
    v->slen = n;
    v->scap = n + 1;
    return v;
}

/* NUL terminated view of a string value. Strings may share a buffer with
 * a longer string that was appended to in place; the terminator is then
 * gone and a private copy is made. */
static const char *nv_cstr(nv v) {
    if (nv_type_of(v) != NV_STR) {
        return nv_display(v);
    }
    if (v->s[v->slen] == 0) {
        return v->s;
    }
    return nv_strndup(v->s, (size_t)v->slen);
}

static NvArr *nv_arr_new(void) {
    NvArr *a = (NvArr *)nv_alloc(sizeof(NvArr));
    a->len = 0;
    a->heap = 0;
    a->cap = 8;
    a->items = (nv *)nv_alloc(sizeof(nv) * (size_t)a->cap);
    return a;
}

/* Growing inside the arena leaves every previous copy behind, which doubles
 * the memory of a large array. Above this size we switch to realloc. */
#define NV_ARR_HEAP_AT 4096

static void nv_arr_grow(NvArr *a) {
    int cap = a->cap * 2;
    if (cap >= NV_ARR_HEAP_AT) {
        if (a->heap) {
            a->items = (nv *)realloc(a->items, sizeof(nv) * (size_t)cap);
        } else {
            nv *items = (nv *)malloc(sizeof(nv) * (size_t)cap);
            memcpy(items, a->items, sizeof(nv) * (size_t)a->len);
            a->items = items;
            a->heap = 1;
        }
        if (!a->items) {
            nv_error("out of memory");
        }
    } else {
        nv *items = (nv *)nv_alloc(sizeof(nv) * (size_t)cap);
        memcpy(items, a->items, sizeof(nv) * (size_t)a->len);
        a->items = items;
    }
    a->cap = cap;
}

static void nv_arr_push(NvArr *a, nv v) {
    if (a->len == a->cap) {
        nv_arr_grow(a);
    }
    a->items[a->len++] = v;
}

static nv nv_arr(void) {
    nv v = nv_new(NV_ARR);
    v->a = nv_arr_new();
    return v;
}

static nv nv_arr_of(int count, ...) {
    nv v = nv_arr();
    va_list ap;
    int i;
    va_start(ap, count);
    for (i = 0; i < count; i++) {
        nv_arr_push(v->a, va_arg(ap, nv));
    }
    va_end(ap);
    return v;
}

static unsigned nv_key_hash(const char *key) {
    unsigned h = 2166136261u;
    for (; *key; key++) {
        h = (h ^ (unsigned char)*key) * 16777619u;
    }
    return h;
}

static NvMap *nv_map_new_cap(int cap) {
    NvMap *m = (NvMap *)nv_alloc(sizeof(NvMap));
    m->len = 0;
    m->cap = cap < 1 ? 1 : cap;
    m->items = (NvEntry *)nv_alloc(sizeof(NvEntry) * (size_t)m->cap);
    m->index = 0;
    m->mask = 0;
    m->sorted = 1;
    return m;
}

static NvMap *nv_map_new(void) { return nv_map_new_cap(4); }

/* (Re)builds the hash index; called when it grows or entries move. */
static void nv_map_reindex(NvMap *m, int slots) {
    int i;
    while (slots < (m->len + 1) * 2) {
        slots *= 2;
    }
    m->mask = slots - 1;
    m->index = (int *)nv_alloc(sizeof(int) * (size_t)slots);
    memset(m->index, 0, sizeof(int) * (size_t)slots);
    for (i = 0; i < m->len; i++) {
        unsigned slot = nv_key_hash(m->items[i].key) & (unsigned)m->mask;
        while (m->index[slot]) {
            slot = (slot + 1) & (unsigned)m->mask;
        }
        m->index[slot] = i + 1;
    }
}

/* Below this size a linear scan beats hashing - and object field maps,
 * which are the bulk of all maps, stay index free (and small). */
#define NV_MAP_LINEAR 8

/* Position of `key` in items, or -1. */
static int nv_map_find(NvMap *m, const char *key) {
    unsigned slot;
    if (m->len == 0) {
        return -1;
    }
    if (!m->index) {
        int i;
        if (m->len <= NV_MAP_LINEAR) {
            for (i = 0; i < m->len; i++) {
                if (strcmp(m->items[i].key, key) == 0) {
                    return i;
                }
            }
            return -1;
        }
        nv_map_reindex(m, 16);
    }
    slot = nv_key_hash(key) & (unsigned)m->mask;
    while (m->index[slot]) {
        int at = m->index[slot] - 1;
        if (strcmp(m->items[at].key, key) == 0) {
            return at;
        }
        slot = (slot + 1) & (unsigned)m->mask;
    }
    return -1;
}

static void nv_map_append(NvMap *m, const char *key, nv val) {
    unsigned slot;
    if (m->len == m->cap) {
        NvEntry *items = (NvEntry *)nv_alloc(sizeof(NvEntry) * (size_t)m->cap * 2);
        memcpy(items, m->items, sizeof(NvEntry) * (size_t)m->len);
        m->items = items;
        m->cap *= 2;
    }
    m->items[m->len].key = key;
    m->items[m->len].val = val;
    m->len++;
    if (m->sorted && m->len > 1 && strcmp(m->items[m->len - 2].key, key) > 0) {
        m->sorted = 0;
    }
    if (!m->index) {
        if (m->len <= NV_MAP_LINEAR) {
            return;                      /* stays index free */
        }
        nv_map_reindex(m, 16);
        return;
    }
    if ((m->len + 1) * 2 > m->mask + 1) {
        nv_map_reindex(m, m->mask + 1);
        return;
    }
    slot = nv_key_hash(key) & (unsigned)m->mask;
    while (m->index[slot]) {
        slot = (slot + 1) & (unsigned)m->mask;
    }
    m->index[slot] = m->len;
}

/* `key` must outlive the map (string literal, class table, arena string). */
static void nv_map_set_static(NvMap *m, const char *key, nv val) {
    int at = nv_map_find(m, key);
    if (at >= 0) {
        m->items[at].val = val;
        return;
    }
    nv_map_append(m, key, val);
}

static void nv_map_set(NvMap *m, const char *key, nv val) {
    int at = nv_map_find(m, key);
    if (at >= 0) {
        m->items[at].val = val;
        return;
    }
    nv_map_append(m, nv_strndup(key, strlen(key)), val);
}

static nv nv_map_get(NvMap *m, const char *key) {
    int at = nv_map_find(m, key);
    return at >= 0 ? m->items[at].val : 0;
}

static int nv_map_has(NvMap *m, const char *key) { return nv_map_find(m, key) >= 0; }

static void nv_map_remove(NvMap *m, const char *key) {
    int at = nv_map_find(m, key);
    if (at < 0) {
        return;
    }
    memmove(m->items + at, m->items + at + 1, sizeof(NvEntry) * (size_t)(m->len - at - 1));
    m->len--;
    if (m->index) {
        nv_map_reindex(m, m->mask + 1);
    }
}

static int nv_entry_cmp(const void *a, const void *b) {
    return strcmp(((const NvEntry *)a)->key, ((const NvEntry *)b)->key);
}

/* Every read that exposes the order sorts first. */
static void nv_map_order(NvMap *m) {
    if (m->sorted) {
        return;
    }
    qsort(m->items, (size_t)m->len, sizeof(NvEntry), nv_entry_cmp);
    m->sorted = 1;
    if (m->index) {
        nv_map_reindex(m, m->mask + 1);
    }
}

static nv nv_map(void) {
    nv v = nv_new(NV_MAP);
    v->m = nv_map_new();
    return v;
}

static const char *nv_display(nv v);

static nv nv_map_of(int pairs, ...) {
    nv v = nv_map();
    va_list ap;
    int i;
    va_start(ap, pairs);
    for (i = 0; i < pairs; i++) {
        nv k = va_arg(ap, nv);
        nv val = va_arg(ap, nv);
        nv_map_set(v->m, nv_display(k), val);
    }
    va_end(ap);
    return v;
}

/* ------------------------------------------------------------------ */
/* Type names, display                                                 */
/* ------------------------------------------------------------------ */

static const char *nv_type_name(nv v) {
    switch (nv_type_of(v)) {
    case NV_INT:
        return "integer";
    case NV_FLOAT:
        return "float";
    case NV_BOOL:
        return "bool";
    case NV_STR:
        return "string";
    case NV_ARR:
        return "array";
    case NV_MAP:
        return "map";
    case NV_OBJ:
        return v->o->cls->name;
    default:
        return "unknown";
    }
}

/* Digits back to front into a stack buffer, then one arena copy. sprintf()
 * showed up in every profile of code that puts numbers into strings. */
static const char *nv_fmt_int(long long i) {
    char buf[24];
    char *end = buf + sizeof(buf);
    char *p = end;
    unsigned long long u = i < 0 ? 0ULL - (unsigned long long)i : (unsigned long long)i;
    char *out;
    size_t len;
    *--p = 0;
    do {
        *--p = (char)('0' + (int)(u % 10ULL));
        u /= 10ULL;
    } while (u);
    if (i < 0) {
        *--p = '-';
    }
    len = (size_t)(end - p);
    out = (char *)nv_alloc(len);
    memcpy(out, p, len);
    return out;
}

static const char *nv_fmt_float(double f) {
    char buf[64];
    sprintf(buf, "%f", f);
    return nv_strndup(buf, strlen(buf));
}

/* Containers currently being printed/serialized, to cut reference cycles. */
static const void *nv_visit_stack[256];
static int nv_visit_depth = 0;

static int nv_visit_enter(const void *container) {
    int i;
    for (i = 0; i < nv_visit_depth; i++) {
        if (nv_visit_stack[i] == container) {
            return 0;
        }
    }
    if (nv_visit_depth < 256) {
        nv_visit_stack[nv_visit_depth] = container;
    }
    nv_visit_depth++;
    return 1;
}

static void nv_visit_leave(void) { nv_visit_depth--; }

static const void *nv_container_id(nv v) {
    if (nv_type_of(v) == NV_ARR) {
        return v->a;
    }
    if (nv_type_of(v) == NV_MAP) {
        return v->m;
    }
    if (nv_type_of(v) == NV_OBJ) {
        return v->o;
    }
    return 0;
}

static void nv_display_into(NvSb *sb, nv v) {
    int i;
    const void *id = nv_container_id(v);
    if (id && !nv_visit_enter(id)) {
        nv_sb_add(sb, "...");
        return;
    }
    switch (nv_type_of(v)) {
    case NV_INT:
        nv_sb_add(sb, nv_fmt_int(nv_ival(v)));
        break;
    case NV_FLOAT:
        nv_sb_add(sb, nv_fmt_float(v->f));
        break;
    case NV_BOOL:
        nv_sb_add(sb, nv_ival(v) ? "true" : "false");
        break;
    case NV_STR:
        nv_sb_addn(sb, v->s, v->slen);
        break;
    case NV_ARR:
        nv_sb_addc(sb, '[');
        for (i = 0; i < v->a->len; i++) {
            if (i > 0) {
                nv_sb_add(sb, ", ");
            }
            nv_display_into(sb, v->a->items[i]);
        }
        nv_sb_addc(sb, ']');
        break;
    case NV_MAP:
        nv_map_order(v->m);
        nv_sb_addc(sb, '{');
        for (i = 0; i < v->m->len; i++) {
            if (i > 0) {
                nv_sb_add(sb, ", ");
            }
            nv_sb_add(sb, v->m->items[i].key);
            nv_sb_add(sb, ": ");
            nv_display_into(sb, v->m->items[i].val);
        }
        nv_sb_addc(sb, '}');
        break;
    case NV_OBJ:
        if (v->o->name) {
            nv_sb_add(sb, v->o->name);
            break;
        }
        nv_sb_add(sb, v->o->cls->name);
        nv_sb_addc(sb, '{');
        {
            int count = nv_class_field_count(v->o->cls);
            int *order = nv_field_order(v->o->cls, count);
            for (i = 0; i < count; i++) {
                if (i > 0) {
                    nv_sb_add(sb, ", ");
                }
                nv_sb_add(sb, nv_field_name_at(v->o->cls, order[i], 0));
                nv_sb_add(sb, ": ");
                nv_display_into(sb, nv_fields(v->o)[order[i]]);
            }
        }
        nv_sb_addc(sb, '}');
        break;
    default:
        break;
    }
    if (id) {
        nv_visit_leave();
    }
}

static const char *nv_display(nv v) {
    NvSb sb;
    if (nv_type_of(v) == NV_STR) {
        return nv_cstr(v);
    }
    if (nv_type_of(v) == NV_BOOL) {
        return nv_ival(v) ? "true" : "false";
    }
    if (nv_type_of(v) == NV_INT) {
        return nv_fmt_int(nv_ival(v));
    }
    if (nv_type_of(v) == NV_FLOAT) {
        return nv_fmt_float(v->f);
    }
    nv_sb_init(&sb);
    nv_display_into(&sb, v);
    return nv_sb_finish(&sb);
}

/* The "data" of a value: what the interpreter compared strings against. */
static const char *nv_data(nv v) {
    switch (nv_type_of(v)) {
    case NV_INT:
    case NV_FLOAT:
    case NV_BOOL:
    case NV_STR:
        return nv_display(v);
    case NV_OBJ:
        return v->o->name ? v->o->name : "";
    default:
        return "";
    }
}

static nv nv_to_str(nv v) { return nv_type_of(v) == NV_STR ? v : nv_str(nv_display(v)); }

/* ------------------------------------------------------------------ */
/* Numbers, coercion, truthiness                                       */
/* ------------------------------------------------------------------ */

static int nv_is_num(nv v) { return nv_type_of(v) == NV_INT || nv_type_of(v) == NV_FLOAT; }

static double nv_as_double(nv v) {
    if (nv_type_of(v) == NV_FLOAT) {
        return v->f;
    }
    if (nv_type_of(v) == NV_INT || nv_type_of(v) == NV_BOOL) {
        return (double)nv_ival(v);
    }
    if (nv_type_of(v) == NV_STR) {
        return atof(nv_cstr(v));
    }
    return 0.0;
}

static long long nv_as_int(nv v) {
    if (nv_type_of(v) == NV_INT || nv_type_of(v) == NV_BOOL) {
        return nv_ival(v);
    }
    if (nv_type_of(v) == NV_FLOAT) {
        return (long long)v->f;
    }
    if (nv_type_of(v) == NV_STR) {
        return atoll(nv_cstr(v));
    }
    return 0;
}

static int nv_truthy(nv v) { return v == &nv_true_val; }

static const char *nv_normalize_type(const char *t) {
    if (strcmp(t, "str") == 0) {
        return "string";
    }
    if (strcmp(t, "int") == 0) {
        return "integer";
    }
    if (strcmp(t, "boolean") == 0) {
        return "bool";
    }
    if (strncmp(t, "array", 5) == 0) {
        return "array";
    }
    if (strncmp(t, "map", 3) == 0) {
        return "map";
    }
    return t;
}

static int nv_type_is(nv v, const char *t) {
    t = nv_normalize_type(t);
    return strcmp(nv_type_name(v), t) == 0;
}

/* Implicit conversions applied to parameters, fields and return values. */
static nv nv_coerce(nv v, const char *t) {
    t = nv_normalize_type(t);
    if (strcmp(t, "integer") == 0) {
        if (nv_type_of(v) == NV_FLOAT) {
            return nv_int((long long)v->f);
        }
        return v;
    }
    if (strcmp(t, "float") == 0) {
        if (nv_type_of(v) == NV_INT) {
            return nv_float((double)nv_ival(v));
        }
        return v;
    }
    if (strcmp(t, "string") == 0) {
        if (nv_type_of(v) != NV_STR && nv_type_of(v) != NV_NULL) {
            return nv_str(nv_data(v));
        }
        return v;
    }
    return v;
}

static nv nv_coerce_int(nv v) { return nv_type_of(v) == NV_FLOAT ? nv_int((long long)v->f) : v; }

static nv nv_coerce_float(nv v) { return nv_is_tagged(v) || nv_type_of(v) == NV_INT ? nv_float((double)nv_ival(v)) : v; }

static nv nv_coerce_string(nv v) {
    int t = nv_type_of(v);
    return t == NV_STR || t == NV_NULL ? v : nv_str(nv_data(v));
}

static nv nv_default(const char *t) {
    t = nv_normalize_type(t);
    if (strcmp(t, "integer") == 0) {
        return nv_int(0);
    }
    if (strcmp(t, "float") == 0) {
        return nv_float(0.0);
    }
    if (strcmp(t, "string") == 0) {
        return &nv_empty_str_val;
    }
    if (strcmp(t, "bool") == 0) {
        return nv_bool(0);
    }
    return nv_nil;
}

/* ------------------------------------------------------------------ */
/* Operators                                                           */
/* ------------------------------------------------------------------ */

/* Concatenation. The result gets spare capacity, and when the left
 * operand still owns the end of its buffer the right side is appended in
 * place, so `s = s + x` loops run in amortized linear time. Older values
 * sharing the buffer keep their length; nv_cstr() copies them on demand. */
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
    if (nv_type_of(l) == NV_STR && l->scap > 0 && l->s[l->slen] == 0 && rl > 0 && rs[0] != 0 &&
        (size_t)l->scap > ll + rl) {
        buf = (char *)l->s;
        memmove(buf + ll, rs, rl);
        buf[ll + rl] = 0;
        v = nv_new(NV_STR);
        v->s = buf;
        v->slen = (int)(ll + rl);
        v->scap = l->scap;
        return v;
    }
    cap = (ll + rl) * 2 + 16;
    buf = (char *)nv_alloc(cap);
    memcpy(buf, ls, ll);
    memcpy(buf + ll, rs, rl);
    buf[ll + rl] = 0;
    v = nv_new(NV_STR);
    v->s = buf;
    v->slen = (int)(ll + rl);
    v->scap = (int)cap;
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
    if (acc->scap > 0 && acc->s[acc->slen] == 0 && (size_t)acc->scap > at + total) {
        buf = (char *)acc->s;             /* still owns the end of its buffer */
        cap = (size_t)acc->scap;
    } else {
        cap = (at + total) * 2 + 16;
        buf = (char *)nv_alloc(cap);
        memcpy(buf, acc->s, at);
    }
    for (j = 0; j < m; j++) {
        memmove(buf + at, strs[j], lens[j]);   /* a part may live in buf */
        at += lens[j];
    }
    buf[at] = 0;
    v = nv_new(NV_STR);
    v->s = buf;
    v->slen = (int)at;
    v->scap = (int)cap;
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
        if (l->o->name && r->o->name) {
            return strcmp(l->o->name, r->o->name) == 0 && l->o->cls == r->o->cls;
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
        nv_map_set(t->m, nv_display(k), v);
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

/* ------------------------------------------------------------------ */
/* Classes, objects, enums                                             */
/* ------------------------------------------------------------------ */

static NvClass **nv_classes = 0;
static int nv_nclasses = 0;
static int nv_class_cap = 0;

static NvClass *nv_find_class(const char *name) {
    int i;
    for (i = 0; i < nv_nclasses; i++) {
        if (strcmp(nv_classes[i]->name, name) == 0) {
            return nv_classes[i];
        }
    }
    return 0;
}

static NvClass *nv_class_define(const char *name, const char *base, int isAbstract, int isEnum) {
    NvClass *c = (NvClass *)nv_alloc(sizeof(NvClass));
    memset(c, 0, sizeof(NvClass));
    c->totalFields = -1;
    c->name = name;
    c->base = base && base[0] ? base : 0;
    c->isAbstract = isAbstract;
    c->isEnum = isEnum;
    c->fieldCap = 8;
    c->fieldNames = (const char **)nv_alloc(sizeof(char *) * 8);
    c->fieldTypes = (const char **)nv_alloc(sizeof(char *) * 8);
    c->methodCap = 8;
    c->methods = (NvMethod *)nv_alloc(sizeof(NvMethod) * 8);
    c->constants = nv_map_new();
    c->constantOrder = nv_arr_new();
    if (nv_nclasses == nv_class_cap) {
        NvClass **grown;
        nv_class_cap = nv_class_cap ? nv_class_cap * 2 : 16;
        grown = (NvClass **)nv_alloc(sizeof(NvClass *) * (size_t)nv_class_cap);
        if (nv_nclasses) {
            memcpy(grown, nv_classes, sizeof(NvClass *) * (size_t)nv_nclasses);
        }
        nv_classes = grown;
    }
    nv_classes[nv_nclasses++] = c;
    return c;
}

static void nv_class_field(NvClass *c, const char *name, const char *type) {
    if (c->nfields == c->fieldCap) {
        const char **names = (const char **)nv_alloc(sizeof(char *) * (size_t)c->fieldCap * 2);
        const char **types = (const char **)nv_alloc(sizeof(char *) * (size_t)c->fieldCap * 2);
        memcpy(names, c->fieldNames, sizeof(char *) * (size_t)c->nfields);
        memcpy(types, c->fieldTypes, sizeof(char *) * (size_t)c->nfields);
        c->fieldNames = names;
        c->fieldTypes = types;
        c->fieldCap *= 2;
    }
    c->fieldNames[c->nfields] = name;
    c->fieldTypes[c->nfields] = type;
    c->nfields++;
}

static void nv_class_method(NvClass *c, const char *name, int arity, NvMethodFn fn) {
    if (c->nmethods == c->methodCap) {
        NvMethod *grown = (NvMethod *)nv_alloc(sizeof(NvMethod) * (size_t)c->methodCap * 2);
        memcpy(grown, c->methods, sizeof(NvMethod) * (size_t)c->nmethods);
        c->methods = grown;
        c->methodCap *= 2;
    }
    c->methods[c->nmethods].name = name;
    c->methods[c->nmethods].arity = arity;
    c->methods[c->nmethods].fn = fn;
    c->nmethods++;
}

static void nv_class_ctor(NvClass *c, int arity, NvMethodFn fn) {
    c->ctor = fn;
    c->ctorArity = arity;
}

static NvClass *nv_class_base(NvClass *c) { return c->base ? nv_find_class(c->base) : 0; }

/* field type if `name` is a field somewhere in the class chain */
static const char *nv_class_field_type(NvClass *c, const char *name) {
    int guard = 0;
    while (c && guard++ < 64) {
        int i;
        for (i = 0; i < c->nfields; i++) {
            if (strcmp(c->fieldNames[i], name) == 0) {
                return c->fieldTypes[i];
            }
        }
        c = nv_class_base(c);
    }
    return 0;
}

static NvMethod *nv_class_find_method(NvClass *c, const char *name, int arity) {
    NvMethod *byName = 0;
    int guard = 0;
    while (c && guard++ < 64) {
        int i;
        for (i = 0; i < c->nmethods; i++) {
            if (strcmp(c->methods[i].name, name) == 0) {
                if (c->methods[i].arity == arity) {
                    return &c->methods[i];
                }
                if (!byName) {
                    byName = &c->methods[i];
                }
            }
        }
        if (byName) {
            return byName; /* most derived definition wins */
        }
        c = nv_class_base(c);
    }
    return byName;
}

/* Fields are laid out base class first, then the class itself. */
static int nv_class_field_count(NvClass *c);

static int nv_field_index(NvClass *c, const char *name) {
    NvClass *base = nv_class_base(c);
    int offset = base ? nv_class_field_count(base) : 0;
    int i;
    for (i = 0; i < c->nfields; i++) {
        if (strcmp(c->fieldNames[i], name) == 0) {
            return offset + i;
        }
    }
    return base ? nv_field_index(base, name) : -1;
}

/* Name and declared type of the field at `index`. */
static const char *nv_field_name_at(NvClass *c, int index, const char **type) {
    NvClass *base = nv_class_base(c);
    int offset = base ? nv_class_field_count(base) : 0;
    if (index >= offset) {
        if (type) {
            *type = c->fieldTypes[index - offset];
        }
        return c->fieldNames[index - offset];
    }
    return base ? nv_field_name_at(base, index, type) : 0;
}

static int *nv_field_order(NvClass *c, int count) {
    int i, j;
    if (c->order) {
        return c->order;
    }
    c->order = (int *)nv_alloc(sizeof(int) * (size_t)(count < 1 ? 1 : count));
    for (i = 0; i < count; i++) {
        /* insertion sort: field counts are tiny */
        const char *name = nv_field_name_at(c, i, 0);
        for (j = i; j > 0 && strcmp(nv_field_name_at(c, c->order[j - 1], 0), name) > 0; j--) {
            c->order[j] = c->order[j - 1];
        }
        c->order[j] = i;
    }
    return c->order;
}



static void nv_init_fields(NvClass *c, nv *fields) {
    int count = nv_class_field_count(c);
    if (count > 0) {
        memcpy(fields, nv_class_defaults(c), sizeof(nv) * (size_t)count);
    }
}

static int nv_class_field_count(NvClass *c) {
    int n = 0, guard = 0;
    NvClass *walk = c;
    if (c->totalFields >= 0) {
        return c->totalFields;
    }
    while (walk && guard++ < 64) {
        n += walk->nfields;
        walk = nv_class_base(walk);
    }
    c->totalFields = n;
    return n;
}

/* Default value per field slot, computed once per class. */
static nv *nv_class_defaults(NvClass *c) {
    int count, i;
    if (c->defaults) {
        return c->defaults;
    }
    count = nv_class_field_count(c);
    c->defaults = (nv *)nv_alloc(sizeof(nv) * (size_t)(count < 1 ? 1 : count));
    for (i = 0; i < count; i++) {
        const char *type = 0;
        nv_field_name_at(c, i, &type);
        c->defaults[i] = type ? nv_default(type) : nv_nil;
    }
    return c->defaults;
}

/* Coercion by type name costs a normalize plus up to three strcmp. The kind
 * is the same for every object of a class, so it is computed once. */
static signed char nv_type_kind(const char *t) {
    t = nv_normalize_type(t);
    if (strcmp(t, "integer") == 0) {
        return 1;
    }
    if (strcmp(t, "float") == 0) {
        return 2;
    }
    if (strcmp(t, "string") == 0) {
        return 3;
    }
    return 0;
}

static inline nv nv_coerce_kind(nv v, signed char kind, const char *type) {
    if (kind == 0) {
        return v;
    }
    if (kind == 1 && nv_type_of(v) != NV_FLOAT) {
        return v;
    }
    if (kind == 3 && nv_type_of(v) == NV_STR) {
        return v;
    }
    return nv_coerce(v, type);
}

/* Field types and kinds in slot order (base class first). */
static void nv_class_layout(NvClass *c) {
    int count, i;
    if (c->flatKinds) {
        return;
    }
    count = nv_class_field_count(c);
    if (count < 1) {
        count = 1;
    }
    c->flatTypes = (const char **)nv_alloc(sizeof(const char *) * (size_t)count);
    c->flatKinds = (signed char *)nv_alloc(sizeof(signed char) * (size_t)count);
    for (i = 0; i < nv_class_field_count(c); i++) {
        const char *type = 0;
        nv_field_name_at(c, i, &type);
        c->flatTypes[i] = type;
        c->flatKinds[i] = type ? nv_type_kind(type) : 0;
    }
}

/* Value, object header and field slots live in one arena block. */
typedef struct NvObjBlock {
    NvVal val;
    NvObj obj;
} NvObjBlock;

static nv nv_new_object_of(NvClass *c);

/* Class pointer cached per call site: the name lookup happens once. */
static nv nv_new_object_cached(NvClass **cache, const char *className) {
    if (!*cache) {
        *cache = nv_find_class(className);
        if (!*cache) {
            nv_error("unknown class '%s'", className);
        }
    }
    return nv_new_object_of(*cache);
}

static nv nv_new_object(const char *className) {
    NvClass *c = nv_find_class(className);
    if (!c) {
        nv_error("unknown class '%s'", className);
    }
    return nv_new_object_of(c);
}

static nv nv_new_object_of(NvClass *c) {
    NvObjBlock *b;
    int nfields;
    if (c->isAbstract) {
        nv_error("cannot instantiate abstract class '%s'", c->name);
    }
    nfields = nv_class_field_count(c);
    b = (NvObjBlock *)nv_alloc(sizeof(NvObjBlock) + sizeof(nv) * (size_t)(nfields < 1 ? 1 : nfields));
    b->val.type = NV_OBJ;
    b->val.slen = 0;
    b->val.scap = 0;
    b->val.o = &b->obj;
    b->obj.cls = c;
    b->obj.name = 0;
    nv_init_fields(c, nv_fields(&b->obj));
    return &b->val;
}

static nv nv_construct_obj(nv obj, nv *args, int n) {
    NvClass *c = obj->o->cls;
    if (!c->ctorResolved) {
        NvClass *walk = c;
        int guard = 0;
        while (walk && guard++ < 64) {
            if (walk->ctor) {
                c->resolvedCtor = walk->ctor;
                break;
            }
            walk = nv_class_base(walk);
        }
        c->ctorResolved = 1;
    }
    if (c->resolvedCtor) {
        c->resolvedCtor(obj, args, n);
        return obj;
    }
    /* no constructor: positional field initialization (base fields first) */
    {
        int count = nv_class_field_count(c);
        nv *slots = nv_fields(obj->o);
        int k;
        nv_class_layout(c);
        for (k = 0; k < n && k < count; k++) {
            slots[k] = nv_coerce_kind(args[k], c->flatKinds[k], c->flatTypes[k]);
        }
    }
    return obj;
}

static nv nv_construct_args(const char *className, nv *args, int n) {
    return nv_construct_obj(nv_new_object(className), args, n);
}

static nv nv_construct(const char *className, int n, ...) {
    nv args[64];
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n && i < 64; i++) {
        args[i] = va_arg(ap, nv);
    }
    va_end(ap);
    return nv_construct_args(className, args, n);
}

/* The common arities without va_list - object construction is hot. */
static inline nv nv_construct0(NvClass **cache, const char *className) {
    return nv_construct_obj(nv_new_object_cached(cache, className), 0, 0);
}

static inline nv nv_construct1(NvClass **cache, const char *className, nv a0) {
    nv args[1];
    args[0] = a0;
    return nv_construct_obj(nv_new_object_cached(cache, className), args, 1);
}

static inline nv nv_construct2(NvClass **cache, const char *className, nv a0, nv a1) {
    nv args[2];
    args[0] = a0;
    args[1] = a1;
    return nv_construct_obj(nv_new_object_cached(cache, className), args, 2);
}

static inline nv nv_construct3(NvClass **cache, const char *className, nv a0, nv a1, nv a2) {
    nv args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return nv_construct_obj(nv_new_object_cached(cache, className), args, 3);
}

/* Same, with the class pointer cached in a static at the call site. */
static nv nv_construct_cached(NvClass **cache, const char *className, int n, ...) {
    nv args[64];
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n && i < 64; i++) {
        args[i] = va_arg(ap, nv);
    }
    va_end(ap);
    return nv_construct_obj(nv_new_object_cached(cache, className), args, n);
}

/* Object literal: Name{field=value, ...} - no constructor is run. */
static nv nv_new_object_fields_cached(NvClass **cache, const char *className, int n, ...) {
    nv obj = nv_new_object_cached(cache, className);
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        const char *name = va_arg(ap, const char *);
        nv val = va_arg(ap, nv);
        int at = nv_field_index(obj->o->cls, name);
        if (at < 0) {
            nv_error("no field '%s' on %s", name, obj->o->cls->name);
        }
        nv_fields(obj->o)[at] = val;
    }
    va_end(ap);
    return obj;
}

static nv nv_new_object_fields(const char *className, int n, ...) {
    nv obj = nv_new_object(className);
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        const char *name = va_arg(ap, const char *);
        nv val = va_arg(ap, nv);
        int at = nv_field_index(obj->o->cls, name);
        if (at < 0) {
            nv_error("no field '%s' on %s", name, obj->o->cls->name);
        }
        nv_fields(obj->o)[at] = val;
    }
    va_end(ap);
    return obj;
}

static void nv_enum_add(const char *enumName, const char *constName, nv obj) {
    NvClass *c = nv_find_class(enumName);
    obj->o->name = constName;
    nv_map_set(c->constants, constName, obj);
    nv_arr_push(c->constantOrder, obj);
}

static nv nv_enum_get(const char *enumName, const char *constName) {
    NvClass *c = nv_find_class(enumName);
    nv v = c ? nv_map_get(c->constants, constName) : 0;
    if (!v) {
        nv_error("no constant '%s' in enum %s", constName, enumName);
    }
    return v;
}

static nv nv_enum_values(const char *enumName) {
    NvClass *c = nv_find_class(enumName);
    nv v = nv_arr();
    int i;
    if (c) {
        for (i = 0; i < c->constantOrder->len; i++) {
            nv_arr_push(v->a, c->constantOrder->items[i]);
        }
    }
    return v;
}

static int nv_deprecated_warned_dummy NV_UNUSED = 0;

static void nv_warn_deprecated(int *flag, const char *method, const char *since, const char *text) {
    if (*flag) {
        return;
    }
    *flag = 1;
    printf("[warning] Method '%s' is deprecated", method);
    if (since[0]) {
        printf(" (since %s)", since);
    }
    if (text[0]) {
        printf(": %s", text);
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Strings helpers                                                     */
/* ------------------------------------------------------------------ */

static nv nv_substr(nv s, long long start, long long end) {
    long long n = s->slen;
    if (start < 0) {
        start = 0;
    }
    if (end > n) {
        end = n;
    }
    if (start > end) {
        start = end;
    }
    return nv_strn(s->s + start, (int)(end - start));
}

static long long nv_str_index_of(nv s, nv needle, long long from) {
    const char *ns = nv_display(needle);
    const char *hs = nv_cstr(s);
    const char *p;
    if (from < 0) {
        from = 0;
    }
    if (from > s->slen) {
        return -1;
    }
    p = strstr(hs + from, ns);
    return p ? (long long)(p - hs) : -1;
}

static nv nv_str_split(nv s, nv sep) {
    nv out = nv_arr();
    const char *sp = nv_display(sep);
    size_t sl = strlen(sp);
    const char *cur = nv_cstr(s);
    const char *p;
    if (sl == 0) {
        int i;
        for (i = 0; i < s->slen; i++) {
            nv_arr_push(out->a, nv_strn(s->s + i, 1));
        }
        return out;
    }
    while ((p = strstr(cur, sp)) != 0) {
        nv_arr_push(out->a, nv_strn(cur, (int)(p - cur)));
        cur = p + sl;
    }
    nv_arr_push(out->a, nv_str(cur));
    return out;
}

/* Splits on runs of whitespace - the hot path of most text processing. */
static nv nv_str_words(nv s) {
    nv out = nv_arr();
    const char *text = nv_cstr(s);
    int i = 0, start;
    while (text[i]) {
        while (text[i] && isspace((unsigned char)text[i])) {
            i++;
        }
        if (!text[i]) {
            break;
        }
        start = i;
        while (text[i] && !isspace((unsigned char)text[i])) {
            i++;
        }
        nv_arr_push(out->a, nv_strn(text + start, i - start));
    }
    return out;
}

static nv nv_str_replace(nv s, nv from, nv to) {
    NvSb sb;
    const char *f = nv_display(from);
    const char *t = nv_display(to);
    size_t fl = strlen(f);
    const char *cur = nv_cstr(s);
    const char *p;
    if (fl == 0) {
        return s;
    }
    nv_sb_init(&sb);
    while ((p = strstr(cur, f)) != 0) {
        nv_sb_addn(&sb, cur, (int)(p - cur));
        nv_sb_add(&sb, t);
        cur = p + fl;
    }
    nv_sb_add(&sb, cur);
    {
        int len = sb.len;
        return nv_str_own(nv_sb_finish(&sb), len);
    }
}

static nv nv_str_trim(nv s) {
    int a = 0, b = s->slen;
    while (a < b && isspace((unsigned char)s->s[a])) {
        a++;
    }
    while (b > a && isspace((unsigned char)s->s[b - 1])) {
        b--;
    }
    return nv_strn(s->s + a, b - a);
}

static nv nv_str_case(nv s, int upper) {
    char *buf = nv_strndup(s->s, (size_t)s->slen);
    int i;
    for (i = 0; i < s->slen; i++) {
        buf[i] = (char)(upper ? toupper((unsigned char)buf[i]) : tolower((unsigned char)buf[i]));
    }
    return nv_str_own(buf, s->slen);
}

static nv nv_arr_join(nv a, nv sep) {
    NvSb sb;
    const char *sp = nv_display(sep);
    int i, len;
    nv_sb_init(&sb);
    for (i = 0; i < a->a->len; i++) {
        if (i > 0) {
            nv_sb_add(&sb, sp);
        }
        nv_display_into(&sb, a->a->items[i]);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static long long nv_arr_index_of(nv a, nv v) {
    int i;
    for (i = 0; i < a->a->len; i++) {
        if (nv_equals(a->a->items[i], v)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Method calls on any value                                           */
/* ------------------------------------------------------------------ */

/* Resolving a member means walking field names and method names with strcmp,
 * and the same (class, name) pair is asked for over and over. This cache is
 * keyed on the *pointers*: every name comes from a string literal in the
 * generated C, so a hit costs two compares and no string work. Classes are
 * fully registered before the first call runs, so entries never go stale. */
#define NV_MCACHE 2048
typedef struct NvMember {
    NvClass *cls;
    const char *name;
    int arity;
    int slot;          /* >= 0: field slot, -1: method */
    const char *ftype; /* declared field type, for setter coercion */
    signed char kind;  /* nv_type_kind of ftype */
    NvMethodFn fn;
} NvMember;
static NvMember nv_mcache[NV_MCACHE];

/* The resolved member, or 0 when the class has neither field nor method. */
static NvMember *nv_resolve_member(NvClass *c, const char *name, int arity) {
    size_t h = (((size_t)(uintptr_t)c >> 4) ^ ((size_t)(uintptr_t)name >> 2)
                ^ (size_t)(arity * 31)) & (size_t)(NV_MCACHE - 1);
    NvMember *e = &nv_mcache[h];
    NvMethod *m;
    int at;
    if (e->cls == c && e->name == name && e->arity == arity) {
        return e;
    }
    at = nv_field_index(c, name);
    if (at >= 0) {
        const char *ftype = 0;
        nv_field_name_at(c, at, &ftype);
        e->cls = c;
        e->name = name;
        e->arity = arity;
        e->slot = at;
        e->ftype = ftype;
        e->kind = ftype ? nv_type_kind(ftype) : 0;
        e->fn = 0;
        return e;
    }
    m = nv_class_find_method(c, name, arity);
    if (!m) {
        return 0;
    }
    e->cls = c;
    e->name = name;
    e->arity = arity;
    e->slot = -1;
    e->ftype = 0;
    e->fn = m->fn;
    return e;
}

static nv nv_invoke_args(nv t, const char *name, nv *args, int n) {
    if (nv_type_of(t) == NV_OBJ) {
        NvMember *e = nv_resolve_member(t->o->cls, name, n);
        if (e) {
            if (e->slot < 0) {
                return e->fn(t, args, n);
            }
            if (n == 0) {
                return nv_fields(t->o)[e->slot];   /* getter */
            }
            nv_fields(t->o)[e->slot] = nv_coerce_kind(args[0], e->kind, e->ftype);
            return nv_nil;                         /* setter */
        }
        nv_error("unknown member '%s' on %s", name, t->o->cls->name);
    }
    if (strcmp(name, "length") == 0) {
        if (nv_type_of(t) == NV_ARR) {
            return nv_int(t->a->len);
        }
        if (nv_type_of(t) == NV_MAP) {
            return nv_int(t->m->len);
        }
        if (nv_type_of(t) == NV_STR) {
            return nv_int(t->slen);
        }
        nv_error("'%s' has no length", nv_type_name(t));
    }
    if (nv_type_of(t) == NV_ARR) {
        if (strcmp(name, "append") == 0 || strcmp(name, "push") == 0) {
            int i;
            for (i = 0; i < n; i++) {
                nv_arr_push(t->a, args[i]);
            }
            return nv_nil;
        }
        if (strcmp(name, "pop") == 0) {
            if (t->a->len == 0) {
                nv_error("pop() on empty array");
            }
            return t->a->items[--t->a->len];
        }
        if (strcmp(name, "contains") == 0) {
            return nv_bool(n > 0 && nv_arr_index_of(t, args[0]) >= 0);
        }
        if (strcmp(name, "indexOf") == 0) {
            return nv_int(n > 0 ? nv_arr_index_of(t, args[0]) : -1);
        }
        if (strcmp(name, "join") == 0) {
            return nv_arr_join(t, n > 0 ? args[0] : nv_str(""));
        }
        if (strcmp(name, "clear") == 0) {
            t->a->len = 0;
            return nv_nil;
        }
        if (strcmp(name, "insert") == 0 && n == 2) {
            long long at = nv_as_int(args[0]);
            int i;
            if (at < 0 || at > t->a->len) {
                nv_error("insert index %lld out of bounds (size %d)", at, t->a->len);
            }
            nv_arr_push(t->a, nv_nil);
            for (i = t->a->len - 1; i > at; i--) {
                t->a->items[i] = t->a->items[i - 1];
            }
            t->a->items[at] = args[1];
            return nv_nil;
        }
        if (strcmp(name, "remove") == 0 && n == 1) {
            long long at = nv_as_int(args[0]);
            int i;
            if (at < 0 || at >= t->a->len) {
                nv_error("remove index %lld out of bounds (size %d)", at, t->a->len);
            }
            for (i = (int)at; i < t->a->len - 1; i++) {
                t->a->items[i] = t->a->items[i + 1];
            }
            t->a->len--;
            return nv_nil;
        }
    }
    if (nv_type_of(t) == NV_MAP) {
        if (strcmp(name, "has") == 0) {
            return nv_bool(n > 0 && nv_map_has(t->m, nv_display(args[0])));
        }
        if (strcmp(name, "keys") == 0) {
            nv out = nv_arr();
            int i;
            nv_map_order(t->m);
            for (i = 0; i < t->m->len; i++) {
                nv_arr_push(out->a, nv_str(t->m->items[i].key));
            }
            return out;
        }
        if (strcmp(name, "values") == 0) {
            nv out = nv_arr();
            int i;
            nv_map_order(t->m);
            for (i = 0; i < t->m->len; i++) {
                nv_arr_push(out->a, t->m->items[i].val);
            }
            return out;
        }
        if (strcmp(name, "remove") == 0) {
            if (n > 0) {
                nv_map_remove(t->m, nv_display(args[0]));
            }
            return nv_nil;
        }
        if (strcmp(name, "get") == 0 && n == 2) {
            nv v = nv_map_get(t->m, nv_display(args[0]));
            return v ? v : args[1];
        }
    }
    if (nv_type_of(t) == NV_STR) {
        if (strcmp(name, "charAt") == 0) {
            long long i = n > 0 ? nv_as_int(args[0]) : 0;
            if (i < 0 || i >= t->slen) {
                nv_error("charAt index %lld out of bounds (length %d)", i, t->slen);
            }
            return nv_strn(t->s + i, 1);
        }
        if (strcmp(name, "substring") == 0) {
            long long start = n > 0 ? nv_as_int(args[0]) : 0;
            long long end = n > 1 ? nv_as_int(args[1]) : t->slen;
            return nv_substr(t, start, end);
        }
        if (strcmp(name, "indexOf") == 0) {
            return nv_int(n > 0 ? nv_str_index_of(t, args[0], n > 1 ? nv_as_int(args[1]) : 0) : -1);
        }
        if (strcmp(name, "contains") == 0) {
            return nv_bool(n > 0 && nv_str_index_of(t, args[0], 0) >= 0);
        }
        if (strcmp(name, "startsWith") == 0) {
            const char *p = n > 0 ? nv_display(args[0]) : "";
            size_t pl = strlen(p);
            return nv_bool(pl <= (size_t)t->slen && memcmp(t->s, p, pl) == 0);
        }
        if (strcmp(name, "endsWith") == 0) {
            const char *p = n > 0 ? nv_display(args[0]) : "";
            size_t pl = strlen(p);
            return nv_bool(pl <= (size_t)t->slen && memcmp(t->s + t->slen - pl, p, pl) == 0);
        }
        if (strcmp(name, "split") == 0) {
            return nv_str_split(t, n > 0 ? args[0] : nv_str(""));
        }
        if (strcmp(name, "replace") == 0 && n == 2) {
            return nv_str_replace(t, args[0], args[1]);
        }
        if (strcmp(name, "trim") == 0) {
            return nv_str_trim(t);
        }
        if (strcmp(name, "toUpper") == 0) {
            return nv_str_case(t, 1);
        }
        if (strcmp(name, "toLower") == 0) {
            return nv_str_case(t, 0);
        }
    }
    nv_error("unknown method '%s()' on '%s'", name, nv_type_name(t));
    return nv_nil;
}

/* `obj.field()` and `obj.method()` without building an argument array. */
static inline nv nv_invoke0(nv t, const char *name) {
    if (nv_type_of(t) == NV_OBJ) {
        NvMember *e = nv_resolve_member(t->o->cls, name, 0);
        if (e) {
            return e->slot >= 0 ? nv_fields(t->o)[e->slot] : e->fn(t, 0, 0);
        }
    }
    return nv_invoke_args(t, name, 0, 0);
}

/* Calls with a known small arity skip the va_list: the arguments go into a
 * stack array directly. `nv_invoke` stays for the variadic cases. */
static inline nv nv_invoke1(nv t, const char *name, nv a) {
    nv args[1];
    args[0] = a;
    return nv_invoke_args(t, name, args, 1);
}

static inline nv nv_invoke2(nv t, const char *name, nv a, nv b) {
    nv args[2];
    args[0] = a;
    args[1] = b;
    return nv_invoke_args(t, name, args, 2);
}

static inline nv nv_invoke3(nv t, const char *name, nv a, nv b, nv c) {
    nv args[3];
    args[0] = a;
    args[1] = b;
    args[2] = c;
    return nv_invoke_args(t, name, args, 3);
}

/* `x.length()` and `x.has(k)` are, after append, the most frequent dynamic
 * calls; going straight to the collection skips the dispatch chain. */
static inline nv nv_length_of(nv t, const char *name) {
    int type = nv_type_of(t);
    if (type == NV_ARR) {
        return nv_int(t->a->len);
    }
    if (type == NV_MAP) {
        return nv_int(t->m->len);
    }
    if (type == NV_STR) {
        return nv_int(t->slen);
    }
    return nv_invoke0(t, name);
}

static inline nv nv_has_key(nv t, const char *name, nv key) {
    if (nv_type_of(t) == NV_MAP) {
        return nv_bool(nv_map_has(t->m, nv_display(key)));
    }
    return nv_invoke1(t, name, key);
}

/* `x.append(v)` is the hottest dynamic call there is: on an array it is a
 * push, everything else takes the general path. */
static inline nv nv_append(nv t, const char *name, nv v) {
    if (nv_type_of(t) == NV_ARR) {
        nv_arr_push(t->a, v);
        return nv_nil;
    }
    return nv_invoke1(t, name, v);
}

static nv nv_invoke(nv t, const char *name, int n, ...) {
    nv args[64];
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n && i < 64; i++) {
        args[i] = va_arg(ap, nv);
    }
    va_end(ap);
    return nv_invoke_args(t, name, args, n);
}

/* ------------------------------------------------------------------ */
/* Iteration                                                           */
/* ------------------------------------------------------------------ */

/* Iteration walks a snapshot, so the body may change the collection while
 * looping. The snapshot is allocated at its final size instead of growing. */
static NvArr *nv_arr_with_capacity(int cap) {
    NvArr *a = (NvArr *)nv_alloc(sizeof(NvArr));
    a->len = 0;
    a->heap = 0;
    a->cap = cap < 1 ? 1 : cap;
    if (a->cap >= NV_ARR_HEAP_AT) {
        a->items = (nv *)malloc(sizeof(nv) * (size_t)a->cap);
        if (!a->items) {
            nv_error("out of memory");
        }
        a->heap = 1;
    } else {
        a->items = (nv *)nv_alloc(sizeof(nv) * (size_t)a->cap);
    }
    return a;
}

static NvArr *nv_iter(nv v);

/* `for (x in xs)` walks a snapshot so the collection can be modified inside
 * the loop. When the body cannot call anything, nothing can modify it and
 * the array itself is walked - no copy of up to millions of slots. */
static NvArr *nv_iter_live(nv v) {
    if (nv_type_of(v) == NV_ARR) {
        return v->a;
    }
    return nv_iter(v);
}

static NvArr *nv_iter(nv v) {
    NvArr *out;
    int i;
    if (nv_type_of(v) == NV_ARR) {
        out = nv_arr_with_capacity(v->a->len);
        memcpy(out->items, v->a->items, sizeof(nv) * (size_t)v->a->len);
        out->len = v->a->len;
        return out;
    }
    out = nv_arr_new();
    if (nv_type_of(v) == NV_MAP) {
        nv_map_order(v->m);
        for (i = 0; i < v->m->len; i++) {
            nv_arr_push(out, nv_str(v->m->items[i].key));
        }
        return out;
    }
    if (nv_type_of(v) == NV_STR) {
        for (i = 0; i < v->slen; i++) {
            nv_arr_push(out, nv_strn(v->s + i, 1));
        }
        return out;
    }
    nv_error("cannot iterate over a value of type %s", nv_type_name(v));
    return out;
}

/* ------------------------------------------------------------------ */
/* I/O and builtins                                                    */
/* ------------------------------------------------------------------ */

static void nv_print(nv v) { fputs(nv_display(v), stdout); }

static void nv_println(nv v) {
    fputs(nv_display(v), stdout);
    fputc('\n', stdout);
}

static void nv_eprintln(nv v) {
    fflush(stdout);
    fputs(nv_display(v), stderr);
    fputc('\n', stderr);
}

static nv nv_args_global = 0;

static void nv_init_args(int argc, char **argv) {
    int i;
#ifdef _WIN32
    /* LF line endings on every platform (no CRLF translation) */
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    for (i = 0; i < 256; i++) {
        char c = (char)i;
        nv v = nv_new(NV_STR);
        v->s = nv_strndup(&c, 1);
        v->slen = 1;
        v->scap = 0;
        nv_char_table[i] = v;
    }
    nv_empty_str_val.s = "";
    nv_args_global = nv_arr();
    for (i = 1; i < argc; i++) {
        nv_arr_push(nv_args_global->a, nv_str(argv[i]));
    }
}

static nv nv_args(void) { return nv_args_global ? nv_args_global : nv_arr(); }

static nv nv_read_file(nv path) {
    FILE *f = fopen(nv_display(path), "rb");
    long n;
    char *buf;
    size_t rd;
    if (!f) {
        return nv_str("");
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)nv_alloc((size_t)n + 1);
    rd = fread(buf, 1, (size_t)n, f);
    buf[rd] = 0;
    fclose(f);
    return nv_str_own(buf, (int)rd);
}

static nv nv_write_file(nv path, nv content) {
    FILE *f = fopen(nv_display(path), "wb");
    const char *s;
    if (!f) {
        nv_error("cannot write file '%s'", nv_display(path));
    }
    s = nv_display(content);
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    return nv_nil;
}

static nv nv_remove_file(nv path) {
    return nv_bool(remove(nv_display(path)) == 0);
}

static int nv_path_exists_c(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static nv nv_file_exists(nv path) { return nv_bool(nv_path_exists_c(nv_display(path))); }

static nv nv_read_line(void) {
    NvSb sb;
    int c;
    int len;
    nv_sb_init(&sb);
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (c != '\r') {
            nv_sb_addc(&sb, (char)c);
        }
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_parse_int(nv v) {
    if (nv_type_of(v) == NV_INT) {
        return v;
    }
    if (nv_type_of(v) == NV_FLOAT) {
        return nv_int((long long)v->f);
    }
    return nv_int(atoll(nv_display(v)));
}

static nv nv_parse_float(nv v) {
    if (nv_type_of(v) == NV_FLOAT) {
        return v;
    }
    return nv_float(nv_as_double(v));
}

static nv nv_chr(nv code) {
    char c = (char)nv_as_int(code);
    return nv_strn(&c, 1);
}

static nv nv_ord(nv s) {
    const char *p = nv_display(s);
    return nv_int(p[0] ? (unsigned char)p[0] : 0);
}

static nv nv_typeof_builtin(nv v) { return nv_str(nv_type_name(v)); }

/* cmd.exe strips the outer quotes of `"prog" "arg"`; one more pair of
 * quotes around the whole line keeps them intact. */
static const char *nv_shell_line(const char *cmd) {
#ifdef _WIN32
    size_t n = strlen(cmd);
    char *buf = (char *)nv_alloc(n + 3);
    buf[0] = '"';
    memcpy(buf + 1, cmd, n);
    buf[n + 1] = '"';
    buf[n + 2] = 0;
    return buf;
#else
    return cmd;
#endif
}

static int nv_exit_status(int rc) {
#ifdef _WIN32
    return rc;
#else
    if (rc == -1) {
        return -1;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return -1;
#endif
}

static nv nv_exec(nv cmd) {
    int rc;
    fflush(stdout);
    fflush(stderr);
    rc = system(nv_shell_line(nv_display(cmd)));
    return nv_int(nv_exit_status(rc));
}

static nv nv_env(nv name) {
    const char *v = getenv(nv_display(name));
    return nv_str(v ? v : "");
}

static nv nv_exit(nv code) {
    fflush(stdout);
    exit((int)nv_as_int(code));
    return nv_nil;
}

static nv nv_platform(void) {
#if defined(_WIN32)
    return nv_str("windows");
#elif defined(__APPLE__)
    return nv_str("macos");
#elif defined(__linux__)
    return nv_str("linux");
#else
    return nv_str("unix");
#endif
}

/* ------------------------------------------------------------------ */
/* path module                                                         */
/* ------------------------------------------------------------------ */

static nv nv_path_join_args(nv *args, int n) {
    NvSb sb;
    int i, len;
    nv_sb_init(&sb);
    for (i = 0; i < n; i++) {
        const char *p = nv_display(args[i]);
        if (p[0] == 0) {
            continue;
        }
        if (sb.len > 0 && sb.buf[sb.len - 1] != '/' && sb.buf[sb.len - 1] != '\\') {
            nv_sb_addc(&sb, '/');
        }
        nv_sb_add(&sb, p);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_path_join(int n, ...) {
    nv args[64];
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n && i < 64; i++) {
        args[i] = va_arg(ap, nv);
    }
    va_end(ap);
    return nv_path_join_args(args, n);
}

/* Paths reported by the runtime use forward slashes on every platform. */
static nv nv_path_slashes(const char *s) {
#ifdef _WIN32
    char *buf = nv_strndup(s, strlen(s));
    char *p;
    for (p = buf; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return nv_str_own(buf, (int)strlen(buf));
#else
    return nv_str(s);
#endif
}

static nv nv_path_absolute(void) {
    char buf[4096];
    if (!NV_GETCWD(buf, sizeof(buf))) {
        return nv_str(".");
    }
    return nv_path_slashes(buf);
}

static nv nv_path_exists(nv p) { return nv_bool(nv_path_exists_c(nv_display(p))); }

static nv nv_path_dirname(nv p) {
    const char *s = nv_display(p);
    int n = (int)strlen(s);
    while (n > 0 && s[n - 1] != '/' && s[n - 1] != '\\') {
        n--;
    }
    if (n == 0) {
        return nv_str(".");
    }
    if (n > 1) {
        n--; /* drop the separator itself */
    }
    return nv_strn(s, n);
}

static nv nv_path_basename(nv p) {
    const char *s = nv_display(p);
    int n = (int)strlen(s);
    int i = n;
    while (i > 0 && s[i - 1] != '/' && s[i - 1] != '\\') {
        i--;
    }
    return nv_str(s + i);
}

static nv nv_path_stem(nv p) {
    nv base = nv_path_basename(p);
    int i = base->slen;
    while (i > 0 && base->s[i - 1] != '.') {
        i--;
    }
    if (i <= 1) {
        return base;
    }
    return nv_strn(base->s, i - 1);
}

static nv nv_path_temp(void) {
    const char *t = getenv("TMPDIR");
    if (!t || !t[0]) {
        t = getenv("TEMP");
    }
    if (!t || !t[0]) {
        t = getenv("TMP");
    }
    if (!t || !t[0]) {
        t = "/tmp";
    }
    return nv_path_slashes(t);
}

static nv nv_path_extension(nv p) {
    nv base = nv_path_basename(p);
    int i = base->slen;
    while (i > 0 && base->s[i - 1] != '.') {
        i--;
    }
    if (i <= 1) {
        return nv_str("");
    }
    return nv_strn(base->s + i - 1, base->slen - i + 1);
}

static nv nv_path_is_absolute(nv p) {
    const char *s = nv_display(p);
    if (s[0] == '/' || s[0] == '\\') {
        return nv_bool(1);
    }
    return nv_bool(isalpha((unsigned char)s[0]) && s[1] == ':');
}

static nv nv_path_separator(void) {
#ifdef _WIN32
    return nv_str("\\");
#else
    return nv_str("/");
#endif
}

/* Collapses "." and ".." segments and duplicate separators. */
static nv nv_path_normalize(nv p) {
    const char *s = nv_display(p);
    const char *segs[1024];
    int lens[1024];
    int n = 0, i = 0, len = (int)strlen(s), k;
    NvSb sb;
    int rooted = 0, out;
    char drive[3] = {0, 0, 0};
    if (isalpha((unsigned char)s[0]) && s[1] == ':') {
        drive[0] = s[0];
        drive[1] = ':';
        i = 2;
    }
    if (s[i] == '/' || s[i] == '\\') {
        rooted = 1;
    }
    while (i < len) {
        int start;
        while (i < len && (s[i] == '/' || s[i] == '\\')) {
            i++;
        }
        start = i;
        while (i < len && s[i] != '/' && s[i] != '\\') {
            i++;
        }
        if (i == start) {
            continue;
        }
        if (i - start == 1 && s[start] == '.') {
            continue;
        }
        if (i - start == 2 && s[start] == '.' && s[start + 1] == '.') {
            if (n > 0 && !(lens[n - 1] == 2 && segs[n - 1][0] == '.' && segs[n - 1][1] == '.')) {
                n--;
                continue;
            }
            if (rooted) {
                continue;
            }
        }
        if (n < 1024) {
            segs[n] = s + start;
            lens[n] = i - start;
            n++;
        }
    }
    nv_sb_init(&sb);
    if (drive[0]) {
        nv_sb_add(&sb, drive);
    }
    if (rooted) {
        nv_sb_addc(&sb, '/');
    }
    for (k = 0; k < n; k++) {
        if (k > 0) {
            nv_sb_addc(&sb, '/');
        }
        nv_sb_addn(&sb, segs[k], lens[k]);
    }
    if (sb.len == 0) {
        nv_sb_addc(&sb, '.');
    }
    out = sb.len;
    return nv_str_own(nv_sb_finish(&sb), out);
}

static nv nv_path_absolute_of(nv p) {
    if (nv_truthy(nv_path_is_absolute(p))) {
        return nv_path_normalize(p);
    }
    return nv_path_normalize(nv_path_join(2, nv_path_absolute(), p));
}

/* Path of `target` relative to directory `base` (both made absolute). */
static nv nv_path_relative(nv base, nv target) {
    nv b = nv_path_absolute_of(base);
    nv t = nv_path_absolute_of(target);
    nv bparts = nv_str_split(b, nv_str("/"));
    nv tparts = nv_str_split(t, nv_str("/"));
    int common = 0, i;
    nv out = nv_arr();
    while (common < bparts->a->len && common < tparts->a->len &&
           strcmp(nv_cstr(bparts->a->items[common]), nv_cstr(tparts->a->items[common])) == 0) {
        common++;
    }
    for (i = common; i < bparts->a->len; i++) {
        if (bparts->a->items[i]->slen > 0) {
            nv_arr_push(out->a, nv_str(".."));
        }
    }
    for (i = common; i < tparts->a->len; i++) {
        if (tparts->a->items[i]->slen > 0) {
            nv_arr_push(out->a, tparts->a->items[i]);
        }
    }
    if (out->a->len == 0) {
        return nv_str(".");
    }
    return nv_arr_join(out, nv_str("/"));
}

/* ------------------------------------------------------------------ */
/* os module                                                           */
/* ------------------------------------------------------------------ */

static int nv_stat_mode(const char *p, int *isdir) {
    struct stat st;
    if (stat(p, &st) != 0) {
        return 0;
    }
#ifdef S_ISDIR
    *isdir = S_ISDIR(st.st_mode);
#else
    *isdir = (st.st_mode & S_IFMT) == S_IFDIR;
#endif
    return 1;
}

static nv nv_os_is_dir(nv p) {
    int isdir = 0;
    return nv_bool(nv_stat_mode(nv_display(p), &isdir) && isdir);
}

static nv nv_os_is_file(nv p) {
    int isdir = 0;
    return nv_bool(nv_stat_mode(nv_display(p), &isdir) && !isdir);
}

/* mkdir -p */
static nv nv_os_mkdir(nv p) {
    const char *s = nv_display(p);
    size_t n = strlen(s), i;
    char *buf = nv_strndup(s, n);
    int isdir = 0;
    for (i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = 0;
            if (!(buf[i - 1] == ':' && i == 2)) {
                NV_MKDIR(buf);
            }
            buf[i] = saved;
        }
    }
    NV_MKDIR(buf);
    return nv_bool(nv_stat_mode(buf, &isdir) && isdir);
}

static nv nv_os_rmdir(nv p) { return nv_bool(NV_RMDIR(nv_display(p)) == 0); }

static int nv_name_cmp(const void *a, const void *b) {
    return strcmp(nv_cstr(*(const nv *)a), nv_cstr(*(const nv *)b));
}

/* Entries of a directory (without . and ..), sorted. */
static nv nv_os_list_dir(nv p) {
    nv out = nv_arr();
    const char *dir = nv_display(p);
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE h;
    char *pattern = (char *)nv_alloc(strlen(dir) + 3);
    strcpy(pattern, dir);
    strcat(pattern, "/*");
    h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        nv_error("cannot list directory '%s'", dir);
    }
    do {
        if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0) {
            nv_arr_push(out->a, nv_str(data.cFileName));
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) {
        nv_error("cannot list directory '%s'", dir);
    }
    while ((e = readdir(d)) != 0) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            nv_arr_push(out->a, nv_str(e->d_name));
        }
    }
    closedir(d);
#endif
    if (out->a->len > 1) {
        qsort(out->a->items, (size_t)out->a->len, sizeof(nv), nv_name_cmp);
    }
    return out;
}

/* rm -rf */
static nv nv_os_remove_all(nv p) {
    int isdir = 0;
    const char *s = nv_display(p);
    if (!nv_stat_mode(s, &isdir)) {
        return nv_bool(0);
    }
    if (isdir) {
        nv entries = nv_os_list_dir(p);
        int i;
        for (i = 0; i < entries->a->len; i++) {
            nv_os_remove_all(nv_path_join(2, p, entries->a->items[i]));
        }
        return nv_bool(NV_RMDIR(s) == 0);
    }
    return nv_bool(remove(s) == 0);
}

static nv nv_os_rename(nv from, nv to) { return nv_bool(rename(nv_display(from), nv_display(to)) == 0); }

static nv nv_os_copy(nv from, nv to) {
    FILE *in = fopen(nv_display(from), "rb");
    FILE *out;
    char buf[65536];
    size_t n;
    if (!in) {
        return nv_bool(0);
    }
    out = fopen(nv_display(to), "wb");
    if (!out) {
        fclose(in);
        return nv_bool(0);
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return nv_bool(1);
}

static nv nv_os_chdir(nv p) { return nv_bool(NV_CHDIR(nv_display(p)) == 0); }

static nv nv_os_file_size(nv p) {
    struct stat st;
    if (stat(nv_display(p), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_size);
}

static nv nv_os_modified(nv p) {
    struct stat st;
    if (stat(nv_display(p), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_mtime);
}

static nv nv_append_file(nv path, nv content) {
    FILE *f = fopen(nv_display(path), "ab");
    const char *s;
    if (!f) {
        nv_error("cannot write file '%s'", nv_display(path));
    }
    s = nv_display(content);
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    return nv_nil;
}

/* Runs a command and returns what it printed to stdout. */
static nv nv_os_output(nv cmd) {
    FILE *p;
    NvSb sb;
    char buf[4096];
    size_t n;
    int len;
    fflush(stdout);
    fflush(stderr);
    p = NV_POPEN(nv_shell_line(nv_display(cmd)), "r");
    if (!p) {
        nv_error("cannot run '%s'", nv_display(cmd));
    }
    nv_sb_init(&sb);
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) {
        nv_sb_addn(&sb, buf, (int)n);
    }
    NV_PCLOSE(p);
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_os_set_env(nv name, nv value) {
#ifdef _WIN32
    return nv_bool(_putenv_s(nv_display(name), nv_display(value)) == 0);
#else
    return nv_bool(setenv(nv_display(name), nv_display(value), 1) == 0);
#endif
}

static nv nv_os_time(void) { return nv_int((long long)time(0)); }

static nv nv_os_clock(void) {
#ifdef _WIN32
    FILETIME ft;
    unsigned long long t;
    GetSystemTimeAsFileTime(&ft);
    t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return nv_float((double)(t - 116444736000000000ULL) / 10000000.0);
#else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return nv_float((double)tv.tv_sec + (double)tv.tv_usec / 1000000.0);
#endif
}

static nv nv_os_sleep(nv ms) {
    long long m = nv_as_int(ms);
#ifdef _WIN32
    Sleep((DWORD)m);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(m / 1000);
    ts.tv_nsec = (long)(m % 1000) * 1000000L;
    nanosleep(&ts, 0);
#endif
    return nv_nil;
}

static nv nv_os_home(void) {
    const char *h = getenv("HOME");
    if (!h || !h[0]) {
        h = getenv("USERPROFILE");
    }
    return nv_path_slashes(h ? h : "");
}

static nv nv_os_pid(void) { return nv_int((long long)NV_GETPID()); }

/* ------------------------------------------------------------------ */
/* std natives: math, time, random, fmt, hash, io, arrays              */
/* ------------------------------------------------------------------ */

static nv nv_math_sqrt(nv x) { return nv_float(sqrt(nv_as_double(x))); }
static nv nv_math_pow(nv b, nv e) { return nv_float(pow(nv_as_double(b), nv_as_double(e))); }
static nv nv_math_floor(nv x) { return nv_int((long long)floor(nv_as_double(x))); }
static nv nv_math_ceil(nv x) { return nv_int((long long)ceil(nv_as_double(x))); }
static nv nv_math_round(nv x) { return nv_int((long long)floor(nv_as_double(x) + 0.5)); }
static nv nv_math_sin(nv x) { return nv_float(sin(nv_as_double(x))); }
static nv nv_math_cos(nv x) { return nv_float(cos(nv_as_double(x))); }
static nv nv_math_tan(nv x) { return nv_float(tan(nv_as_double(x))); }
static nv nv_math_atan2(nv y, nv x) { return nv_float(atan2(nv_as_double(y), nv_as_double(x))); }
static nv nv_math_log(nv x) { return nv_float(log(nv_as_double(x))); }
static nv nv_math_exp(nv x) { return nv_float(exp(nv_as_double(x))); }

static struct tm *nv_gmtime(nv unixSeconds, struct tm *out) {
    time_t t = (time_t)nv_as_int(unixSeconds);
    struct tm *g = gmtime(&t);
    if (g) {
        *out = *g;
    } else {
        memset(out, 0, sizeof(*out));
    }
    return out;
}

static nv nv_time_format(nv unixSeconds, nv layout) {
    char buf[256];
    struct tm tm;
    nv_gmtime(unixSeconds, &tm);
    if (strftime(buf, sizeof(buf), nv_display(layout), &tm) == 0) {
        buf[0] = 0;
    }
    return nv_str(buf);
}

static nv nv_time_iso(nv unixSeconds) { return nv_time_format(unixSeconds, nv_str("%Y-%m-%dT%H:%M:%SZ")); }

static nv nv_time_parts(nv unixSeconds) {
    struct tm tm;
    nv m = nv_map();
    nv_gmtime(unixSeconds, &tm);
    nv_map_set_static(m->m, "year", nv_int(tm.tm_year + 1900));
    nv_map_set_static(m->m, "month", nv_int(tm.tm_mon + 1));
    nv_map_set_static(m->m, "day", nv_int(tm.tm_mday));
    nv_map_set_static(m->m, "hour", nv_int(tm.tm_hour));
    nv_map_set_static(m->m, "minute", nv_int(tm.tm_min));
    nv_map_set_static(m->m, "second", nv_int(tm.tm_sec));
    nv_map_set_static(m->m, "weekday", nv_int(tm.tm_wday));
    nv_map_set_static(m->m, "yearday", nv_int(tm.tm_yday + 1));
    return m;
}

static unsigned long long nv_rng_state = 0;

static nv nv_random_seed(nv value) {
    nv_rng_state = (unsigned long long)nv_as_int(value) * 2654435761ULL + 88172645463325252ULL;
    return nv_nil;
}

static nv nv_random_next(void) {
    unsigned long long x;
    if (nv_rng_state == 0) {
        nv_random_seed(nv_int((long long)time(0) ^ (long long)NV_GETPID()));
    }
    x = nv_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    nv_rng_state = x;
    return nv_int((long long)(x >> 2));
}

static nv nv_fmt_fixed(nv x, nv decimals) {
    char buf[64];
    int d = (int)nv_as_int(decimals);
    if (d < 0) {
        d = 0;
    }
    if (d > 20) {
        d = 20;
    }
    sprintf(buf, "%.*f", d, nv_as_double(x));
    return nv_str(buf);
}

static nv nv_hash_fnv1a(nv text) {
    const unsigned char *p = (const unsigned char *)nv_display(text);
    unsigned long long h = 14695981039346656037ULL;
    for (; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return nv_int((long long)(h >> 2));
}

static nv nv_hash_crc32(nv text) {
    const unsigned char *p = (const unsigned char *)nv_display(text);
    unsigned int crc = 0xFFFFFFFFu;
    for (; *p; p++) {
        int k;
        crc ^= *p;
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return nv_int((long long)(crc ^ 0xFFFFFFFFu));
}

static nv nv_read_all(void) {
    NvSb sb;
    char buf[4096];
    size_t n;
    int len;
    nv_sb_init(&sb);
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        nv_sb_addn(&sb, buf, (int)n);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_io_write(nv text) {
    fputs(nv_display(text), stdout);
    return nv_nil;
}

static nv nv_io_write_err(nv text) {
    fputs(nv_display(text), stderr);
    return nv_nil;
}

static nv nv_io_flush(void) {
    fflush(stdout);
    fflush(stderr);
    return nv_nil;
}

static int nv_sort_cmp(const void *a, const void *b) {
    nv l = *(const nv *)a;
    nv r = *(const nv *)b;
    if (nv_is_num(l) && nv_is_num(r)) {
        double x = nv_as_double(l), y = nv_as_double(r);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    return strcmp(nv_data(l), nv_data(r));
}

/* Specialised comparators: no type dispatch and no string materialisation
 * per comparison, which is most of the work when sorting a large array. */
static int nv_sort_cmp_tagged(const void *a, const void *b) {
    long long x = nv_ival(*(const nv *)a);
    long long y = nv_ival(*(const nv *)b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int nv_sort_cmp_text(const void *a, const void *b) {
    return strcmp(nv_cstr(*(const nv *)a), nv_cstr(*(const nv *)b));
}

/* Introsort over raw 64-bit values: no indirect call per comparison, which
 * is what makes the generic qsort path slow for large integer arrays. */
static void nv_sort_ll(long long *values, int left, int right) {
    while (right - left > 12) {
        long long pivot, tmp;
        int i = left, j = right, middle = left + (right - left) / 2;
        if (values[middle] < values[left]) {
            tmp = values[middle]; values[middle] = values[left]; values[left] = tmp;
        }
        if (values[right] < values[left]) {
            tmp = values[right]; values[right] = values[left]; values[left] = tmp;
        }
        if (values[right] < values[middle]) {
            tmp = values[right]; values[right] = values[middle]; values[middle] = tmp;
        }
        pivot = values[middle];
        while (i <= j) {
            while (values[i] < pivot) {
                i++;
            }
            while (values[j] > pivot) {
                j--;
            }
            if (i <= j) {
                tmp = values[i]; values[i] = values[j]; values[j] = tmp;
                i++; j--;
            }
        }
        /* recurse into the smaller side, loop on the larger one */
        if (j - left < right - i) {
            nv_sort_ll(values, left, j);
            left = i;
        } else {
            nv_sort_ll(values, i, right);
            right = j;
        }
    }
    {
        int i;
        for (i = left + 1; i <= right; i++) {
            long long value = values[i];
            int j = i - 1;
            while (j >= left && values[j] > value) {
                values[j + 1] = values[j];
                j--;
            }
            values[j + 1] = value;
        }
    }
}

/* A sorted copy (numbers numerically, everything else by text). */
static nv nv_arr_sorted(nv a) {
    nv out;
    int i, tagged = 1, text = 1;
    if (nv_type_of(a) != NV_ARR) {
        nv_error("sort() needs an array");
    }
    out = nv_new(NV_ARR);
    out->a = nv_arr_with_capacity(a->a->len);
    memcpy(out->a->items, a->a->items, sizeof(nv) * (size_t)a->a->len);
    out->a->len = a->a->len;
    for (i = 0; i < out->a->len; i++) {
        nv value = out->a->items[i];
        if (!nv_is_tagged(value)) {
            tagged = 0;
            if (nv_type_of(value) != NV_STR) {
                text = 0;
            }
        } else {
            text = 0;
        }
        if (!tagged && !text) {
            break;
        }
    }
    if (out->a->len > 1) {
        if (tagged) {
            /* unbox, sort as plain integers, tag again */
            long long *values = (long long *)malloc(sizeof(long long) * (size_t)out->a->len);
            if (!values) {
                nv_error("out of memory");
            }
            for (i = 0; i < out->a->len; i++) {
                values[i] = nv_ival(out->a->items[i]);
            }
            nv_sort_ll(values, 0, out->a->len - 1);
            for (i = 0; i < out->a->len; i++) {
                out->a->items[i] = nv_int(values[i]);
            }
            free(values);
            return out;
        }
        qsort(out->a->items, (size_t)out->a->len, sizeof(nv), text ? nv_sort_cmp_text : nv_sort_cmp);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* http module - driven by the curl command line tool                  */
/* ------------------------------------------------------------------ */

static nv nv_json_stringify(nv v);

static const char *nv_shell_quote(const char *s) {
    NvSb sb;
    nv_sb_init(&sb);
#ifdef _WIN32
    nv_sb_addc(&sb, '"');
    for (; *s; s++) {
        if (*s == '"') {
            nv_sb_add(&sb, "\\\"");
        } else {
            nv_sb_addc(&sb, *s);
        }
    }
    nv_sb_addc(&sb, '"');
#else
    nv_sb_addc(&sb, '\'');
    for (; *s; s++) {
        if (*s == '\'') {
            nv_sb_add(&sb, "'\\''");
        } else {
            nv_sb_addc(&sb, *s);
        }
    }
    nv_sb_addc(&sb, '\'');
#endif
    return nv_sb_finish(&sb);
}

static int nv_http_counter = 0;

static nv nv_http_temp_name(const char *what) {
    char buf[64];
    sprintf(buf, "novus-http-%lld-%d-%s", (long long)NV_GETPID(), nv_http_counter++, what);
    return nv_path_join(2, nv_path_temp(), nv_str(buf));
}

/* Parses "Key: value" lines of a dumped header block into a map. */
static nv nv_http_parse_headers(nv text) {
    nv lines = nv_str_split(text, nv_str("\n"));
    nv out = nv_map();
    int i;
    for (i = 0; i < lines->a->len; i++) {
        nv line = nv_str_trim(lines->a->items[i]);
        const char *s = nv_cstr(line);
        const char *colon = strchr(s, ':');
        if (!colon || strncmp(s, "HTTP/", 5) == 0) {
            continue;
        }
        {
            nv key = nv_str_trim(nv_strn(s, (int)(colon - s)));
            nv val = nv_str_trim(nv_str(colon + 1));
            nv_map_set(out->m, nv_cstr(nv_str_case(key, 0)), val);
        }
    }
    return out;
}

/* {status, ok, body, headers, error} - never aborts on transport errors. */
static nv nv_http_request(nv method, nv url, nv body, nv headers) {
    nv outFile = nv_http_temp_name("body");
    nv hdrFile = nv_http_temp_name("headers");
    nv errFile = nv_http_temp_name("stderr");
    nv bodyFile = 0;
    nv result = nv_map();
    nv statusText;
    NvSb cmd;
    int hasContentType = 0, i;
    nv_sb_init(&cmd);
    nv_sb_add(&cmd, "curl -s -S -L --max-redirs 10 -X ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(nv_str_case(nv_to_str(method), 1))));
    nv_sb_add(&cmd, " --output ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(outFile)));
    nv_sb_add(&cmd, " --dump-header ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(hdrFile)));
    nv_sb_add(&cmd, " --stderr ");
    nv_sb_add(&cmd, nv_shell_quote(nv_cstr(errFile)));
    nv_sb_add(&cmd, " --write-out ");
    nv_sb_add(&cmd, nv_shell_quote("%{http_code}"));
    if (headers && nv_type_of(headers) == NV_MAP) {
        nv_map_order(headers->m);
        for (i = 0; i < headers->m->len; i++) {
            nv line = nv_concat(nv_concat(nv_str(headers->m->items[i].key), nv_str(": ")), headers->m->items[i].val);
            if (strcmp(nv_cstr(nv_str_case(nv_str(headers->m->items[i].key), 0)), "content-type") == 0) {
                hasContentType = 1;
            }
            nv_sb_add(&cmd, " -H ");
            nv_sb_add(&cmd, nv_shell_quote(nv_cstr(line)));
        }
    }
    if (body && nv_type_of(body) != NV_NULL && !(nv_type_of(body) == NV_STR && body->slen == 0)) {
        nv text = body;
        if (nv_type_of(body) == NV_MAP || nv_type_of(body) == NV_ARR || nv_type_of(body) == NV_OBJ) {
            text = nv_json_stringify(body);
            if (!hasContentType) {
                nv_sb_add(&cmd, " -H ");
                nv_sb_add(&cmd, nv_shell_quote("Content-Type: application/json"));
            }
        }
        bodyFile = nv_http_temp_name("request");
        nv_write_file(bodyFile, text);
        nv_sb_add(&cmd, " --data-binary @");
        nv_sb_add(&cmd, nv_shell_quote(nv_cstr(bodyFile)));
    }
    nv_sb_add(&cmd, " ");
    nv_sb_add(&cmd, nv_shell_quote(nv_display(url)));
    {
        int len = cmd.len;
        nv command = nv_str_own(nv_sb_finish(&cmd), len);
        statusText = nv_str_trim(nv_os_output(command));
    }
    {
        long long status = atoll(nv_cstr(statusText));
        nv error = nv_str_trim(nv_read_file(errFile));
        if (statusText->slen == 0) {
            error = nv_str("http: could not run curl (is it installed and in PATH?)");
        }
        nv_map_set(result->m, "status", nv_int(status));
        nv_map_set(result->m, "ok", nv_bool(status >= 200 && status < 300));
        nv_map_set(result->m, "body", nv_read_file(outFile));
        nv_map_set(result->m, "headers", nv_http_parse_headers(nv_read_file(hdrFile)));
        nv_map_set(result->m, "error", status == 0 ? (error->slen ? error : nv_str("http: request failed")) : nv_str(""));
    }
    remove(nv_cstr(outFile));
    remove(nv_cstr(hdrFile));
    remove(nv_cstr(errFile));
    if (bodyFile) {
        remove(nv_cstr(bodyFile));
    }
    return result;
}

static nv nv_http_simple(const char *method, nv url, nv body) {
    nv r = nv_http_request(nv_str(method), url, body, nv_map());
    nv error = nv_map_get(r->m, "error");
    if (error->slen) {
        nv_error("%s (%s %s)", nv_cstr(error), method, nv_display(url));
    }
    return nv_map_get(r->m, "body");
}

static nv nv_http_get(nv url) { return nv_http_simple("GET", url, nv_nil); }
static nv nv_http_post(nv url, nv body) { return nv_http_simple("POST", url, body); }
static nv nv_http_put(nv url, nv body) { return nv_http_simple("PUT", url, body); }
static nv nv_http_delete(nv url) { return nv_http_simple("DELETE", url, nv_nil); }

static nv nv_http_download(nv url, nv file) {
    nv r = nv_http_request(nv_str("GET"), url, nv_nil, nv_map());
    if (!nv_truthy(nv_map_get(r->m, "ok"))) {
        return nv_bool(0);
    }
    nv_write_file(file, nv_map_get(r->m, "body"));
    return nv_bool(1);
}

/* ------------------------------------------------------------------ */
/* json module                                                         */
/* ------------------------------------------------------------------ */

static void nv_json_string(NvSb *sb, const char *s) {
    nv_sb_addc(sb, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':
            nv_sb_add(sb, "\\\"");
            break;
        case '\\':
            nv_sb_add(sb, "\\\\");
            break;
        case '\n':
            nv_sb_add(sb, "\\n");
            break;
        case '\r':
            nv_sb_add(sb, "\\r");
            break;
        case '\t':
            nv_sb_add(sb, "\\t");
            break;
        case '\b':
            nv_sb_add(sb, "\\b");
            break;
        case '\f':
            nv_sb_add(sb, "\\f");
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                sprintf(buf, "\\u%04x", c);
                nv_sb_add(sb, buf);
            } else {
                nv_sb_addc(sb, (char)c);
            }
        }
    }
    nv_sb_addc(sb, '"');
}

static void nv_json_float(NvSb *sb, double f) {
    char buf[64];
    int prec;
    for (prec = 15; prec <= 17; prec++) {
        sprintf(buf, "%.*g", prec, f);
        if (atof(buf) == f) {
            break;
        }
    }
    if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'n') && !strchr(buf, 'i')) {
        strcat(buf, ".0");
    }
    nv_sb_add(sb, buf);
}

static void nv_json_indent(NvSb *sb, int indent, int depth) {
    int i;
    if (indent <= 0) {
        return;
    }
    nv_sb_addc(sb, '\n');
    for (i = 0; i < indent * depth; i++) {
        nv_sb_addc(sb, ' ');
    }
}

static void nv_json_write(NvSb *sb, nv v, int indent, int depth) {
    int i;
    const void *id = nv_container_id(v);
    if (id && !nv_visit_enter(id)) {
        nv_sb_add(sb, "null"); /* reference cycle */
        return;
    }
    switch (nv_type_of(v)) {
    case NV_INT:
        nv_sb_add(sb, nv_fmt_int(nv_ival(v)));
        break;
    case NV_FLOAT:
        nv_json_float(sb, v->f);
        break;
    case NV_BOOL:
        nv_sb_add(sb, nv_ival(v) ? "true" : "false");
        break;
    case NV_STR:
        nv_json_string(sb, nv_cstr(v));
        break;
    case NV_ARR:
        if (v->a->len == 0) {
            nv_sb_add(sb, "[]");
            break;
        }
        nv_sb_addc(sb, '[');
        for (i = 0; i < v->a->len; i++) {
            if (i > 0) {
                nv_sb_addc(sb, ',');
            }
            nv_json_indent(sb, indent, depth + 1);
            nv_json_write(sb, v->a->items[i], indent, depth + 1);
        }
        nv_json_indent(sb, indent, depth);
        nv_sb_addc(sb, ']');
        break;
    case NV_OBJ: {
        int count = nv_class_field_count(v->o->cls);
        int *order = nv_field_order(v->o->cls, count);
        if (v->o->name && count == 0) {
            nv_json_string(sb, v->o->name);
            break;
        }
        if (count == 0) {
            nv_sb_add(sb, "{}");
            break;
        }
        nv_sb_addc(sb, '{');
        for (i = 0; i < count; i++) {
            if (i > 0) {
                nv_sb_addc(sb, ',');
            }
            nv_json_indent(sb, indent, depth + 1);
            nv_json_string(sb, nv_field_name_at(v->o->cls, order[i], 0));
            nv_sb_add(sb, indent > 0 ? ": " : ":");
            nv_json_write(sb, nv_fields(v->o)[order[i]], indent, depth + 1);
        }
        nv_json_indent(sb, indent, depth);
        nv_sb_addc(sb, '}');
        break;
    }
    case NV_MAP: {
        NvMap *m = v->m;
        nv_map_order(m);
        if (m->len == 0) {
            nv_sb_add(sb, "{}");
            break;
        }
        nv_sb_addc(sb, '{');
        for (i = 0; i < m->len; i++) {
            if (i > 0) {
                nv_sb_addc(sb, ',');
            }
            nv_json_indent(sb, indent, depth + 1);
            nv_json_string(sb, m->items[i].key);
            nv_sb_add(sb, indent > 0 ? ": " : ":");
            nv_json_write(sb, m->items[i].val, indent, depth + 1);
        }
        nv_json_indent(sb, indent, depth);
        nv_sb_addc(sb, '}');
        break;
    }
    default:
        nv_sb_add(sb, "null");
    }
    if (id) {
        nv_visit_leave();
    }
}

static nv nv_json_dump(nv v, int indent) {
    NvSb sb;
    int len;
    nv_sb_init(&sb);
    nv_json_write(&sb, v, indent, 0);
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_json_stringify(nv v) { return nv_json_dump(v, 0); }

static nv nv_json_pretty(nv v) { return nv_json_dump(v, 2); }

typedef struct NvJsonP {
    const char *s;
    int pos;
} NvJsonP;

static void nv_json_ws(NvJsonP *p) {
    while (p->s[p->pos] && isspace((unsigned char)p->s[p->pos])) {
        p->pos++;
    }
}

static void nv_json_fail(NvJsonP *p, const char *what) {
    nv_error("json parse error at position %d: %s", p->pos, what);
}

static void nv_json_utf8(NvSb *sb, unsigned int cp) {
    if (cp < 0x80) {
        nv_sb_addc(sb, (char)cp);
    } else if (cp < 0x800) {
        nv_sb_addc(sb, (char)(0xC0 | (cp >> 6)));
        nv_sb_addc(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        nv_sb_addc(sb, (char)(0xE0 | (cp >> 12)));
        nv_sb_addc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        nv_sb_addc(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        nv_sb_addc(sb, (char)(0xF0 | (cp >> 18)));
        nv_sb_addc(sb, (char)(0x80 | ((cp >> 12) & 0x3F)));
        nv_sb_addc(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        nv_sb_addc(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

static nv nv_json_parse_string(NvJsonP *p) {
    NvSb sb;
    int len;
    nv_sb_init(&sb);
    p->pos++; /* opening quote */
    while (p->s[p->pos] && p->s[p->pos] != '"') {
        char c = p->s[p->pos];
        if (c == '\\') {
            char e = p->s[++p->pos];
            switch (e) {
            case 'n':
                nv_sb_addc(&sb, '\n');
                break;
            case 't':
                nv_sb_addc(&sb, '\t');
                break;
            case 'r':
                nv_sb_addc(&sb, '\r');
                break;
            case 'b':
                nv_sb_addc(&sb, '\b');
                break;
            case 'f':
                nv_sb_addc(&sb, '\f');
                break;
            case 'u': {
                unsigned int cp = 0;
                int k;
                for (k = 0; k < 4; k++) {
                    char h = p->s[++p->pos];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp |= (unsigned int)(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        cp |= (unsigned int)(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        cp |= (unsigned int)(h - 'A' + 10);
                    } else {
                        nv_json_fail(p, "bad unicode escape");
                    }
                }
                nv_json_utf8(&sb, cp);
                break;
            }
            default:
                nv_sb_addc(&sb, e);
            }
            p->pos++;
        } else {
            nv_sb_addc(&sb, c);
            p->pos++;
        }
    }
    if (p->s[p->pos] != '"') {
        nv_json_fail(p, "unterminated string");
    }
    p->pos++;
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_json_parse_value(NvJsonP *p) {
    char c;
    nv_json_ws(p);
    c = p->s[p->pos];
    if (c == '{') {
        nv m = nv_map();
        p->pos++;
        nv_json_ws(p);
        if (p->s[p->pos] == '}') {
            p->pos++;
            return m;
        }
        for (;;) {
            nv key, val;
            nv_json_ws(p);
            if (p->s[p->pos] != '"') {
                nv_json_fail(p, "expected object key");
            }
            key = nv_json_parse_string(p);
            nv_json_ws(p);
            if (p->s[p->pos] != ':') {
                nv_json_fail(p, "expected ':'");
            }
            p->pos++;
            val = nv_json_parse_value(p);
            nv_map_set_static(m->m, nv_cstr(key), val); /* arena string, never extended */
            nv_json_ws(p);
            if (p->s[p->pos] == ',') {
                p->pos++;
                continue;
            }
            if (p->s[p->pos] == '}') {
                p->pos++;
                return m;
            }
            nv_json_fail(p, "expected ',' or '}'");
        }
    }
    if (c == '[') {
        nv a = nv_arr();
        p->pos++;
        nv_json_ws(p);
        if (p->s[p->pos] == ']') {
            p->pos++;
            return a;
        }
        for (;;) {
            nv_arr_push(a->a, nv_json_parse_value(p));
            nv_json_ws(p);
            if (p->s[p->pos] == ',') {
                p->pos++;
                continue;
            }
            if (p->s[p->pos] == ']') {
                p->pos++;
                return a;
            }
            nv_json_fail(p, "expected ',' or ']'");
        }
    }
    if (c == '"') {
        return nv_json_parse_string(p);
    }
    if (strncmp(p->s + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return nv_bool(1);
    }
    if (strncmp(p->s + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return nv_bool(0);
    }
    if (strncmp(p->s + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return nv_nil;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        int start = p->pos;
        int isFloat = 0;
        char *tmp;
        nv out;
        if (c == '-') {
            p->pos++;
        }
        while (isdigit((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
        if (p->s[p->pos] == '.') {
            isFloat = 1;
            p->pos++;
            while (isdigit((unsigned char)p->s[p->pos])) {
                p->pos++;
            }
        }
        if (p->s[p->pos] == 'e' || p->s[p->pos] == 'E') {
            isFloat = 1;
            p->pos++;
            if (p->s[p->pos] == '+' || p->s[p->pos] == '-') {
                p->pos++;
            }
            while (isdigit((unsigned char)p->s[p->pos])) {
                p->pos++;
            }
        }
        tmp = nv_strndup(p->s + start, (size_t)(p->pos - start));
        out = isFloat ? nv_float(atof(tmp)) : nv_int(atoll(tmp));
        return out;
    }
    if (c == 0) {
        nv_json_fail(p, "unexpected end of input");
    }
    nv_json_fail(p, "unexpected character");
    return nv_nil;
}

static nv nv_json_parse(nv text) {
    NvJsonP p;
    nv v;
    if (nv_type_of(text) != NV_STR) {
        /* already structured data: convert (objects become maps) */
        text = nv_json_stringify(text);
    }
    p.s = nv_display(text);
    p.pos = 0;
    nv_json_ws(&p);
    if (p.s[p.pos] == 0) {
        nv_json_fail(&p, "attempting to parse an empty input");
    }
    v = nv_json_parse_value(&p);
    nv_json_ws(&p);
    if (p.s[p.pos] != 0) {
        nv_json_fail(&p, "trailing characters");
    }
    return v;
}

static nv nv_json_save(nv v, nv dir, nv file) {
    nv target = nv_path_join(2, dir, file);
    nv_os_mkdir(nv_path_dirname(target));
    return nv_write_file(target, nv_json_pretty(v));
}

static nv nv_json_load(nv file) {
    if (!nv_path_exists_c(nv_display(file))) {
        nv_error("json.load: cannot open '%s'", nv_display(file));
    }
    return nv_json_parse(nv_read_file(file));
}

static int nv_json_scan(NvJsonP *p);

static int nv_json_scan_string(NvJsonP *p) {
    p->pos++;
    while (p->s[p->pos] && p->s[p->pos] != '"') {
        if (p->s[p->pos] == '\\') {
            p->pos++;
            if (!p->s[p->pos]) {
                return 0;
            }
        }
        p->pos++;
    }
    if (p->s[p->pos] != '"') {
        return 0;
    }
    p->pos++;
    return 1;
}

/* Validates without aborting. */
static int nv_json_scan(NvJsonP *p) {
    char c;
    nv_json_ws(p);
    c = p->s[p->pos];
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        p->pos++;
        nv_json_ws(p);
        if (p->s[p->pos] == close) {
            p->pos++;
            return 1;
        }
        for (;;) {
            nv_json_ws(p);
            if (close == '}') {
                if (p->s[p->pos] != '"' || !nv_json_scan_string(p)) {
                    return 0;
                }
                nv_json_ws(p);
                if (p->s[p->pos] != ':') {
                    return 0;
                }
                p->pos++;
            }
            if (!nv_json_scan(p)) {
                return 0;
            }
            nv_json_ws(p);
            if (p->s[p->pos] == ',') {
                p->pos++;
                continue;
            }
            if (p->s[p->pos] == close) {
                p->pos++;
                return 1;
            }
            return 0;
        }
    }
    if (c == '"') {
        return nv_json_scan_string(p);
    }
    if (strncmp(p->s + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return 1;
    }
    if (strncmp(p->s + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return 1;
    }
    if (strncmp(p->s + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return 1;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        int start = p->pos;
        if (c == '-') {
            p->pos++;
        }
        while (isdigit((unsigned char)p->s[p->pos]) || p->s[p->pos] == '.' || p->s[p->pos] == 'e' ||
               p->s[p->pos] == 'E' || p->s[p->pos] == '+' || p->s[p->pos] == '-') {
            p->pos++;
        }
        return p->pos > start + (c == '-' ? 1 : 0);
    }
    return 0;
}

static nv nv_json_is_valid(nv text) {
    NvJsonP p;
    p.s = nv_display(text);
    p.pos = 0;
    if (!nv_json_scan(&p)) {
        return nv_bool(0);
    }
    nv_json_ws(&p);
    return nv_bool(p.s[p.pos] == 0);
}

#endif /* NOVUS_RT_H */
