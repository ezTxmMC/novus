/* nv_json_parse.h - json.parse, json.tryParse, json.load and json.save. Part of the Novus runtime; included by nv_json.h.
 * Depends on: nv_json_string.h (strings, errors), nv_json_text.h (depth limit), nv_json_write.h (stringify, pretty), nv_data.h (maps, trap state). */
#ifndef NV_JSON_PARSE_H
#define NV_JSON_PARSE_H

#include "nv_json_string.h"
#include "nv_json_write.h"

/* ------------------------------------------------------------------ */
/* json parser                                                         */
/* ------------------------------------------------------------------ */

/* Lenient on purpose where a strict reader would stop (see json.parse and
 * json.tryParse): raw control characters inside strings, "01", "1.", a bare
 * "-" and backslash escapes it does not know are read. json.isValid is the
 * strict check. */

static void nv_json_enter(NvJsonCursor *cursor) {
    if (++cursor->depth > NV_JSON_MAX_DEPTH) {
        nv_error("json parse error at position %d: nesting is deeper than %d levels", cursor->pos, NV_JSON_MAX_DEPTH);
    }
}

static nv nv_json_parse_value(NvJsonCursor *cursor);

/* Key and colon. A key is a C string inside the map, so one with a NUL byte
 * (raw or \u0000) would be cut short and could collide with another key: it
 * is an error instead. */
static nv nv_json_parse_key(NvJsonCursor *cursor) {
    nv key;
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) != '"') {
        nv_json_fail(cursor, "expected object key");
    }
    key = nv_json_parse_string(cursor);
    if (memchr(key->s, 0, (size_t)key->slen)) {
        nv_json_fail(cursor, "object key contains a NUL byte");
    }
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) != ':') {
        nv_json_fail(cursor, "expected ':'");
    }
    cursor->pos++;
    return key;
}

/* After a member: 1 after a comma, 0 after the closing character. */
static int nv_json_more_members(NvJsonCursor *cursor, char closing, const char *expected) {
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) == ',') {
        cursor->pos++;
        return 1;
    }
    if (nv_json_peek(cursor) != closing) {
        nv_json_fail(cursor, expected);
    }
    cursor->pos++;
    return 0;
}

static nv nv_json_parse_object(NvJsonCursor *cursor) {
    nv map = nv_map();
    nv_json_enter(cursor);
    cursor->pos++;
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) == '}') {
        cursor->pos++;
        cursor->depth--;
        return map;
    }
    do {
        nv key = nv_json_parse_key(cursor);
        nv value = nv_json_parse_value(cursor);
        nv_map_set_static(map->m, nv_cstr(key), value); /* a private copy, never extended */
    } while (nv_json_more_members(cursor, '}', "expected ',' or '}'"));
    cursor->depth--;
    return map;
}

static nv nv_json_parse_array(NvJsonCursor *cursor) {
    nv array = nv_arr();
    nv_json_enter(cursor);
    cursor->pos++;
    nv_json_skip_space(cursor);
    if (nv_json_peek(cursor) == ']') {
        cursor->pos++;
        cursor->depth--;
        return array;
    }
    do {
        nv_arr_push(array->a, nv_json_parse_value(cursor));
    } while (nv_json_more_members(cursor, ']', "expected ',' or ']'"));
    cursor->depth--;
    return array;
}

static void nv_json_skip_exponent(NvJsonCursor *cursor) {
    cursor->pos++;
    if (nv_json_peek(cursor) == '+' || nv_json_peek(cursor) == '-') {
        cursor->pos++;
    }
    nv_json_skip_digits(cursor);
}

static nv nv_json_parse_number(NvJsonCursor *cursor) {
    int start = cursor->pos;
    int is_float = 0;
    char *text;
    if (nv_json_peek(cursor) == '-') {
        cursor->pos++;
    }
    nv_json_skip_digits(cursor);
    if (nv_json_peek(cursor) == '.') {
        is_float = 1;
        cursor->pos++;
        nv_json_skip_digits(cursor);
    }
    if (nv_json_peek(cursor) == 'e' || nv_json_peek(cursor) == 'E') {
        is_float = 1;
        nv_json_skip_exponent(cursor);
    }
    text = nv_strndup(cursor->text + start, (size_t)(cursor->pos - start));
    return is_float ? nv_float(atof(text)) : nv_int(atoll(text));
}

