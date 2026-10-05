/* nv_os_fs.h - directories and files of the os module. Part of the Novus runtime; included by nv_os.h.
 * Depends on: nv_path.h (nv_path_join), nv_data.h (nv_error). */
#ifndef NV_OS_FS_H
#define NV_OS_FS_H

/* ------------------------------------------------------------------ */
/* os: files and directories                                           */
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

static int nv_is_dot_entry(const char *name) {
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

#ifdef _WIN32

/* Adds the entries of `dir` (without . and ..) to `out`, in no order. */
static void nv_list_dir_into(nv out, const char *dir) {
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
        if (!nv_is_dot_entry(data.cFileName)) {
            nv_arr_push(out->a, nv_str(data.cFileName));
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
}

#else

static void nv_list_dir_into(nv out, const char *dir) {
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) {
        nv_error("cannot list directory '%s'", dir);
    }
    while ((e = readdir(d)) != 0) {
        if (!nv_is_dot_entry(e->d_name)) {
            nv_arr_push(out->a, nv_str(e->d_name));
        }
    }
    closedir(d);
}

#endif /* _WIN32 */

/* Entries of a directory (without . and ..), sorted. */
static nv nv_os_list_dir(nv p) {
    nv out = nv_arr();
    nv_list_dir_into(out, nv_display(p));
    if (out->a->len > 1) {
        qsort(out->a->items, (size_t)out->a->len, sizeof(nv), nv_name_cmp);
    }
    return out;
}

/* What a path itself is: a link is never followed. */
#define NV_ENTRY_MISSING 0
#define NV_ENTRY_FILE 1     /* anything that is deleted like a file, links to files and (unix) to directories included */
#define NV_ENTRY_DIR 2
#define NV_ENTRY_DIR_LINK 3 /* Windows: a directory symlink or junction, deleted like an empty directory */

static int nv_entry_kind(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path); /* describes the link, not its target */
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return NV_ENTRY_MISSING;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return NV_ENTRY_FILE;
    }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? NV_ENTRY_DIR_LINK : NV_ENTRY_DIR;
#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        return NV_ENTRY_MISSING;
    }
    return S_ISDIR(st.st_mode) ? NV_ENTRY_DIR : NV_ENTRY_FILE;
#endif
}

static nv nv_os_remove_all(nv p);

static int nv_remove_dir_tree(nv dir) {
    nv entries = nv_os_list_dir(dir);
    int i;
    for (i = 0; i < entries->a->len; i++) {
        nv_os_remove_all(nv_path_join(2, dir, entries->a->items[i]));
    }
    return NV_RMDIR(nv_display(dir)) == 0;
}

static int nv_is_path_separator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* The length of `path` without trailing separators, a root ("/", "C:\\") kept:
 * "link/" names the link, but the system resolves it as the link's target. */
static size_t nv_trimmed_path_length(const char *path) {
    size_t len = strlen(path);
    while (len > 1 && nv_is_path_separator(path[len - 1])) {
        if (len == 3 && path[1] == ':') {
            break;
        }
        len--;
    }
    return len;
}

/* rm -rf. A link inside the tree is removed itself, and so is a link named
 * with trailing separators; what it points to stays. */
static nv nv_os_remove_all(nv p) {
    const char *given = nv_display(p);
    nv trimmed = nv_strn(given, (int)nv_trimmed_path_length(given));
    const char *path = nv_display(trimmed);
    int kind = nv_entry_kind(path);
    if (kind == NV_ENTRY_MISSING) {
        return nv_bool(0);
    }
    if (kind == NV_ENTRY_DIR) {
        return nv_bool(nv_remove_dir_tree(trimmed));
    }
    if (kind == NV_ENTRY_DIR_LINK) {
        return nv_bool(NV_RMDIR(path) == 0);
    }
    return nv_bool(remove(path) == 0);
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

#endif /* NV_OS_FS_H */
