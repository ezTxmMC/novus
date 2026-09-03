/* nv_memory.h - allocator, garbage collector and the thread registry. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_MEMORY_H
#define NV_MEMORY_H

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

/*
 * A mark-sweep garbage collector that keeps the heap the size of what the
 * program is using, with no compiler support beyond one call per global.
 *
 * Layout. Memory comes from the operating system in regions of 256 KB,
 * aligned to their size. A region holds cells of one size class (16, 24,
 * 32, ... 128 in steps of 8 or 16, then four steps per doubling up to
 * 32768 bytes, so a block wastes at most a quarter of its cell and the
 * common small ones - a value, an array header, a two field object - waste
 * nothing) and of one kind: SCAN cells may contain pointers
 * and are walked by the collector, ATOMIC cells are known to hold bytes
 * only (string contents, hash indexes, buffers) and are never looked into.
 * Anything larger than a class gets a mapping of its own. A two level
 * table keyed on the address bits above the region size maps any address
 * to its region, which is how the collector decides in a few loads whether
 * a word it finds on a stack points into the heap at all.
 *
 * Allocation. Every thread owns, per size class and kind, a region it is
 * currently filling; it claims runs of free cells from that region's
 * allocation bitmap and hands them out with a pointer increment (nv_alloc
 * is inline for that reason). Nothing is locked on that path; the heap
 * lock is taken only when a thread needs another region. A cell's bit in
 * the allocation bitmap is set as it is handed out, so the collector knows
 * which cells hold values and which are free and full of stale bytes.
 *
 * Roots. The collector is conservative about stacks and registers: every
 * word on every thread's stack (and every parked virtual thread's stack)
 * that could be a pointer into an allocated cell keeps that cell alive.
 * Interior pointers count too - a substring points into the middle of its
 * parent's bytes, a map key into a string's bytes. A pointer just past the
 * end of a cell does not: it is the address of the next cell, and treating
 * it as both would keep every dead neighbour of a live cell alive (nothing
 * in the runtime holds only an end pointer). Everything else that reaches the heap from
 * outside it is registered: the compiler emits nv_gc_root() for each
 * global, the runtime keeps its own tables (classes, the argument vector,
 * task results, channel buffers) in root blocks (nv_root_alloc) that are
 * scanned in full. The heap itself is scanned as it is marked: a SCAN cell
 * is walked word by word, an ATOMIC cell is not.
 *
 * Stopping the world. A collection begins on whichever thread ran out of
 * room. It brings every other registered thread to a halt - with a signal
 * on unix, whose handler notes the stack pointer and waits for a second
 * signal; with SuspendThread and GetThreadContext on Windows - marks,
 * sweeps and lets them go. The collector itself never allocates, never
 * takes a lock a stopped thread might hold and never calls into the C
 * library beyond mmap/munmap, so a thread may be stopped anywhere.
 *
 * Sweeping. Free cells are simply cells whose mark bit stayed clear:
 * alloc &= mark, per 64 cells at a time. A region with nothing left in it
 * goes back to the system at once, except for a reserve the size of the
 * next collection threshold that is kept for reuse, so a program that
 * churns through memory does not churn through page faults as well.
 *
 * Pacing. The next collection runs once as many bytes have been claimed as
 * were live after the last one (NOVUS_GC_GROWTH percent of it, default
 * 100), never sooner than every NOVUS_GC_MIN megabytes (default 8) per
 * registered thread: every thread that allocates gets that much between
 * collections, so a program on thirty-two threads is not stopped thirty-two
 * times as often as the same program on one. The heap therefore stays
 * within about twice the live data, or the threads' allowance. NOVUS_GC=off
 * turns collection off; NOVUS_GC_STATS=1 prints a summary at exit,
 * NOVUS_GC_STATS=2 a line per collection as well and NOVUS_GC_STATS=3 what
 * survived the last one, per size class.
 *
 * Costs worth knowing: a thread that allocates owns one region per size
 * class it touches, so an operating system thread costs a few hundred KB
 * of address space per class (only the pages it fills are ever resident);
 * virtual threads share their carrier's. And conservative scanning means a
 * stale word on a stack can keep a dead object alive for a while - tables
 * are zeroed on allocation and growth so that at least the heap itself
 * never does.
 */

#define NV_GC_REGION_SHIFT 18
#define NV_GC_REGION ((size_t)1 << NV_GC_REGION_SHIFT) /* 256 KB */
#define NV_GC_HEADER 8192                              /* region header, bitmaps included */
#define NV_GC_LARGE 32768                              /* above this a block maps its own region */
#define NV_GC_MIN_CELL 16
#define NV_GC_CELLS_MAX ((NV_GC_REGION - NV_GC_HEADER) / NV_GC_MIN_CELL)
#define NV_GC_BITMAP_WORDS ((NV_GC_CELLS_MAX + 63) / 64)
#define NV_GC_NCLASSES 43
#define NV_GC_L1_BITS 18 /* address bits 47..30 */
#define NV_GC_L2_BITS 12 /* address bits 29..18 */

enum { NV_GC_SCAN = 0, NV_GC_ATOMIC = 1 };

static const unsigned nv_gc_class_size[NV_GC_NCLASSES] = {
    16,   24,   32,   40,   48,   56,   64,   80,   96,    112,   128,   160,   192,  224,
    256,  320,  384,  448,  512,  640,  768,  896,  1024,  1280,  1536,  1792,  2048, 2560,
    3072, 3584, 4096, 5120, 6144, 7168, 8192, 10240, 12288, 14336, 16384, 20480, 24576, 28672,
    32768};

/* size in 8 byte steps -> class; built once by nv_gc_init */
static unsigned char nv_gc_class_of[NV_GC_LARGE / 8 + 1];

typedef struct NvRegion {
    struct NvRegion *next;      /* every region, the sweep walks this list */
    struct NvRegion *classNext; /* the regions of one size class and kind */
    void *mapBase;              /* what to hand back (on Windows the reservation) */
    size_t mapSize;
    char *cells;                /* first cell */
    size_t cellSize;
    unsigned ncells;
    unsigned nalloc;            /* allocated cells, exact after each sweep */
    unsigned cursor;            /* allocation: first cell not yet looked at */
    unsigned char cls, kind, large, owned;
    uint64_t alloc[NV_GC_BITMAP_WORDS];
    uint64_t mark[NV_GC_BITMAP_WORDS];
} NvRegion;

