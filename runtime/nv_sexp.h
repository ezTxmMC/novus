/* nv_sexp.h - the compiler's string-encoded AST and tokens (compiler/ast, compiler/lexer). Part of the Novus runtime; included by novus_rt.h.
 * Depends on: nv_strings.h (nv_substr), nv_memory.h (nv_gc_epoch). The natives are only called by the compiler, so a program that
 * does not use them would get an unused-function warning for each: NV_UNUSED. */
#ifndef NV_SEXP_H
#define NV_SEXP_H

/* ------------------------------------------------------------------ */
/* S-expression nodes                                                  */
/* ------------------------------------------------------------------ */

/* The compiler keeps its syntax tree as strings - `(call f (str "a") 1)` -
 * and every node is a substring view of its parent. Asking for the n-th
 * child of a node used to scan the node from its start, character by
 * character, so walking a block of k statements cost k scans of the whole
 * block. Here a node is scanned once: the places where its children start and
 * end are remembered, and the next question about the same node is an array
 * read. The memory is a small per-thread cache keyed on the bytes' address
 * and length. The bytes of a string never change, but their memory can be
 * handed out again, so the cache is emptied when a collection has run since
 * it was filled (nv_gc_epoch).
 *
 * Two kinds of nodes live in it. A short node is cheap to scan again, so the
 * cache keeps it in a direct mapped table where a newer node may take its
 * place. A long node - a block of thousands of statements - is not: it is
 * asked about once per statement, with a lot of short nodes asked about in
 * between, and losing its bounds would turn the walk quadratic. It stays
 * until the next collection. */

#define NV_SEXP_SMALL_SLOTS 512      /* a power of two */
#define NV_SEXP_PINNED_LENGTH 512    /* from this many bytes a node stays remembered */
#define NV_SEXP_PINNED_BUCKETS 1024  /* a power of two */
#define NV_SEXP_PINNED_LIMIT 65536   /* long nodes remembered at most, between two collections */
#define NV_SEXP_MIN_LENGTH 2         /* shorter text has no child: the scan starts after the first byte */
#define NV_SEXP_INLINE_CHILDREN 8    /* children whose bounds need no allocation */
#define NV_SEXP_HASH_SPREAD 2654435761u

/* A hook for the unit test of the runtime: it counts the bytes the scans read. */
#ifndef NV_SEXP_COUNT_SCAN
#define NV_SEXP_COUNT_SCAN(bytes) ((void)(bytes))
#endif

typedef struct NvSexpNode {
    const char *text;
    int length;
    int count;          /* children found so far */
    int capacity;       /* children the bounds array holds */
    int complete;       /* the closing parenthesis or the end of the text was reached */
    long long resume;   /* where the scan for the next child continues */
    int *bounds;        /* start and end of each child found */
    struct NvSexpNode *next; /* the next long node of the same bucket */
    int inlineBounds[2 * NV_SEXP_INLINE_CHILDREN];
} NvSexpNode;

typedef struct NvSexpCache {
    long long epoch;    /* the collection the entries were made after */
    int pinnedCount;
    NvSexpNode small[NV_SEXP_SMALL_SLOTS];
    NvSexpNode *pinned[NV_SEXP_PINNED_BUCKETS];
} NvSexpCache;

static NV_TLS NvSexpCache *nv_sexp_cache = 0;

/* The index just past the list that starts at text[at], a parenthesis:
 * strings inside it may hold parentheses. An unterminated list ends at the
 * end of the text. */
static long long nv_sexp_list_end(const char *text, long long length, long long at) {
    long long depth = 0;
    int inQuote = 0;
    long long start = at;
    while (at < length) {
        char c = text[at];
        if (inQuote && c == '\\') {
            at += 2;
            continue;
        }
        if (inQuote) {
            inQuote = c != '"';
            at++;
            continue;
        }
        inQuote = c == '"';
        depth += c == '(';
        depth -= c == ')';
        at++;
        if (c == ')' && depth == 0) {
            NV_SEXP_COUNT_SCAN(at - start);
            return at;
        }
    }
    NV_SEXP_COUNT_SCAN(length - start);
    return length;
}

/* The index just past the quoted string that starts at text[at]; an
 * unterminated one may end past the text, as it always did. */
