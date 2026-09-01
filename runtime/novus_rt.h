/*
 * novus_rt.h - the C runtime embedded into every program that novusc emits.
 *
 * Values are dynamically typed (NvVal) like in the original tree-walking
 * interpreter, so integers, floats, bools, strings, arrays, maps, class
 * instances and enum constants can all flow through the same variables.
 * Memory is arena allocated and never freed (bootstrap-style runtime).
 *
 * Portable C99: builds with gcc, clang, zig cc and mingw on Linux, macOS
 * and Windows.
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
} NvArr;

typedef struct NvEntry {
    const char *key;
    nv val;
} NvEntry;

typedef struct NvMap { /* entries kept sorted by key (like std::map) */
    NvEntry *items;
    int len;
    int cap;
} NvMap;

typedef struct NvClass NvClass;

typedef struct NvObj {
    NvClass *cls;
    NvMap *fields;
    const char *name; /* enum constant name, NULL for class instances */
} NvObj;

struct NvVal {
    int type;
    long long i;   /* NV_INT and NV_BOOL */
    double f;      /* NV_FLOAT */
    const char *s; /* NV_STR: slen bytes; NUL terminated unless a later
                      concatenation appended into the shared buffer */
    int slen;
    int scap;      /* writable capacity of s (0: read-only / not owned) */
    NvArr *a;
    NvMap *m;
    NvObj *o;
};

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
    int ctorArity;
    NvMap *constants; /* enum constants */
    NvArr *constantOrder;
};

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

static char *nv_arena_ptr = 0;
static size_t nv_arena_left = 0;

static void *nv_alloc(size_t n) {
    void *p;
    n = (n + 15) & ~(size_t)15;
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

static NvVal nv_nil_val = {NV_NULL, 0, 0.0, "", 0, 0, 0, 0, 0};
static NvVal nv_true_val = {NV_BOOL, 1, 0.0, "true", 4, 0, 0, 0, 0};
static NvVal nv_false_val = {NV_BOOL, 0, 0.0, "false", 5, 0, 0, 0, 0};
static NvVal nv_small_ints[1280];
static int nv_small_ints_ready = 0;
static nv nv_nil = &nv_nil_val;
static nv nv_char_table[256];

static nv nv_new(int type) {
    nv v = (nv)nv_alloc(sizeof(NvVal));
    memset(v, 0, sizeof(NvVal));
    v->type = type;
    v->s = "";
    return v;
}

static nv nv_int(long long i) {
    nv v;
    if (i >= -256 && i < 1024 && nv_small_ints_ready) {
        return &nv_small_ints[i + 256];
    }
    v = nv_new(NV_INT);
    v->i = i;
    return v;
}

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
    if (v->type != NV_STR) {
        return v->s;
    }
    if (v->s[v->slen] == 0) {
        return v->s;
    }
    return nv_strndup(v->s, (size_t)v->slen);
}

static NvArr *nv_arr_new(void) {
    NvArr *a = (NvArr *)nv_alloc(sizeof(NvArr));
    a->len = 0;
    a->cap = 8;
    a->items = (nv *)nv_alloc(sizeof(nv) * (size_t)a->cap);
    return a;
}

