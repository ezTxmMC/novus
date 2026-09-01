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
#ifdef __APPLE__
/* makecontext/swapcontext, which the virtual threads switch stacks with,
 * are hidden there without it (and _DARWIN_C_SOURCE above keeps the BSD
 * extensions that _XOPEN_SOURCE would otherwise take away) */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif
#endif

#include <ctype.h>
#include <math.h>
#include <signal.h>
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
#include <winsock2.h>
#include <ws2tcpip.h>
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
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
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

/* Thread local storage. Everything a thread mutates while it runs - its
 * corner of the arena, the caches, the last error of a subsystem - lives
 * here, so threads never contend for it and never see each other's. */
#if defined(_MSC_VER)
#define NV_TLS __declspec(thread)
#elif defined(__GNUC__)
#define NV_TLS __thread
#else
#define NV_TLS _Thread_local
#endif

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
    int heap;     /* items came from malloc: grow with realloc, see NV_MAP_HEAP_AT */
    int indexHeap; /* likewise for index, which is rebuilt rather than grown */
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

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

/* Every thread bump allocates in a chunk of its own, so allocation stays a
 * pointer increment with no lock in sight. The chunks themselves come from
 * malloc and are never handed back, so a value one thread allocated stays
 * valid in every other. */
static NV_TLS char *nv_arena_ptr = 0;
static NV_TLS size_t nv_arena_left = 0;

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

/* Growable string buffers.
 *
 * Only nv_concat() and nv_add_chain() leave spare room at the end of a
 * buffer, and only they append into it. Those buffers - and no others -
 * keep their capacity in a word right in front of the bytes, which takes it
 * out of NvVal: a value is 16 bytes instead of 24, and the literals,
 * substrings and plain copies that are almost every string in a program
 * stop paying for a capacity they never had a use for. */
static char *nv_buf_alloc(size_t cap) {
    char *p = (char *)nv_alloc(sizeof(unsigned) + cap);
    *(unsigned *)p = (unsigned)cap;
    return p + sizeof(unsigned);
}

static size_t nv_buf_cap(const char *s) { return *(const unsigned *)(s - sizeof(unsigned)); }

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

static NvVal nv_nil_val = {NV_NULL, 0, 0, 0, {0}};
static NvVal nv_true_val = {NV_BOOL, 0, 0, 0, {1}};
static NvVal nv_false_val = {NV_BOOL, 0, 0, 0, {0}};
static NvVal nv_empty_str_val = {NV_STR, 0, NV_F_STABLE, 0, {0}};
static nv nv_nil = &nv_nil_val;
static nv nv_char_table[256];