/* A thread's current run of free cells in a region, per class and kind. */
typedef struct NvTlab {
    char *cur;
    char *end;
    NvRegion *region;
    unsigned idx; /* cell index of cur */
} NvTlab;

typedef struct NvThread {
    struct NvThread *next;
    char *stackTop;         /* highest address of the thread's own stack */
    char *volatile sp;      /* where it was stopped */
    void *volatile vt;      /* the NvVThread it is running, if any */
    volatile int stopped;
    volatile int go;
    int lost;               /* could not be signalled: not waited for */
#ifdef _WIN32
    HANDLE handle;
    CONTEXT regs;
#else
    pthread_t id;
#endif
    NvTlab tlab[2][NV_GC_NCLASSES];
} NvThread;

static NV_TLS NvThread *nv_cur_thread = 0;

/* --- atomics ------------------------------------------------------- */

#if defined(_MSC_VER)
#define NV_ATOMIC_LOAD(p) (MemoryBarrier(), *(p))
#define NV_ATOMIC_STORE(p, v) (*(p) = (v), MemoryBarrier())
static size_t nv_atomic_add_size(volatile size_t *p, size_t v) {
    return (size_t)InterlockedExchangeAdd64((volatile LONGLONG *)p, (LONGLONG)v) + v;
}
#else
#define NV_ATOMIC_LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define NV_ATOMIC_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
static size_t nv_atomic_add_size(volatile size_t *p, size_t v) { return __atomic_add_fetch(p, v, __ATOMIC_ACQ_REL); }
#endif

static int nv_gc_popcount(uint64_t x) {
#if defined(__GNUC__)
    return __builtin_popcountll(x);
#else
    int n = 0;
    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
#endif
}

static int nv_gc_ctz(uint64_t x) {
#if defined(__GNUC__)
    return __builtin_ctzll(x);
#else
    int n = 0;
    while (!(x & 1)) {
        x >>= 1;
        n++;
    }
    return n;
#endif
}

/* --- the heap ------------------------------------------------------ */

static NvMutexRaw nv_gc_lock;        /* regions, class lists, pacing */
static NvMutexRaw nv_gc_thread_lock; /* the thread list; held for a whole collection */
static NvMutexRaw nv_gc_root_lock;   /* root blocks and ranges */
static int nv_gc_ready = 0;
static int nv_gc_enabled = 1;
static int nv_gc_collecting = 0;
static int nv_gc_stats_wanted = 0;

static NvRegion *nv_gc_regions = 0; /* all regions in use */
static NvRegion *nv_gc_class_list[2][NV_GC_NCLASSES];
static NvRegion *nv_gc_class_scan[2][NV_GC_NCLASSES]; /* where the search for room resumes */
static NvRegion *nv_gc_spare = 0;                       /* empty regions kept for reuse */
static size_t nv_gc_spare_bytes = 0;
static NvThread *nv_gc_threads = 0;

static NvRegion ***nv_gc_l1 = 0;
static uintptr_t nv_gc_lo = ~(uintptr_t)0; /* bounds of everything ever mapped */
static uintptr_t nv_gc_hi = 0;

static volatile size_t nv_gc_since = 0; /* bytes claimed since the last collection */
static size_t nv_gc_threshold = 0;
static size_t nv_gc_min_bytes = 8u << 20;
static size_t nv_gc_growth = 100; /* percent of the live set */
static size_t nv_gc_live = 0;     /* bytes in allocated cells after the last sweep */
static size_t nv_gc_mapped = 0;   /* bytes of regions in use or spare */
static size_t nv_gc_mapped_peak = 0;
static long long nv_gc_count = 0;
static long long nv_gc_pause_total_us = 0;
static long long nv_gc_pause_max_us = 0;

static void **nv_gc_mstack = 0; /* (cell, size) pairs still to be walked */
static size_t nv_gc_mstack_cap = 0;
static size_t nv_gc_mstack_len = 0;

typedef struct NvRootBlock {
    struct NvRootBlock *next;
    struct NvRootBlock *prev;
    size_t size;
    size_t pad;
} NvRootBlock;

typedef struct NvRootRange {
    void *at;
    size_t bytes;
} NvRootRange;

static NvRootBlock *nv_gc_root_blocks = 0;
static NvRootRange *nv_gc_root_ranges = 0;
static int nv_gc_root_len = 0;
static int nv_gc_root_cap = 0;

/* Implemented with the scheduler (nv_threads.h): the stack bounds of a
 * virtual thread, and a walk over every parked virtual thread's stack. */
static int nv_gc_vt_bounds(void *vt, char **lo, char **hi);
static void nv_gc_scan_fibers(void);

static void nv_gc_fatal(const char *what) {
    fprintf(stderr, "error: %s\n", what);
    exit(1);
}

static long long nv_gc_now_us(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (long long)((double)c.QuadPart * 1000000.0 / (double)f.QuadPart);
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#else
    return (long long)time(0) * 1000000;
#endif
}

/* --- pages from the system ----------------------------------------- */

/* `size` bytes (a multiple of the region size) aligned to the region size.
 * Fresh pages are zero and cost nothing until touched. */
static void *nv_gc_os_map(size_t size, void **mapBase, size_t *mapSize) {
    size_t total = size + NV_GC_REGION;
    char *p, *aligned;
#ifdef _WIN32
    p = (char *)VirtualAlloc(0, total, MEM_RESERVE, PAGE_READWRITE);
    if (!p) {
        return 0;
    }
    aligned = (char *)(((uintptr_t)p + NV_GC_REGION - 1) & ~(uintptr_t)(NV_GC_REGION - 1));
    if (!VirtualAlloc(aligned, size, MEM_COMMIT, PAGE_READWRITE)) {
        VirtualFree(p, 0, MEM_RELEASE);
        return 0;
    }
    *mapBase = p;
    *mapSize = total;
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    p = (char *)mmap(0, total, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED) {
        return 0;
    }
    aligned = (char *)(((uintptr_t)p + NV_GC_REGION - 1) & ~(uintptr_t)(NV_GC_REGION - 1));
    if (aligned > p) {
        munmap(p, (size_t)(aligned - p));
    }
    if ((p + total) > (aligned + size)) {
        munmap(aligned + size, (size_t)((p + total) - (aligned + size)));
    }
    *mapBase = aligned;
    *mapSize = size;
#endif
    return aligned;
}