static long long nv_sexp_quote_end(const char *text, long long length, long long at) {
    long long start = at;
    at++;
    while (at < length && text[at] != '"') {
        if (text[at] == '\\') {
            at++;
        }
        at++;
    }
    NV_SEXP_COUNT_SCAN(at - start);
    return at + 1;
}

/* The index just past the bare atom that starts at text[at]. */
static long long nv_sexp_bare_end(const char *text, long long length, long long at) {
    long long start = at;
    while (at < length && text[at] != ' ' && text[at] != ')') {
        at++;
    }
    NV_SEXP_COUNT_SCAN(at - start);
    return at;
}

/* The index just past the node that starts at text[at]. */
static long long nv_sexp_node_end(const char *text, long long length, long long at) {
    if (text[at] == '(') {
        return nv_sexp_list_end(text, length, at);
    }
    if (text[at] == '"') {
        return nv_sexp_quote_end(text, length, at);
    }
    return nv_sexp_bare_end(text, length, at);
}

static size_t nv_sexp_hash(const char *text, int length) {
    return ((size_t)(uintptr_t)text >> 3) ^ ((size_t)length * NV_SEXP_HASH_SPREAD);
}

static void nv_sexp_drop_bounds(NvSexpNode *node) {
    if (node->bounds && node->bounds != node->inlineBounds) {
        free(node->bounds);
    }
    node->bounds = 0;
}

static void nv_sexp_reset(NvSexpNode *node, const char *text, int length) {
    nv_sexp_drop_bounds(node);
    node->text = text;
    node->length = length;
    node->count = 0;
    node->capacity = NV_SEXP_INLINE_CHILDREN;
    node->complete = 0;
    node->resume = 1; /* the head of the list starts after its parenthesis */
    node->bounds = node->inlineBounds;
}

/* Forgets every node. */
static void nv_sexp_flush(NvSexpCache *cache) {
    int i;
    for (i = 0; i < NV_SEXP_SMALL_SLOTS; i++) {
        nv_sexp_drop_bounds(&cache->small[i]);
        cache->small[i].text = 0;
    }
    for (i = 0; i < NV_SEXP_PINNED_BUCKETS; i++) {
        NvSexpNode *node = cache->pinned[i];
        while (node) {
            NvSexpNode *next = node->next;
            nv_sexp_drop_bounds(node);
            free(node);
            node = next;
        }
        cache->pinned[i] = 0;
    }
    cache->pinnedCount = 0;
    cache->epoch = nv_gc_epoch;
}

/* The cache of this thread: made when first needed, emptied when a collection
 * has run since it was filled. */
static NvSexpCache *nv_sexp_current(void) {
    if (!nv_sexp_cache) {
        nv_sexp_cache = (NvSexpCache *)calloc(1, sizeof(NvSexpCache));
        if (!nv_sexp_cache) {
            nv_error("out of memory");
        }
        nv_sexp_cache->epoch = nv_gc_epoch;
    }
    if (nv_sexp_cache->epoch != nv_gc_epoch) {
        nv_sexp_flush(nv_sexp_cache);
    }
    return nv_sexp_cache;
}

/* Gives the memory of this thread's cache back; called when a thread ends,
 * for the cache is thread local and nothing else would ever free it. */
static void nv_sexp_release(void) {
    if (!nv_sexp_cache) {
        return;
    }
    nv_sexp_flush(nv_sexp_cache);
    free(nv_sexp_cache);
    nv_sexp_cache = 0;
}

static NvSexpNode *nv_sexp_small_node(NvSexpCache *cache, const char *text, int length) {
    NvSexpNode *node = &cache->small[nv_sexp_hash(text, length) & (NV_SEXP_SMALL_SLOTS - 1)];
    if (node->text != text || node->length != length) {
        nv_sexp_reset(node, text, length);
    }
    return node;
}