static nv nv_new(int type) {
    nv v = (nv)nv_alloc(sizeof(NvVal));
    memset(v, 0, sizeof(NvVal));
    v->type = (unsigned char)type;
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

/* The value and its bytes come out of one block: one allocation instead of
 * two, and no padding wasted between them. */
static nv nv_strn(const char *s, int n) {
    nv v;
    char *bytes;
    if (n == 1 && nv_char_table[(unsigned char)s[0]]) {
        return nv_char_table[(unsigned char)s[0]];
    }
    if (n <= 0) {
        return &nv_empty_str_val;
    }
    v = (nv)nv_alloc(sizeof(NvVal) + (size_t)n + 1);
    memset(v, 0, sizeof(NvVal));
    bytes = (char *)(v + 1);
    memcpy(bytes, s, (size_t)n);
    bytes[n] = 0;
    v->type = NV_STR;
    v->flags = NV_F_STABLE;   /* private bytes, NUL terminated, never appended to */
    v->s = bytes;
    v->slen = n;
    return v;
}

static nv nv_str(const char *s) { return nv_strn(s, (int)strlen(s)); }

/* Every string literal in a program is one value, laid down by the compiler
 * as a static and filled in once at startup - no boxing, no hash lookup and
 * no allocation when the expression it sits in runs. */
static void nv_init_literals(NvVal *vals, const char *const *texts, int n) {
    int i;
    for (i = 0; i < n; i++) {
        vals[i].type = NV_STR;
        vals[i].owns = 0;
        vals[i].flags = NV_F_STABLE;
        vals[i].slen = (int)strlen(texts[i]);
        vals[i].s = texts[i];
    }
}

static nv nv_str_own(const char *s, int n) {
    nv v = nv_new(NV_STR);
    v->s = s;
    v->slen = n;
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

static NvArr *nv_arr_new_cap(int cap) {
    NvArr *a = (NvArr *)nv_alloc(sizeof(NvArr));
    a->len = 0;
    a->heap = 0;
    a->cap = cap < 4 ? 4 : cap;
    a->items = (nv *)nv_alloc(sizeof(nv) * (size_t)a->cap);
    return a;
}

static NvArr *nv_arr_new(void) { return nv_arr_new_cap(4); }

/* Growing inside the arena leaves every previous copy behind - the arena
 * never hands anything back - so a table that doubles its way to N entries
 * costs 2N. malloc does hand the old one back, and realloc often widens the
 * table where it stands, so everything past a small table grows there. The
 * threshold only keeps the many tiny arrays out of malloc's bookkeeping. */
#define NV_ARR_HEAP_AT 64

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

/* An array literal knows how many items it holds: sizing the block for them
 * up front skips the growth copies, which the arena would keep forever. */
static nv nv_arr_of(int count, ...) {
    nv v = nv_new(NV_ARR);
    va_list ap;
    int i;
    v->a = nv_arr_new_cap(count);
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
    m->heap = 0;
    m->indexHeap = 0;
    return m;
}

static NvMap *nv_map_new(void) { return nv_map_new_cap(4); }

/* The same trap arrays avoid with NV_ARR_HEAP_AT: a map that doubles its way
 * to N entries leaves 2N behind in the arena. Maps are smaller and far more
 * numerous than arrays (every object literal, every set of options), so the
 * threshold is lower - past eight entries the entry table comes from malloc,
 * and the index does from twice that. */
#define NV_MAP_HEAP_AT 8

/* Slots for `len` entries: a power of two with half again as much room as
 * the entries need. Two slots per entry (what this used to ask for) rounds
 * up to the next power of two on top of that and spends twice the memory for
 * a probe count nothing could measure. */
#define NV_MAP_SLOTS_FOR(len) ((len) + 1 + ((len) + 1) / 2)

/* (Re)builds the hash index; called when it grows or entries move. */
static void nv_map_reindex(NvMap *m, int slots) {
    int i;
    while (slots < NV_MAP_SLOTS_FOR(m->len)) {
        slots *= 2;
    }
    m->mask = slots - 1;
    if (slots >= NV_MAP_HEAP_AT * 2) {
        /* the index is rebuilt from nothing, so the old one is dead the
         * moment the new one exists: hand it back rather than leak it */
        if (m->indexHeap) {
            free(m->index);
        }
        m->index = (int *)malloc(sizeof(int) * (size_t)slots);
        if (!m->index) {
            nv_error("out of memory");
        }
        m->indexHeap = 1;
    } else {
        m->index = (int *)nv_alloc(sizeof(int) * (size_t)slots);
    }
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
        int cap = m->cap * 2;
        if (cap >= NV_MAP_HEAP_AT) {
            if (m->heap) {
                m->items = (NvEntry *)realloc(m->items, sizeof(NvEntry) * (size_t)cap);
            } else {
                NvEntry *items = (NvEntry *)malloc(sizeof(NvEntry) * (size_t)cap);
                if (items) {
                    memcpy(items, m->items, sizeof(NvEntry) * (size_t)m->len);
                }
                m->items = items;
                m->heap = 1;
            }
            if (!m->items) {
                nv_error("out of memory");
            }
        } else {
            NvEntry *items = (NvEntry *)nv_alloc(sizeof(NvEntry) * (size_t)cap);
            memcpy(items, m->items, sizeof(NvEntry) * (size_t)m->len);
            m->items = items;
        }
        m->cap = cap;
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
    if (NV_MAP_SLOTS_FOR(m->len) > m->mask + 1) {
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

/* A map key is a NUL terminated string that outlives the map, so handing it
 * out means pointing at it - no copy of the bytes, and it goes straight back
 * into another map as a key without one either. */
static nv nv_map_key_view(const char *key) {
    nv v = nv_new(NV_STR);
    v->flags = NV_F_STABLE;
    v->s = key;
    v->slen = (int)strlen(key);
    return v;
}

/* The bytes a stable string points at outlive any map, so the entry can use
 * them as its key directly. */
static void nv_map_set_key(NvMap *m, nv key, nv val) {
    if (nv_type_of(key) == NV_STR && (key->flags & NV_F_STABLE)) {
        nv_map_set_static(m, key->s, val);
        return;
    }
    nv_map_set(m, nv_display(key), val);
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

/* Like nv_arr_of: the literal's size is known, so the entry table is right
 * the first time. */
static nv nv_map_of(int pairs, ...) {
    nv v = nv_new(NV_MAP);
    va_list ap;
    int i;
    v->m = nv_map_new_cap(pairs);
    va_start(ap, pairs);
    for (i = 0; i < pairs; i++) {
        nv k = va_arg(ap, nv);
        nv val = va_arg(ap, nv);
        nv_map_set_key(v->m, k, val);
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
static NV_TLS const void *nv_visit_stack[256];
static NV_TLS int nv_visit_depth = 0;

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
    case NV_OBJ: {
        const char *constName = nv_obj_name(v->o);
        if (constName) {
            nv_sb_add(sb, constName);
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
    }
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
    case NV_OBJ: {
        const char *constName = nv_obj_name(v->o);
        return constName ? constName : "";
    }
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
/* How much room a concatenation asks for.
 *
 * Most concatenations are one-shot - `"a" + b` in an argument, a key, a
 * message - and never grow again, so they get exactly what they need. A left
 * side that already owns a buffer is the second or later step of `s = s + x`,
 * and that one doubles: the copies stay amortized O(1) and the buffers the
 * arena can never hand back stay a bounded multiple of the string. Telling
 * the two apart is what `owns` is for. */
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
    /* enum objects carry their constant name in one slot behind the fields */
    b = (NvObjBlock *)nv_alloc(sizeof(NvObjBlock) +
                               sizeof(nv) * (size_t)(nfields < 1 ? 1 : nfields) +
                               (c->isEnum ? sizeof(const char *) : 0));
    b->val.type = NV_OBJ;
    b->val.owns = 0;
    b->val.flags = 0;
    b->val.slen = 0;
    b->val.o = &b->obj;
    b->obj.cls = c;
    nv_init_fields(c, nv_fields(&b->obj));
    if (c->isEnum) {
        *(const char **)(nv_fields(&b->obj) + nfields) = 0;
    }
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
    *(const char **)(nv_fields(obj->o) + nv_class_field_count(obj->o->cls)) = constName;
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

/* A substring shares its parent's bytes instead of copying them: it owns
 * nothing of its own.
 *
 * Nothing can observe the difference. The in-place append of nv_concat only
 * runs when the left side owns its buffer, so a view always takes
 * the copying path; nv_cstr() makes a private copy when the terminator it
 * needs is not there; and everything else works from slen. What changes is
 * the cost: a program that slices a large string - which is what the compiler
 * does to every node of its own AST - stops allocating a copy per slice.
 * That is the difference between compiling a large program in a few hundred
 * megabytes and in tens of gigabytes, because the arena never frees. */
static nv nv_substr(nv s, long long start, long long end) {
    long long n = s->slen;
    nv v;
    if (start < 0) {
        start = 0;
    }
    if (end > n) {
        end = n;
    }
    if (start > end) {
        start = end;
    }
    /* one character and the empty string are cached or trivial: copy them */
    if (end - start <= 1) {
        return nv_strn(s->s + start, (int)(end - start));
    }
    v = nv_new(NV_STR);
    v->s = s->s + start;
    v->slen = (int)(end - start);
    return v;
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
static NV_TLS NvMember nv_mcache[NV_MCACHE];

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
            nv out = nv_new(NV_ARR);
            int i;
            nv_map_order(t->m);
            out->a = nv_arr_new_cap(t->m->len);
            for (i = 0; i < t->m->len; i++) {
                nv_arr_push(out->a, nv_map_key_view(t->m->items[i].key));
            }
            return out;
        }
        if (strcmp(name, "values") == 0) {
            nv out = nv_new(NV_ARR);
            int i;
            nv_map_order(t->m);
            out->a = nv_arr_new_cap(t->m->len);
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
    if (nv_type_of(v) == NV_MAP) {
        /* A key already is a NUL terminated string that outlives the loop, so
         * the loop variable points at it instead of copying it - and stays
         * usable as a key of another map without a copy either. */
        nv_map_order(v->m);
        out = nv_arr_with_capacity(v->m->len);
        for (i = 0; i < v->m->len; i++) {
            nv_arr_push(out, nv_map_key_view(v->m->items[i].key));
        }
        return out;
    }
    if (nv_type_of(v) == NV_STR) {
        out = nv_arr_with_capacity(v->slen);
        for (i = 0; i < v->slen; i++) {
            nv_arr_push(out, nv_strn(v->s + i, 1));
        }
        return out;
    }
    nv_error("cannot iterate over a value of type %s", nv_type_name(v));
    return nv_arr_new();
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
        v->flags = NV_F_STABLE;
        v->s = nv_strndup(&c, 1);
        v->slen = 1;
        nv_char_table[i] = v;
    }
    nv_empty_str_val.s = "";
    nv_conc_init();
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
    int len;
    if (!f) {
        nv_error("cannot write file '%s'", nv_display(path));
    }
    /* by byte length, not to the first NUL: readFile() already returns
     * binary content faithfully, and writing it back has to match */
    s = nv_bin(content, &len);
    fwrite(s, 1, (size_t)len, f);
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
    int len;
    if (!f) {
        nv_error("cannot write file '%s'", nv_display(path));
    }
    s = nv_bin(content, &len);
    fwrite(s, 1, (size_t)len, f);
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

/* Interrupts.
 *
 * A program that owns something worth writing out - a world, a database, an
 * open file - has to be told that it is being asked to stop, rather than
 * simply being killed. The handler only raises a flag; interrupted() reads it
 * and clears it, so the program decides when and where to act on it. */
static volatile sig_atomic_t nv_interrupt_flag = 0;

static void nv_interrupt_handler(int signal_number) {
    (void)signal_number;
    nv_interrupt_flag = 1;
}

static nv nv_os_catch_interrupt(void) {
    signal(SIGINT, nv_interrupt_handler);
#ifdef SIGTERM
    signal(SIGTERM, nv_interrupt_handler);
#endif
    return nv_nil;
}

static nv nv_os_interrupted(void) {
    int raised = nv_interrupt_flag != 0;
    nv_interrupt_flag = 0;
    return nv_bool(raised);
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

/* Per thread, so two threads drawing numbers never step on one another -
 * and so seed() makes the thread that calls it reproducible. */
static NV_TLS unsigned long long nv_rng_state = 0;

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
        const char *constName = nv_obj_name(v->o);
        if (constName && count == 0) {
            nv_json_string(sb, constName);
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

/* ------------------------------------------------------------------ */
/* Binary buffers                                                      */
/* ------------------------------------------------------------------ */

/* Novus strings carry an explicit length (slen) and charAt/substring work on
 * bytes, so a string is also the natural byte buffer: it survives NUL bytes
 * and every value in 0..255. Only the NUL terminated views (nv_cstr,
 * nv_display) would truncate, so everything below goes through nv_bin(). */
static const char *nv_bin(nv v, int *len) {
    if (nv_type_of(v) == NV_STR) {
        *len = v->slen;
        return v->s;
    }
    {
        const char *s = nv_display(v);
        *len = (int)strlen(s);
        return s;
    }
}

/* Reads past the end of a buffer set this flag instead of aborting: a server
 * has to survive a truncated or hostile packet. bytes.failed() reads it. */
static NV_TLS int nv_bytes_err = 0;

static nv nv_bytes_failed(void) { return nv_bool(nv_bytes_err != 0); }
static nv nv_bytes_clear_error(void) {
    nv_bytes_err = 0;
    return nv_nil;
}

/* Unsigned big endian read of `n` bytes at `off`; 0 and the error flag when
 * the buffer is too short. */
static unsigned long long nv_bytes_raw(nv b, long long off, int n) {
    int len;
    const char *s = nv_bin(b, &len);
    unsigned long long v = 0;
    int i;
    if (off < 0 || off + n > len) {
        nv_bytes_err = 1;
        return 0;
    }
    for (i = 0; i < n; i++) {
        v = (v << 8) | (unsigned char)s[off + i];
    }
    return v;
}

static nv nv_bytes_u8(nv b, nv off) { return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 1)); }
static nv nv_bytes_u16(nv b, nv off) { return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 2)); }
static nv nv_bytes_u32(nv b, nv off) { return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 4)); }

static nv nv_bytes_i8(nv b, nv off) {
    return nv_int((signed char)(unsigned char)nv_bytes_raw(b, nv_as_int(off), 1));
}
static nv nv_bytes_i16(nv b, nv off) {
    return nv_int((short)(unsigned short)nv_bytes_raw(b, nv_as_int(off), 2));
}
static nv nv_bytes_i32(nv b, nv off) {
    return nv_int((int)(unsigned int)nv_bytes_raw(b, nv_as_int(off), 4));
}
static nv nv_bytes_i64(nv b, nv off) {
    return nv_int((long long)nv_bytes_raw(b, nv_as_int(off), 8));
}

static nv nv_bytes_f32(nv b, nv off) {
    unsigned int bits = (unsigned int)nv_bytes_raw(b, nv_as_int(off), 4);
    float f;
    memcpy(&f, &bits, 4);
    return nv_float((double)f);
}

static nv nv_bytes_f64(nv b, nv off) {
    unsigned long long bits = nv_bytes_raw(b, nv_as_int(off), 8);
    double d;
    memcpy(&d, &bits, 8);
    return nv_float(d);
}

/* VarInt/VarLong: seven bits per byte, high bit continues. varIntSize()
 * returns how many bytes the value at `off` occupies, or 0 when the buffer
 * ends inside it (an incomplete read, not an error) and -1 when it is longer
 * than the protocol allows. */
static int nv_varint_len(const char *s, int len, long long off, int maxBytes) {
    int i;
    for (i = 0; i < maxBytes; i++) {
        if (off + i >= len) {
            return 0;
        }
        if (((unsigned char)s[off + i] & 0x80) == 0) {
            return i + 1;
        }
    }
    return -1;
}

static nv nv_bytes_varint_size(nv b, nv off) {
    int len;
    const char *s = nv_bin(b, &len);
    return nv_int(nv_varint_len(s, len, nv_as_int(off), 5));
}

static nv nv_bytes_varlong_size(nv b, nv off) {
    int len;
    const char *s = nv_bin(b, &len);
    return nv_int(nv_varint_len(s, len, nv_as_int(off), 10));
}

static unsigned long long nv_varint_value(nv b, long long off, int maxBytes) {
    int len;
    const char *s = nv_bin(b, &len);
    unsigned long long v = 0;
    int i;
    for (i = 0; i < maxBytes; i++) {
        unsigned char byte;
        if (off + i >= len) {
            nv_bytes_err = 1;
            return 0;
        }
        byte = (unsigned char)s[off + i];
        v |= (unsigned long long)(byte & 0x7f) << (i * 7);
        if ((byte & 0x80) == 0) {
            return v;
        }
    }
    nv_bytes_err = 1;
    return 0;
}

static nv nv_bytes_varint(nv b, nv off) {
    return nv_int((int)(unsigned int)nv_varint_value(b, nv_as_int(off), 5));
}

static nv nv_bytes_varlong(nv b, nv off) {
    return nv_int((long long)nv_varint_value(b, nv_as_int(off), 10));
}

/* Writers: every one returns the encoded bytes as a fresh string. */
static nv nv_bytes_put_raw(unsigned long long v, int n) {
    char buf[8];
    int i;
    for (i = 0; i < n; i++) {
        buf[i] = (char)((v >> ((n - 1 - i) * 8)) & 0xff);
    }
    return nv_strn(buf, n);
}

static nv nv_bytes_put_u8(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 1); }
static nv nv_bytes_put_i16(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 2); }
static nv nv_bytes_put_i32(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 4); }
static nv nv_bytes_put_i64(nv v) { return nv_bytes_put_raw((unsigned long long)nv_as_int(v), 8); }

static nv nv_bytes_put_f32(nv v) {
    float f = (float)nv_as_double(v);
    unsigned int bits;
    memcpy(&bits, &f, 4);
    return nv_bytes_put_raw(bits, 4);
}

static nv nv_bytes_put_f64(nv v) {
    double d = nv_as_double(v);
    unsigned long long bits;
    memcpy(&bits, &d, 8);
    return nv_bytes_put_raw(bits, 8);
}

static nv nv_bytes_put_varnum(unsigned long long v, int maxBytes) {
    char buf[10];
    int n = 0;
    while (n < maxBytes) {
        unsigned char byte = (unsigned char)(v & 0x7f);
        v >>= 7;
        if (v == 0) {
            buf[n++] = (char)byte;
            break;
        }
        buf[n++] = (char)(byte | 0x80);
    }
    return nv_strn(buf, n);
}

static nv nv_bytes_put_varint(nv v) {
    return nv_bytes_put_varnum((unsigned long long)(unsigned int)(int)nv_as_int(v), 5);
}

static nv nv_bytes_put_varlong(nv v) {
    return nv_bytes_put_varnum((unsigned long long)nv_as_int(v), 10);
}

/* The number of bytes writeVarInt() would produce - the protocol needs it to
 * reserve the length prefix before the body is known. */
static nv nv_bytes_varint_written(nv v) {
    unsigned int val = (unsigned int)(int)nv_as_int(v);
    int n = 1;
    while (val >= 0x80) {
        val >>= 7;
        n++;
    }
    return nv_int(n);
}

static nv nv_bytes_slice(nv b, nv from, nv to) {
    int len;
    const char *s = nv_bin(b, &len);
    long long a = nv_as_int(from);
    long long z = nv_as_int(to);
    if (a < 0) {
        a = 0;
    }
    if (z > len) {
        z = len;
    }
    if (z <= a) {
        return nv_str("");
    }
    return nv_strn(s + a, (int)(z - a));
}

static nv nv_bytes_size(nv b) {
    int len;
    nv_bin(b, &len);
    return nv_int(len);
}

/* Joins an array of byte strings in one pass - the packet writer builds its
 * payload as a list of pieces and flattens it once. */
static nv nv_bytes_join(nv parts) {
    NvArr *a;
    int total = 0;
    int i;
    char *buf;
    int at = 0;
    if (nv_type_of(parts) != NV_ARR) {
        return nv_str("");
    }
    a = parts->a;
    for (i = 0; i < a->len; i++) {
        int len;
        nv_bin(a->items[i], &len);
        total += len;
    }
    buf = (char *)nv_alloc((size_t)total + 1);
    for (i = 0; i < a->len; i++) {
        int len;
        const char *s = nv_bin(a->items[i], &len);
        memcpy(buf + at, s, (size_t)len);
        at += len;
    }
    buf[total] = 0;
    return nv_str_own(buf, total);
}

/* n zero bytes. */
static nv nv_bytes_zeros(nv count) {
    long long n = nv_as_int(count);
    char *buf;
    if (n <= 0) {
        return nv_str("");
    }
    buf = (char *)nv_alloc((size_t)n + 1);
    memset(buf, 0, (size_t)n + 1);
    return nv_str_own(buf, (int)n);
}

static nv nv_bytes_hex(nv b) {
    static const char *digits = "0123456789abcdef";
    int len;
    const char *s = nv_bin(b, &len);
    char *buf = (char *)nv_alloc((size_t)len * 2 + 1);
    int i;
    for (i = 0; i < len; i++) {
        buf[i * 2] = digits[((unsigned char)s[i]) >> 4];
        buf[i * 2 + 1] = digits[((unsigned char)s[i]) & 0xf];
    }
    buf[len * 2] = 0;
    return nv_str_own(buf, len * 2);
}

static int nv_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static nv nv_bytes_from_hex(nv text) {
    int len;
    const char *s = nv_bin(text, &len);
    char *buf = (char *)nv_alloc((size_t)len / 2 + 1);
    int n = 0;
    int i = 0;
    while (i + 1 < len) {
        int hi = nv_hex_digit(s[i]);
        int lo = nv_hex_digit(s[i + 1]);
        if (hi < 0 || lo < 0) {
            break;
        }
        buf[n++] = (char)((hi << 4) | lo);
        i += 2;
    }
    buf[n] = 0;
    return nv_str_own(buf, n);
}

/* array<integer> of the byte values, and back. */
static nv nv_bytes_to_array(nv b) {
    int len;
    const char *s = nv_bin(b, &len);
    nv out = nv_arr();
    int i;
    for (i = 0; i < len; i++) {
        nv_arr_push(out->a, nv_int((unsigned char)s[i]));
    }
    return out;
}

static nv nv_bytes_of_array(nv values) {
    NvArr *a;
    char *buf;
    int i;
    if (nv_type_of(values) != NV_ARR) {
        return nv_str("");
    }
    a = values->a;
    buf = (char *)nv_alloc((size_t)a->len + 1);
    for (i = 0; i < a->len; i++) {
        buf[i] = (char)(nv_as_int(a->items[i]) & 0xff);
    }
    buf[a->len] = 0;
    return nv_str_own(buf, a->len);
}

/* Index of `needle` in `haystack` at or after `from`, byte exact (-1 when
 * absent). The string builtin stops at a NUL byte; this one does not. */
static nv nv_bytes_index_of(nv haystack, nv needle, nv from) {
    int hlen;
    int nlen;
    const char *h = nv_bin(haystack, &hlen);
    const char *n = nv_bin(needle, &nlen);
    long long start = nv_as_int(from);
    long long i;
    if (start < 0) {
        start = 0;
    }
    if (nlen == 0) {
        return nv_int(start <= hlen ? start : -1);
    }
    for (i = start; i + nlen <= hlen; i++) {
        if (memcmp(h + i, n, (size_t)nlen) == 0) {
            return nv_int(i);
        }
    }
    return nv_int(-1);
}

/* Byte exact equality (the == operator compares NUL terminated views). */
static nv nv_bytes_equal(nv a, nv b) {
    int alen;
    int blen;
    const char *x = nv_bin(a, &alen);
    const char *y = nv_bin(b, &blen);
    return nv_bool(alen == blen && memcmp(x, y, (size_t)alen) == 0);
}

/* ------------------------------------------------------------------ */
/* TCP sockets                                                         */
/* ------------------------------------------------------------------ */

/* Non-blocking sockets plus poll(): one thread drives every connection, which
 * is what a tick loop wants anyway. A socket is an integer handle in Novus;
 * net.status() reports what the last call did, because a Novus method cannot
 * return a value and an error at once. */
#ifdef _WIN32
typedef SOCKET nv_sock;
#define NV_BADSOCK INVALID_SOCKET
#define NV_SOCKERR(e) (WSAGetLastError() == (e))
#define NV_EWOULDBLOCK WSAEWOULDBLOCK
#else
typedef int nv_sock;
#define NV_BADSOCK (-1)
#define NV_SOCKERR(e) (errno == (e))
#define NV_EWOULDBLOCK EWOULDBLOCK
#endif

enum { NV_NET_OK = 0, NV_NET_AGAIN = 1, NV_NET_CLOSED = 2, NV_NET_ERROR = 3 };

static NV_TLS int nv_net_last = NV_NET_OK;
static NV_TLS char nv_net_message[256];

static void nv_net_fail(const char *what) {
    nv_net_last = NV_NET_ERROR;
#ifdef _WIN32
    snprintf(nv_net_message, sizeof(nv_net_message), "%s: winsock error %d", what, WSAGetLastError());
#else
    snprintf(nv_net_message, sizeof(nv_net_message), "%s: %s", what, strerror(errno));
#endif
}

static void nv_net_start(void) {
#ifdef _WIN32
    static int started = 0;
    if (!started) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        started = 1;
    }
#endif
}

