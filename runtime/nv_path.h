/* nv_path.h - the path module. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_PATH_H
#define NV_PATH_H

/* ------------------------------------------------------------------ */
/* path module                                                         */
/* ------------------------------------------------------------------ */

static nv nv_path_join_args(nv *args, int n) {
    NvSb sb;
    int i, len;
    nv_sb_init(&sb);
    for (i = 0; i < n; i++) {
        const char *p = nv_display(args[i]);
        if (p[0] == 0) {
            continue;
        }
        if (sb.len > 0 && sb.buf[sb.len - 1] != '/' && sb.buf[sb.len - 1] != '\\') {
            nv_sb_addc(&sb, '/');
        }
        nv_sb_add(&sb, p);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_path_join(int n, ...) {
    nv args[64];
    va_list ap;
    int i;
    va_start(ap, n);
    for (i = 0; i < n && i < 64; i++) {
        args[i] = va_arg(ap, nv);
    }
    va_end(ap);
    return nv_path_join_args(args, n);
}

/* Paths reported by the runtime use forward slashes on every platform. */
static nv nv_path_slashes(const char *s) {
#ifdef _WIN32
    char *buf = nv_strndup(s, strlen(s));
    char *p;
    for (p = buf; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return nv_str_own(buf, (int)strlen(buf));
#else
    return nv_str(s);
#endif
}

static nv nv_path_absolute(void) {
    char buf[4096];
    if (!NV_GETCWD(buf, sizeof(buf))) {
        return nv_str(".");
    }
    return nv_path_slashes(buf);
}

static nv nv_path_exists(nv p) { return nv_bool(nv_path_exists_c(nv_display(p))); }

static nv nv_path_dirname(nv p) {
    const char *s = nv_display(p);
    int n = (int)strlen(s);
    while (n > 0 && s[n - 1] != '/' && s[n - 1] != '\\') {
        n--;
    }
    if (n == 0) {
        return nv_str(".");
    }
    if (n > 1) {
        n--; /* drop the separator itself */
    }
    return nv_strn(s, n);
}

static nv nv_path_basename(nv p) {
    const char *s = nv_display(p);
    int n = (int)strlen(s);
    int i = n;
    while (i > 0 && s[i - 1] != '/' && s[i - 1] != '\\') {
        i--;
    }
    return nv_str(s + i);
}

static nv nv_path_stem(nv p) {
    nv base = nv_path_basename(p);
    int i = base->slen;
    while (i > 0 && base->s[i - 1] != '.') {
        i--;
    }
    if (i <= 1) {
        return base;
    }
    return nv_strn(base->s, i - 1);
}

static nv nv_path_temp(void) {
    const char *t = getenv("TMPDIR");
    if (!t || !t[0]) {
        t = getenv("TEMP");
    }
    if (!t || !t[0]) {
        t = getenv("TMP");
    }
    if (!t || !t[0]) {
        t = "/tmp";
    }
    return nv_path_slashes(t);
}

static nv nv_path_extension(nv p) {
    nv base = nv_path_basename(p);
    int i = base->slen;
    while (i > 0 && base->s[i - 1] != '.') {
        i--;
    }
    if (i <= 1) {
        return nv_str("");
    }
    return nv_strn(base->s + i - 1, base->slen - i + 1);
}

static nv nv_path_is_absolute(nv p) {
    const char *s = nv_display(p);
    if (s[0] == '/' || s[0] == '\\') {
        return nv_bool(1);
    }
    return nv_bool(isalpha((unsigned char)s[0]) && s[1] == ':');
}

static nv nv_path_separator(void) {
#ifdef _WIN32
    return nv_str("\\");
#else
    return nv_str("/");
#endif
}

/* Collapses "." and ".." segments and duplicate separators. */
static nv nv_path_normalize(nv p) {
    const char *s = nv_display(p);
    const char *segs[1024];
    int lens[1024];
    int n = 0, i = 0, len = (int)strlen(s), k;
    NvSb sb;
    int rooted = 0, out;
    char drive[3] = {0, 0, 0};
    if (isalpha((unsigned char)s[0]) && s[1] == ':') {
        drive[0] = s[0];
        drive[1] = ':';
        i = 2;
    }
    if (s[i] == '/' || s[i] == '\\') {
        rooted = 1;
    }
    while (i < len) {
        int start;
        while (i < len && (s[i] == '/' || s[i] == '\\')) {
            i++;
        }
        start = i;
        while (i < len && s[i] != '/' && s[i] != '\\') {
            i++;
        }
        if (i == start) {
            continue;
        }
        if (i - start == 1 && s[start] == '.') {
            continue;
        }
        if (i - start == 2 && s[start] == '.' && s[start + 1] == '.') {
            if (n > 0 && !(lens[n - 1] == 2 && segs[n - 1][0] == '.' && segs[n - 1][1] == '.')) {
                n--;
                continue;
            }
            if (rooted) {
                continue;
            }
        }
        if (n < 1024) {
            segs[n] = s + start;
            lens[n] = i - start;
            n++;
        }
    }
    nv_sb_init(&sb);
    if (drive[0]) {
        nv_sb_add(&sb, drive);
    }
    if (rooted) {
        nv_sb_addc(&sb, '/');
    }
    for (k = 0; k < n; k++) {
        if (k > 0) {
            nv_sb_addc(&sb, '/');
        }
        nv_sb_addn(&sb, segs[k], lens[k]);
    }
    if (sb.len == 0) {
        nv_sb_addc(&sb, '.');
    }
    out = sb.len;
    return nv_str_own(nv_sb_finish(&sb), out);
}

static nv nv_path_absolute_of(nv p) {
    if (nv_truthy(nv_path_is_absolute(p))) {
        return nv_path_normalize(p);
    }
    return nv_path_normalize(nv_path_join(2, nv_path_absolute(), p));
}

/* Path of `target` relative to directory `base` (both made absolute). */
static nv nv_path_relative(nv base, nv target) {
    nv b = nv_path_absolute_of(base);
    nv t = nv_path_absolute_of(target);
    nv bparts = nv_str_split(b, nv_str("/"));
    nv tparts = nv_str_split(t, nv_str("/"));
    int common = 0, i;
    nv out = nv_arr();
    while (common < bparts->a->len && common < tparts->a->len &&
           strcmp(nv_cstr(bparts->a->items[common]), nv_cstr(tparts->a->items[common])) == 0) {
        common++;
    }
    for (i = common; i < bparts->a->len; i++) {
        if (bparts->a->items[i]->slen > 0) {
            nv_arr_push(out->a, nv_str(".."));
        }
    }
    for (i = common; i < tparts->a->len; i++) {
        if (tparts->a->items[i]->slen > 0) {
            nv_arr_push(out->a, tparts->a->items[i]);
        }
    }
    if (out->a->len == 0) {
        return nv_str(".");
    }
    return nv_arr_join(out, nv_str("/"));
}


#endif /* NV_PATH_H */
