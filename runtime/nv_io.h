/* nv_io.h - console I/O, files and the builtin functions. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_IO_H
#define NV_IO_H

/* ------------------------------------------------------------------ */
/* I/O and builtins                                                    */
/* ------------------------------------------------------------------ */

static void nv_print(nv v) { fputs(nv_display(v), stdout); }

static void nv_println(nv v) {
    fputs(nv_display(v), stdout);
    fputc('\n', stdout);
}

static void nv_eprintln(nv v) {
    fflush(stdout);
    fputs(nv_display(v), stderr);
    fputc('\n', stderr);
}

static nv nv_args_global = 0;

/* The first thing a program does: the collector is set up before the first
 * allocation, and the runtime's own tables are made known to it. */
static void nv_init_args(int argc, char **argv) {
    volatile char marker = 0;
    int i;
#ifdef _WIN32
    /* LF line endings on every platform (no CRLF translation) */
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    /* the frame of main() lies above this one; 4 KB covers it if the system
     * cannot report the stack's real top */
    nv_gc_init((char *)&marker + 4096);
    nv_gc_add_root(nv_char_table, sizeof(nv_char_table));
    nv_gc_add_root(&nv_args_global, sizeof(nv_args_global));
    for (i = 0; i < 256; i++) {
        char c = (char)i;
        nv v = nv_new(NV_STR);
        v->flags = NV_F_STABLE;
        v->s = nv_strndup(&c, 1);
        v->slen = 1;
        nv_char_table[i] = v;
    }
    nv_empty_str_val.s = "";
    nv_conc_init();
    nv_args_global = nv_arr();
    for (i = 1; i < argc; i++) {
        nv_arr_push(nv_args_global->a, nv_str(argv[i]));
    }
}

static nv nv_args(void) { return nv_args_global ? nv_args_global : nv_arr(); }

static nv nv_read_file(nv path) {
    FILE *f = fopen(nv_display(path), "rb");
    long n;
    char *buf;
    size_t rd;
    if (!f) {
        return nv_str("");
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)nv_alloc_atomic((size_t)n + 1);
    rd = fread(buf, 1, (size_t)n, f);
    buf[rd] = 0;
    fclose(f);
    return nv_str_own(buf, (int)rd);
}

static nv nv_write_file(nv path, nv content) {
    FILE *f = fopen(nv_display(path), "wb");
    const char *s;
    int len;
    if (!f) {
        nv_error("cannot write file '%s'", nv_display(path));
    }
    /* by byte length, not to the first NUL: readFile() already returns
     * binary content faithfully, and writing it back has to match */
    s = nv_bin(content, &len);
    fwrite(s, 1, (size_t)len, f);
    fclose(f);
    return nv_nil;
}

static nv nv_remove_file(nv path) {
    return nv_bool(remove(nv_display(path)) == 0);
}

static int nv_path_exists_c(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static nv nv_file_exists(nv path) { return nv_bool(nv_path_exists_c(nv_display(path))); }

static nv nv_read_line(void) {
    NvSb sb;
    int c;
    int len;
    nv_sb_init(&sb);
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (c != '\r') {
            nv_sb_addc(&sb, (char)c);
        }
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_parse_int(nv v) {
    if (nv_type_of(v) == NV_INT) {
        return v;
    }
    if (nv_type_of(v) == NV_FLOAT) {
        return nv_int((long long)v->f);
    }
    return nv_int(atoll(nv_display(v)));
}

static nv nv_parse_float(nv v) {
    if (nv_type_of(v) == NV_FLOAT) {
        return v;
    }
    return nv_float(nv_as_double(v));
}

static nv nv_chr(nv code) {
    char c = (char)nv_as_int(code);
    return nv_strn(&c, 1);
}

static nv nv_ord(nv s) {
    const char *p = nv_display(s);
    return nv_int(p[0] ? (unsigned char)p[0] : 0);
}

static nv nv_typeof_builtin(nv v) { return nv_str(nv_type_name(v)); }

/* cmd.exe strips the outer quotes of `"prog" "arg"`; one more pair of
 * quotes around the whole line keeps them intact. */
static const char *nv_shell_line(const char *cmd) {
#ifdef _WIN32
    size_t n = strlen(cmd);
    char *buf = (char *)nv_alloc_atomic(n + 3);
    buf[0] = '"';
    memcpy(buf + 1, cmd, n);
    buf[n + 1] = '"';
    buf[n + 2] = 0;
    return buf;
#else
    return cmd;
#endif
}

static int nv_exit_status(int rc) {
#ifdef _WIN32
    return rc;
#else
    if (rc == -1) {
        return -1;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return -1;
#endif
}

static nv nv_exec(nv cmd) {
    int rc;
    fflush(stdout);
    fflush(stderr);
    rc = system(nv_shell_line(nv_display(cmd)));
    return nv_int(nv_exit_status(rc));
}

static nv nv_env(nv name) {
    const char *v = getenv(nv_display(name));
    return nv_str(v ? v : "");
}

static nv nv_exit(nv code) {
    fflush(stdout);
    exit((int)nv_as_int(code));
    return nv_nil;
}

static nv nv_platform(void) {
#if defined(_WIN32)
    return nv_str("windows");
#elif defined(__APPLE__)
    return nv_str("macos");
#elif defined(__linux__)
    return nv_str("linux");
#else
    return nv_str("unix");
#endif
}


#endif /* NV_IO_H */