static int nv_net_would_block(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static void nv_net_nonblocking(nv_sock s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void nv_net_shutclose(nv_sock s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

/* Resolves host:port. An empty or "0.0.0.0" host binds every interface. */
static struct addrinfo *nv_net_resolve(const char *host, int port, int passive) {
    struct addrinfo hints;
    struct addrinfo *result = 0;
    char service[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo((host && host[0]) ? host : 0, service, &hints, &result) != 0) {
        return 0;
    }
    return result;
}

static nv nv_net_listen(nv host, nv port) {
    struct addrinfo *info;
    nv_sock s;
    int on = 1;
    nv_net_start();
    nv_net_last = NV_NET_OK;
    info = nv_net_resolve(nv_cstr(host), (int)nv_as_int(port), 1);
    if (!info) {
        nv_net_last = NV_NET_ERROR;
        snprintf(nv_net_message, sizeof(nv_net_message), "listen: cannot resolve host");
        return nv_int(-1);
    }
    s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (s == NV_BADSOCK) {
        nv_net_fail("socket");
        freeaddrinfo(info);
        return nv_int(-1);
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    if (bind(s, info->ai_addr, (int)info->ai_addrlen) != 0) {
        nv_net_fail("bind");
        nv_net_shutclose(s);
        freeaddrinfo(info);
        return nv_int(-1);
    }
    freeaddrinfo(info);
    if (listen(s, 128) != 0) {
        nv_net_fail("listen");
        nv_net_shutclose(s);
        return nv_int(-1);
    }
    nv_net_nonblocking(s);
    return nv_int((long long)s);
}

/* -1 and status AGAIN when no connection is waiting. */
static nv nv_net_accept(nv server) {
    nv_sock s = (nv_sock)nv_as_int(server);
    nv_sock c;
    int on = 1;
    nv_net_last = NV_NET_OK;
    c = accept(s, 0, 0);
    if (c == NV_BADSOCK) {
        nv_net_last = nv_net_would_block() ? NV_NET_AGAIN : NV_NET_ERROR;
        if (nv_net_last == NV_NET_ERROR) {
            nv_net_fail("accept");
        }
        return nv_int(-1);
    }
    nv_net_nonblocking(c);
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
    return nv_int((long long)c);
}

static nv nv_net_connect(nv host, nv port) {
    struct addrinfo *info;
    nv_sock s;
    int on = 1;
    nv_net_start();
    nv_net_last = NV_NET_OK;
    info = nv_net_resolve(nv_cstr(host), (int)nv_as_int(port), 0);
    if (!info) {
        nv_net_last = NV_NET_ERROR;
        snprintf(nv_net_message, sizeof(nv_net_message), "connect: cannot resolve host");
        return nv_int(-1);
    }
    s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (s == NV_BADSOCK) {
        nv_net_fail("socket");
        freeaddrinfo(info);
        return nv_int(-1);
    }
    if (connect(s, info->ai_addr, (int)info->ai_addrlen) != 0) {
        nv_net_fail("connect");
        nv_net_shutclose(s);
        freeaddrinfo(info);
        return nv_int(-1);
    }
    freeaddrinfo(info);
    nv_net_nonblocking(s);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
    return nv_int((long long)s);
}

/* Up to `max` bytes. "" with status AGAIN means nothing was ready, "" with
 * status CLOSED means the peer hung up. */
static nv nv_net_recv(nv sock, nv max) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    long long want = nv_as_int(max);
    char *buf;
    long long got;
    nv_net_last = NV_NET_OK;
    if (want <= 0) {
        return nv_str("");
    }
    if (want > 1024 * 1024) {
        want = 1024 * 1024;
    }
    buf = (char *)nv_alloc((size_t)want + 1);
    got = (long long)recv(s, buf, (int)want, 0);
    if (got == 0) {
        nv_net_last = NV_NET_CLOSED;
        return nv_str("");
    }
    if (got < 0) {
        nv_net_last = nv_net_would_block() ? NV_NET_AGAIN : NV_NET_ERROR;
        if (nv_net_last == NV_NET_ERROR) {
            nv_net_fail("recv");
        }
        return nv_str("");
    }
    buf[got] = 0;
    return nv_str_own(buf, (int)got);
}

/* Bytes actually handed to the kernel; the caller keeps the remainder in its
 * own out-buffer and retries (status AGAIN when the socket is full). */
static nv nv_net_send(nv sock, nv data) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    int len;
    const char *bytes = nv_bin(data, &len);
    long long sent;
    nv_net_last = NV_NET_OK;
    if (len == 0) {
        return nv_int(0);
    }
#ifdef MSG_NOSIGNAL
    sent = (long long)send(s, bytes, len, MSG_NOSIGNAL);
#else
    sent = (long long)send(s, bytes, len, 0);
#endif
    if (sent < 0) {
        nv_net_last = nv_net_would_block() ? NV_NET_AGAIN : NV_NET_ERROR;
        if (nv_net_last == NV_NET_ERROR) {
            nv_net_fail("send");
        }
        return nv_int(0);
    }
    return nv_int(sent);
}

static nv nv_net_close(nv sock) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    if ((long long)s >= 0) {
        nv_net_shutclose(s);
    }
    return nv_nil;
}

/* Waits until one of `sockets` is readable (or `timeoutMs` passes) and
 * returns those that are. A timeout of 0 polls, -1 blocks. */
static nv nv_net_poll(nv sockets, nv timeoutMs) {
    NvArr *a;
    nv out = nv_arr();
    int i;
    int count;
#ifdef _WIN32
    WSAPOLLFD *fds;
#else
    struct pollfd *fds;
#endif
    nv_net_last = NV_NET_OK;
    if (nv_type_of(sockets) != NV_ARR) {
        return out;
    }
    a = sockets->a;
    if (a->len == 0) {
        return out;
    }
#ifdef _WIN32
    fds = (WSAPOLLFD *)nv_alloc(sizeof(WSAPOLLFD) * (size_t)a->len);
#else
    fds = (struct pollfd *)nv_alloc(sizeof(struct pollfd) * (size_t)a->len);
#endif
    for (i = 0; i < a->len; i++) {
        fds[i].fd = (nv_sock)nv_as_int(a->items[i]);
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }
#ifdef _WIN32
    count = WSAPoll(fds, (ULONG)a->len, (INT)nv_as_int(timeoutMs));
#else
    count = poll(fds, (nfds_t)a->len, (int)nv_as_int(timeoutMs));
#endif
    if (count < 0) {
        if (!nv_net_would_block()) {
            nv_net_fail("poll");
        }
        return out;
    }
    for (i = 0; i < a->len; i++) {
        if (fds[i].revents != 0) {
            nv_arr_push(out->a, a->items[i]);
        }
    }
    return out;
}

/* Like poll(), but reports writability - used to drain a backed up send
 * buffer without spinning. */
static nv nv_net_poll_write(nv sockets, nv timeoutMs) {
    NvArr *a;
    nv out = nv_arr();
    int i;
    int count;
#ifdef _WIN32
    WSAPOLLFD *fds;
#else
    struct pollfd *fds;
#endif
    nv_net_last = NV_NET_OK;
    if (nv_type_of(sockets) != NV_ARR) {
        return out;
    }
    a = sockets->a;
    if (a->len == 0) {
        return out;
    }
#ifdef _WIN32
    fds = (WSAPOLLFD *)nv_alloc(sizeof(WSAPOLLFD) * (size_t)a->len);
#else
    fds = (struct pollfd *)nv_alloc(sizeof(struct pollfd) * (size_t)a->len);
#endif
    for (i = 0; i < a->len; i++) {
        fds[i].fd = (nv_sock)nv_as_int(a->items[i]);
        fds[i].events = POLLOUT;
        fds[i].revents = 0;
    }
#ifdef _WIN32
    count = WSAPoll(fds, (ULONG)a->len, (INT)nv_as_int(timeoutMs));
#else
    count = poll(fds, (nfds_t)a->len, (int)nv_as_int(timeoutMs));
#endif
    if (count < 0) {
        return out;
    }
    for (i = 0; i < a->len; i++) {
        if (fds[i].revents != 0) {
            nv_arr_push(out->a, a->items[i]);
        }
    }
    return out;
}

static nv nv_net_status(void) { return nv_int(nv_net_last); }
static nv nv_net_error(void) { return nv_str(nv_net_message); }

/* "1.2.3.4:56789" of the connected peer. */
static nv nv_net_peer(nv sock) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    struct sockaddr_storage addr;
    char host[64];
    char out[96];
#ifdef _WIN32
    int len = (int)sizeof(addr);
#else
    socklen_t len = (socklen_t)sizeof(addr);
#endif
    if (getpeername(s, (struct sockaddr *)&addr, &len) != 0) {
        return nv_str("");
    }
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)&addr;
        if (!inet_ntop(AF_INET, &v4->sin_addr, host, sizeof(host))) {
            return nv_str("");
        }
        snprintf(out, sizeof(out), "%s:%d", host, (int)ntohs(v4->sin_port));
        return nv_str(out);
    }
    return nv_str("");
}