static NvSexpNode *nv_sexp_pinned_node(NvSexpCache *cache, const char *text, int length) {
    size_t bucket = nv_sexp_hash(text, length) & (NV_SEXP_PINNED_BUCKETS - 1);
    NvSexpNode *node;
    for (node = cache->pinned[bucket]; node; node = node->next) {
        if (node->text == text && node->length == length) {
            return node;
        }
    }
    if (cache->pinnedCount >= NV_SEXP_PINNED_LIMIT) {
        nv_sexp_flush(cache);
    }
    node = (NvSexpNode *)calloc(1, sizeof(NvSexpNode));
    if (!node) {
        nv_error("out of memory");
    }
    nv_sexp_reset(node, text, length);
    node->next = cache->pinned[bucket];
    cache->pinned[bucket] = node;
    cache->pinnedCount++;
    return node;
}

static NvSexpNode *nv_sexp_node(const char *text, int length) {
    NvSexpCache *cache = nv_sexp_current();
    if (length < NV_SEXP_PINNED_LENGTH) {
        return nv_sexp_small_node(cache, text, length);
    }
    return nv_sexp_pinned_node(cache, text, length);
}

static void nv_sexp_remember(NvSexpNode *node, long long start, long long end) {
    if (node->count == node->capacity) {
        int capacity = node->capacity * 2;
        int *grown = (int *)malloc(sizeof(int) * 2 * (size_t)capacity);
        if (!grown) {
            nv_error("out of memory");
        }
        memcpy(grown, node->bounds, sizeof(int) * 2 * (size_t)node->count);
        if (node->bounds != node->inlineBounds) {
            free(node->bounds);
        }
        node->bounds = grown;
        node->capacity = capacity;
    }
    node->bounds[2 * node->count] = (int)start;
    node->bounds[2 * node->count + 1] = (int)(end < node->length ? end : node->length);
    node->count++;
}

/* Scans on until the node knows its child number `want` (or all of them). */
static void nv_sexp_scan(NvSexpNode *node, long long want) {
    const char *text = node->text;
    long long length = node->length;
    long long at = node->resume;
    while (!node->complete && node->count <= want) {
        long long start;
        while (at < length && text[at] == ' ') {
            at++;
        }
        if (at >= length || text[at] == ')') {
            node->complete = 1;
            break;
        }
        start = at;
        at = nv_sexp_node_end(text, length, at);
        nv_sexp_remember(node, start, at);
    }
    node->resume = at;
}

static void nv_sexp_check(nv s, const char *what) {
    if (nv_type_of(s) != NV_STR) {
        nv_error("%s expects a string but got %s", what, nv_type_name(s));
    }
}

/* ast.nodeChild: the index-th child of a list node (0 is the head), "" if absent. */
static NV_UNUSED nv nv_sexp_child(nv s, nv index) {
    NvSexpNode *node;
    long long want = nv_as_int(index);
    nv_sexp_check(s, "nodeChild");
    if (want < 0 || s->slen < NV_SEXP_MIN_LENGTH) {
        return nv_str("");
    }
    node = nv_sexp_node(s->s, s->slen);
    nv_sexp_scan(node, want);
    if (want >= node->count) {
        return nv_str("");
    }
    return nv_substr(s, node->bounds[2 * want], node->bounds[2 * want + 1]);
}

/* ast.nodeCount: the number of children of a list node, the head included. */
static NV_UNUSED nv nv_sexp_count(nv s) {
    NvSexpNode *node;
    nv_sexp_check(s, "nodeCount");
    if (s->slen < NV_SEXP_MIN_LENGTH) {
        return nv_int(0);
    }
    node = nv_sexp_node(s->s, s->slen);
    nv_sexp_scan(node, (long long)1 << 40);
    return nv_int(node->count);
}

/* ast.isList */
static NV_UNUSED nv nv_sexp_is_list(nv s) {
    nv_sexp_check(s, "isList");
    return nv_bool(s->slen > 0 && s->s[0] == '(');
}

/* ast.nodeHead: the first child of a list, "" for anything else. */
static NV_UNUSED nv nv_sexp_head(nv s) {
    nv_sexp_check(s, "nodeHead");
    if (s->slen == 0 || s->s[0] != '(') {
        return nv_str("");
    }
    return nv_sexp_child(s, nv_int(0));
}

/* ast.atomEnd: the index just past the node that starts at s[at]. */
static NV_UNUSED nv nv_sexp_atom_end(nv s, nv at) {
    long long start = nv_as_int(at);
    nv_sexp_check(s, "atomEnd");
    if (start < 0 || start >= s->slen) {
        nv_error("charAt index %lld out of bounds (length %d)", start, s->slen);
    }
    return nv_int(nv_sexp_node_end(s->s, s->slen, start));
}

