/* nv_invoke.h - method calls on any value and iteration. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_INVOKE_H
#define NV_INVOKE_H

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
    a->cap = cap < 1 ? 1 : cap;
    a->items = nv_items_alloc(a->cap);
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


#endif /* NV_INVOKE_H */