/* ------------------------------------------------------------------ */
/* zlib (RFC 1950) and deflate (RFC 1951)                              */
/* ------------------------------------------------------------------ */

/* Self contained on purpose: a Novus program only ever needs a C compiler,
 * so linking against the system zlib is not an option. Inflate handles all
 * three block types; deflate emits fixed Huffman blocks with an LZ77 hash
 * chain, which is what a game protocol wants (fast, good enough ratio). */

static unsigned long nv_adler32(const unsigned char *data, size_t len) {
    unsigned long a = 1;
    unsigned long b = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

/* ---- bit reader ---- */

typedef struct {
    const unsigned char *in;
    size_t inlen;
    size_t inpos;
    int bitbuf;
    int bitcnt;
    unsigned char *out;
    size_t outlen;
    size_t outcap;
    int error;
} NvInfl;

static void nv_infl_put(NvInfl *s, unsigned char c) {
    if (s->outlen == s->outcap) {
        size_t cap = s->outcap ? s->outcap * 2 : 1024;
        unsigned char *grown = (unsigned char *)realloc(s->out, cap);
        if (!grown) {
            s->error = 1;
            return;
        }
        s->out = grown;
        s->outcap = cap;
    }
    s->out[s->outlen++] = c;
}

static int nv_infl_bits(NvInfl *s, int need) {
    long value = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) {
            s->error = 1;
            return 0;
        }
        value |= (long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(value >> need);
    s->bitcnt -= need;
    return (int)(value & ((1L << need) - 1));
}

/* ---- canonical Huffman ---- */

typedef struct {
    short *count;  /* number of codes of each length 0..15 */
    short *symbol; /* symbols in canonical order */
} NvHuff;

static int nv_huff_decode(NvInfl *s, const NvHuff *h) {
    int len;
    int code = 0;
    int first = 0;
    int index = 0;
    for (len = 1; len <= 15; len++) {
        int count;
        code |= nv_infl_bits(s, 1);
        if (s->error) {
            return -1;
        }
        count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int nv_huff_build(NvHuff *h, const short *lengths, int n) {
    int symbol;
    int len;
    int left;
    short offs[16];
    for (len = 0; len <= 15; len++) {
        h->count[len] = 0;
    }
    for (symbol = 0; symbol < n; symbol++) {
        h->count[lengths[symbol]]++;
    }
    if (h->count[0] == n) {
        return 0; /* no codes at all - an empty (but legal) table */
    }
    left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return -1; /* over-subscribed */
        }
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) {
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    }
    for (symbol = 0; symbol < n; symbol++) {
        if (lengths[symbol] != 0) {
            h->symbol[offs[lengths[symbol]]++] = (short)symbol;
        }
    }
    return 0;
}

static const short nv_len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                      15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short nv_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short nv_dist_base[30] = {1,    2,    3,    4,    5,    7,     9,    13,
                                       17,   25,   33,   49,   65,   97,    129,  193,
                                       257,  385,  513,  769,  1025, 1537,  2049, 3073,
                                       4097, 6145, 8193, 12289, 16385, 24577};
static const short nv_dist_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                        4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                        9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int nv_infl_codes(NvInfl *s, const NvHuff *lencode, const NvHuff *distcode) {
    for (;;) {
        int symbol = nv_huff_decode(s, lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 256) {
            nv_infl_put(s, (unsigned char)symbol);
            if (s->error) {
                return -1;
            }
            continue;
        }
        if (symbol == 256) {
            return 0;
        }
        symbol -= 257;
        if (symbol >= 29) {
            return -1;
        }
        {
            int length = nv_len_base[symbol] + nv_infl_bits(s, nv_len_extra[symbol]);
            int dsym = nv_huff_decode(s, distcode);
            size_t dist;
            int i;
            if (dsym < 0 || dsym >= 30) {
                return -1;
            }
            dist = (size_t)(nv_dist_base[dsym] + nv_infl_bits(s, nv_dist_extra[dsym]));
            if (s->error || dist > s->outlen) {
                return -1;
            }
            for (i = 0; i < length; i++) {
                nv_infl_put(s, s->out[s->outlen - dist]);
                if (s->error) {
                    return -1;
                }
            }
        }
    }
}

static int nv_infl_stored(NvInfl *s) {
    unsigned len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen) {
        return -1;
    }
    len = (unsigned)s->in[s->inpos] | ((unsigned)s->in[s->inpos + 1] << 8);
    s->inpos += 4; /* LEN and its complement NLEN */
    if (s->inpos + len > s->inlen) {
        return -1;
    }
    while (len--) {
        nv_infl_put(s, s->in[s->inpos++]);
        if (s->error) {
            return -1;
        }
    }
    return 0;
}

static int nv_infl_fixed(NvInfl *s) {
    static short lencnt[16];
    static short lensym[288];
    static short distcnt[16];
    static short distsym[30];
    static NvHuff lencode;
    static NvHuff distcode;
    static int built = 0;
    if (!built) {
        short lengths[288];
        int symbol;
        for (symbol = 0; symbol < 144; symbol++) {
            lengths[symbol] = 8;
        }
        for (; symbol < 256; symbol++) {
            lengths[symbol] = 9;
        }
        for (; symbol < 280; symbol++) {
            lengths[symbol] = 7;
        }
        for (; symbol < 288; symbol++) {
            lengths[symbol] = 8;
        }
        lencode.count = lencnt;
        lencode.symbol = lensym;
        nv_huff_build(&lencode, lengths, 288);
        for (symbol = 0; symbol < 30; symbol++) {
            lengths[symbol] = 5;
        }
        distcode.count = distcnt;
        distcode.symbol = distsym;
        nv_huff_build(&distcode, lengths, 30);
        built = 1;
    }
    return nv_infl_codes(s, &lencode, &distcode);
}

static int nv_infl_dynamic(NvInfl *s) {
    static const short order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
    short lencnt[16];
    short lensym[288];
    short distcnt[16];
    short distsym[30];
    NvHuff lencode;
    NvHuff distcode;
    short lengths[320];
    int nlen;
    int ndist;
    int ncode;
    int index;
    nlen = nv_infl_bits(s, 5) + 257;
    ndist = nv_infl_bits(s, 5) + 1;
    ncode = nv_infl_bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30) {
        return -1;
    }
    for (index = 0; index < ncode; index++) {
        lengths[order[index]] = (short)nv_infl_bits(s, 3);
    }
    for (; index < 19; index++) {
        lengths[order[index]] = 0;
    }
    lencode.count = lencnt;
    lencode.symbol = lensym;
    if (nv_huff_build(&lencode, lengths, 19) != 0) {
        return -1;
    }
    index = 0;
    while (index < nlen + ndist) {
        int symbol = nv_huff_decode(s, &lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
            continue;
        }
        {
            short len = 0;
            int repeat;
            if (symbol == 16) {
                if (index == 0) {
                    return -1;
                }
                len = lengths[index - 1];
                repeat = 3 + nv_infl_bits(s, 2);
            } else if (symbol == 17) {
                repeat = 3 + nv_infl_bits(s, 3);
            } else {
                repeat = 11 + nv_infl_bits(s, 7);
            }
            if (index + repeat > nlen + ndist) {
                return -1;
            }
            while (repeat--) {
                lengths[index++] = len;
            }
        }
    }
    if (lengths[256] == 0) {
        return -1; /* no end-of-block code */
    }
    lencode.count = lencnt;
    lencode.symbol = lensym;
    if (nv_huff_build(&lencode, lengths, nlen) != 0) {
        return -1;
    }
    distcode.count = distcnt;
    distcode.symbol = distsym;
    if (nv_huff_build(&distcode, lengths + nlen, ndist) != 0) {
        return -1;
    }
    return nv_infl_codes(s, &lencode, &distcode);
}

/* Raw deflate stream -> bytes. `limit` (when > 0) caps the output so a tiny
 * hostile packet cannot expand into gigabytes. */
static int nv_inflate_raw(const unsigned char *in, size_t inlen, size_t limit, unsigned char **out,
                          size_t *outlen) {
    NvInfl s;
    int last;
    memset(&s, 0, sizeof(s));
    s.in = in;
    s.inlen = inlen;
    do {
        int type;
        int rc;
        last = nv_infl_bits(&s, 1);
        type = nv_infl_bits(&s, 2);
        if (s.error) {
            free(s.out);
            return -1;
        }
        if (type == 0) {
            rc = nv_infl_stored(&s);
        } else if (type == 1) {
            rc = nv_infl_fixed(&s);
        } else if (type == 2) {
            rc = nv_infl_dynamic(&s);
        } else {
            rc = -1;
        }
        if (rc != 0 || s.error) {
            free(s.out);
            return -1;
        }
        if (limit > 0 && s.outlen > limit) {
            free(s.out);
            return -1;
        }
    } while (!last);
    *out = s.out;
    *outlen = s.outlen;
    return 0;
}

/* ---- deflate ---- */

