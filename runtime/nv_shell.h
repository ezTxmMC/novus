/* nv_shell.h - quoting of one argument for the shell: os.shellQuote and os.shellQuoteFor. Part of the Novus runtime; included by nv_os.h.
 * Depends on: nv_data.h (NvSb, nv_error). Used by: nv_http.h. */
#ifndef NV_SHELL_H
#define NV_SHELL_H

/* ------------------------------------------------------------------ */
/* shell quoting                                                       */
/* ------------------------------------------------------------------ */

/* Both quoting functions are plain string code and are compiled on every
 * platform, so the Windows rules can be tested (os.shellQuoteFor) wherever
 * the tests run. What cmd.exe then does with the result was checked against
 * its documented behaviour and by round trips under Wine's cmd.exe (a
 * reimplementation), never on a real Windows. */

/* sh: single quotes; a single quote inside is written '\'' . Safe for any text. */
static const char *nv_shell_quote_unix(const char *argument) {
    NvSb sb;
    nv_sb_init(&sb);
    nv_sb_addc(&sb, '\'');
    for (; *argument; argument++) {
        if (*argument == '\'') {
            nv_sb_add(&sb, "'\\''");
            continue;
        }
        nv_sb_addc(&sb, *argument);
    }
    nv_sb_addc(&sb, '\'');
    return nv_sb_finish(&sb);
}

/* cmd.exe /c, then the argument parser of the program (CommandLineToArgvW).
 *
 * The argument goes between double quotes, where cmd.exe leaves & | < > ^ ( )
 * alone. Three characters cannot stay inside them: '"' would end the quoted
 * region for cmd.exe, '%' is expanded to a variable and '!' to a delayed
 * expansion even between quotes. Each of those leaves the quotes for one
 * character and is escaped with ^ there: `a%b` becomes `"a"^%"b"`, which the
 * program reads as the single argument a%b. For the program's parser, a run
 * of backslashes before a quote (the closing one included) is doubled, and
 * an embedded '"' is written \" . A line break cannot be quoted at all and
 * is an error. */
#define NV_CMD_QUOTE '"'
#define NV_CMD_ESCAPE '^'

static int nv_cmd_leaves_quotes(char byte) { return byte == NV_CMD_QUOTE || byte == '%' || byte == '!'; }

static void nv_cmd_add_backslashes(NvSb *sb, int count) {
    int k;
    for (k = 0; k < count; k++) {
        nv_sb_addc(sb, '\\');
    }
}

static void nv_cmd_add_outside_quotes(NvSb *sb, char byte) {
    nv_sb_addc(sb, NV_CMD_QUOTE); /* closes the region */
    if (byte == NV_CMD_QUOTE) {
        nv_sb_addc(sb, '\\'); /* for the program: a literal quote */
    }
    nv_sb_addc(sb, NV_CMD_ESCAPE);
    nv_sb_addc(sb, byte);
    nv_sb_addc(sb, NV_CMD_QUOTE); /* opens it again */
}

static void nv_cmd_reject_line_break(NvSb *sb, char byte) {
    if (byte != '\n' && byte != '\r') {
        return;
    }
    free(sb->buf);
    nv_error("os.shellQuote: a line break cannot be quoted for cmd.exe");
}

/* A character that is not a backslash, after `backslashes` of them. */
static void nv_cmd_add_plain(NvSb *sb, char byte, int backslashes) {
    if (nv_cmd_leaves_quotes(byte)) {
        nv_cmd_add_backslashes(sb, backslashes * 2);
        nv_cmd_add_outside_quotes(sb, byte);
        return;
    }
    nv_cmd_add_backslashes(sb, backslashes);
    nv_sb_addc(sb, byte);
}

static const char *nv_shell_quote_cmd(const char *argument) {
    NvSb sb;
    int backslashes = 0;
    nv_sb_init(&sb);
    nv_sb_addc(&sb, NV_CMD_QUOTE);
    for (; *argument; argument++) {
        nv_cmd_reject_line_break(&sb, *argument);
        if (*argument == '\\') {
            backslashes++;
            continue;
        }
        nv_cmd_add_plain(&sb, *argument, backslashes);
        backslashes = 0;
    }
    nv_cmd_add_backslashes(&sb, backslashes * 2);
    nv_sb_addc(&sb, NV_CMD_QUOTE);
    return nv_sb_finish(&sb);
}

/* The quoting of the platform the program runs on. */
static const char *nv_shell_quote(const char *argument) {
#ifdef _WIN32
    return nv_shell_quote_cmd(argument);
#else
    return nv_shell_quote_unix(argument);
#endif
}

static nv nv_os_shell_quote(nv argument) { return nv_str(nv_shell_quote(nv_display(argument))); }

/* The quoting for another platform: "windows" (cmd.exe), "linux", "macos" or "unix" (sh). */
static nv nv_os_shell_quote_for(nv platform, nv argument) {
    const char *name = nv_display(platform);
    if (strcmp(name, "windows") == 0) {
        return nv_str(nv_shell_quote_cmd(nv_display(argument)));
    }
    if (strcmp(name, "unix") == 0 || strcmp(name, "linux") == 0 || strcmp(name, "macos") == 0) {
        return nv_str(nv_shell_quote_unix(nv_display(argument)));
    }
    nv_error("os.shellQuoteFor: unknown platform '%s'", name);
    return nv_nil;
}

#endif /* NV_SHELL_H */