static void nv_gc_os_unmap(void *mapBase, size_t mapSize) {
#ifdef _WIN32
    (void)mapSize;
    VirtualFree(mapBase, 0, MEM_RELEASE);
#else
    munmap(mapBase, mapSize);
#endif
}

/* --- address -> region --------------------------------------------- */

static NvRegion **nv_gc_l2_for(uintptr_t a, int create) {
    size_t i1 = (a >> (NV_GC_REGION_SHIFT + NV_GC_L2_BITS)) & (((size_t)1 << NV_GC_L1_BITS) - 1);
    NvRegion **l2 = nv_gc_l1[i1];
    if (!l2 && create) {
        void *base;
        size_t size;
        l2 = (NvRegion **)nv_gc_os_map(NV_GC_REGION, &base, &size); /* room for far more than 4096 entries */
        if (!l2) {
            nv_gc_fatal("out of memory");
        }
        nv_gc_l1[i1] = l2;
    }
    return l2;
}

static void nv_gc_dir_set(char *base, size_t size, NvRegion *r) {
    uintptr_t a;
    for (a = (uintptr_t)base; a < (uintptr_t)base + size; a += NV_GC_REGION) {
        NvRegion **l2 = nv_gc_l2_for(a, 1);
        l2[(a >> NV_GC_REGION_SHIFT) & (((size_t)1 << NV_GC_L2_BITS) - 1)] = r;
    }
    if ((uintptr_t)base < nv_gc_lo) {
        nv_gc_lo = (uintptr_t)base;
    }
    if ((uintptr_t)base + size > nv_gc_hi) {
        nv_gc_hi = (uintptr_t)base + size;
    }
}

static inline NvRegion *nv_gc_region_of(uintptr_t a) {
    NvRegion **l2;
    if (a - nv_gc_lo >= nv_gc_hi - nv_gc_lo) {
        return 0;
    }
    l2 = nv_gc_l1[(a >> (NV_GC_REGION_SHIFT + NV_GC_L2_BITS)) & (((size_t)1 << NV_GC_L1_BITS) - 1)];
    if (!l2) {
        return 0;
    }
    return l2[(a >> NV_GC_REGION_SHIFT) & (((size_t)1 << NV_GC_L2_BITS) - 1)];
}

/* --- regions ------------------------------------------------------- */

static void nv_gc_region_setup(NvRegion *r, int cls, int kind) {
    r->cells = (char *)r + NV_GC_HEADER;
    r->cellSize = nv_gc_class_size[cls];
    r->ncells = (unsigned)((NV_GC_REGION - NV_GC_HEADER) / r->cellSize);
    r->nalloc = 0;
    r->cursor = 0;
    r->cls = (unsigned char)cls;
    r->kind = (unsigned char)kind;
    r->large = 0;
    r->owned = 0;
    memset(r->alloc, 0, sizeof(r->alloc));
    memset(r->mark, 0, sizeof(r->mark));
}

/* Called with the heap lock held. */
static NvRegion *nv_gc_region_new(int cls, int kind) {
    NvRegion *r = nv_gc_spare;
    if (r) {
        nv_gc_spare = r->next;
        nv_gc_spare_bytes -= NV_GC_REGION;
    } else {
        void *base;
        size_t size;
        r = (NvRegion *)nv_gc_os_map(NV_GC_REGION, &base, &size);
        if (!r) {
            nv_gc_fatal("out of memory");
        }
        r->mapBase = base;
        r->mapSize = size;
        nv_gc_dir_set((char *)r, NV_GC_REGION, r);
        nv_gc_mapped += NV_GC_REGION;
        if (nv_gc_mapped > nv_gc_mapped_peak) {
            nv_gc_mapped_peak = nv_gc_mapped;
        }
    }
    nv_gc_region_setup(r, cls, kind);
    r->next = nv_gc_regions;
    nv_gc_regions = r;
    r->classNext = nv_gc_class_list[kind][cls];
    nv_gc_class_list[kind][cls] = r;
    return r;
}

/* A region of this class with room in it, or a new one. Called with the
 * heap lock held; the caller owns the result until it is used up. */
static NvRegion *nv_gc_region_get(int cls, int kind) {
    NvRegion *r = nv_gc_class_scan[kind][cls];
    while (r && (r->owned || r->cursor >= r->ncells)) {
        r = r->classNext;
    }
    if (r) {
        nv_gc_class_scan[kind][cls] = r->classNext;
    } else {
        nv_gc_class_scan[kind][cls] = 0;
        r = nv_gc_region_new(cls, kind);
    }
    r->owned = 1;
    return r;
}

/* Claims the next run of free cells of the thread's current region into
 * its allocation buffer. 0 when the region has none left. */
static int nv_gc_take_run(NvTlab *t) {
    NvRegion *r = t->region;
    unsigned i = r->cursor, n = r->ncells, end;
    while (i < n) {
        uint64_t w = r->alloc[i >> 6] >> (i & 63);
        if (w & 1) {
            /* allocated: skip to the next clear bit of this word */
            unsigned skip = (unsigned)nv_gc_ctz(~w);
            i += skip;
            continue;
        }
        break;
    }
    if (i >= n) {
        r->cursor = n;
        return 0;
    }
    /* the run: clear bits from i on, within this word, then whole words */
    end = i;
    for (;;) {
        uint64_t w = r->alloc[end >> 6] >> (end & 63);
        unsigned room = 64 - (end & 63);
        unsigned free_ = w ? (unsigned)nv_gc_ctz(w) : room;
        end += free_;
        if (free_ < room || end >= n) {
            break;
        }
    }
    if (end > n) {
        end = n;
    }
    r->cursor = end;
    t->idx = i;
    t->cur = r->cells + (size_t)i * r->cellSize;
    t->end = r->cells + (size_t)end * r->cellSize;
    nv_atomic_add_size(&nv_gc_since, (size_t)(end - i) * r->cellSize);
    return 1;
}

