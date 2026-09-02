/* nv_display.h - type names, display, numeric coercion and truthiness. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_DISPLAY_H
#define NV_DISPLAY_H

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

/* Digits back to front into a stack buffer, then one heap copy. sprintf()
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
    out = (char *)nv_alloc_atomic(len);
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


#endif /* NV_DISPLAY_H */
