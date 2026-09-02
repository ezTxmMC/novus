/* nv_platform.h - system headers, feature macros and platform shims. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_PLATFORM_H
#define NV_PLATFORM_H

/* POSIX extensions (popen, setenv, gettimeofday, nanosleep, dirent) */
#if !defined(_WIN32)
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1 /* pthread_getattr_np: the garbage collector needs the stack bounds */
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifdef __APPLE__
/* makecontext/swapcontext, which the virtual threads switch stacks with,
 * are hidden there without it (and _DARWIN_C_SOURCE above keeps the BSD
 * extensions that _XOPEN_SOURCE would otherwise take away) */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif
#endif

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define NV_GETCWD _getcwd
#define NV_CHDIR _chdir
#define NV_RMDIR _rmdir
#define NV_MKDIR(p) _mkdir(p)
#define NV_POPEN _popen
#define NV_PCLOSE _pclose
#define NV_GETPID _getpid
#else
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#define NV_GETCWD getcwd
#define NV_CHDIR chdir
#define NV_RMDIR rmdir
#define NV_MKDIR(p) mkdir(p, 0755)
#define NV_POPEN popen
#define NV_PCLOSE pclose
#define NV_GETPID getpid
#endif

#ifdef __GNUC__
#define NV_UNUSED __attribute__((unused))
#define NV_NOINLINE __attribute__((noinline))
#define NV_LIKELY(x) __builtin_expect(!!(x), 1)
#define NV_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NV_UNUSED
#define NV_NOINLINE
#define NV_LIKELY(x) (x)
#define NV_UNLIKELY(x) (x)
#endif

/* Thread local storage. Everything a thread mutates while it runs - its
 * allocation buffers, the caches, the last error of a subsystem - lives
 * here, so threads never contend for it and never see each other's. */
#if defined(_MSC_VER)
#define NV_TLS __declspec(thread)
#elif defined(__GNUC__)
#define NV_TLS __thread
#else
#define NV_TLS _Thread_local
#endif

/* ------------------------------------------------------------------ */
/* Thread primitives                                                   */
/* ------------------------------------------------------------------ */

/* Mutexes, condition variables, the clock and the processor count: what the
 * allocator (nv_memory.h) and the scheduler (nv_threads.h) both build on. */
#ifdef _WIN32

typedef CRITICAL_SECTION NvMutexRaw;
typedef CONDITION_VARIABLE NvCondRaw;
#define NV_MUTEX_INIT(m) InitializeCriticalSection(m)
#define NV_MUTEX_LOCK(m) EnterCriticalSection(m)
#define NV_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define NV_COND_INIT(c) InitializeConditionVariable(c)
#define NV_COND_WAIT(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define NV_COND_WAIT_MS(c, m, ms) SleepConditionVariableCS(c, m, (DWORD)(ms))
#define NV_COND_SIGNAL(c) WakeConditionVariable(c)
#define NV_COND_BROADCAST(c) WakeAllConditionVariable(c)
#define NV_HAVE_FIBERS 1

#else

#if defined(__unix__) || defined(__APPLE__)
#include <ucontext.h>
#define NV_HAVE_FIBERS 1
#else
#define NV_HAVE_FIBERS 0
#endif

typedef pthread_mutex_t NvMutexRaw;
typedef pthread_cond_t NvCondRaw;
#define NV_MUTEX_INIT(m) pthread_mutex_init(m, 0)
#define NV_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define NV_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define NV_COND_INIT(c) pthread_cond_init(c, 0)
#define NV_COND_WAIT(c, m) pthread_cond_wait(c, m)
#define NV_COND_SIGNAL(c) pthread_cond_signal(c)
#define NV_COND_BROADCAST(c) pthread_cond_broadcast(c)

static void nv_cond_wait_ms(NvCondRaw *c, NvMutexRaw *m, long long ms) {
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    ts.tv_sec = time(0);
    ts.tv_nsec = 0;
#endif
    ts.tv_sec += (time_t)(ms / 1000);
    ts.tv_nsec += (long)((ms % 1000) * 1000000L);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(c, m, &ts);
}
#define NV_COND_WAIT_MS(c, m, ms) nv_cond_wait_ms(c, m, ms)

#endif

static long long nv_now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    return (long long)time(0) * 1000;
#endif
}

static int nv_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors < 1 ? 1 : (int)si.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : (int)n;
#else
    return 1;
#endif
}

#endif /* NV_PLATFORM_H */
