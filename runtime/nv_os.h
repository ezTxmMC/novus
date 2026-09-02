/* nv_os.h - the os module. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_OS_H
#define NV_OS_H

/* ------------------------------------------------------------------ */
/* os module                                                           */
/* ------------------------------------------------------------------ */

static int nv_stat_mode(const char *p, int *isdir) {
    struct stat st;
    if (stat(p, &st) != 0) {
        return 0;
    }
#ifdef S_ISDIR
    *isdir = S_ISDIR(st.st_mode);
#else
    *isdir = (st.st_mode & S_IFMT) == S_IFDIR;
#endif
    return 1;
}

static nv nv_os_is_dir(nv p) {
    int isdir = 0;
    return nv_bool(nv_stat_mode(nv_display(p), &isdir) && isdir);
}

static nv nv_os_is_file(nv p) {
    int isdir = 0;
    return nv_bool(nv_stat_mode(nv_display(p), &isdir) && !isdir);
}

/* mkdir -p */
static nv nv_os_mkdir(nv p) {
    const char *s = nv_display(p);
    size_t n = strlen(s), i;
    char *buf = nv_strndup(s, n);
    int isdir = 0;
    for (i = 1; i < n; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = 0;
            if (!(buf[i - 1] == ':' && i == 2)) {
                NV_MKDIR(buf);
            }
            buf[i] = saved;
        }
    }
    NV_MKDIR(buf);
    return nv_bool(nv_stat_mode(buf, &isdir) && isdir);
}

static nv nv_os_rmdir(nv p) { return nv_bool(NV_RMDIR(nv_display(p)) == 0); }

static int nv_name_cmp(const void *a, const void *b) {
    return strcmp(nv_cstr(*(const nv *)a), nv_cstr(*(const nv *)b));
}

/* Entries of a directory (without . and ..), sorted. */
static nv nv_os_list_dir(nv p) {
    nv out = nv_arr();
    const char *dir = nv_display(p);
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE h;
    char *pattern = (char *)nv_alloc_atomic(strlen(dir) + 3);
    strcpy(pattern, dir);
    strcat(pattern, "/*");
    h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        nv_error("cannot list directory '%s'", dir);
    }
    do {
        if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0) {
            nv_arr_push(out->a, nv_str(data.cFileName));
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) {
        nv_error("cannot list directory '%s'", dir);
    }
    while ((e = readdir(d)) != 0) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            nv_arr_push(out->a, nv_str(e->d_name));
        }
    }
    closedir(d);
#endif
    if (out->a->len > 1) {
        qsort(out->a->items, (size_t)out->a->len, sizeof(nv), nv_name_cmp);
    }
    return out;
}

/* rm -rf */
static nv nv_os_remove_all(nv p) {
    int isdir = 0;
    const char *s = nv_display(p);
    if (!nv_stat_mode(s, &isdir)) {
        return nv_bool(0);
    }
    if (isdir) {
        nv entries = nv_os_list_dir(p);
        int i;
        for (i = 0; i < entries->a->len; i++) {
            nv_os_remove_all(nv_path_join(2, p, entries->a->items[i]));
        }
        return nv_bool(NV_RMDIR(s) == 0);
    }
    return nv_bool(remove(s) == 0);
}

static nv nv_os_rename(nv from, nv to) { return nv_bool(rename(nv_display(from), nv_display(to)) == 0); }

static nv nv_os_copy(nv from, nv to) {
    FILE *in = fopen(nv_display(from), "rb");
    FILE *out;
    char buf[65536];
    size_t n;
    if (!in) {
        return nv_bool(0);
    }
    out = fopen(nv_display(to), "wb");
    if (!out) {
        fclose(in);
        return nv_bool(0);
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return nv_bool(1);
}

static nv nv_os_chdir(nv p) { return nv_bool(NV_CHDIR(nv_display(p)) == 0); }

static nv nv_os_file_size(nv p) {
    struct stat st;
    if (stat(nv_display(p), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_size);
}

static nv nv_os_modified(nv p) {
    struct stat st;
    if (stat(nv_display(p), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_mtime);
}

static nv nv_append_file(nv path, nv content) {
    FILE *f = fopen(nv_display(path), "ab");
    const char *s;
    int len;
    if (!f) {
        nv_error("cannot write file '%s'", nv_display(path));
    }
    s = nv_bin(content, &len);
    fwrite(s, 1, (size_t)len, f);
    fclose(f);
    return nv_nil;
}

/* Runs a command and returns what it printed to stdout. */
static nv nv_os_output(nv cmd) {
    FILE *p;
    NvSb sb;
    char buf[4096];
    size_t n;
    int len;
    fflush(stdout);
    fflush(stderr);
    p = NV_POPEN(nv_shell_line(nv_display(cmd)), "r");
    if (!p) {
        nv_error("cannot run '%s'", nv_display(cmd));
    }
    nv_sb_init(&sb);
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) {
        nv_sb_addn(&sb, buf, (int)n);
    }
    NV_PCLOSE(p);
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

static nv nv_os_set_env(nv name, nv value) {
#ifdef _WIN32
    return nv_bool(_putenv_s(nv_display(name), nv_display(value)) == 0);
#else
    return nv_bool(setenv(nv_display(name), nv_display(value), 1) == 0);
#endif
}

static nv nv_os_time(void) { return nv_int((long long)time(0)); }

static nv nv_os_clock(void) {
#ifdef _WIN32
    FILETIME ft;
    unsigned long long t;
    GetSystemTimeAsFileTime(&ft);
    t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return nv_float((double)(t - 116444736000000000ULL) / 10000000.0);
#else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return nv_float((double)tv.tv_sec + (double)tv.tv_usec / 1000000.0);
#endif
}

static nv nv_os_sleep(nv ms) {
    long long m = nv_as_int(ms);
#ifdef _WIN32
    Sleep((DWORD)m);
#else
    struct timespec ts, left;
    ts.tv_sec = (time_t)(m / 1000);
    ts.tv_nsec = (long)(m % 1000) * 1000000L;
    /* a collection interrupts the sleep with a signal: sleep on for the rest */
    while (nanosleep(&ts, &left) != 0 && errno == EINTR) {
        ts = left;
    }
#endif
    return nv_nil;
}

static nv nv_os_home(void) {
    const char *h = getenv("HOME");
    if (!h || !h[0]) {
        h = getenv("USERPROFILE");
    }
    return nv_path_slashes(h ? h : "");
}

/* Interrupts.
 *
 * A program that owns something worth writing out - a world, a database, an
 * open file - has to be told that it is being asked to stop, rather than
 * simply being killed. The handler only raises a flag; interrupted() reads it
 * and clears it, so the program decides when and where to act on it. */
static volatile sig_atomic_t nv_interrupt_flag = 0;

static void nv_interrupt_handler(int signal_number) {
    (void)signal_number;
    nv_interrupt_flag = 1;
}

static nv nv_os_catch_interrupt(void) {
    signal(SIGINT, nv_interrupt_handler);
#ifdef SIGTERM
    signal(SIGTERM, nv_interrupt_handler);
#endif
    return nv_nil;
}

static nv nv_os_interrupted(void) {
    int raised = nv_interrupt_flag != 0;
    nv_interrupt_flag = 0;
    return nv_bool(raised);
}

static nv nv_os_pid(void) { return nv_int((long long)NV_GETPID()); }


#endif /* NV_OS_H */