static void nv_arr_push(NvArr *a, nv v) {
    if (a->len == a->cap) {
        nv *items = (nv *)nv_alloc(sizeof(nv) * (size_t)a->cap * 2);
        memcpy(items, a->items, sizeof(nv) * (size_t)a->len);
        a->items = items;
        a->cap *= 2;
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

static NvMap *nv_map_new(void) {
    NvMap *m = (NvMap *)nv_alloc(sizeof(NvMap));
    m->len = 0;
    m->cap = 8;
    m->items = (NvEntry *)nv_alloc(sizeof(NvEntry) * (size_t)m->cap);
    return m;
}

/* binary search; returns index of key or -(insertion point) - 1 */
static int nv_map_find(NvMap *m, const char *key) {
    int lo = 0, hi = m->len - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(m->items[mid].key, key);
        if (c == 0) {
            return mid;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -lo - 1;
}

static void nv_map_set(NvMap *m, const char *key, nv val) {
    int idx = nv_map_find(m, key);
    int pos;
    if (idx >= 0) {
        m->items[idx].val = val;
        return;
    }
    pos = -idx - 1;
    if (m->len == m->cap) {
        NvEntry *items = (NvEntry *)nv_alloc(sizeof(NvEntry) * (size_t)m->cap * 2);
        memcpy(items, m->items, sizeof(NvEntry) * (size_t)m->len);
        m->items = items;
        m->cap *= 2;
    }
    memmove(m->items + pos + 1, m->items + pos, sizeof(NvEntry) * (size_t)(m->len - pos));
    m->items[pos].key = nv_strndup(key, strlen(key));
    m->items[pos].val = val;
    m->len++;
}

static nv nv_map_get(NvMap *m, const char *key) {
    int idx = nv_map_find(m, key);
    return idx >= 0 ? m->items[idx].val : 0;
}

static int nv_map_has(NvMap *m, const char *key) { return nv_map_find(m, key) >= 0; }

static void nv_map_remove(NvMap *m, const char *key) {
    int idx = nv_map_find(m, key);
    if (idx < 0) {
        return;
    }
    memmove(m->items + idx, m->items + idx + 1, sizeof(NvEntry) * (size_t)(m->len - idx - 1));
    m->len--;
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
    switch (v->type) {
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

static const char *nv_fmt_int(long long i) {
    char buf[32];
    sprintf(buf, "%lld", i);
    return nv_strndup(buf, strlen(buf));
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
    if (v->type == NV_ARR) {
        return v->a;
    }
    if (v->type == NV_MAP) {
        return v->m;
    }
    if (v->type == NV_OBJ) {
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
    switch (v->type) {
    case NV_INT:
        nv_sb_add(sb, nv_fmt_int(v->i));
        break;
    case NV_FLOAT:
        nv_sb_add(sb, nv_fmt_float(v->f));
        break;
    case NV_BOOL:
        nv_sb_add(sb, v->i ? "true" : "false");
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
        for (i = 0; i < v->o->fields->len; i++) {
            if (i > 0) {
                nv_sb_add(sb, ", ");
            }
            nv_sb_add(sb, v->o->fields->items[i].key);
            nv_sb_add(sb, ": ");
            nv_display_into(sb, v->o->fields->items[i].val);
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
    if (v->type == NV_STR) {
        return nv_cstr(v);
    }
    if (v->type == NV_BOOL) {
        return v->i ? "true" : "false";
    }
    if (v->type == NV_INT) {
        return nv_fmt_int(v->i);
    }
    if (v->type == NV_FLOAT) {
        return nv_fmt_float(v->f);
    }
    nv_sb_init(&sb);
    nv_display_into(&sb, v);
    return nv_sb_finish(&sb);
}

/* The "data" of a value: what the interpreter compared strings against. */
static const char *nv_data(nv v) {
    switch (v->type) {
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

static nv nv_to_str(nv v) { return v->type == NV_STR ? v : nv_str(nv_display(v)); }

/* ------------------------------------------------------------------ */
/* Numbers, coercion, truthiness                                       */
/* ------------------------------------------------------------------ */

static int nv_is_num(nv v) { return v->type == NV_INT || v->type == NV_FLOAT; }

static double nv_as_double(nv v) {
    if (v->type == NV_FLOAT) {
        return v->f;
    }
    if (v->type == NV_INT || v->type == NV_BOOL) {
        return (double)v->i;
    }
    if (v->type == NV_STR) {
        return atof(nv_cstr(v));
    }
    return 0.0;
}

static long long nv_as_int(nv v) {
    if (v->type == NV_INT || v->type == NV_BOOL) {
        return v->i;
    }
    if (v->type == NV_FLOAT) {
        return (long long)v->f;
    }
    if (v->type == NV_STR) {
        return atoll(nv_cstr(v));
    }
    return 0;
}

static int nv_truthy(nv v) { return v->type == NV_BOOL && v->i != 0; }

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
        if (v->type == NV_FLOAT) {
            return nv_int((long long)v->f);
        }
        return v;
    }
    if (strcmp(t, "float") == 0) {
        if (v->type == NV_INT) {
            return nv_float((double)v->i);
        }
        return v;
    }
    if (strcmp(t, "string") == 0) {
        if (v->type != NV_STR && v->type != NV_NULL) {
            return nv_str(nv_data(v));
        }
        return v;
    }
    return v;
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
        return nv_str("");
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
    if (l->type == NV_STR) {
        ls = l->s;
        ll = (size_t)l->slen;
    } else {
        ls = nv_display(l);
        ll = strlen(ls);
    }
    if (r->type == NV_STR) {
        rs = r->s;
        rl = (size_t)r->slen;
    } else {
        rs = nv_display(r);
        rl = strlen(rs);
    }
    if (rl == 0 && l->type == NV_STR) {
        return l;
    }
    if (l->type == NV_STR && l->scap > 0 && l->s[l->slen] == 0 && rl > 0 && rs[0] != 0 &&
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

static void nv_arith_check(nv l, nv r, const char *op) {
    if (!nv_is_num(l) || !nv_is_num(r)) {
        nv_error("cannot apply '%s' to %s and %s", op, nv_type_name(l), nv_type_name(r));
    }
}

static nv nv_add(nv l, nv r) {
    if (l->type == NV_STR || r->type == NV_STR) {
        return nv_concat(l, r);
    }
    nv_arith_check(l, r, "+");
    if (l->type == NV_FLOAT || r->type == NV_FLOAT) {
        return nv_float(nv_as_double(l) + nv_as_double(r));
    }
    return nv_int(l->i + r->i);
}

static nv nv_sub(nv l, nv r) {
    nv_arith_check(l, r, "-");
    if (l->type == NV_FLOAT || r->type == NV_FLOAT) {
        return nv_float(nv_as_double(l) - nv_as_double(r));
    }
    return nv_int(l->i - r->i);
}

static nv nv_mul(nv l, nv r) {
    nv_arith_check(l, r, "*");
    if (l->type == NV_FLOAT || r->type == NV_FLOAT) {
        return nv_float(nv_as_double(l) * nv_as_double(r));
    }
    return nv_int(l->i * r->i);
}

static nv nv_div(nv l, nv r) {
    nv_arith_check(l, r, "/");
    if (l->type == NV_FLOAT || r->type == NV_FLOAT) {
        double d = nv_as_double(r);
        return nv_float(d != 0.0 ? nv_as_double(l) / d : 0.0);
    }
    return nv_int(r->i != 0 ? l->i / r->i : 0);
}

static nv nv_mod(nv l, nv r) {
    nv_arith_check(l, r, "%");
    if (l->type == NV_FLOAT || r->type == NV_FLOAT) {
        return nv_float(fmod(nv_as_double(l), nv_as_double(r)));
    }
    return nv_int(r->i != 0 ? l->i % r->i : 0);
}

static nv nv_neg(nv v) {
    if (v->type == NV_FLOAT) {
        return nv_float(-v->f);
    }
    if (v->type == NV_INT) {
        return nv_int(-v->i);
    }
    nv_error("cannot negate %s", nv_type_name(v));
    return nv_nil;
}

static nv nv_not(nv v) { return nv_bool(!nv_truthy(v)); }

static int nv_equals(nv l, nv r) {
    if (l->type == NV_STR || r->type == NV_STR) {
        return strcmp(nv_data(l), nv_data(r)) == 0;
    }
    if (l->type == NV_BOOL || r->type == NV_BOOL) {
        return strcmp(nv_data(l), nv_data(r)) == 0;
    }
    if (nv_is_num(l) && nv_is_num(r)) {
        return nv_as_double(l) == nv_as_double(r);
    }
    if (l->type == NV_OBJ && r->type == NV_OBJ) {
        if (l->o->name && r->o->name) {
            return strcmp(l->o->name, r->o->name) == 0 && l->o->cls == r->o->cls;
        }
        return l->o == r->o;
    }
    if (l->type != r->type) {
        return 0;
    }
    if (l->type == NV_ARR) {
        return l->a == r->a;
    }
    if (l->type == NV_MAP) {
        return l->m == r->m;
    }
    return 1; /* nil == nil */
}

static int nv_compare(nv l, nv r, const char *op) {
    if (l->type == NV_STR || r->type == NV_STR) {
        return strcmp(nv_data(l), nv_data(r));
    }
    if (nv_is_num(l) && nv_is_num(r)) {
        double a = nv_as_double(l), b = nv_as_double(r);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    nv_error("cannot compare %s and %s with '%s'", nv_type_name(l), nv_type_name(r), op);
    return 0;
}

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
    if (t->type == NV_MAP) {
        const char *key = nv_display(k);
        nv v = nv_map_get(t->m, key);
        if (!v) {
            nv_error("key '%s' not found in map", key);
        }
        return v;
    }
    if (t->type == NV_ARR) {
        long long i = nv_as_int(k);
        if (i < 0 || i >= t->a->len) {
            nv_error("array index %lld out of bounds (size %d)", i, t->a->len);
        }
        return t->a->items[i];
    }
    if (t->type == NV_STR) {
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
    if (t->type == NV_MAP) {
        nv_map_set(t->m, nv_display(k), v);
        return;
    }
    if (t->type == NV_ARR) {
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
    if (t->type == NV_OBJ) {
        v = nv_map_get(t->o->fields, name);
        if (!v) {
            nv_error("no property '%s' on value of type %s", name, t->o->cls->name);
        }
        return v;
    }
    if (t->type == NV_MAP) {
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
    if (t->type == NV_OBJ) {
        nv_map_set(t->o->fields, name, v);
        return;
    }
    if (t->type == NV_MAP) {
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

static void nv_init_fields(NvClass *c, NvMap *fields) {
    int i;
    NvClass *base = nv_class_base(c);
    if (base) {
        nv_init_fields(base, fields);
    }
    for (i = 0; i < c->nfields; i++) {
        nv_map_set(fields, c->fieldNames[i], nv_default(c->fieldTypes[i]));
    }
}

static nv nv_new_object(const char *className) {
    NvClass *c = nv_find_class(className);
    nv v;
    if (!c) {
        nv_error("unknown class '%s'", className);
    }
    if (c->isAbstract) {
        nv_error("cannot instantiate abstract class '%s'", className);
    }
    v = nv_new(NV_OBJ);
    v->o = (NvObj *)nv_alloc(sizeof(NvObj));
    v->o->cls = c;
    v->o->fields = nv_map_new();
    v->o->name = 0;
    nv_init_fields(c, v->o->fields);
    return v;
}

static nv nv_construct_args(const char *className, nv *args, int n) {
    nv obj = nv_new_object(className);
    NvClass *c = obj->o->cls;
    int guard = 0;
    while (c && guard++ < 64) {
        if (c->ctor) {
            c->ctor(obj, args, n);
            return obj;
        }
        c = nv_class_base(c);
    }
    /* no constructor: positional field initialization (base fields first) */
    {
        NvMap *f = obj->o->fields;
        NvArr *order = nv_arr_new();
        NvClass *chain[64];
        int depth = 0, i, k = 0;
        for (c = obj->o->cls; c && depth < 64; c = nv_class_base(c)) {
            chain[depth++] = c;
        }
        for (i = depth - 1; i >= 0; i--) {
            int j;
            for (j = 0; j < chain[i]->nfields && k < n; j++, k++) {
                nv_map_set(f, chain[i]->fieldNames[j], nv_coerce(args[k], chain[i]->fieldTypes[j]));
            }
        }
        (void)order;
    }
    return obj;
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

/* Object literal: Name{field=value, ...} - no constructor is run. */
static nv nv_new_object_fields(const char *className, int n, ...) {
    nv obj = nv_new_object(className);
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        const char *name = va_arg(ap, const char *);
        nv val = va_arg(ap, nv);
        nv_map_set(obj->o->fields, name, val);
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

static nv nv_invoke_args(nv t, const char *name, nv *args, int n) {
    if (t->type == NV_OBJ) {
        const char *ftype = nv_class_field_type(t->o->cls, name);
        NvMethod *m;
        if (ftype) {
            if (n == 0) {
                nv v = nv_map_get(t->o->fields, name);
                return v ? v : nv_nil;
            }
            nv_map_set(t->o->fields, name, nv_coerce(args[0], ftype));
            return nv_nil;
        }
        m = nv_class_find_method(t->o->cls, name, n);
        if (m) {
            return m->fn(t, args, n);
        }
        nv_error("unknown member '%s' on %s", name, t->o->cls->name);
    }
    if (strcmp(name, "length") == 0) {
        if (t->type == NV_ARR) {
            return nv_int(t->a->len);
        }
        if (t->type == NV_MAP) {
            return nv_int(t->m->len);
        }
        if (t->type == NV_STR) {
            return nv_int(t->slen);
        }
        nv_error("'%s' has no length", nv_type_name(t));
    }
    if (t->type == NV_ARR) {
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
    if (t->type == NV_MAP) {
        if (strcmp(name, "has") == 0) {
            return nv_bool(n > 0 && nv_map_has(t->m, nv_display(args[0])));
        }
        if (strcmp(name, "keys") == 0) {
            nv out = nv_arr();
            int i;
            for (i = 0; i < t->m->len; i++) {
                nv_arr_push(out->a, nv_str(t->m->items[i].key));
            }
            return out;
        }
        if (strcmp(name, "values") == 0) {
            nv out = nv_arr();
            int i;
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
    if (t->type == NV_STR) {
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

static NvArr *nv_iter(nv v) {
    NvArr *out = nv_arr_new();
    int i;
    if (v->type == NV_ARR) {
        for (i = 0; i < v->a->len; i++) {
            nv_arr_push(out, v->a->items[i]);
        }
        return out;
    }
    if (v->type == NV_MAP) {
        for (i = 0; i < v->m->len; i++) {
            nv_arr_push(out, nv_str(v->m->items[i].key));
        }
        return out;
    }
    if (v->type == NV_STR) {
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
    for (i = 0; i < 1280; i++) {
        memset(&nv_small_ints[i], 0, sizeof(NvVal));
        nv_small_ints[i].type = NV_INT;
        nv_small_ints[i].i = i - 256;
        nv_small_ints[i].s = "";
    }
    nv_small_ints_ready = 1;
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
    if (v->type == NV_INT) {
        return v;
    }
    if (v->type == NV_FLOAT) {
        return nv_int((long long)v->f);
    }
    return nv_int(atoll(nv_display(v)));
}

static nv nv_parse_float(nv v) {
    if (v->type == NV_FLOAT) {
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

static nv nv_type_of(nv v) { return nv_str(nv_type_name(v)); }

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
    if (headers && headers->type == NV_MAP) {
        for (i = 0; i < headers->m->len; i++) {
            nv line = nv_concat(nv_concat(nv_str(headers->m->items[i].key), nv_str(": ")), headers->m->items[i].val);
            if (strcmp(nv_cstr(nv_str_case(nv_str(headers->m->items[i].key), 0)), "content-type") == 0) {
                hasContentType = 1;
            }
            nv_sb_add(&cmd, " -H ");
            nv_sb_add(&cmd, nv_shell_quote(nv_cstr(line)));
        }
    }
    if (body && body->type != NV_NULL && !(body->type == NV_STR && body->slen == 0)) {
        nv text = body;
        if (body->type == NV_MAP || body->type == NV_ARR || body->type == NV_OBJ) {
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
    switch (v->type) {
    case NV_INT:
        nv_sb_add(sb, nv_fmt_int(v->i));
        break;
    case NV_FLOAT:
        nv_json_float(sb, v->f);
        break;
    case NV_BOOL:
        nv_sb_add(sb, v->i ? "true" : "false");
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
    case NV_MAP:
    case NV_OBJ: {
        NvMap *m = v->type == NV_MAP ? v->m : v->o->fields;
        if (v->type == NV_OBJ && v->o->name && m->len == 0) {
            nv_json_string(sb, v->o->name);
            break;
        }
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
            nv_map_set(m->m, nv_cstr(key), val);
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
    if (text->type != NV_STR) {
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