/* --- root blocks and ranges ---------------------------------------- */

/* Memory outside the heap that may point into it: scanned in full at
 * every collection. Zeroed, like calloc. */
static void *nv_root_alloc(size_t n) {
    NvRootBlock *b = (NvRootBlock *)calloc(1, sizeof(NvRootBlock) + n);
    if (!b) {
        nv_gc_fatal("out of memory");
    }
    b->size = n;
    NV_MUTEX_LOCK(&nv_gc_root_lock);
    b->next = nv_gc_root_blocks;
    b->prev = 0;
    if (nv_gc_root_blocks) {
        nv_gc_root_blocks->prev = b;
    }
    nv_gc_root_blocks = b; /* one store: a collector walking the list sees either state */
    NV_MUTEX_UNLOCK(&nv_gc_root_lock);
    return b + 1;
}

static void nv_root_free(void *p) {
    NvRootBlock *b;
    if (!p) {
        return;
    }
    b = (NvRootBlock *)p - 1;
    NV_MUTEX_LOCK(&nv_gc_root_lock);
    if (b->prev) {
        b->prev->next = b->next;
    } else {
        nv_gc_root_blocks = b->next;
    }
    if (b->next) {
        b->next->prev = b->prev;
    }
    NV_MUTEX_UNLOCK(&nv_gc_root_lock);
    free(b);
}

static void *nv_root_realloc(void *p, size_t oldn, size_t newn) {
    void *q = nv_root_alloc(newn);
    if (p && oldn) {
        memcpy(q, p, oldn < newn ? oldn : newn);
    }
    nv_root_free(p);
    return q;
}

/* A static or global that holds heap pointers. */
static void nv_gc_add_root(void *at, size_t bytes) {
    NV_MUTEX_LOCK(&nv_gc_root_lock);
    if (nv_gc_root_len == nv_gc_root_cap) {
        int cap = nv_gc_root_cap ? nv_gc_root_cap * 2 : 64;
        NvRootRange *grown = (NvRootRange *)realloc(nv_gc_root_ranges, sizeof(NvRootRange) * (size_t)cap);
        if (!grown) {
            nv_gc_fatal("out of memory");
        }
        nv_gc_root_ranges = grown;
        nv_gc_root_cap = cap;
    }
    nv_gc_root_ranges[nv_gc_root_len].at = at;
    nv_gc_root_ranges[nv_gc_root_len].bytes = bytes;
    nv_gc_root_len++;
    NV_MUTEX_UNLOCK(&nv_gc_root_lock);
}

/* The compiler emits one of these per global before initializing it. */
static void nv_gc_root(nv *slot) { nv_gc_add_root(slot, sizeof(nv)); }

/* --- threads ------------------------------------------------------- */

static char *nv_gc_stack_top(void) {
#if defined(_WIN32)
    return (char *)((NT_TIB *)NtCurrentTeb())->StackBase;
#elif defined(__APPLE__)
    return (char *)pthread_get_stackaddr_np(pthread_self());
#elif defined(__linux__)
    pthread_attr_t attr;
    void *addr = 0;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        if (addr && size) {
            return (char *)addr + size;
        }
    }
    return 0;
#else
    return 0;
#endif
}

/* Registers the calling thread: from now on a collection stops it and
 * scans its stack. `top` is the highest stack address to scan, 0 for the
 * one the system reports. */
static NvThread *nv_gc_thread_attach(char *top) {
    NvThread *t = nv_cur_thread;
    if (t) {
        return t;
    }
    t = (NvThread *)calloc(1, sizeof(NvThread));
    if (!t) {
        nv_gc_fatal("out of memory");
    }
    t->stackTop = nv_gc_stack_top();
    if (!t->stackTop) {
        t->stackTop = top;
    }
    if (!t->stackTop) {
        nv_gc_fatal("cannot find the stack of a thread");
    }
#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &t->handle, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
#else
    t->id = pthread_self();
#endif
    nv_cur_thread = t;
    NV_MUTEX_LOCK(&nv_gc_thread_lock);
    t->next = nv_gc_threads;
    nv_gc_threads = t;
    NV_MUTEX_UNLOCK(&nv_gc_thread_lock);
    return t;
}

/* The reverse, for a thread about to end: whatever it still holds is on
 * its stack no more, so nothing of it must be scanned again. */
static void nv_gc_thread_detach(void) {
    NvThread *t = nv_cur_thread;
    NvThread **at;
    int kind, cls;
    if (!t) {
        return;
    }
    NV_MUTEX_LOCK(&nv_gc_lock);
    for (kind = 0; kind < 2; kind++) {
        for (cls = 0; cls < NV_GC_NCLASSES; cls++) {
            NvTlab *tl = &t->tlab[kind][cls];
            if (tl->region) {
                tl->region->owned = 0; /* the room it still has is found again after a sweep */
                tl->region = 0;
                tl->cur = tl->end = 0;
            }
        }
    }
    NV_MUTEX_UNLOCK(&nv_gc_lock);
    NV_MUTEX_LOCK(&nv_gc_thread_lock);
    for (at = &nv_gc_threads; *at; at = &(*at)->next) {
        if (*at == t) {
            *at = t->next;
            break;
        }
    }
    NV_MUTEX_UNLOCK(&nv_gc_thread_lock);
    nv_cur_thread = 0;
#ifdef _WIN32
    CloseHandle(t->handle);
#endif
    free(t);
}

/* --- stopping the world -------------------------------------------- */

#ifndef _WIN32

#if defined(__linux__) && defined(SIGPWR)
#define NV_GC_SIG_SUSPEND SIGPWR
#define NV_GC_SIG_RESUME SIGXCPU
#else
#define NV_GC_SIG_SUSPEND SIGXCPU
#define NV_GC_SIG_RESUME SIGXFSZ
#endif