typedef struct {
    unsigned char *out;
    size_t len;
    size_t cap;
    int bitbuf;
    int bitcnt;
    int error;
} NvDefl;

static void nv_defl_byte(NvDefl *s, unsigned char c) {
    if (s->len == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 1024;
        unsigned char *grown = (unsigned char *)realloc(s->out, cap);
        if (!grown) {
            s->error = 1;
            return;
        }
        s->out = grown;
        s->cap = cap;
    }
    s->out[s->len++] = c;
}

/* Deflate is a little endian bit stream: values go in LSB first. */
static void nv_defl_bits(NvDefl *s, int value, int count) {
    s->bitbuf |= value << s->bitcnt;
    s->bitcnt += count;
    while (s->bitcnt >= 8) {
        nv_defl_byte(s, (unsigned char)(s->bitbuf & 0xff));
        s->bitbuf >>= 8;
        s->bitcnt -= 8;
    }
}

/* Huffman codes travel MSB first, so they are reversed into the stream. */
static void nv_defl_huff(NvDefl *s, int code, int count) {
    int i;
    for (i = count - 1; i >= 0; i--) {
        nv_defl_bits(s, (code >> i) & 1, 1);
    }
}

static void nv_defl_literal(NvDefl *s, int symbol) {
    if (symbol < 144) {
        nv_defl_huff(s, 0x30 + symbol, 8);
    } else if (symbol < 256) {
        nv_defl_huff(s, 0x190 + symbol - 144, 9);
    } else if (symbol < 280) {
        nv_defl_huff(s, symbol - 256, 7);
    } else {
        nv_defl_huff(s, 0xc0 + symbol - 280, 8);
    }
}

#define NV_DEFL_WBITS 15
#define NV_DEFL_WSIZE (1 << NV_DEFL_WBITS)
#define NV_DEFL_HBITS 15
#define NV_DEFL_HSIZE (1 << NV_DEFL_HBITS)
#define NV_DEFL_MAXMATCH 258
#define NV_DEFL_MINMATCH 3
#define NV_DEFL_CHAIN 32

static int nv_defl_length_code(int length) {
    int i;
    for (i = 28; i >= 0; i--) {
        if (length >= nv_len_base[i]) {
            return i;
        }
    }
    return 0;
}

static int nv_defl_dist_code(int dist) {
    int i;
    for (i = 29; i >= 0; i--) {
        if (dist >= nv_dist_base[i]) {
            return i;
        }
    }
    return 0;
}

/* Raw deflate: one fixed Huffman block with LZ77 matches found through a
 * hash of three bytes and a chain of previous positions. */
static int nv_deflate_raw(const unsigned char *in, size_t inlen, unsigned char **out,
                          size_t *outlen) {
    NvDefl s;
    int *head = 0;
    int *prev = 0;
    size_t pos = 0;
    size_t i;
    memset(&s, 0, sizeof(s));
    head = (int *)malloc(sizeof(int) * NV_DEFL_HSIZE);
    prev = (int *)malloc(sizeof(int) * (inlen > 0 ? inlen : 1));
    if (!head || !prev) {
        free(head);
        free(prev);
        return -1;
    }
    for (i = 0; i < NV_DEFL_HSIZE; i++) {
        head[i] = -1;
    }
    nv_defl_bits(&s, 1, 1); /* BFINAL */
    nv_defl_bits(&s, 1, 2); /* fixed Huffman */
    while (pos < inlen) {
        int bestLen = 0;
        size_t bestDist = 0;
        unsigned hash = 0;
        if (pos + NV_DEFL_MINMATCH <= inlen) {
            hash = ((unsigned)in[pos] << 10) ^ ((unsigned)in[pos + 1] << 5) ^ (unsigned)in[pos + 2];
            hash &= NV_DEFL_HSIZE - 1;
            {
                int candidate = head[hash];
                int tries = NV_DEFL_CHAIN;
                while (candidate >= 0 && tries-- > 0) {
                    size_t dist = pos - (size_t)candidate;
                    int len = 0;
                    if (dist == 0 || dist > NV_DEFL_WSIZE) {
                        break;
                    }
                    while (len < NV_DEFL_MAXMATCH && pos + len < inlen &&
                           in[(size_t)candidate + len] == in[pos + len]) {
                        len++;
                    }
                    if (len > bestLen) {
                        bestLen = len;
                        bestDist = dist;
                        if (len >= NV_DEFL_MAXMATCH) {
                            break;
                        }
                    }
                    candidate = prev[candidate];
                }
            }
            prev[pos] = head[hash];
            head[hash] = (int)pos;
        }
        if (bestLen >= NV_DEFL_MINMATCH) {
            int lc = nv_defl_length_code(bestLen);
            int dc = nv_defl_dist_code((int)bestDist);
            nv_defl_literal(&s, 257 + lc);
            nv_defl_bits(&s, bestLen - nv_len_base[lc], nv_len_extra[lc]);
            nv_defl_huff(&s, dc, 5);
            nv_defl_bits(&s, (int)bestDist - nv_dist_base[dc], nv_dist_extra[dc]);
            /* every skipped position still has to enter the hash chain */
            for (i = 1; i < (size_t)bestLen; i++) {
                size_t at = pos + i;
                if (at + NV_DEFL_MINMATCH <= inlen) {
                    unsigned h = ((unsigned)in[at] << 10) ^ ((unsigned)in[at + 1] << 5) ^
                                 (unsigned)in[at + 2];
                    h &= NV_DEFL_HSIZE - 1;
                    prev[at] = head[h];
                    head[h] = (int)at;
                }
            }
            pos += (size_t)bestLen;
            continue;
        }
        nv_defl_literal(&s, in[pos]);
        pos++;
    }
    nv_defl_literal(&s, 256); /* end of block */
    if (s.bitcnt > 0) {
        nv_defl_byte(&s, (unsigned char)(s.bitbuf & 0xff));
    }
    free(head);
    free(prev);
    if (s.error) {
        free(s.out);
        return -1;
    }
    *out = s.out;
    *outlen = s.len;
    return 0;
}

/* ---- the Novus facing calls ---- */

/* zlib container: 0x78 0x9C, deflate data, big endian Adler-32. */
static nv nv_zlib_compress(nv data) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char *raw = 0;
    size_t rawlen = 0;
    unsigned long adler;
    char *result;
    size_t total;
    if (nv_deflate_raw((const unsigned char *)bytes, (size_t)len, &raw, &rawlen) != 0) {
        free(raw);
        return nv_str("");
    }
    adler = nv_adler32((const unsigned char *)bytes, (size_t)len);
    total = rawlen + 6;
    result = (char *)nv_alloc(total + 1);
    result[0] = (char)0x78;
    result[1] = (char)0x9c;
    memcpy(result + 2, raw, rawlen);
    result[rawlen + 2] = (char)((adler >> 24) & 0xff);
    result[rawlen + 3] = (char)((adler >> 16) & 0xff);
    result[rawlen + 4] = (char)((adler >> 8) & 0xff);
    result[rawlen + 5] = (char)(adler & 0xff);
    result[total] = 0;
    free(raw);
    return nv_str_own(result, (int)total);
}

/* `limit` > 0 rejects a stream that expands beyond it. "" on any failure -
 * zlib.failed() tells the two apart from an empty input. */
static NV_TLS int nv_zlib_err = 0;

static nv nv_zlib_decompress(nv data, nv limit) {
    int len;
    const char *bytes = nv_bin(data, &len);
    unsigned char *raw = 0;
    size_t rawlen = 0;
    size_t cap = (size_t)nv_as_int(limit);
    char *result;
    nv_zlib_err = 0;
    if (len < 2) {
        nv_zlib_err = 1;
        return nv_str("");
    }
    /* skip the 2 byte zlib header; FDICT (bit 5 of FLG) is not supported */
    if (((unsigned char)bytes[1] & 0x20) != 0) {
        nv_zlib_err = 1;
        return nv_str("");
    }
    if (nv_inflate_raw((const unsigned char *)bytes + 2, (size_t)len - 2, cap, &raw, &rawlen) != 0) {
        free(raw);
        nv_zlib_err = 1;
        return nv_str("");
    }
    result = (char *)nv_alloc(rawlen + 1);
    memcpy(result, raw, rawlen);
    result[rawlen] = 0;
    free(raw);
    return nv_str_own(result, (int)rawlen);
}

static nv nv_zlib_failed(void) { return nv_bool(nv_zlib_err != 0); }

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
    out = (char *)nv_alloc((size_t)len + 1);
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

/* ---- hashes and randomness for Novus ---- */

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
    buf = (char *)nv_alloc((size_t)n + 1);
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

/* ------------------------------------------------------------------ */
/* Threads, virtual threads, tasks                                     */
/* ------------------------------------------------------------------ */

/* Two kinds of thread, one set of primitives.
 *
 * A `thread` is an operating system thread. It costs what the system charges
 * for one - a stack measured in megabytes - and the kernel schedules it, so
 * a program has tens or hundreds of them, not millions.
 *
 * A `virtual` thread is a stack of its own (a hundred kilobytes reserved, of
 * which only the pages it touches are ever backed by memory) that a small
 * pool of carrier threads runs. Blocking one - awaiting a task, a channel, a
 * lock, thread.sleep - parks its stack and hands the carrier to the next
 * runnable virtual thread instead of handing it back to the kernel, so a
 * program can have as many of them as it has work. The stacks are switched
 * with ucontext on unix and with fibers on Windows.
 *
 * Everything that blocks goes through one parking lot: a queue of waiters on
 * an object, each of them either a virtual thread to make runnable again or
 * an operating system thread to signal. That is why a lock, a channel or an
 * await reads the same whichever kind of thread runs into it, and why a
 * virtual thread that blocks costs no operating system thread.
 *
 * A virtual thread never moves to another carrier. It could - the run queue
 * would only have to be shared - but a stack that is switched under the feet
 * of the compiler must not be allowed to wake up on a different thread: the
 * address of a thread local is a value like any other, and a compiler is
 * free to keep one in a register across the switch. Staying put keeps every
 * thread local (the thread's arena above all) the one the code that reads it
 * was compiled to reach. */

#ifdef _WIN32

typedef CRITICAL_SECTION NvMutexRaw;
typedef CONDITION_VARIABLE NvCondRaw;
#define NV_MUTEX_INIT(m) InitializeCriticalSection(m)
#define NV_MUTEX_LOCK(m) EnterCriticalSection(m)
#define NV_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define NV_COND_INIT(c) InitializeConditionVariable(c)
#define NV_COND_WAIT(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define NV_COND_WAIT_MS(c, m, ms) SleepConditionVariableCS(c, m, (DWORD)(ms))
#define NV_COND_SIGNAL(c) WakeConditionVariable(c)
#define NV_COND_BROADCAST(c) WakeAllConditionVariable(c)
#define NV_HAVE_FIBERS 1

#else

#include <pthread.h>
#include <sched.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <ucontext.h>
#define NV_HAVE_FIBERS 1
#else
#define NV_HAVE_FIBERS 0
#endif

typedef pthread_mutex_t NvMutexRaw;
typedef pthread_cond_t NvCondRaw;
#define NV_MUTEX_INIT(m) pthread_mutex_init(m, 0)
#define NV_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define NV_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define NV_COND_INIT(c) pthread_cond_init(c, 0)
#define NV_COND_WAIT(c, m) pthread_cond_wait(c, m)
#define NV_COND_SIGNAL(c) pthread_cond_signal(c)
#define NV_COND_BROADCAST(c) pthread_cond_broadcast(c)