/* Reads true, false or null into *value: 1, or 0 when the text here is none of them. */
static int nv_json_parse_word(NvJsonCursor *cursor, nv *value) {
    if (nv_json_at_word(cursor, "true")) {
        cursor->pos += 4;
        *value = nv_bool(1);
        return 1;
    }
    if (nv_json_at_word(cursor, "false")) {
        cursor->pos += 5;
        *value = nv_bool(0);
        return 1;
    }
    if (nv_json_at_word(cursor, "null")) {
        cursor->pos += 4;
        *value = nv_nil;
        return 1;
    }
    return 0;
}

static nv nv_json_parse_value(NvJsonCursor *cursor) {
    char first;
    nv word;
    nv_json_skip_space(cursor);
    first = nv_json_peek(cursor);
    if (first == '{') {
        return nv_json_parse_object(cursor);
    }
    if (first == '[') {
        return nv_json_parse_array(cursor);
    }
    if (first == '"') {
        return nv_json_parse_string(cursor);
    }
    if (nv_json_parse_word(cursor, &word)) {
        return word;
    }
    if (first == '-' || isdigit((unsigned char)first)) {
        return nv_json_parse_number(cursor);
    }
    if (nv_json_at_end(cursor)) {
        nv_json_fail(cursor, "unexpected end of input");
    }
    nv_json_fail(cursor, "unexpected character");
    return nv_nil;
}

/* The whole text must be one value; trailing characters - after a NUL
 * byte as well - are an error. */
static nv nv_json_parse(nv text) {
    NvJsonCursor cursor;
    nv value;
    if (nv_type_of(text) != NV_STR) {
        /* already structured data: convert (objects become maps) */
        text = nv_json_stringify(text);
    }
    nv_json_open(&cursor, text);
    nv_json_skip_space(&cursor);
    if (nv_json_at_end(&cursor)) {
        nv_json_fail(&cursor, "attempting to parse an empty input");
    }
    value = nv_json_parse_value(&cursor);
    nv_json_skip_space(&cursor);
    if (!nv_json_at_end(&cursor)) {
        nv_json_fail(&cursor, "trailing characters");
    }
    return value;
}

/* json.parse, with the error trapped: the value, or nothing when the text is
 * not parseable (a valid `null` also yields nothing - isValid tells them
 * apart). Built on the same trap as nv_try_run, but an enclosing tryRun keeps
 * its failure flag and its message: both are restored afterwards. */
static nv nv_json_leave_trap(jmp_buf *outer, const NvTrapState *saved, nv result) {
    nv_trap = outer;
    nv_trap_restore(saved);
    return result;
}

static nv nv_json_try_parse(nv text) {
    NvTrapState saved;
    jmp_buf here;
    jmp_buf *outer = nv_trap;
    int visiting = nv_visit_depth;
    nv result;
    nv_trap_save(&saved);
    nv_trap = &here;
    if (setjmp(here) != 0) {
        nv_visit_depth = visiting;
        return nv_json_leave_trap(outer, &saved, nv_nil);
    }
    result = nv_json_parse(text);
    return nv_json_leave_trap(outer, &saved, result);
}

static nv nv_json_save(nv value, nv dir, nv file) {
    nv target = nv_path_join(2, dir, file);
    nv_os_mkdir(nv_path_dirname(target));
    return nv_write_file(target, nv_json_pretty(value));
}

static nv nv_json_load(nv file) {
    if (!nv_path_exists_c(nv_display(file))) {
        nv_error("json.load: cannot open '%s'", nv_display(file));
    }
    return nv_json_parse(nv_read_file(file));
}

#endif /* NV_JSON_PARSE_H */
