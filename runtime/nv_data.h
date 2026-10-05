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

/* The builder indexes with int (as a string's length does), so the largest
 * text it can hold is INT_MAX bytes, terminator included. */
#define NV_SB_MAX_BYTES ((size_t)INT_MAX)

static void nv_error(const char *fmt, ...); /* defined below */

/* Raises the catchable "text is longer" error when `bytes` do not fit into a
 * string (every builder and concatenation checks before it allocates). */
static void nv_check_text_length(size_t bytes) {
    if (bytes > NV_SB_MAX_BYTES - 1) {
        nv_error("text is longer than %lld bytes", (long long)(NV_SB_MAX_BYTES - 1));
    }
}

static void nv_sb_grow(NvSb *sb, size_t needed) {
    size_t cap = (size_t)sb->cap;
    if (needed > NV_SB_MAX_BYTES) {
        free(sb->buf);
        sb->buf = 0;
        nv_check_text_length(needed);
    }
    while (cap < needed) {
        cap *= 2;
    }
    if (cap > NV_SB_MAX_BYTES) {
        cap = NV_SB_MAX_BYTES;
    }
    sb->buf = (char *)realloc(sb->buf, cap);
    sb->cap = (int)cap;
}

static void nv_sb_addn(NvSb *sb, const char *s, int n) {
    size_t needed = (size_t)sb->len + (size_t)n + 1;
    if (needed > (size_t)sb->cap) {
        nv_sb_grow(sb, needed);
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

/* Set by nv_try_run(): a runtime error unwinds to it instead of ending the
 * program, so a server can answer one failed request and go on. */
static NV_TLS jmp_buf *nv_trap = 0;
#define NV_TRAP_MESSAGE_BYTES 4096 /* longer messages are cut, not overrun */
static NV_TLS char nv_trap_message[NV_TRAP_MESSAGE_BYTES];
static NV_TLS int nv_trap_failed = 0;

static void nv_error(const char *fmt, ...) {
    va_list ap;
    if (nv_trap) {
        va_start(ap, fmt);
        vsnprintf(nv_trap_message, sizeof(nv_trap_message), fmt, ap);
        va_end(ap);
        longjmp(*nv_trap, 1);
    }
    fflush(stdout);
    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    nv_terminate(1);
}

/* The failure state of the enclosing tryRun: code that traps errors for its
 * own purpose (json.tryParse) saves it first and puts it back afterwards, so
 * tryFailed() and tryError() keep describing the task's failure. The message
 * only matters while a failure is recorded, so nothing is copied otherwise
 * (a trapped parse of valid text is the common case). */
typedef struct NvTrapState {
    char *message; /* a copy of the recorded message, or 0 when none is recorded */
    int failed;
} NvTrapState;

static void nv_trap_save(NvTrapState *state) {
    state->failed = nv_trap_failed;
    state->message = nv_trap_failed ? nv_strndup(nv_trap_message, strlen(nv_trap_message)) : 0;
}

static void nv_trap_restore(const NvTrapState *state) {
    nv_trap_failed = state->failed;
    if (state->message) {
        strcpy(nv_trap_message, state->message);
    }
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

/* A value whose payload is a number and nothing else. It holds no pointer,
 * and no value ever changes its type after it is made, so the collector can
 * be told never to look inside it: an ATOMIC cell is swept like any other
 * but is not walked when marking. Numbers are what a program allocates most
 * of, so this is most of the marking work. */
static nv nv_new_number(int type) {
    nv v = (nv)nv_alloc_atomic(sizeof(NvVal));
    memset(v, 0, sizeof(NvVal));
    v->type = (unsigned char)type;
    return v;
}

static nv nv_int(long long i) {
    nv v;
    if (i > -NV_TAG_LIMIT && i < NV_TAG_LIMIT) {
        return (nv)(uintptr_t)(((uintptr_t)i << 1) | 1u);
    }
    v = nv_new_number(NV_INT);
    v->i = i;
    return v;
}

static int nv_exit_code(nv v) { return nv_type_of(v) == NV_INT ? (int)nv_ival(v) : 0; }

static inline nv nv_float(double f) {
    nv v = nv_float_immediate(f);
    if (v) {
        return v;
    }
    v = nv_new_number(NV_FLOAT);
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
    m->ord = 0;
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

/* A sorted map this big gets a position list when a key arrives out of
 * order (see NvMap); below it sorting everything again is as cheap. */
#define NV_MAP_ORD_MIN 64

/* The position list: its first int is the number of items it covers. */
static int nv_map_nord(const NvMap *m) { return m->ord[0]; }
static int *nv_map_ord_slots(NvMap *m) { return m->ord + 1; }

/* The position list follows the entry table when that grows. */
static void nv_map_grow_ord(NvMap *m) {
    int *ord;
    if (!m->ord) {
        return;
    }
    ord = (int *)nv_alloc_atomic(sizeof(int) * ((size_t)m->cap + 1));
    memcpy(ord, m->ord, sizeof(int) * ((size_t)nv_map_nord(m) + 1));
    m->ord = ord;
}

/* Called when the entry that was just appended breaks the key order of the
 * items: everything before it is in order, so it becomes the sorted part of
 * a position list and the new entry the pending one. */
static void nv_map_unsort(NvMap *m) {
    int i;
    m->sorted = 0;
    if (m->len - 1 < NV_MAP_ORD_MIN) {
        return;
    }
    m->ord = (int *)nv_alloc_atomic(sizeof(int) * ((size_t)m->cap + 1));
    m->ord[0] = m->len - 1;
    for (i = 0; i < m->len - 1; i++) {
        nv_map_ord_slots(m)[i] = i;
    }
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
        nv_map_grow_ord(m);
    }
    m->items[m->len].key = key;
    m->items[m->len].val = val;
    m->len++;
    if (m->sorted && m->len > 1 && strcmp(m->items[m->len - 2].key, key) > 0) {
        nv_map_unsort(m);
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

/* The index slot that holds item `at`. */
static unsigned nv_map_slot_of(NvMap *m, int at) {
    unsigned slot = nv_key_hash(m->items[at].key) & (unsigned)m->mask;
    while (m->index[slot] != at + 1) {
        slot = (slot + 1) & (unsigned)m->mask;
    }
    return slot;
}

/* Empties the index slot `hole` of a linear probing table: the entries of
 * the run behind it that probed past it move up, so no lookup stops early. */
static void nv_map_index_close(NvMap *m, unsigned hole) {
    unsigned next = (hole + 1) & (unsigned)m->mask;
    while (m->index[next]) {
        unsigned wanted = nv_key_hash(m->items[m->index[next] - 1].key) & (unsigned)m->mask;
        unsigned probed = (next - wanted) & (unsigned)m->mask;
        unsigned gap = (next - hole) & (unsigned)m->mask;
        if (probed >= gap) {
            m->index[hole] = m->index[next];
            hole = next;
        }
        next = (next + 1) & (unsigned)m->mask;
    }
    m->index[hole] = 0;
}

/* Back to plain items in no particular order: the next read that shows the
 * order sorts them. */
static void nv_map_drop_ord(NvMap *m) {
    m->ord = 0;
    m->sorted = 0;
}

/* Taking an entry out costs the length of one probe run, not of the map: the
 * last entry fills the gap. The order of the entries is only ever shown
 * sorted (nv_map_order), so where an entry sits does not matter. */
static void nv_map_remove(NvMap *m, const char *key) {
    int at = nv_map_find(m, key);
    int last;
    if (at < 0) {
        return;
    }
    last = m->len - 1;
    if (m->ord && (at != last || last < nv_map_nord(m))) {
        nv_map_drop_ord(m);  /* keeping the list right would cost O(n) per removal */
    }
    if (m->index) {
        nv_map_index_close(m, nv_map_slot_of(m, at));
    }
    if (at != last) {
        if (m->index) {
            m->index[nv_map_slot_of(m, last)] = at + 1;
        }
        m->items[at] = m->items[last];
        m->sorted = 0;
    }
    memset(m->items + last, 0, sizeof(NvEntry));
    m->len--;
}

static int nv_entry_cmp(const void *a, const void *b) {
    return strcmp(((const NvEntry *)a)->key, ((const NvEntry *)b)->key);
}

/* Pending entries up to this many are put into the position list one by
 * one; more are cheaper to sort together with everything else. */
#define NV_MAP_INSERT_MAX 32

/* The first place in ord whose key is not below `key`. */
static int nv_map_ord_lower_bound(NvMap *m, const char *key) {
    int low = 0;
    int high = nv_map_nord(m);
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (strcmp(m->items[nv_map_ord_slots(m)[mid]].key, key) < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

static void nv_map_ord_place_pending(NvMap *m) {
    while (nv_map_nord(m) < m->len) {
        int covered = nv_map_nord(m);
        int slot = nv_map_ord_lower_bound(m, m->items[covered].key);
        int *slots = nv_map_ord_slots(m);
        memmove(slots + slot + 1, slots + slot, sizeof(int) * (size_t)(covered - slot));
        slots[slot] = covered;
        m->ord[0] = covered + 1;
    }
}

static int nv_map_in_order(NvMap *m) { return m->ord ? nv_map_nord(m) == m->len : m->sorted; }

static void nv_map_sort_items(NvMap *m) {
    qsort(m->items, (size_t)m->len, sizeof(NvEntry), nv_entry_cmp);
    m->sorted = 1;
    m->ord = 0;
    if (m->index) {
        nv_map_reindex(m, m->mask + 1);
    }
}

/* Every read that exposes the order sorts first, and reads the entries
 * through nv_map_nth afterwards. */
static void nv_map_order(NvMap *m) {
    if (nv_map_in_order(m)) {
        return;
    }
    if (m->ord && m->len - nv_map_nord(m) <= NV_MAP_INSERT_MAX) {
        nv_map_ord_place_pending(m);
        return;
    }
    nv_map_sort_items(m);
}

/* The entry that is number `i` in key order; only valid after nv_map_order. */
static NvEntry *nv_map_nth(NvMap *m, int i) { return m->items + (m->ord ? nv_map_ord_slots(m)[i] : i); }

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
