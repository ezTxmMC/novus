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

/* The string functions below work on the explicit length of their operands:
 * a substring view is read where it lies instead of being copied to get a
 * terminator, and a NUL byte is an ordinary byte. */

/* First place at or after `from` where the `nl` bytes of `needle` occur in
 * the `hl` bytes of `hay`, -1 when there is none. An empty needle is found
 * at `from`. The C library's memmem is used where there is one: it is a
 * vectorised two-way search, which a loop over memchr cannot match on text
 * where the first byte of the needle is common. */
static long long nv_mem_find(const char *hay, size_t hl, const char *needle, size_t nl, size_t from) {
    const char *at;
    if (from > hl || nl > hl - from) {
        return -1;
    }
    if (nl == 0) {
        return (long long)from;
    }
#if defined(__linux__) || defined(__APPLE__)
    at = (const char *)memmem(hay + from, hl - from, needle, nl);
    return at ? (long long)(at - hay) : -1;
#else
    {
        const char *last = hay + hl - nl;
        at = hay + from;
        while (at <= last) {
            at = (const char *)memchr(at, needle[0], (size_t)(last - at) + 1);
            if (!at) {
                return -1;
            }
            if (at[nl - 1] == needle[nl - 1] && memcmp(at + 1, needle + 1, nl - 1) == 0) {
                return (long long)(at - hay);
            }
            at++;
        }
        return -1;
    }
#endif
}

static long long nv_str_index_of(nv s, nv needle, long long from) {
    size_t needleLen;
    const char *needleText = nv_arg_text(needle, &needleLen);
    if (from < 0) {
        from = 0;
    }
    if (from > s->slen) {
        return -1;
    }
    return nv_mem_find(s->s, (size_t)s->slen, needleText, needleLen, (size_t)from);
}

static nv nv_str_split(nv s, nv sep) {
    nv out = nv_arr();
    size_t sepLen;
    const char *sepText = nv_arg_text(sep, &sepLen);
    long long start = 0;
    long long at;
    if (sepLen == 0) {
        int i;
        for (i = 0; i < s->slen; i++) {
            nv_arr_push(out->a, nv_strn(s->s + i, 1));
        }
        return out;
    }
    while ((at = nv_mem_find(s->s, (size_t)s->slen, sepText, sepLen, (size_t)start)) >= 0) {
        nv_arr_push(out->a, nv_strn(s->s + start, (int)(at - start)));
        start = at + (long long)sepLen;
    }
    nv_arr_push(out->a, nv_strn(s->s + start, (int)(s->slen - start)));
    return out;
}

/* Splits on runs of whitespace - the hot path of most text processing. */
static nv nv_str_words(nv s) {
    nv out = nv_arr();
    const char *text = s->s;
    int length = s->slen;
    int i = 0, start;
    while (i < length) {
        while (i < length && isspace((unsigned char)text[i])) {
            i++;
        }
        if (i >= length) {
            break;
        }
        start = i;
        while (i < length && !isspace((unsigned char)text[i])) {
            i++;
        }
        nv_arr_push(out->a, nv_strn(text + start, i - start));
    }
    return out;
}

static nv nv_str_replace(nv s, nv from, nv to) {
    NvSb sb;
    size_t fromLen, toLen;
    const char *fromText = nv_arg_text(from, &fromLen);
    const char *toText = nv_arg_text(to, &toLen);
    long long start = 0;
    long long at;
    int len;
    if (fromLen == 0) {
        return s;
    }
    at = nv_mem_find(s->s, (size_t)s->slen, fromText, fromLen, 0);
    if (at < 0) {
        return s;
    }
    nv_sb_init(&sb);
    while (at >= 0) {
        nv_sb_addn(&sb, s->s + start, (int)(at - start));
        nv_sb_addn(&sb, toText, (int)toLen);
        start = at + (long long)fromLen;
        at = nv_mem_find(s->s, (size_t)s->slen, fromText, fromLen, (size_t)start);
    }
    nv_sb_addn(&sb, s->s + start, (int)(s->slen - start));
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

/* A string with nothing to trim is itself. Otherwise the result is a copy,
 * not a view like a substring: what is trimmed off is often most of a big
 * string (a whole file's padding, a line out of a buffer), and a view would
 * keep all of it alive for as long as the short result is. The copy is also a
 * private, terminated string, which a map takes as a key without copying it
 * again. */
static nv nv_str_trim(nv s) {
    int a = 0, b = s->slen;
    while (a < b && isspace((unsigned char)s->s[a])) {
        a++;
    }
    while (b > a && isspace((unsigned char)s->s[b - 1])) {
        b--;
    }
    if (a == 0 && b == s->slen) {
        return s;
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
    size_t sepLen;
    const char *sepText = nv_arg_text(sep, &sepLen);
    int i, len;
    nv_sb_init(&sb);
    for (i = 0; i < a->a->len; i++) {
        if (i > 0) {
            nv_sb_addn(&sb, sepText, (int)sepLen);
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