static void nv_cond_wait_ms(NvCondRaw *c, NvMutexRaw *m, long long ms) {
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    ts.tv_sec = time(0);
    ts.tv_nsec = 0;
#endif
    ts.tv_sec += (time_t)(ms / 1000);
    ts.tv_nsec += (long)((ms % 1000) * 1000000L);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(c, m, &ts);
}
#define NV_COND_WAIT_MS(c, m, ms) nv_cond_wait_ms(c, m, ms)

#endif

static long long nv_now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    return (long long)time(0) * 1000;
#endif
}

static int nv_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors < 1 ? 1 : (int)si.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : (int)n;
#else
    return 1;
#endif
}

enum { NV_VT_NEW = 0, NV_VT_READY, NV_VT_RUNNING, NV_VT_PARKED, NV_VT_DONE };

typedef nv (*NvSpawnFn)(nv *args, int n);

typedef struct NvVThread NvVThread;
typedef struct NvCarrier NvCarrier;

#if NV_HAVE_FIBERS
#ifdef _WIN32
typedef LPVOID NvCtx;
#else
typedef ucontext_t NvCtx;
#endif
#else
typedef int NvCtx;
#endif

struct NvVThread {
    NvCtx ctx;
    void *stackBase;      /* what has to be handed back, guard page included */
    size_t stackSize;     /* usable bytes */
    NvCarrier *carrier;   /* the one carrier that ever runs this stack */
    NvSpawnFn fn;
    nv *args;
    int nargs;
    int task;             /* the task this virtual thread completes */
    int state;
    NvMutexRaw *parkLock; /* the carrier releases it once the stack is off */
    long long wakeAt;     /* deadline while on the timer queue */
    NvVThread *next;      /* run queue, timer queue or free list */
};

struct NvCarrier {
    NvCtx sched;
    NvMutexRaw lock;
    NvCondRaw cond;
    NvVThread *runHead, *runTail;
    NvVThread *timers; /* sorted by wakeAt */
    NvVThread *pool;   /* finished stacks, ready to run something else */
};

static NV_TLS NvVThread *nv_cur_vt = 0;
static NV_TLS NvCarrier *nv_cur_carrier = 0;
static NV_TLS int nv_tls_anchor = 0; /* its address identifies an os thread */

/* Who is running: the virtual thread when there is one, the operating system
 * thread otherwise. A lock compares owners with it. */
static void *nv_runner(void) {
    return nv_cur_vt ? (void *)nv_cur_vt : (void *)&nv_tls_anchor;
}

/* --- parking lot --------------------------------------------------- */

typedef struct NvWaiter {
    struct NvWaiter *next;
    NvVThread *vt;   /* the waiter is a virtual thread ... */
    NvCondRaw *cond; /* ... or an operating system thread with this condition */
    int ready;
} NvWaiter;

typedef struct NvPark {
    NvWaiter *head;
    NvWaiter *tail;
} NvPark;

static NV_TLS NvCondRaw nv_self_cond_storage;
static NV_TLS int nv_self_cond_made = 0;

static NvCondRaw *nv_self_cond(void) {
    if (!nv_self_cond_made) {
        NV_COND_INIT(&nv_self_cond_storage);
        nv_self_cond_made = 1;
    }
    return &nv_self_cond_storage;
}

static void nv_carrier_wake(NvVThread *vt);
static void nv_vt_park(NvMutexRaw *m);

/* Blocks the current runner on `p`. `m` must be held on the way in and is
 * held again on the way out, so the condition `p` stands for gets re-checked
 * in a loop, exactly the way a condition variable wants it. */
static void nv_park_self(NvPark *p, NvMutexRaw *m) {
    NvWaiter w;
    w.next = 0;
    w.ready = 0;
    w.vt = nv_cur_vt;
    w.cond = w.vt ? 0 : nv_self_cond();
    if (p->tail) {
        p->tail->next = &w;
    } else {
        p->head = &w;
    }
    p->tail = &w;
    if (w.vt) {
        nv_vt_park(m);
    } else {
        while (!w.ready) {
            NV_COND_WAIT(w.cond, m);
        }
    }
}

/* Both are called with the mutex that guards `p` held. */
static void nv_unpark_one(NvPark *p) {
    NvWaiter *w = p->head;
    if (!w) {
        return;
    }
    p->head = w->next;
    if (!p->head) {
        p->tail = 0;
    }
    w->ready = 1;
    if (w->vt) {
        nv_carrier_wake(w->vt);
    } else {
        NV_COND_SIGNAL(w->cond);
    }
}

static void nv_unpark_all(NvPark *p) {
    while (p->head) {
        nv_unpark_one(p);
    }
}

/* --- the carrier pool ---------------------------------------------- */

#define NV_MAX_CARRIERS 256

static NvMutexRaw nv_sched_lock; /* the carrier table and the live count */
static NvMutexRaw nv_rt_lock;    /* handles and task state */
static NvCarrier *nv_carrier_table[NV_MAX_CARRIERS];
static int nv_carriers_running = 0;
static int nv_carriers_wanted = 0;
static int nv_spawn_turn = 0;
static int nv_vt_live = 0;
static size_t nv_vstack_size = 0;
static int nv_conc_ready = 0;

/* Set up before any second thread exists: nv_init_args() calls this, and so
 * does every entry point that can be the first one a program reaches. */
static void nv_conc_init(void) {
    const char *env;
    if (nv_conc_ready) {
        return;
    }
    nv_conc_ready = 1;
    NV_MUTEX_INIT(&nv_sched_lock);
    NV_MUTEX_INIT(&nv_rt_lock);
    nv_carriers_wanted = nv_cpu_count();
    if (nv_carriers_wanted > NV_MAX_CARRIERS) {
        nv_carriers_wanted = NV_MAX_CARRIERS;
    }
    env = getenv("NOVUS_THREADS");
    if (env && atoi(env) > 0) {
        nv_carriers_wanted = atoi(env) > NV_MAX_CARRIERS ? NV_MAX_CARRIERS : atoi(env);
    }
    nv_vstack_size = 128 * 1024;
    env = getenv("NOVUS_VSTACK");
    if (env && atoi(env) > 0) {
        nv_vstack_size = (size_t)atoi(env) * 1024;
    }
}

/* Called with c->lock held. */
static void nv_runq_push(NvCarrier *c, NvVThread *vt) {
    vt->state = NV_VT_READY;
    vt->next = 0;
    if (c->runTail) {
        c->runTail->next = vt;
    } else {
        c->runHead = vt;
    }
    c->runTail = vt;
    NV_COND_SIGNAL(&c->cond);
}

static void nv_carrier_wake(NvVThread *vt) {
    NvCarrier *c = vt->carrier;
    NV_MUTEX_LOCK(&c->lock);
    nv_runq_push(c, vt);
    NV_MUTEX_UNLOCK(&c->lock);
}

/* Called with c->lock held: everything whose deadline has passed. */
static void nv_timers_fire(NvCarrier *c, long long now) {
    while (c->timers && c->timers->wakeAt <= now) {
        NvVThread *vt = c->timers;
        c->timers = vt->next;
        nv_runq_push(c, vt);
    }
}

static NvVThread *nv_runq_take(NvCarrier *c) {
    NvVThread *vt;
    NV_MUTEX_LOCK(&c->lock);
    for (;;) {
        long long now = nv_now_ms();
        nv_timers_fire(c, now);
        if (c->runHead) {
            vt = c->runHead;
            c->runHead = vt->next;
            if (!c->runHead) {
                c->runTail = 0;
            }
            vt->next = 0;
            NV_MUTEX_UNLOCK(&c->lock);
            return vt;
        }
        if (c->timers) {
            long long wait = c->timers->wakeAt - now;
            NV_COND_WAIT_MS(&c->cond, &c->lock, wait < 1 ? 1 : wait);
        } else {
            NV_COND_WAIT(&c->cond, &c->lock);
        }
    }
}

/* --- stacks and contexts ------------------------------------------- */

#if NV_HAVE_FIBERS && !defined(_WIN32)
static size_t nv_page_size(void) {
    long n = sysconf(_SC_PAGESIZE);
    return n < 1 ? 4096 : (size_t)n;
}
#endif

static void nv_vt_body(NvVThread *vt);

#if NV_HAVE_FIBERS
#ifdef _WIN32
static VOID CALLBACK nv_fiber_entry(PVOID arg) {
    (void)arg;
    for (;;) {
        nv_vt_body(nv_cur_vt);
    }
}
static void nv_ctx_switch(NvCtx *from, NvCtx *to) {
    (void)from;
    SwitchToFiber(*to);
}
static int nv_ctx_start(NvVThread *vt) {
    vt->ctx = CreateFiber(vt->stackSize, nv_fiber_entry, vt);
    return vt->ctx != 0;
}
static void nv_carrier_enter(NvCarrier *c) { c->sched = ConvertThreadToFiber(0); }
#else
#ifdef __APPLE__
/* ucontext is marked deprecated there and has no replacement; every use of
 * it is in the three functions below */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
static void nv_ucontext_entry(void) {
    for (;;) {
        nv_vt_body(nv_cur_vt);
    }
}
static void nv_ctx_switch(NvCtx *from, NvCtx *to) { swapcontext(from, to); }
static int nv_ctx_start(NvVThread *vt) {
    size_t page = nv_page_size();
    char *base = (char *)mmap(0, vt->stackSize + page, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_NORESERVE
                                  | MAP_NORESERVE
#endif
                              ,
                              -1, 0);
    if (base == MAP_FAILED) {
        return 0;
    }
    /* a page the stack must not grow into, so an overflow faults instead of
     * quietly becoming somebody else's memory */
    mprotect(base, page, PROT_NONE);
    vt->stackBase = base;
    if (getcontext(&vt->ctx) != 0) {
        munmap(base, vt->stackSize + page);
        vt->stackBase = 0;
        return 0;
    }
    vt->ctx.uc_stack.ss_sp = base + page;
    vt->ctx.uc_stack.ss_size = vt->stackSize;
    vt->ctx.uc_link = 0;
    makecontext(&vt->ctx, nv_ucontext_entry, 0);
    return 1;
}
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif
static void nv_carrier_enter(NvCarrier *c) { (void)c; }
#endif
#else /* no contexts here: `virtual` falls back to an operating system thread */
static void nv_ctx_switch(NvCtx *from, NvCtx *to) {
    (void)from;
    (void)to;
}
static int nv_ctx_start(NvVThread *vt) {
    (void)vt;
    return 0;
}
static void nv_carrier_enter(NvCarrier *c) { (void)c; }
#endif

/* Parks the running virtual thread. `m` is held on the way in; the carrier
 * releases it once this stack is off the processor, because releasing it
 * here would let the unparker resume a stack that is still running. It is
 * held again when the virtual thread comes back. */
static void nv_vt_park(NvMutexRaw *m) {
    NvVThread *vt = nv_cur_vt;
    vt->state = NV_VT_PARKED;
    vt->parkLock = m;
    nv_ctx_switch(&vt->ctx, &vt->carrier->sched);
    NV_MUTEX_LOCK(m);
}

static void nv_task_complete(int handle, nv result);

static void nv_vt_body(NvVThread *vt) {
    nv result = vt->fn ? vt->fn(vt->args, vt->nargs) : nv_nil;
    vt->fn = 0;
    vt->args = 0;
    vt->state = NV_VT_DONE;
    nv_task_complete(vt->task, result);
    /* Back to the carrier. If this virtual thread comes out of the pool for
     * another task the carrier resumes it right here, and the loop in the
     * entry function runs the next body on the same stack. */
    nv_ctx_switch(&vt->ctx, &vt->carrier->sched);
}

