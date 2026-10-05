/* nv_os_stat.h - file sizes, times, real paths and links of the os module. Part of the Novus runtime; included by nv_os.h.
 * Depends on: nv_path.h (nv_path_slashes), nv_memory.h (nv_alloc_atomic). */
#ifndef NV_OS_STAT_H
#define NV_OS_STAT_H

/* ------------------------------------------------------------------ */
/* os: stat                                                            */
/* ------------------------------------------------------------------ */

#define NV_MILLIS_PER_SECOND 1000LL
#define NV_NANOS_PER_MILLI 1000000LL
#define NV_TICKS_PER_MILLI 10000LL         /* a Windows FILETIME counts 100 ns ... */
#define NV_TICKS_PER_SECOND 10000000LL
#define NV_WINDOWS_EPOCH_TICKS 116444736000000000LL /* ... from 1601-01-01; this many up to 1970-01-01 */

static nv nv_os_file_size(nv path) {
    struct stat st;
    if (stat(nv_display(path), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_size);
}

static nv nv_os_modified(nv path) {
    struct stat st;
    if (stat(nv_display(path), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_mtime);
}

/* Unix milliseconds of a Windows FILETIME; negative for a time before 1970
 * (rounded down, so the unsigned subtraction never wraps). */
static long long nv_ticks_to_unix_millis(unsigned long long ticks) {
    long long since_epoch = (long long)ticks - NV_WINDOWS_EPOCH_TICKS;
    long long millis = since_epoch / NV_TICKS_PER_MILLI;
    if (since_epoch % NV_TICKS_PER_MILLI < 0) {
        millis--;
    }
    return millis;
}

#ifdef _WIN32

/* GetFinalPathNameByHandle answers \\?\C:\dir or \\?\UNC\server\share: back to the usual spelling. */
static const char *nv_windows_plain_path(const char *final_path) {
    static const char extended[] = "\\\\?\\";
    static const char extended_unc[] = "\\\\?\\UNC\\";
    NvSb sb;
    if (strncmp(final_path, extended_unc, sizeof(extended_unc) - 1) == 0) {
        nv_sb_init(&sb);
        nv_sb_add(&sb, "\\\\");
        nv_sb_add(&sb, final_path + sizeof(extended_unc) - 1);
        return nv_sb_finish(&sb);
    }
    if (strncmp(final_path, extended, sizeof(extended) - 1) == 0) {
        return final_path + sizeof(extended) - 1;
    }
    return final_path;
}

/* The final path of an open handle (links, junctions and short names
 * resolved, any length), or NULL. */
static const char *nv_windows_final_path(HANDLE handle) {
    DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    DWORD needed = GetFinalPathNameByHandleA(handle, 0, 0, flags); /* with the terminator */
    char *buffer;
    DWORD written;
    if (needed == 0) {
        return 0;
    }
    buffer = (char *)nv_alloc_atomic((size_t)needed + 1);
    written = GetFinalPathNameByHandleA(handle, buffer, needed + 1, flags);
    if (written == 0 || written > needed) {
        return 0;
    }
    return nv_windows_plain_path(buffer);
}

/* The path with every link resolved and made absolute; "" when it does not exist. */
static nv nv_os_realpath(nv path) {
    DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE handle = CreateFileA(nv_display(path), 0, sharing, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    const char *final_path;
    if (handle == INVALID_HANDLE_VALUE) {
        return nv_str("");
    }
    final_path = nv_windows_final_path(handle);
    CloseHandle(handle);
    return final_path ? nv_path_slashes(final_path) : nv_str("");
}

/* Whether the path itself is a link or junction (a link to nowhere counts). */
static nv nv_os_is_symlink(nv path) {
    DWORD attributes = GetFileAttributesA(nv_display(path));
    return nv_bool(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
}

/* Modification time in unix milliseconds, -1 when the path is missing. */
static nv nv_os_modified_millis(nv path) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    unsigned long long ticks;
    if (!GetFileAttributesExA(nv_display(path), GetFileExInfoStandard, &info)) {
        return nv_int(-1);
    }
    ticks = ((unsigned long long)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    return nv_int(nv_ticks_to_unix_millis(ticks));
}

#else

/* The path with every symlink resolved and made absolute; "" when it does not exist. */
static nv nv_os_realpath(nv path) {
    char *resolved = realpath(nv_display(path), 0);
    nv result;
    if (!resolved) {
        return nv_str("");
    }
    result = nv_str(resolved);
    free(resolved);
    return result;
}

/* Whether the path itself is a symbolic link (a link to nowhere counts). */
static nv nv_os_is_symlink(nv path) {
    struct stat st;
    return nv_bool(lstat(nv_display(path), &st) == 0 && S_ISLNK(st.st_mode));
}

static long long nv_stat_mtime_nanos(const struct stat *st) {
#ifdef __APPLE__
    return (long long)st->st_mtimespec.tv_nsec;
#else
    return (long long)st->st_mtim.tv_nsec;
#endif
}

/* Modification time in unix milliseconds, -1 when the path is missing. */
static nv nv_os_modified_millis(nv path) {
    struct stat st;
    if (stat(nv_display(path), &st) != 0) {
        return nv_int(-1);
    }
    return nv_int((long long)st.st_mtime * NV_MILLIS_PER_SECOND + nv_stat_mtime_nanos(&st) / NV_NANOS_PER_MILLI);
}

#endif /* _WIN32 */

#endif /* NV_OS_STAT_H */