/* Runs on the thread being stopped, on its own stack: the registers the
 * kernel saved on the way in lie above this frame, so a scan from here to
 * the top of the stack sees them all. */
static void nv_gc_suspend_handler(int sig) {
    NvThread *t = nv_cur_thread;
    sigset_t wait;
    volatile char marker = 0;
    int saved = errno;
    (void)sig;
    if (!t) {
        return;
    }
    t->sp = (char *)&marker;
    sigfillset(&wait);
    sigdelset(&wait, NV_GC_SIG_RESUME);
    NV_ATOMIC_STORE(&t->stopped, 1);
    while (!NV_ATOMIC_LOAD(&t->go)) {
        sigsuspend(&wait); /* the resume signal is blocked outside this call: no lost wakeup */
    }
    NV_ATOMIC_STORE(&t->go, 0);
    NV_ATOMIC_STORE(&t->stopped, 0);
    errno = saved;
}

static void nv_gc_resume_handler(int sig) {
    NvThread *t = nv_cur_thread;
    (void)sig;
    if (t) {
        NV_ATOMIC_STORE(&t->go, 1);
    }
}

static void nv_gc_signals_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = nv_gc_suspend_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, NV_GC_SIG_RESUME);
    sigaction(NV_GC_SIG_SUSPEND, &sa, 0);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = nv_gc_resume_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(NV_GC_SIG_RESUME, &sa, 0);
}

/* Called with the thread lock held, which stays held until the world runs
 * again: no thread can register or leave in between. */
static void nv_gc_stop_world(NvThread *self) {
    NvThread *t;
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self) {
            continue;
        }
        t->lost = 0;
        NV_ATOMIC_STORE(&t->stopped, 0);
        if (pthread_kill(t->id, NV_GC_SIG_SUSPEND) != 0) {
            t->lost = 1; /* gone without detaching: nothing of it to scan */
        }
    }
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self || t->lost) {
            continue;
        }
        while (!NV_ATOMIC_LOAD(&t->stopped)) {
            sched_yield();
        }
    }
}

static void nv_gc_start_world(NvThread *self) {
    NvThread *t;
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self || t->lost) {
            continue;
        }
        pthread_kill(t->id, NV_GC_SIG_RESUME);
    }
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self || t->lost) {
            continue;
        }
        while (NV_ATOMIC_LOAD(&t->stopped)) {
            sched_yield();
        }
    }
}

#else /* _WIN32 */

static void nv_gc_signals_init(void) {}

static void nv_gc_stop_world(NvThread *self) {
    NvThread *t;
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self) {
            continue;
        }
        t->lost = SuspendThread(t->handle) == (DWORD)-1;
    }
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self || t->lost) {
            continue;
        }
        memset(&t->regs, 0, sizeof(t->regs));
        t->regs.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(t->handle, &t->regs)) { /* also waits for the suspension to land */
            t->lost = 1;
            continue;
        }
#if defined(_M_X64) || defined(__x86_64__)
        t->sp = (char *)t->regs.Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
        t->sp = (char *)t->regs.Sp;
#else
        t->sp = (char *)t->regs.Esp;
#endif
    }
}

static void nv_gc_start_world(NvThread *self) {
    NvThread *t;
    for (t = nv_gc_threads; t; t = t->next) {
        if (t != self && !t->lost) {
            ResumeThread(t->handle);
        }
    }
}

#endif

/* --- marking ------------------------------------------------------- */

static void nv_gc_mstack_push(void *cell, size_t size) {
    if (nv_gc_mstack_len + 2 > nv_gc_mstack_cap) {
        size_t cap = nv_gc_mstack_cap ? nv_gc_mstack_cap * 2 : (NV_GC_REGION / sizeof(void *));
        void *base;
        size_t mapped;
        void **grown = (void **)nv_gc_os_map((cap * sizeof(void *) + NV_GC_REGION - 1) & ~(NV_GC_REGION - 1), &base, &mapped);
        if (!grown) {
            nv_gc_fatal("out of memory");
        }
        if (nv_gc_mstack) {
            memcpy(grown, nv_gc_mstack, nv_gc_mstack_len * sizeof(void *));
            nv_gc_os_unmap(nv_gc_mstack, nv_gc_mstack_cap * sizeof(void *));
        }
        nv_gc_mstack = grown;
        nv_gc_mstack_cap = cap;
    }
    nv_gc_mstack[nv_gc_mstack_len++] = cell;
    nv_gc_mstack[nv_gc_mstack_len++] = (void *)size;
}

static inline void nv_gc_mark_cell(NvRegion *r, size_t idx) {
    size_t w = idx >> 6;
    uint64_t bit = (uint64_t)1 << (idx & 63);
    if (!(r->alloc[w] & bit) || (r->mark[w] & bit)) {
        return;
    }
    r->mark[w] |= bit;
    if (r->kind == NV_GC_SCAN) {
        nv_gc_mstack_push(r->cells + idx * r->cellSize, r->cellSize);
    }
}

/* Marks whatever `a` points into: a cell, or the inside of one. */
static inline void nv_gc_mark_word(uintptr_t a) {
    NvRegion *r;
    size_t off, idx;
    if (a - nv_gc_lo >= nv_gc_hi - nv_gc_lo) {
        return;
    }
    r = nv_gc_region_of(a);
    if (!r || a < (uintptr_t)r->cells) {
        return;
    }
    off = a - (uintptr_t)r->cells;
    idx = r->large ? (off < r->cellSize ? 0 : 1) : off / r->cellSize;
    if (idx < r->ncells) {
        nv_gc_mark_cell(r, idx);
    }
}

static void nv_gc_scan_range(const char *lo, const char *hi) {
    const uintptr_t *p = (const uintptr_t *)(((uintptr_t)lo + sizeof(uintptr_t) - 1) & ~(uintptr_t)(sizeof(uintptr_t) - 1));
    const uintptr_t *end = (const uintptr_t *)((uintptr_t)hi & ~(uintptr_t)(sizeof(uintptr_t) - 1));
    for (; p < end; p++) {
        nv_gc_mark_word(*p);
    }
}

