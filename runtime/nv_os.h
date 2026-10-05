/* nv_os.h - the os module. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_OS_H
#define NV_OS_H

/* ------------------------------------------------------------------ */
/* os module                                                           */
/* ------------------------------------------------------------------ */

#include "nv_shell.h"   /* shell quoting */
#include "nv_os_stat.h" /* file times, realpath, symlinks (needs nv_path.h) */
#include "nv_os_fs.h"   /* files and directories */
#include "nv_os_proc.h" /* os.output, os.run */

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
    return nv_float((double)((long long)t - NV_WINDOWS_EPOCH_TICKS) / (double)NV_TICKS_PER_SECOND);
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