static void nv_carrier_loop(NvCarrier *c) {
    nv_carrier_enter(c);
    nv_cur_carrier = c;
    for (;;) {
        NvVThread *vt = nv_runq_take(c);
        int state;
        NvMutexRaw *held;
        nv_cur_vt = vt;
        vt->state = NV_VT_RUNNING;
        nv_ctx_switch(&c->sched, &vt->ctx);
        nv_cur_vt = 0;
        /* Read the state before releasing the lock the virtual thread parked
         * under - after that release the unparker owns it. */
        state = vt->state;
        held = vt->parkLock;
        vt->parkLock = 0;
        if (held) {
            NV_MUTEX_UNLOCK(held);
        }
        if (state == NV_VT_DONE) {
            NV_MUTEX_LOCK(&c->lock);
            vt->next = c->pool;
            c->pool = vt;
            NV_MUTEX_UNLOCK(&c->lock);
            NV_MUTEX_LOCK(&nv_sched_lock);
            nv_vt_live--;
            NV_MUTEX_UNLOCK(&nv_sched_lock);
        } else if (state == NV_VT_READY) {
            nv_carrier_wake(vt); /* yielded */
        }
    }
}

/* --- operating system threads -------------------------------------- */

#ifdef _WIN32
static DWORD WINAPI nv_carrier_main(LPVOID arg) {
    nv_carrier_loop((NvCarrier *)arg);
    return 0;
}
static int nv_thread_start(LPTHREAD_START_ROUTINE fn, void *arg) {
    HANDLE h = CreateThread(0, 0, fn, arg, 0, 0);
    if (!h) {
        return 0;
    }
    CloseHandle(h);
    return 1;
}
#else
static void *nv_carrier_main(void *arg) {
    nv_carrier_loop((NvCarrier *)arg);
    return 0;
}
static int nv_thread_start(void *(*fn)(void *), void *arg) {
    pthread_t t;
    pthread_attr_t attr;
    int rc;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(&t, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc == 0;
}
#endif

/* Called with nv_sched_lock held. */
static NvCarrier *nv_carrier_add(void) {
    NvCarrier *c;
    if (nv_carriers_running >= nv_carriers_wanted) {
        return 0;
    }
    c = (NvCarrier *)calloc(1, sizeof(NvCarrier));
    if (!c) {
        return 0;
    }
    NV_MUTEX_INIT(&c->lock);
    NV_COND_INIT(&c->cond);
    nv_carrier_table[nv_carriers_running] = c;
    if (!nv_thread_start(nv_carrier_main, c)) {
        free(c);
        return 0;
    }
    nv_carriers_running++;
    return c;
}

/* --- handles ------------------------------------------------------- */

enum { NV_H_TASK = 1, NV_H_LOCK, NV_H_CHAN, NV_H_ATOMIC, NV_H_WG };

typedef struct NvHandleSlot {
    unsigned char kind;
    void *ptr;
} NvHandleSlot;

static NvHandleSlot *nv_handles = 0;
static int nv_handle_len = 0;
static int nv_handle_cap = 0;

/* Called with nv_rt_lock held. Handles are one based; 0 is "none". */
static int nv_handle_new(unsigned char kind, void *ptr) {
    if (nv_handle_len == nv_handle_cap) {
        int cap = nv_handle_cap < 16 ? 16 : nv_handle_cap * 2;
        NvHandleSlot *grown = (NvHandleSlot *)realloc(nv_handles, sizeof(NvHandleSlot) * (size_t)cap);
        if (!grown) {
            nv_error("out of memory");
        }
        nv_handles = grown;
        nv_handle_cap = cap;
    }
    nv_handles[nv_handle_len].kind = kind;
    nv_handles[nv_handle_len].ptr = ptr;
    return ++nv_handle_len;
}

static void *nv_handle_of(nv v, unsigned char kind, const char *what) {
    long long i = nv_type_of(v) == NV_INT ? nv_ival(v) : -1;
    void *p = 0;
    nv_conc_init();
    NV_MUTEX_LOCK(&nv_rt_lock);
    if (i >= 1 && i <= nv_handle_len && nv_handles[i - 1].kind == kind) {
        p = nv_handles[i - 1].ptr;
    }
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    if (!p) {
        nv_error("not a %s handle: %s", what, nv_display(v));
    }
    return p;
}

/* --- tasks --------------------------------------------------------- */

typedef struct NvTask {
    int done;
    nv result;
    NvPark park;
} NvTask;

static void nv_task_complete(int handle, nv result) {
    NvTask *t;
    NV_MUTEX_LOCK(&nv_rt_lock);
    t = (NvTask *)nv_handles[handle - 1].ptr;
    t->result = result;
    t->done = 1;
    nv_unpark_all(&t->park);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
}

typedef struct NvOsTask {
    NvSpawnFn fn;
    nv *args;
    int nargs;
    int task;
} NvOsTask;

#ifdef _WIN32
static DWORD WINAPI nv_os_task_main(LPVOID arg) {
#else
static void *nv_os_task_main(void *arg) {
#endif
    NvOsTask *a = (NvOsTask *)arg;
    int task = a->task;
    nv result = a->fn(a->args, a->nargs);
    free(a);
    nv_task_complete(task, result);
    return 0;
}

/* Everything the runtime builds on first use - the field layout of a class,
 * its fields in name order, its defaults, which constructor a subclass
 * inherits. Built here, on one thread, so that no two threads race to build
 * the same cache once a program has more than one. */
static void nv_class_warm_all(void) {
    int i;
    for (i = 0; i < nv_nclasses; i++) {
        NvClass *c = nv_classes[i];
        nv_class_layout(c);
        nv_field_order(c, nv_class_field_count(c));
        nv_class_defaults(c);
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
    }
}

/* The first spawn of a program happens while it is still single threaded,
 * which is the moment to build everything the runtime would otherwise build
 * on first use - class layouts, field orders, defaults - so that no two
 * threads ever race to build the same cache. */
static void nv_conc_boot(void) {
    static int booted = 0;
    nv_conc_init();
    if (booted) {
        return;
    }
    booted = 1;
    nv_class_warm_all();
}

/* Called with c->lock held. */
static NvVThread *nv_vt_take(NvCarrier *c) {
    NvVThread *vt = c->pool;
    if (vt) {
        c->pool = vt->next;
        vt->next = 0;
        return vt;
    }
    vt = (NvVThread *)calloc(1, sizeof(NvVThread));
    if (!vt) {
        return 0;
    }
    vt->stackSize = nv_vstack_size;
    vt->carrier = c;
    if (!nv_ctx_start(vt)) {
        free(vt);
        return 0;
    }
    return vt;
}

/* `thread f(...)` and `virtual f(...)`. The arguments were evaluated by the
 * thread that spawns and are handed to the new one; the result is a task
 * handle to await. */
static nv nv_spawn(int wantVirtual, NvSpawnFn fn, int nargs, ...) {
    va_list ap;
    NvTask *task;
    nv *args = 0;
    int handle, i;
    nv_conc_boot();
    if (nargs > 0) {
        args = (nv *)nv_alloc(sizeof(nv) * (size_t)nargs);
        va_start(ap, nargs);
        for (i = 0; i < nargs; i++) {
            args[i] = va_arg(ap, nv);
        }
        va_end(ap);
    }
    task = (NvTask *)calloc(1, sizeof(NvTask));
    if (!task) {
        nv_error("out of memory");
    }
    task->result = nv_nil;
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_TASK, task);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
#if NV_HAVE_FIBERS
    if (wantVirtual) {
        NvCarrier *c = 0;
        NvVThread *vt;
        NV_MUTEX_LOCK(&nv_sched_lock);
        /* one carrier to begin with, another whenever the virtual threads
         * outnumber the carriers, up to the parallelism of the machine */
        if (nv_carriers_running == 0 || nv_vt_live >= nv_carriers_running) {
            nv_carrier_add();
        }
        if (nv_carriers_running > 0) {
            c = nv_carrier_table[nv_spawn_turn++ % nv_carriers_running];
            nv_vt_live++;
        }
        NV_MUTEX_UNLOCK(&nv_sched_lock);
        if (c) {
            NV_MUTEX_LOCK(&c->lock);
            vt = nv_vt_take(c);
            if (vt) {
                vt->fn = fn;
                vt->args = args;
                vt->nargs = nargs;
                vt->task = handle;
                nv_runq_push(c, vt);
                NV_MUTEX_UNLOCK(&c->lock);
                return nv_int(handle);
            }
            NV_MUTEX_UNLOCK(&c->lock);
            NV_MUTEX_LOCK(&nv_sched_lock);
            nv_vt_live--;
            NV_MUTEX_UNLOCK(&nv_sched_lock);
            /* no stack to be had: an operating system thread will do */
        }
    }
#else
    (void)wantVirtual;
#endif
    {
        NvOsTask *a = (NvOsTask *)malloc(sizeof(NvOsTask));
        if (!a) {
            nv_error("out of memory");
        }
        a->fn = fn;
        a->args = args;
        a->nargs = nargs;
        a->task = handle;
        if (!nv_thread_start(nv_os_task_main, a)) {
            /* the system refused a thread: run the work here rather than
             * leave a task that is never going to complete */
            free(a);
            nv_task_complete(handle, fn(args, nargs));
        }
    }
    return nv_int(handle);
}

/* await: the value of a task, once it has one. Awaiting anything that is not
 * a task is that value, so `await` may be written wherever a result is
 * expected without knowing whether the call it came from was async. */
static nv nv_await(nv v) {
    NvTask *t;
    nv result;
    long long i = nv_type_of(v) == NV_INT ? nv_ival(v) : -1;
    if (i < 1 || !nv_conc_ready) {
        return v;
    }
    NV_MUTEX_LOCK(&nv_rt_lock);
    if (i > nv_handle_len || nv_handles[i - 1].kind != NV_H_TASK) {
        NV_MUTEX_UNLOCK(&nv_rt_lock);
        return v;
    }
    t = (NvTask *)nv_handles[i - 1].ptr;
    while (!t->done) {
        nv_park_self(&t->park, &nv_rt_lock);
    }
    result = t->result;
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return result;
}

static nv nv_task_is_done(nv v) {
    NvTask *t = (NvTask *)nv_handle_of(v, NV_H_TASK, "task");
    int done;
    NV_MUTEX_LOCK(&nv_rt_lock);
    done = t->done;
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_bool(done);
}

static nv nv_await_all(nv tasks) {
    nv out;
    int i;
    if (nv_type_of(tasks) != NV_ARR) {
        nv_error("awaitAll expects an array of tasks");
    }
    out = nv_new(NV_ARR);
    out->a = nv_arr_new_cap(tasks->a->len);
    for (i = 0; i < tasks->a->len; i++) {
        nv_arr_push(out->a, nv_await(tasks->a->items[i]));
    }
    return out;
}

/* --- giving up the processor --------------------------------------- */

static nv nv_thread_yield(void) {
    NvVThread *vt = nv_cur_vt;
    if (!vt) {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
        return nv_nil;
    }
    vt->state = NV_VT_READY;
    nv_ctx_switch(&vt->ctx, &vt->carrier->sched);
    return nv_nil;
}

/* Sleeping a virtual thread parks it on its carrier's timer queue; the
 * carrier goes on running other virtual threads and wakes it when due. */
static nv nv_thread_sleep(nv msValue) {
    long long ms = nv_as_int(msValue);
    NvVThread *vt = nv_cur_vt;
    NvCarrier *c;
    NvVThread **at;
    if (ms <= 0) {
        return nv_thread_yield();
    }
    if (!vt) {
        return nv_os_sleep(msValue);
    }
    c = vt->carrier;
    NV_MUTEX_LOCK(&c->lock);
    vt->wakeAt = nv_now_ms() + ms;
    at = &c->timers;
    while (*at && (*at)->wakeAt <= vt->wakeAt) {
        at = &(*at)->next;
    }
    vt->next = *at;
    *at = vt;
    nv_vt_park(&c->lock);
    NV_MUTEX_UNLOCK(&c->lock);
    return nv_nil;
}

static nv nv_thread_cpus(void) { return nv_int(nv_cpu_count()); }

static nv nv_thread_parallelism(void) {
    nv_conc_init();
    return nv_int(nv_carriers_wanted);
}

static nv nv_thread_set_parallelism(nv n) {
    long long want = nv_as_int(n);
    nv_conc_init();
    if (want < 1) {
        want = 1;
    }
    if (want > NV_MAX_CARRIERS) {
        want = NV_MAX_CARRIERS;
    }
    NV_MUTEX_LOCK(&nv_sched_lock);
    nv_carriers_wanted = (int)want;
    NV_MUTEX_UNLOCK(&nv_sched_lock);
    return nv_nil;
}

static nv nv_thread_is_virtual(void) { return nv_bool(nv_cur_vt != 0); }

static nv nv_thread_virtual_supported(void) { return nv_bool(NV_HAVE_FIBERS != 0); }

/* A number that is the same for every call from one runner and different for
 * every other runner. */
static nv nv_thread_self_id(void) {
    return nv_int((long long)(((uintptr_t)nv_runner() >> 4) & 0x7fffffff));
}

static nv nv_thread_running(void) {
    int n;
    nv_conc_init();
    NV_MUTEX_LOCK(&nv_sched_lock);
    n = nv_vt_live;
    NV_MUTEX_UNLOCK(&nv_sched_lock);
    return nv_int(n);
}

/* --- locks --------------------------------------------------------- */

typedef struct NvLock {
    NvMutexRaw m;
    void *owner;
    int count;
    NvPark park;
} NvLock;

static NvLock *nv_lock_make(void) {
    NvLock *l = (NvLock *)calloc(1, sizeof(NvLock));
    if (!l) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&l->m);
    return l;
}