static void nv_gc_drain(void) {
    while (nv_gc_mstack_len) {
        size_t size = (size_t)nv_gc_mstack[--nv_gc_mstack_len];
        const char *cell = (const char *)nv_gc_mstack[--nv_gc_mstack_len];
        nv_gc_scan_range(cell, cell + size);
    }
}

/* The stack a stopped thread was on when it stopped: its own, or that of
 * the virtual thread it was running. */
static void nv_gc_scan_thread_stack(NvThread *t, char *sp) {
    char *lo, *hi;
    if (t->vt && nv_gc_vt_bounds(t->vt, &lo, &hi) && sp >= lo && sp < hi) {
        nv_gc_scan_range(sp, hi);
        return;
    }
    if (sp < t->stackTop) {
        nv_gc_scan_range(sp, t->stackTop);
    }
}

/* Roots on the collecting thread itself. Its callee saved registers are
 * spilled into this frame first, and the frame of the function called next
 * lies below it, so scanning up from a local there sees everything. */
static NV_NOINLINE void nv_gc_scan_self_from_here(NvThread *self) {
    volatile char marker = 0;
    nv_gc_scan_thread_stack(self, (char *)&marker);
}

static NV_NOINLINE void nv_gc_scan_self(NvThread *self) {
    jmp_buf regs;
#if defined(__GNUC__)
    __builtin_unwind_init();
#endif
    setjmp(regs);
    nv_gc_scan_range((const char *)&regs, (const char *)&regs + sizeof(regs));
    nv_gc_scan_self_from_here(self);
}

static void nv_gc_mark_roots(NvThread *self) {
    NvThread *t;
    NvRootBlock *b;
    int i;
    nv_gc_scan_self(self);
    for (t = nv_gc_threads; t; t = t->next) {
        if (t == self || t->lost) {
            continue;
        }
#ifdef _WIN32
        nv_gc_scan_range((const char *)&t->regs, (const char *)&t->regs + sizeof(t->regs));
#endif
        nv_gc_scan_thread_stack(t, t->sp);
    }
    nv_gc_scan_fibers();
    for (b = nv_gc_root_blocks; b; b = b->next) {
        nv_gc_scan_range((const char *)(b + 1), (const char *)(b + 1) + b->size);
    }
    for (i = 0; i < nv_gc_root_len; i++) {
        nv_gc_scan_range((const char *)nv_gc_root_ranges[i].at,
                         (const char *)nv_gc_root_ranges[i].at + nv_gc_root_ranges[i].bytes);
    }
}

/* --- sweeping ------------------------------------------------------ */

static void nv_gc_dir_clear(char *base, size_t size) {
    uintptr_t a;
    for (a = (uintptr_t)base; a < (uintptr_t)base + size; a += NV_GC_REGION) {
        NvRegion **l2 = nv_gc_l2_for(a, 0);
        if (l2) {
            l2[(a >> NV_GC_REGION_SHIFT) & (((size_t)1 << NV_GC_L2_BITS) - 1)] = 0;
        }
    }
}

static void nv_gc_release(NvRegion *r) {
    nv_gc_dir_clear((char *)r, r->large ? r->mapSize : NV_GC_REGION);
    nv_gc_mapped -= r->large ? r->mapSize : NV_GC_REGION;
    nv_gc_os_unmap(r->mapBase, r->mapSize);
}

static void nv_gc_sweep(void) {
    NvRegion *r, *next, **at = &nv_gc_regions;
    size_t live = 0;
    int kind, cls;
    for (kind = 0; kind < 2; kind++) {
        for (cls = 0; cls < NV_GC_NCLASSES; cls++) {
            nv_gc_class_list[kind][cls] = 0;
        }
    }
    for (r = nv_gc_regions; r; r = next) {
        next = r->next;
        if (r->large) {
            if (r->mark[0] & 1) {
                r->mark[0] = 0;
                live += r->cellSize;
                at = &r->next;
            } else {
                *at = next;
                nv_gc_release(r);
            }
            continue;
        }
        {
            unsigned words = (r->ncells + 63) / 64, w, count = 0;
            unsigned first = r->ncells;
            for (w = 0; w < words; w++) {
                r->alloc[w] &= r->mark[w];
                r->mark[w] = 0;
                count += (unsigned)nv_gc_popcount(r->alloc[w]);
                if (first == r->ncells && r->alloc[w] != ~(uint64_t)0) {
                    first = w * 64 + (unsigned)nv_gc_ctz(~r->alloc[w]);
                }
            }
            r->nalloc = count;
            live += (size_t)count * r->cellSize;
            if (count == 0 && !r->owned) {
                *at = next;
                if (nv_gc_spare_bytes + NV_GC_REGION <= nv_gc_threshold) {
                    r->next = nv_gc_spare;
                    nv_gc_spare = r;
                    nv_gc_spare_bytes += NV_GC_REGION;
                } else {
                    nv_gc_release(r);
                }
                continue;
            }
            if (!r->owned) {
                r->cursor = first < r->ncells ? first : r->ncells; /* an owner's run is beyond its cursor: leave it */
            }
            at = &r->next;
        }
    }
    /* the class lists, rebuilt from what survived */
    for (r = nv_gc_regions; r; r = r->next) {
        if (!r->large) {
            r->classNext = nv_gc_class_list[r->kind][r->cls];
            nv_gc_class_list[r->kind][r->cls] = r;
        }
    }
    for (kind = 0; kind < 2; kind++) {
        for (cls = 0; cls < NV_GC_NCLASSES; cls++) {
            nv_gc_class_scan[kind][cls] = nv_gc_class_list[kind][cls];
        }
    }
    nv_gc_live = live;
}

/* --- a collection -------------------------------------------------- */

/* Called with the heap lock held. */
/* How many threads are registered. Called with nv_gc_thread_lock held. */
static int nv_gc_thread_count(void) {
    NvThread *t;
    int count = 0;
    for (t = nv_gc_threads; t; t = t->next) {
        count++;
    }
    return count < 1 ? 1 : count;
}