/* ------------------------------------------------------------------ */
/* Quoted strings and tokens                                           */
/* ------------------------------------------------------------------ */

/* How many bytes a character takes in a quoted string atom (ast.sexpStr). */
static int nv_sexp_escaped_size(char c) {
    switch (c) {
    case '\\':
    case '"':
    case '\n':
    case '\r':
    case '\t':
    case '?': /* keeps "??" out of the generated C (trigraphs) */
        return 2;
    default:
        return 1;
    }
}

static char *nv_sexp_put_escaped(char *out, char c) {
    switch (c) {
    case '\\':
        *out++ = '\\';
        *out++ = '\\';
        return out;
    case '"':
        *out++ = '\\';
        *out++ = '"';
        return out;
    case '\n':
        *out++ = '\\';
        *out++ = 'n';
        return out;
    case '\r':
        *out++ = '\\';
        *out++ = 'r';
        return out;
    case '\t':
        *out++ = '\\';
        *out++ = 't';
        return out;
    case '?':
        *out++ = '\\';
        *out++ = '?';
        return out;
    default:
        *out++ = c;
        return out;
    }
}

/* ast.sexpStr: text as a quoted s-expression string atom. */
static NV_UNUSED nv nv_sexp_str(nv text) {
    long long size = 2;
    long long i;
    char *buf;
    char *out;
    nv_sexp_check(text, "sexpStr");
    for (i = 0; i < text->slen; i++) {
        size += nv_sexp_escaped_size(text->s[i]);
    }
    nv_check_text_length((size_t)size);
    buf = (char *)nv_alloc_atomic((size_t)size + 1);
    out = buf;
    *out++ = '"';
    for (i = 0; i < text->slen; i++) {
        out = nv_sexp_put_escaped(out, text->s[i]);
    }
    *out++ = '"';
    *out = 0;
    return nv_str_own(buf, (int)size);
}

/* ast.sexpUnescape: the text a quoted string atom stands for. */
static NV_UNUSED nv nv_sexp_unescape(nv atom) {
    long long length = atom->slen - 2;
    const char *text;
    char *buf;
    long long in = 0;
    long long out = 0;
    nv_sexp_check(atom, "sexpUnescape");
    if (length <= 0) {
        return nv_str("");
    }
    text = atom->s + 1;
    buf = (char *)nv_alloc_atomic((size_t)length + 1);
    while (in < length) {
        char c = text[in];
        if (c != '\\' || in + 1 >= length) {
            buf[out++] = c;
            in++;
            continue;
        }
        c = text[in + 1];
        buf[out++] = c == 'n' ? '\n' : (c == 'r' ? '\r' : (c == 't' ? '\t' : c));
        in += 2;
    }
    buf[out] = 0;
    return nv_str_own(buf, (int)out);
}

/* A token is "KIND file:line value". The index of the first space at or
 * after `from`; a token without one is malformed and fails the way reading
 * past the end of the string does. */
static long long nv_token_space(nv token, long long from) {
    long long at = from;
    while (at < token->slen && token->s[at] != ' ') {
        at++;
    }
    if (at >= token->slen) {
        nv_error("charAt index %lld out of bounds (length %d)", at, token->slen);
    }
    return at;
}

/* lexer.tokKind */
static NV_UNUSED nv nv_token_kind(nv token) {
    nv_sexp_check(token, "tokKind");
    return nv_substr(token, 0, nv_token_space(token, 0));
}

/* lexer.tokPos */
static NV_UNUSED nv nv_token_pos(nv token) {
    long long start;
    nv_sexp_check(token, "tokPos");
    start = nv_token_space(token, 0) + 1;
    return nv_substr(token, start, nv_token_space(token, start));
}

/* lexer.tokVal */
static NV_UNUSED nv nv_token_value(nv token) {
    long long start;
    nv_sexp_check(token, "tokVal");
    start = nv_token_space(token, nv_token_space(token, 0) + 1) + 1;
    return nv_substr(token, start, token->slen);
}

#endif /* NV_SEXP_H */