static nv nv_lock_new(void) {
    NvLock *l;
    int handle;
    nv_conc_init();
    l = nv_lock_make();
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_LOCK, l);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

/* Re-entrant: the runner that holds a lock walks back into it, which is what
 * a `sync` block inside a method a `sync` block called needs. */
static void nv_lock_enter(NvLock *l) {
    void *me = nv_runner();
    NV_MUTEX_LOCK(&l->m);
    if (l->owner == me) {
        l->count++;
        NV_MUTEX_UNLOCK(&l->m);
        return;
    }
    while (l->owner) {
        nv_park_self(&l->park, &l->m);
    }
    l->owner = me;
    l->count = 1;
    NV_MUTEX_UNLOCK(&l->m);
}

static void nv_lock_leave(NvLock *l) {
    NV_MUTEX_LOCK(&l->m);
    if (l->count > 0 && --l->count == 0) {
        l->owner = 0;
        nv_unpark_one(&l->park);
    }
    NV_MUTEX_UNLOCK(&l->m);
}

static nv nv_lock_acquire(nv h) {
    nv_lock_enter((NvLock *)nv_handle_of(h, NV_H_LOCK, "lock"));
    return nv_nil;
}

static nv nv_lock_release(nv h) {
    nv_lock_leave((NvLock *)nv_handle_of(h, NV_H_LOCK, "lock"));
    return nv_nil;
}

static nv nv_lock_try_acquire(nv h) {
    NvLock *l = (NvLock *)nv_handle_of(h, NV_H_LOCK, "lock");
    void *me = nv_runner();
    int got = 0;
    NV_MUTEX_LOCK(&l->m);
    if (l->owner == me) {
        l->count++;
        got = 1;
    } else if (!l->owner) {
        l->owner = me;
        l->count = 1;
        got = 1;
    }
    NV_MUTEX_UNLOCK(&l->m);
    return nv_bool(got);
}

/* The lock behind a bare `sync { ... }`: one per program, made on first use.
 * `sync (lock) { ... }` names one instead. */
static NvLock *nv_sync_default = 0;

static NvLock *nv_sync_enter(nv lock) {
    NvLock *l;
    nv_conc_init();
    if (nv_type_of(lock) == NV_INT) {
        l = (NvLock *)nv_handle_of(lock, NV_H_LOCK, "lock");
    } else {
        NV_MUTEX_LOCK(&nv_rt_lock);
        if (!nv_sync_default) {
            nv_sync_default = nv_lock_make();
        }
        l = nv_sync_default;
        NV_MUTEX_UNLOCK(&nv_rt_lock);
    }
    nv_lock_enter(l);
    return l;
}

static void nv_sync_leave(NvLock *l) { nv_lock_leave(l); }

/* --- channels ------------------------------------------------------ */

typedef struct NvChan {
    NvMutexRaw m;
    nv *items;
    int cap;      /* slots in items */
    int declared; /* what the program asked for; 0 is unbuffered */
    int len;
    int head;
    int closed;
    NvPark senders;
    NvPark receivers;
} NvChan;

static nv nv_chan_new(nv capValue) {
    long long want = nv_as_int(capValue);
    NvChan *c;
    int handle;
    nv_conc_init();
    c = (NvChan *)calloc(1, sizeof(NvChan));
    if (!c) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&c->m);
    c->declared = want < 0 ? 0 : (int)want;
    c->cap = c->declared < 1 ? 1 : c->declared;
    c->items = (nv *)malloc(sizeof(nv) * (size_t)c->cap);
    if (!c->items) {
        nv_error("out of memory");
    }
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_CHAN, c);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

/* false when the channel was closed and the value could not be delivered. */
static nv nv_chan_send(nv h, nv value) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    NV_MUTEX_LOCK(&c->m);
    while (!c->closed && c->len == c->cap) {
        nv_park_self(&c->senders, &c->m);
    }
    if (c->closed) {
        NV_MUTEX_UNLOCK(&c->m);
        return nv_bool(0);
    }
    c->items[(c->head + c->len) % c->cap] = value;
    c->len++;
    nv_unpark_one(&c->receivers);
    if (c->declared == 0) {
        /* unbuffered: the send is over when the value has been taken */
        while (!c->closed && c->len > 0) {
            nv_park_self(&c->senders, &c->m);
        }
    }
    NV_MUTEX_UNLOCK(&c->m);
    return nv_bool(1);
}

/* A received value can be anything, the absence of one included, so the
 * receiving thread is told which of the two it got - the same way `net`
 * reports what its last call did. */
static NV_TLS int nv_chan_got = 0;

static nv nv_chan_received(void) { return nv_bool(nv_chan_got); }

static nv nv_chan_recv(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    nv v;
    nv_chan_got = 0;
    NV_MUTEX_LOCK(&c->m);
    while (c->len == 0 && !c->closed) {
        nv_park_self(&c->receivers, &c->m);
    }
    if (c->len == 0) {
        NV_MUTEX_UNLOCK(&c->m); /* closed and drained */
        return nv_nil;
    }
    nv_chan_got = 1;
    v = c->items[c->head];
    c->head = (c->head + 1) % c->cap;
    c->len--;
    nv_unpark_all(&c->senders);
    NV_MUTEX_UNLOCK(&c->m);
    return v;
}

static nv nv_chan_try_recv(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    nv v = nv_nil;
    nv_chan_got = 0;
    NV_MUTEX_LOCK(&c->m);
    if (c->len > 0) {
        nv_chan_got = 1;
        v = c->items[c->head];
        c->head = (c->head + 1) % c->cap;
        c->len--;
        nv_unpark_all(&c->senders);
    }
    NV_MUTEX_UNLOCK(&c->m);
    return v;
}

static nv nv_chan_close(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    NV_MUTEX_LOCK(&c->m);
    c->closed = 1;
    nv_unpark_all(&c->senders);
    nv_unpark_all(&c->receivers);
    NV_MUTEX_UNLOCK(&c->m);
    return nv_nil;
}

static nv nv_chan_is_closed(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    int closed;
    NV_MUTEX_LOCK(&c->m);
    closed = c->closed;
    NV_MUTEX_UNLOCK(&c->m);
    return nv_bool(closed);
}

static nv nv_chan_length(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    int len;
    NV_MUTEX_LOCK(&c->m);
    len = c->len;
    NV_MUTEX_UNLOCK(&c->m);
    return nv_int(len);
}

/* --- counters ------------------------------------------------------ */

typedef struct NvCounter {
    NvMutexRaw m;
    long long value;
} NvCounter;

static nv nv_counter_new(nv initial) {
    NvCounter *a;
    int handle;
    nv_conc_init();
    a = (NvCounter *)calloc(1, sizeof(NvCounter));
    if (!a) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&a->m);
    a->value = nv_as_int(initial);
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_ATOMIC, a);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

static nv nv_counter_add(nv h, nv delta) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    long long v;
    NV_MUTEX_LOCK(&a->m);
    a->value += nv_as_int(delta);
    v = a->value;
    NV_MUTEX_UNLOCK(&a->m);
    return nv_int(v);
}

static nv nv_counter_get(nv h) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    long long v;
    NV_MUTEX_LOCK(&a->m);
    v = a->value;
    NV_MUTEX_UNLOCK(&a->m);
    return nv_int(v);
}

static nv nv_counter_set(nv h, nv value) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    NV_MUTEX_LOCK(&a->m);
    a->value = nv_as_int(value);
    NV_MUTEX_UNLOCK(&a->m);
    return nv_nil;
}

static nv nv_counter_swap(nv h, nv expect, nv value) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    int swapped = 0;
    NV_MUTEX_LOCK(&a->m);
    if (a->value == nv_as_int(expect)) {
        a->value = nv_as_int(value);
        swapped = 1;
    }
    NV_MUTEX_UNLOCK(&a->m);
    return nv_bool(swapped);
}

/* --- groups -------------------------------------------------------- */

typedef struct NvGroup {
    NvMutexRaw m;
    int count;
    NvPark park;
} NvGroup;

static nv nv_group_new(void) {
    NvGroup *g;
    int handle;
    nv_conc_init();
    g = (NvGroup *)calloc(1, sizeof(NvGroup));
    if (!g) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&g->m);
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_WG, g);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

static nv nv_group_add(nv h, nv n) {
    NvGroup *g = (NvGroup *)nv_handle_of(h, NV_H_WG, "group");
    NV_MUTEX_LOCK(&g->m);
    g->count += (int)nv_as_int(n);
    if (g->count <= 0) {
        nv_unpark_all(&g->park);
    }
    NV_MUTEX_UNLOCK(&g->m);
    return nv_nil;
}

static nv nv_group_done(nv h) {
    NvGroup *g = (NvGroup *)nv_handle_of(h, NV_H_WG, "group");
    NV_MUTEX_LOCK(&g->m);
    g->count--;
    if (g->count <= 0) {
        nv_unpark_all(&g->park);
    }
    NV_MUTEX_UNLOCK(&g->m);
    return nv_nil;
}

static nv nv_group_wait(nv h) {
    NvGroup *g = (NvGroup *)nv_handle_of(h, NV_H_WG, "group");
    NV_MUTEX_LOCK(&g->m);
    while (g->count > 0) {
        nv_park_self(&g->park, &g->m);
    }
    NV_MUTEX_UNLOCK(&g->m);
    return nv_nil;
}

#endif /* NOVUS_RT_H */
