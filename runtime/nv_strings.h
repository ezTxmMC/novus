/* nv_strings.h - string helpers (substring, split, replace, join). Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_STRINGS_H
#define NV_STRINGS_H

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
 * does to every node of its own AST - stops allocating a copy per slice. The
 * view keeps its parent's block alive (an interior pointer is a pointer to
 * the collector), which is exactly the sharing it wanted. */
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


#endif /* NV_STRINGS_H */
