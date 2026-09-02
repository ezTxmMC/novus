/* nv_classes.h - classes, objects and enums. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_CLASSES_H
#define NV_CLASSES_H

/* ------------------------------------------------------------------ */
/* Classes, objects, enums                                             */
/* ------------------------------------------------------------------ */

/* Classes and everything hanging off them - names, method tables, enum
 * constants, defaults - live in root blocks: outside the collected heap,
 * scanned in full by every collection, never freed. */
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
    NvClass *c = (NvClass *)nv_root_alloc(sizeof(NvClass));
    c->totalFields = -1;
    c->name = name;
    c->base = base && base[0] ? base : 0;
    c->isAbstract = isAbstract;
    c->isEnum = isEnum;
    c->fieldCap = 8;
    c->fieldNames = (const char **)nv_root_alloc(sizeof(char *) * 8);
    c->fieldTypes = (const char **)nv_root_alloc(sizeof(char *) * 8);
    c->methodCap = 8;
    c->methods = (NvMethod *)nv_root_alloc(sizeof(NvMethod) * 8);
    c->constants = nv_map_new();
    c->constantOrder = nv_arr_new();
    if (nv_nclasses == nv_class_cap) {
        int cap = nv_class_cap ? nv_class_cap * 2 : 16;
        nv_classes = (NvClass **)nv_root_realloc(nv_classes, sizeof(NvClass *) * (size_t)nv_class_cap,
                                                 sizeof(NvClass *) * (size_t)cap);
        nv_class_cap = cap;
    }
    nv_classes[nv_nclasses++] = c;
    return c;
}

static void nv_class_field(NvClass *c, const char *name, const char *type) {
    if (c->nfields == c->fieldCap) {
        size_t old = sizeof(char *) * (size_t)c->fieldCap;
        c->fieldNames = (const char **)nv_root_realloc(c->fieldNames, old, old * 2);
        c->fieldTypes = (const char **)nv_root_realloc(c->fieldTypes, old, old * 2);
        c->fieldCap *= 2;
    }
    c->fieldNames[c->nfields] = name;
    c->fieldTypes[c->nfields] = type;
    c->nfields++;
}

static void nv_class_method(NvClass *c, const char *name, int arity, NvMethodFn fn) {
    if (c->nmethods == c->methodCap) {
        size_t old = sizeof(NvMethod) * (size_t)c->methodCap;
        c->methods = (NvMethod *)nv_root_realloc(c->methods, old, old * 2);
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
    c->order = (int *)nv_root_alloc(sizeof(int) * (size_t)(count < 1 ? 1 : count));
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
    c->defaults = (nv *)nv_root_alloc(sizeof(nv) * (size_t)(count < 1 ? 1 : count));
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
    c->flatTypes = (const char **)nv_root_alloc(sizeof(const char *) * (size_t)count);
    c->flatKinds = (signed char *)nv_root_alloc(sizeof(signed char) * (size_t)count);
    for (i = 0; i < nv_class_field_count(c); i++) {
        const char *type = 0;
        nv_field_name_at(c, i, &type);
        c->flatTypes[i] = type;
        c->flatKinds[i] = type ? nv_type_kind(type) : 0;
    }
}

/* Value, object header and field slots live in one heap block. */
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


#endif /* NV_CLASSES_H */
