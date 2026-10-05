/* nv_os_proc.h - running commands: os.output and os.run. Part of the Novus runtime; included by nv_os.h.
 * Depends on: nv_shell.h (nv_shell_line via nv_io.h), nv_data.h (NvSb, nv_error). */
#ifndef NV_OS_PROC_H
#define NV_OS_PROC_H

/* ------------------------------------------------------------------ */
/* os: processes                                                       */
/* ------------------------------------------------------------------ */

static nv nv_os_read_stream(FILE *stream) {
    NvSb sb;
    char buf[4096];
    size_t n;
    int len;
    nv_sb_init(&sb);
    while ((n = fread(buf, 1, sizeof(buf), stream)) > 0) {
        nv_sb_addn(&sb, buf, (int)n);
    }
    len = sb.len;
    return nv_str_own(nv_sb_finish(&sb), len);
}

/* Runs a command and returns what it printed to stdout. */
static nv nv_os_output(nv cmd) {
    FILE *p;
    nv text;
    fflush(stdout);
    fflush(stderr);
    p = NV_POPEN(nv_shell_line(nv_display(cmd)), "r");
    if (!p) {
        nv_error("cannot run '%s'", nv_display(cmd));
    }
    text = nv_os_read_stream(p);
    NV_PCLOSE(p);
    return text;
}

/* The command line os.run hands to the shell: the child must not read the
 * program's stdin (for a language server that is the protocol stream).
 * unix: the shell's own stdin is closed to /dev/null first.
 * Windows: a leading `<NUL` redirects the first command of the line, which
 * keeps pipelines and the command's own redirections working (the last
 * redirection of a command wins). A line such as `a & b` leaves `b` on the
 * inherited stdin; write `a <NUL & b <NUL` for that. Run under Wine, not on
 * a real Windows machine. */
static const char *nv_os_run_line(const char *cmd) {
    NvSb sb;
    nv_sb_init(&sb);
#ifdef _WIN32
    nv_sb_add(&sb, "<NUL ");
    nv_sb_add(&sb, cmd);
    return nv_shell_line(nv_sb_finish(&sb));
#else
    nv_sb_add(&sb, "exec </dev/null; ");
    nv_sb_add(&sb, cmd);
    return nv_sb_finish(&sb);
#endif
}

/* {code, output}: the command's exit code (-1 when it was killed by a signal
 * or could not be waited for) and what it printed to stdout. stderr is not
 * captured - add 2>&1 to the command to get it into `output`. */
static nv nv_os_run(nv cmd) {
    FILE *p;
    nv result = nv_map();
    nv text;
    int status;
    fflush(stdout);
    fflush(stderr);
    p = NV_POPEN(nv_os_run_line(nv_display(cmd)), "r");
    if (!p) {
        nv_error("cannot run '%s'", nv_display(cmd));
    }
    text = nv_os_read_stream(p);
    status = NV_PCLOSE(p);
    nv_map_set(result->m, "code", nv_int(nv_exit_status(status)));
    nv_map_set(result->m, "output", text);
    return result;
}

#endif /* NV_OS_PROC_H */