static void nv_gc_collect_locked(void) {
    NvThread *self = nv_cur_thread;
    long long t0;
    if (!nv_gc_enabled || nv_gc_collecting || !self) {
        return;
    }
    nv_gc_collecting = 1;
    t0 = nv_gc_now_us();
    NV_MUTEX_LOCK(&nv_gc_thread_lock);
    nv_gc_stop_world(self);
    nv_gc_mark_roots(self);
    nv_gc_drain();
    nv_gc_sweep();
    nv_gc_threshold = nv_gc_live / 100 * nv_gc_growth;
    {
        /* the floor is per thread: with the thread list held, count them */
        size_t floor = nv_gc_min_bytes * (size_t)nv_gc_thread_count();
        if (nv_gc_threshold < floor) {
            nv_gc_threshold = floor;
        }
    }
    /* the reserve is sized by the new threshold: trim what no longer fits */
    while (nv_gc_spare && nv_gc_spare_bytes > nv_gc_threshold) {
        NvRegion *r = nv_gc_spare;
        nv_gc_spare = r->next;
        nv_gc_spare_bytes -= NV_GC_REGION;
        nv_gc_release(r);
    }
    NV_ATOMIC_STORE(&nv_gc_since, (size_t)0);
    nv_gc_start_world(self);
    NV_MUTEX_UNLOCK(&nv_gc_thread_lock);
    {
        long long took = nv_gc_now_us() - t0;
        nv_gc_count++;
        nv_gc_pause_total_us += took;
        if (took > nv_gc_pause_max_us) {
            nv_gc_pause_max_us = took;
        }
        if (nv_gc_stats_wanted > 1) {
            int threads = 0;
            for (t0 = 0; t0 < 1; t0++) {
                NvThread *t;
                for (t = nv_gc_threads; t; t = t->next) {
                    threads++;
                }
            }
            fprintf(stderr, "[gc] #%lld live %.1f MB, heap %.1f MB, spare %.1f MB, threads %d, pause %.2f ms\n",
                    nv_gc_count, (double)nv_gc_live / 1048576.0, (double)nv_gc_mapped / 1048576.0,
                    (double)nv_gc_spare_bytes / 1048576.0, threads, (double)took / 1000.0);
        }
    }
    nv_gc_collecting = 0;
}

static void nv_gc_maybe_collect_locked(void) {
    if (NV_ATOMIC_LOAD(&nv_gc_since) >= nv_gc_threshold) {
        nv_gc_collect_locked();
    }
}

/* `collect()` from a program: a full collection, whatever the pacing. */
static void nv_gc_collect(void) {
    if (!nv_gc_ready) {
        return;
    }
    NV_MUTEX_LOCK(&nv_gc_lock);
    nv_gc_collect_locked();
    NV_MUTEX_UNLOCK(&nv_gc_lock);
}

static void nv_gc_print_stats(void) {
    fprintf(stderr, "[gc] collections %lld, pause total %.1f ms, max %.1f ms, live %.1f MB, heap peak %.1f MB\n",
            nv_gc_count, (double)nv_gc_pause_total_us / 1000.0, (double)nv_gc_pause_max_us / 1000.0,
            (double)nv_gc_live / 1048576.0, (double)nv_gc_mapped_peak / 1048576.0);
    if (nv_gc_stats_wanted > 2) {
        /* what survived the last collection, per size class */
        NvRegion *r;
        size_t large = 0, largeBytes = 0;
        unsigned regions[2][NV_GC_NCLASSES], cells[2][NV_GC_NCLASSES];
        int kind, cls;
        memset(regions, 0, sizeof(regions));
        memset(cells, 0, sizeof(cells));
        for (r = nv_gc_regions; r; r = r->next) {
            if (r->large) {
                large++;
                largeBytes += r->cellSize;
            } else {
                regions[r->kind][r->cls]++;
                cells[r->kind][r->cls] += r->nalloc;
            }
        }
        for (kind = 0; kind < 2; kind++) {
            for (cls = 0; cls < NV_GC_NCLASSES; cls++) {
                if (regions[kind][cls]) {
                    fprintf(stderr, "[gc]   %s %5u B: %4u regions, %9u cells, %.1f MB\n", kind ? "atomic" : "scan  ",
                            nv_gc_class_size[cls], regions[kind][cls], cells[kind][cls],
                            (double)cells[kind][cls] * nv_gc_class_size[cls] / 1048576.0);
                }
            }
        }
        fprintf(stderr, "[gc]   large: %zu blocks, %.1f MB\n", large, (double)largeBytes / 1048576.0);
    }
}

/* --- initialisation ------------------------------------------------ */

/* Before the first allocation, on the main thread. `top` bounds the scan
 * of the main thread's stack when the system cannot tell. */
static void nv_gc_init(char *top) {
    const char *env;
    void *base;
    size_t size, i;
    int cls;
    if (nv_gc_ready) {
        return;
    }
    nv_gc_ready = 1;
    NV_MUTEX_INIT(&nv_gc_lock);
    NV_MUTEX_INIT(&nv_gc_thread_lock);
    NV_MUTEX_INIT(&nv_gc_root_lock);
    for (i = 0, cls = 0; i <= NV_GC_LARGE / 8; i++) {
        while (i * 8 > nv_gc_class_size[cls]) {
            cls++;
        }
        nv_gc_class_of[i] = (unsigned char)cls;
    }
    nv_gc_l1 = (NvRegion ***)nv_gc_os_map(((size_t)1 << NV_GC_L1_BITS) * sizeof(void *), &base, &size);
    if (!nv_gc_l1) {
        nv_gc_fatal("out of memory");
    }
    env = getenv("NOVUS_GC");
    if (env && (strcmp(env, "off") == 0 || strcmp(env, "0") == 0)) {
        nv_gc_enabled = 0;
    }
    env = getenv("NOVUS_GC_MIN");
    if (env && atoi(env) > 0) {
        nv_gc_min_bytes = (size_t)atoi(env) << 20;
    }
    env = getenv("NOVUS_GC_GROWTH");
    if (env && atoi(env) > 0) {
        nv_gc_growth = (size_t)atoi(env);
    }
    env = getenv("NOVUS_GC_STATS"); /* 1: a summary at exit, 2: a line per collection, 3: survivors per class */
    if (env && env[0] && strcmp(env, "0") != 0) {
        nv_gc_stats_wanted = atoi(env) > 1 ? atoi(env) : 1;
        atexit(nv_gc_print_stats);
    }
    nv_gc_threshold = nv_gc_min_bytes;
    nv_gc_signals_init();
    nv_gc_thread_attach(top);
}

