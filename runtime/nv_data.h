/* nv_data.h - string builder, errors, value constructors, arrays and maps. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_DATA_H
#define NV_DATA_H

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
 * two, and no padding wasted between them. The block is ATOMIC: its only
 * pointer is `s`, which points into the block itself. */
static nv nv_strn(const char *s, int n) {
    nv v;
    char *bytes;
    if (n == 1 && nv_char_table[(unsigned char)s[0]]) {
        return nv_char_table[(unsigned char)s[0]];
    }
    if (n <= 0) {
        return &nv_empty_str_val;
    }
    v = (nv)nv_alloc_atomic(sizeof(NvVal) + (size_t)n + 1);
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

/* Tables that are filled in later are zeroed on allocation: a cell comes
 * back from the collector with whatever its last owner left in it, and a
 * stale pointer in an unused slot would keep that garbage alive. */
static nv *nv_items_alloc(int cap) {
    nv *items = (nv *)nv_alloc(sizeof(nv) * (size_t)cap);
    memset(items, 0, sizeof(nv) * (size_t)cap);
    return items;
}

static NvArr *nv_arr_new_cap(int cap) {
    NvArr *a = (NvArr *)nv_alloc(sizeof(NvArr));
    a->len = 0;
    a->cap = cap < 4 ? 4 : cap;
    a->items = nv_items_alloc(a->cap);
    return a;
}

static NvArr *nv_arr_new(void) { return nv_arr_new_cap(4); }

/* Doubling: the old table becomes garbage the moment nothing points at it
 * any more - a loop that still walks it (for..in over a live array) keeps
 * it alive exactly as long as it needs it. */
static void nv_arr_grow(NvArr *a) {
    int cap = a->cap * 2;
    nv *items = (nv *)nv_alloc(sizeof(nv) * (size_t)cap);
    memcpy(items, a->items, sizeof(nv) * (size_t)a->len);
    memset(items + a->len, 0, sizeof(nv) * (size_t)(cap - a->len));
    a->items = items;
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
 * up front skips the growth copies. */
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
    memset(m->items, 0, sizeof(NvEntry) * (size_t)m->cap);
    m->index = 0;
    m->mask = 0;
    m->sorted = 1;
    return m;
}

static NvMap *nv_map_new(void) { return nv_map_new_cap(4); }

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
    m->index = (int *)nv_alloc_atomic(sizeof(int) * (size_t)slots); /* rebuilt from nothing: the old one is garbage */
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
        NvEntry *items = (NvEntry *)nv_alloc(sizeof(NvEntry) * (size_t)cap);
        memcpy(items, m->items, sizeof(NvEntry) * (size_t)m->len);
        memset(items + m->len, 0, sizeof(NvEntry) * (size_t)(cap - m->len));
        m->items = items;
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

/* `key` must outlive the map (string literal, class table, heap string that
 * the map itself keeps alive through this entry). */
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


#endif /* NV_DATA_H */
