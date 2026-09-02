/* nv_json.h - the json module. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_JSON_H
#define NV_JSON_H

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
            nv_map_set_static(m->m, nv_cstr(key), val); /* a private copy, never extended */
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



#endif /* NV_JSON_H */