/* --- allocation ---------------------------------------------------- */

static void *nv_gc_alloc_large(size_t n, int kind) {
    NvRegion *r;
    void *base;
    void *cells;
    size_t size, mapped;
    n = (n + 7) & ~(size_t)7;
    size = (NV_GC_HEADER + n + NV_GC_REGION - 1) & ~(NV_GC_REGION - 1);
    NV_MUTEX_LOCK(&nv_gc_lock);
    nv_atomic_add_size(&nv_gc_since, n);
    nv_gc_maybe_collect_locked();
    r = (NvRegion *)nv_gc_os_map(size, &base, &mapped);
    if (!r) {
        nv_gc_fatal("out of memory");
    }
    r->mapBase = base;
    r->mapSize = mapped;
    r->cells = (char *)r + NV_GC_HEADER;
    r->cellSize = n;
    r->ncells = 1;
    r->nalloc = 1;
    r->cursor = 1;
    r->cls = 0;
    r->kind = (unsigned char)kind;
    r->large = 1;
    r->owned = 0;
    r->alloc[0] = 1;
    r->mark[0] = 0;
    nv_gc_dir_set((char *)r, size, r);
    nv_gc_mapped += size;
    if (nv_gc_mapped > nv_gc_mapped_peak) {
        nv_gc_mapped_peak = nv_gc_mapped;
    }
    r->next = nv_gc_regions;
    nv_gc_regions = r;
    /* The block's address has to be in this thread's hands before the lock
     * goes: another thread may collect the moment it does, and the only
     * thing that keeps a block nobody has seen yet alive is this pointer on
     * this stack. Read through the header afterwards and the collection
     * may have unmapped it. */
    cells = r->cells;
    NV_MUTEX_UNLOCK(&nv_gc_lock);
    return cells;
}

static NV_NOINLINE void *nv_alloc_slow(size_t n, int kind) {
    NvThread *t = nv_cur_thread;
    NvTlab *tl;
    unsigned cls;
    if (!t) {
        if (!nv_gc_ready) {
            volatile char marker = 0;
            nv_gc_init((char *)&marker + 4096);
            t = nv_cur_thread;
        } else {
            t = nv_gc_thread_attach(0);
        }
    }
    if (n > NV_GC_LARGE) {
        return nv_gc_alloc_large(n, kind);
    }
    cls = nv_gc_class_of[(n + 7) >> 3];
    tl = &t->tlab[kind][cls];
    for (;;) {
        NvRegion *r;
        if (tl->region && nv_gc_take_run(tl)) {
            break;
        }
        NV_MUTEX_LOCK(&nv_gc_lock);
        if (tl->region) {
            tl->region->owned = 0; /* used up; a sweep gives it room again */
            tl->region = 0;
        }
        nv_gc_maybe_collect_locked();
        r = nv_gc_region_get((int)cls, kind);
        NV_MUTEX_UNLOCK(&nv_gc_lock);
        tl->region = r;
        tl->cur = tl->end = 0;
        tl->idx = 0;
    }
    {
        void *p = tl->cur;
        NvRegion *r = tl->region;
        r->alloc[tl->idx >> 6] |= (uint64_t)1 << (tl->idx & 63);
        tl->idx++;
        tl->cur += r->cellSize;
        return p;
    }
}

/* A block of `n` bytes that may hold pointers into the heap. */
static inline void *nv_alloc_kind(size_t n, int kind) {
    NvThread *t = nv_cur_thread;
    NvTlab *tl;
    if (NV_UNLIKELY(n > NV_GC_LARGE || !t)) {
        return nv_alloc_slow(n, kind);
    }
    tl = &t->tlab[kind][nv_gc_class_of[(n + 7) >> 3]];
    if (NV_LIKELY(tl->cur < tl->end)) {
        void *p = tl->cur;
        NvRegion *r = tl->region;
        r->alloc[tl->idx >> 6] |= (uint64_t)1 << (tl->idx & 63);
        tl->idx++;
        tl->cur += r->cellSize;
        return p;
    }
    return nv_alloc_slow(n, kind);
}

static inline void *nv_alloc(size_t n) { return nv_alloc_kind(n, NV_GC_SCAN); }

/* A block that holds bytes only - the collector never looks inside it. */
static inline void *nv_alloc_atomic(size_t n) { return nv_alloc_kind(n, NV_GC_ATOMIC); }

static char *nv_strndup(const char *s, size_t n) {
    char *r = (char *)nv_alloc_atomic(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* Growable string buffers.
 *
 * Only nv_concat() and nv_add_chain() leave spare room at the end of a
 * buffer, and only they append into it. Those buffers - and no others -
 * keep their capacity in a word right in front of the bytes, which takes it
 * out of NvVal: a value is 16 bytes instead of 24, and the literals,
 * substrings and plain copies that are almost every string in a program
 * stop paying for a capacity they never had a use for. */
static char *nv_buf_alloc(size_t cap) {
    char *p = (char *)nv_alloc_atomic(sizeof(unsigned) + cap);
    *(unsigned *)p = (unsigned)cap;
    return p + sizeof(unsigned);
}

static size_t nv_buf_cap(const char *s) { return *(const unsigned *)(s - sizeof(unsigned)); }

/* What a program sees: memory.collect(), memory.used(), memory.heap(). */
static size_t nv_gc_used_bytes(void) { return nv_gc_live + NV_ATOMIC_LOAD(&nv_gc_since); }
static size_t nv_gc_heap_bytes(void) { return nv_gc_mapped; }

#endif /* NV_MEMORY_H */
