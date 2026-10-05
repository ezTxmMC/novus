/* nv_json_write.h - json.stringify and json.pretty. Part of the Novus runtime; included by nv_json.h.
 * Depends on: nv_json_text.h (strings, numbers), nv_data.h (NvSb), nv_display.h (visit stack), nv_classes.h (fields). */
#ifndef NV_JSON_WRITE_H
#define NV_JSON_WRITE_H

#include "nv_json_text.h"

/* ------------------------------------------------------------------ */
/* json writer                                                         */
/* ------------------------------------------------------------------ */

#define NV_JSON_INDENT_STEP 2

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

/* What comes before member `index`: the comma (not before the first) and the line break. */
static void nv_json_write_separator(NvSb *sb, int index, int indent, int depth) {
    if (index > 0) {
        nv_sb_addc(sb, ',');
    }
    nv_json_indent(sb, indent, depth + 1);
}

static void nv_json_write_colon(NvSb *sb, int indent) { nv_sb_add(sb, indent > 0 ? ": " : ":"); }

static void nv_json_write_close(NvSb *sb, char closing, int indent, int depth) {
    nv_json_indent(sb, indent, depth);
    nv_sb_addc(sb, closing);
}

static int nv_json_write(NvSb *sb, nv value, int indent, int depth);

static void nv_json_write_scalar(NvSb *sb, nv value) {
    switch (nv_type_of(value)) {
    case NV_INT:
        nv_sb_add(sb, nv_fmt_int(nv_ival(value)));
        return;
    case NV_FLOAT:
        nv_json_float(sb, nv_fval(value));
        return;
    case NV_BOOL:
        nv_sb_add(sb, nv_ival(value) ? "true" : "false");
        return;
    case NV_STR:
        nv_json_string_n(sb, value->s, value->slen);
        return;
    default:
        nv_sb_add(sb, "null");
    }
}

static int nv_json_write_array(NvSb *sb, NvArr *items, int indent, int depth) {
    int i;
    if (items->len == 0) {
        nv_sb_add(sb, "[]");
        return 1;
    }
    nv_sb_addc(sb, '[');
    for (i = 0; i < items->len; i++) {
        nv_json_write_separator(sb, i, indent, depth);
        if (!nv_json_write(sb, items->items[i], indent, depth + 1)) {
            return 0;
        }
    }
    nv_json_write_close(sb, ']', indent, depth);
    return 1;
}

static int nv_json_write_object(NvSb *sb, NvObj *obj, int indent, int depth) {
    int count = nv_class_field_count(obj->cls);
    int *order = nv_field_order(obj->cls, count);
    const char *constant_name = nv_obj_name(obj);
    int i;
    if (constant_name && count == 0) {
        nv_json_string(sb, constant_name); /* an enum constant is its name */
        return 1;
    }
    if (count == 0) {
        nv_sb_add(sb, "{}");
        return 1;
    }
    nv_sb_addc(sb, '{');
    for (i = 0; i < count; i++) {
        nv_json_write_separator(sb, i, indent, depth);
        nv_json_string(sb, nv_field_name_at(obj->cls, order[i], 0));
        nv_json_write_colon(sb, indent);
        if (!nv_json_write(sb, nv_fields(obj)[order[i]], indent, depth + 1)) {
            return 0;
        }
    }
    nv_json_write_close(sb, '}', indent, depth);
    return 1;
}

static int nv_json_write_map(NvSb *sb, NvMap *map, int indent, int depth) {
    int i;
    nv_map_order(map);
    if (map->len == 0) {
        nv_sb_add(sb, "{}");
        return 1;
    }
    nv_sb_addc(sb, '{');
    for (i = 0; i < map->len; i++) {
        nv_json_write_separator(sb, i, indent, depth);
        nv_json_string(sb, nv_map_nth(map, i)->key);
        nv_json_write_colon(sb, indent);
        if (!nv_json_write(sb, nv_map_nth(map, i)->val, indent, depth + 1)) {
            return 0;
        }
    }
    nv_json_write_close(sb, '}', indent, depth);
    return 1;
}

static int nv_json_write_container(NvSb *sb, nv value, int indent, int depth) {
    switch (nv_type_of(value)) {
    case NV_ARR:
        return nv_json_write_array(sb, value->a, indent, depth);
    case NV_OBJ:
        return nv_json_write_object(sb, value->o, indent, depth);
    default:
        return nv_json_write_map(sb, value->m, indent, depth);
    }
}

/* 0 when the value nests deeper than NV_JSON_MAX_DEPTH. The containers
 * entered so far stay on the visit stack then; nv_json_dump unwinds it. */
static int nv_json_write(NvSb *sb, nv value, int indent, int depth) {
    const void *id = nv_container_id(value);
    int written;
    if (!id) {
        nv_json_write_scalar(sb, value);
        return 1;
    }
    if (depth >= NV_JSON_MAX_DEPTH) {
        return 0;
    }
    if (!nv_visit_enter(id)) {
        nv_sb_add(sb, "null"); /* reference cycle */
        return 1;
    }
    written = nv_json_write_container(sb, value, indent, depth);
    if (written) {
        nv_visit_leave();
    }
    return written;
}

static nv nv_json_dump(nv value, int indent) {
    NvSb sb;
    int len;
    int visiting = nv_visit_depth;
    nv_sb_init(&sb);
    if (!nv_json_write(&sb, value, indent, 0)) {
        nv_visit_depth = visiting;
        free(sb.buf);
        nv_error("json stringify error: nesting is deeper than %d levels", NV_JSON_MAX_DEPTH);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_json_stringify(nv value) { return nv_json_dump(value, 0); }

static nv nv_json_pretty(nv value) { return nv_json_dump(value, NV_JSON_INDENT_STEP); }

#endif /* NV_JSON_WRITE_H */
