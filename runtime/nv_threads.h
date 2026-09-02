/* nv_threads.h - threads, virtual threads, tasks, locks, channels. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_THREADS_H
#define NV_THREADS_H

/* ------------------------------------------------------------------ */
/* Threads, virtual threads, tasks                                     */
/* ------------------------------------------------------------------ */

/* Two kinds of thread, one set of primitives.
 *
 * A `thread` is an operating system thread. It costs what the system charges
 * for one - a stack measured in megabytes - and the kernel schedules it, so
 * a program has tens or hundreds of them, not millions.
 *
 * A `virtual` thread is a stack of its own (a hundred kilobytes reserved, of
 * which only the pages it touches are ever backed by memory) that a small
 * pool of carrier threads runs. Blocking one - awaiting a task, a channel, a
 * lock, thread.sleep - parks its stack and hands the carrier to the next
 * runnable virtual thread instead of handing it back to the kernel, so a
 * program can have as many of them as it has work. The stacks are switched
 * with ucontext on unix and with fibers on Windows.
 *
 * Everything that blocks goes through one parking lot: a queue of waiters on
 * an object, each of them either a virtual thread to make runnable again or
 * an operating system thread to signal. That is why a lock, a channel or an
 * await reads the same whichever kind of thread runs into it, and why a
 * virtual thread that blocks costs no operating system thread.
 *
 * A virtual thread never moves to another carrier. It could - the run queue
 * would only have to be shared - but a stack that is switched under the feet
 * of the compiler must not be allowed to wake up on a different thread: the
 * address of a thread local is a value like any other, and a compiler is
 * free to keep one in a register across the switch. Staying put keeps every
 * thread local (the thread's allocation buffers above all) the one the code
 * that reads it was compiled to reach.
 *
 * The collector (nv_memory.h) sees virtual threads through two hooks at the
 * end of this section: every virtual thread notes its stack pointer before
 * it switches away, and a collection scans each parked stack from there. */

enum { NV_VT_NEW = 0, NV_VT_READY, NV_VT_RUNNING, NV_VT_PARKED, NV_VT_DONE };

typedef nv (*NvSpawnFn)(nv *args, int n);

typedef struct NvVThread NvVThread;
typedef struct NvCarrier NvCarrier;

#if NV_HAVE_FIBERS
#ifdef _WIN32
typedef LPVOID NvCtx;
#else
typedef ucontext_t NvCtx;
#endif
#else
typedef int NvCtx;
#endif

struct NvVThread {
    NvCtx ctx;
    void *stackBase;      /* what has to be handed back, guard page included */
    size_t stackSize;     /* usable bytes */
    char *stackLo;        /* the usable stack: [stackLo, stackTop) */
    char *stackTop;
    char *parkSp;         /* stack pointer at the last switch away, for the collector */
    NvVThread *allNext;   /* every virtual thread ever made, for the collector */
    NvCarrier *carrier;   /* the one carrier that ever runs this stack */
    NvSpawnFn fn;
    nv *args;
    int nargs;
    int task;             /* the task this virtual thread completes */
    int state;
    NvMutexRaw *parkLock; /* the carrier releases it once the stack is off */
    long long wakeAt;     /* deadline while on the timer queue */
    NvVThread *next;      /* run queue, timer queue or free list */
};

struct NvCarrier {
    NvCtx sched;
    NvMutexRaw lock;
    NvCondRaw cond;
    NvVThread *runHead, *runTail;
    NvVThread *timers; /* sorted by wakeAt */
    NvVThread *pool;   /* finished stacks, ready to run something else */
};

static NV_TLS NvVThread *nv_cur_vt = 0;
static NV_TLS NvCarrier *nv_cur_carrier = 0;
static NV_TLS int nv_tls_anchor = 0; /* its address identifies an os thread */
static NvVThread *nv_all_vts = 0;    /* published with one store, so a collector can always walk it */

/* Who is running: the virtual thread when there is one, the operating system
 * thread otherwise. A lock compares owners with it. */
static void *nv_runner(void) {
    return nv_cur_vt ? (void *)nv_cur_vt : (void *)&nv_tls_anchor;
}

/* --- parking lot --------------------------------------------------- */

typedef struct NvWaiter {
    struct NvWaiter *next;
    NvVThread *vt;   /* the waiter is a virtual thread ... */
    NvCondRaw *cond; /* ... or an operating system thread with this condition */
    int ready;
} NvWaiter;

typedef struct NvPark {
    NvWaiter *head;
    NvWaiter *tail;
} NvPark;

static NV_TLS NvCondRaw nv_self_cond_storage;
static NV_TLS int nv_self_cond_made = 0;

static NvCondRaw *nv_self_cond(void) {
    if (!nv_self_cond_made) {
        NV_COND_INIT(&nv_self_cond_storage);
        nv_self_cond_made = 1;
    }
    return &nv_self_cond_storage;
}

static void nv_carrier_wake(NvVThread *vt);
static void nv_vt_park(NvMutexRaw *m);

/* Blocks the current runner on `p`. `m` must be held on the way in and is
 * held again on the way out, so the condition `p` stands for gets re-checked
 * in a loop, exactly the way a condition variable wants it. */
static void nv_park_self(NvPark *p, NvMutexRaw *m) {
    NvWaiter w;
    w.next = 0;
    w.ready = 0;
    w.vt = nv_cur_vt;
    w.cond = w.vt ? 0 : nv_self_cond();
    if (p->tail) {
        p->tail->next = &w;
    } else {
        p->head = &w;
    }
    p->tail = &w;
    if (w.vt) {
        nv_vt_park(m);
    } else {
        while (!w.ready) {
            NV_COND_WAIT(w.cond, m);
        }
    }
}

/* Both are called with the mutex that guards `p` held. */
static void nv_unpark_one(NvPark *p) {
    NvWaiter *w = p->head;
    if (!w) {
        return;
    }
    p->head = w->next;
    if (!p->head) {
        p->tail = 0;
    }
    w->ready = 1;
    if (w->vt) {
        nv_carrier_wake(w->vt);
    } else {
        NV_COND_SIGNAL(w->cond);
    }
}

static void nv_unpark_all(NvPark *p) {
    while (p->head) {
        nv_unpark_one(p);
    }
}

/* --- the carrier pool ---------------------------------------------- */

#define NV_MAX_CARRIERS 256

static NvMutexRaw nv_sched_lock; /* the carrier table and the live count */
static NvMutexRaw nv_rt_lock;    /* handles and task state */
static NvCarrier *nv_carrier_table[NV_MAX_CARRIERS];
static int nv_carriers_running = 0;
static int nv_carriers_wanted = 0;
static int nv_spawn_turn = 0;
static int nv_vt_live = 0;
static size_t nv_vstack_size = 0;
static int nv_conc_ready = 0;

/* Set up before any second thread exists: nv_init_args() calls this, and so
 * does every entry point that can be the first one a program reaches. */
static void nv_conc_init(void) {
    const char *env;
    if (nv_conc_ready) {
        return;
    }
    nv_conc_ready = 1;
    NV_MUTEX_INIT(&nv_sched_lock);
    NV_MUTEX_INIT(&nv_rt_lock);
    nv_carriers_wanted = nv_cpu_count();
    if (nv_carriers_wanted > NV_MAX_CARRIERS) {
        nv_carriers_wanted = NV_MAX_CARRIERS;
    }
    env = getenv("NOVUS_THREADS");
    if (env && atoi(env) > 0) {
        nv_carriers_wanted = atoi(env) > NV_MAX_CARRIERS ? NV_MAX_CARRIERS : atoi(env);
    }
    nv_vstack_size = 128 * 1024;
    env = getenv("NOVUS_VSTACK");
    if (env && atoi(env) > 0) {
        nv_vstack_size = (size_t)atoi(env) * 1024;
    }
}

/* Called with c->lock held. */
static void nv_runq_push(NvCarrier *c, NvVThread *vt) {
    vt->state = NV_VT_READY;
    vt->next = 0;
    if (c->runTail) {
        c->runTail->next = vt;
    } else {
        c->runHead = vt;
    }
    c->runTail = vt;
    NV_COND_SIGNAL(&c->cond);
}

static void nv_carrier_wake(NvVThread *vt) {
    NvCarrier *c = vt->carrier;
    NV_MUTEX_LOCK(&c->lock);
    nv_runq_push(c, vt);
    NV_MUTEX_UNLOCK(&c->lock);
}

/* Called with c->lock held: everything whose deadline has passed. */
static void nv_timers_fire(NvCarrier *c, long long now) {
    while (c->timers && c->timers->wakeAt <= now) {
        NvVThread *vt = c->timers;
        c->timers = vt->next;
        nv_runq_push(c, vt);
    }
}

static NvVThread *nv_runq_take(NvCarrier *c) {
    NvVThread *vt;
    NV_MUTEX_LOCK(&c->lock);
    for (;;) {
        long long now = nv_now_ms();
        nv_timers_fire(c, now);
        if (c->runHead) {
            vt = c->runHead;
            c->runHead = vt->next;
            if (!c->runHead) {
                c->runTail = 0;
            }
            vt->next = 0;
            NV_MUTEX_UNLOCK(&c->lock);
            return vt;
        }
        if (c->timers) {
            long long wait = c->timers->wakeAt - now;
            NV_COND_WAIT_MS(&c->cond, &c->lock, wait < 1 ? 1 : wait);
        } else {
            NV_COND_WAIT(&c->cond, &c->lock);
        }
    }
}

/* --- stacks and contexts ------------------------------------------- */

#if NV_HAVE_FIBERS && !defined(_WIN32)
static size_t nv_page_size(void) {
    long n = sysconf(_SC_PAGESIZE);
    return n < 1 ? 4096 : (size_t)n;
}
#endif

static void nv_vt_body(NvVThread *vt);

#if NV_HAVE_FIBERS
#ifdef _WIN32
static VOID CALLBACK nv_fiber_entry(PVOID arg) {
    volatile char marker = 0;
    NvVThread *vt = (NvVThread *)arg;
    /* the top of a fiber's stack is wherever it starts out */
    vt->stackTop = (char *)&marker;
    vt->stackLo = vt->stackTop - vt->stackSize;
    for (;;) {
        nv_vt_body(nv_cur_vt);
    }
}
static void nv_ctx_switch(NvCtx *from, NvCtx *to) {
    (void)from;
    SwitchToFiber(*to);
}
static int nv_ctx_start(NvVThread *vt) {
    vt->ctx = CreateFiber(vt->stackSize, nv_fiber_entry, vt);
    return vt->ctx != 0;
}
static void nv_carrier_enter(NvCarrier *c) { c->sched = ConvertThreadToFiber(0); }
#else
#ifdef __APPLE__
/* ucontext is marked deprecated there and has no replacement; every use of
 * it is in the three functions below */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
static void nv_ucontext_entry(void) {
    for (;;) {
        nv_vt_body(nv_cur_vt);
    }
}
static void nv_ctx_switch(NvCtx *from, NvCtx *to) { swapcontext(from, to); }
static int nv_ctx_start(NvVThread *vt) {
    size_t page = nv_page_size();
    char *base = (char *)mmap(0, vt->stackSize + page, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_NORESERVE
                                  | MAP_NORESERVE
#endif
                              ,
                              -1, 0);
    if (base == MAP_FAILED) {
        return 0;
    }
    /* a page the stack must not grow into, so an overflow faults instead of
     * quietly becoming somebody else's memory */
    mprotect(base, page, PROT_NONE);
    vt->stackBase = base;
    vt->stackLo = base + page;
    vt->stackTop = base + page + vt->stackSize;
    if (getcontext(&vt->ctx) != 0) {
        munmap(base, vt->stackSize + page);
        vt->stackBase = 0;
        return 0;
    }
    vt->ctx.uc_stack.ss_sp = base + page;
    vt->ctx.uc_stack.ss_size = vt->stackSize;
    vt->ctx.uc_link = 0;
    makecontext(&vt->ctx, nv_ucontext_entry, 0);
    return 1;
}
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif
static void nv_carrier_enter(NvCarrier *c) { (void)c; }
#endif
#else /* no contexts here: `virtual` falls back to an operating system thread */
static void nv_ctx_switch(NvCtx *from, NvCtx *to) {
    (void)from;
    (void)to;
}
static int nv_ctx_start(NvVThread *vt) {
    (void)vt;
    return 0;
}
static void nv_carrier_enter(NvCarrier *c) { (void)c; }
#endif

/* Every switch away from a virtual thread goes through here: it notes where
 * the stack ends, so that a collection scans exactly the live part of it.
 * The frames that matter are the callers', above this one; the margin
 * covers this frame and whatever the switch itself pushes. */
static NV_NOINLINE void nv_vt_switch_out(NvVThread *vt) {
    volatile char marker = 0;
    char *sp = (char *)&marker - 1024;
    vt->parkSp = (vt->stackLo && sp < vt->stackLo) ? vt->stackLo : sp;
    nv_ctx_switch(&vt->ctx, &vt->carrier->sched);
}

/* Parks the running virtual thread. `m` is held on the way in; the carrier
 * releases it once this stack is off the processor, because releasing it
 * here would let the unparker resume a stack that is still running. It is
 * held again when the virtual thread comes back. */
static void nv_vt_park(NvMutexRaw *m) {
    NvVThread *vt = nv_cur_vt;
    vt->state = NV_VT_PARKED;
    vt->parkLock = m;
    nv_vt_switch_out(vt);
    NV_MUTEX_LOCK(m);
}

static void nv_task_complete(int handle, nv result);

static void nv_vt_body(NvVThread *vt) {
    nv result = vt->fn ? vt->fn(vt->args, vt->nargs) : nv_nil;
    vt->fn = 0;
    vt->args = 0;
    vt->state = NV_VT_DONE;
    nv_task_complete(vt->task, result);
    /* Back to the carrier. If this virtual thread comes out of the pool for
     * another task the carrier resumes it right here, and the loop in the
     * entry function runs the next body on the same stack. Nothing on the
     * stack is worth scanning any more. */
    vt->parkSp = vt->stackTop;
    nv_ctx_switch(&vt->ctx, &vt->carrier->sched);
}

static void nv_carrier_loop(NvCarrier *c) {
    NvThread *me = nv_cur_thread;
    nv_carrier_enter(c);
    nv_cur_carrier = c;
    for (;;) {
        NvVThread *vt = nv_runq_take(c);
        int state;
        NvMutexRaw *held;
        nv_cur_vt = vt;
        vt->state = NV_VT_RUNNING;
        me->vt = vt; /* a collection that stops this thread scans that stack */
        nv_ctx_switch(&c->sched, &vt->ctx);
        me->vt = 0;
        nv_cur_vt = 0;
        /* Read the state before releasing the lock the virtual thread parked
         * under - after that release the unparker owns it. */
        state = vt->state;
        held = vt->parkLock;
        vt->parkLock = 0;
        if (held) {
            NV_MUTEX_UNLOCK(held);
        }
        if (state == NV_VT_DONE) {
            NV_MUTEX_LOCK(&c->lock);
            vt->next = c->pool;
            c->pool = vt;
            NV_MUTEX_UNLOCK(&c->lock);
            NV_MUTEX_LOCK(&nv_sched_lock);
            nv_vt_live--;
            NV_MUTEX_UNLOCK(&nv_sched_lock);
        } else if (state == NV_VT_READY) {
            nv_carrier_wake(vt); /* yielded */
        }
    }
}

/* --- operating system threads -------------------------------------- */

#ifdef _WIN32
static DWORD WINAPI nv_carrier_main(LPVOID arg) {
    volatile char marker = 0;
    nv_gc_thread_attach((char *)&marker + 256);
    nv_carrier_loop((NvCarrier *)arg);
    return 0;
}
static int nv_thread_start(LPTHREAD_START_ROUTINE fn, void *arg) {
    HANDLE h = CreateThread(0, 0, fn, arg, 0, 0);
    if (!h) {
        return 0;
    }
    CloseHandle(h);
    return 1;
}
#else
static void *nv_carrier_main(void *arg) {
    volatile char marker = 0;
    nv_gc_thread_attach((char *)&marker + 256);
    nv_carrier_loop((NvCarrier *)arg);
    return 0;
}
static int nv_thread_start(void *(*fn)(void *), void *arg) {
    pthread_t t;
    pthread_attr_t attr;
    int rc;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(&t, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc == 0;
}
#endif

/* Called with nv_sched_lock held. */
static NvCarrier *nv_carrier_add(void) {
    NvCarrier *c;
    if (nv_carriers_running >= nv_carriers_wanted) {
        return 0;
    }
    c = (NvCarrier *)calloc(1, sizeof(NvCarrier));
    if (!c) {
        return 0;
    }
    NV_MUTEX_INIT(&c->lock);
    NV_COND_INIT(&c->cond);
    nv_carrier_table[nv_carriers_running] = c;
    if (!nv_thread_start(nv_carrier_main, c)) {
        free(c);
        return 0;
    }
    nv_carriers_running++;
    return c;
}

/* --- handles ------------------------------------------------------- */

enum { NV_H_TASK = 1, NV_H_LOCK, NV_H_CHAN, NV_H_ATOMIC, NV_H_WG };

typedef struct NvHandleSlot {
    unsigned char kind;
    void *ptr;
} NvHandleSlot;

static NvHandleSlot *nv_handles = 0;
static int nv_handle_len = 0;
static int nv_handle_cap = 0;

/* Called with nv_rt_lock held. Handles are one based; 0 is "none". */
static int nv_handle_new(unsigned char kind, void *ptr) {
    if (nv_handle_len == nv_handle_cap) {
        int cap = nv_handle_cap < 16 ? 16 : nv_handle_cap * 2;
        nv_handles = (NvHandleSlot *)nv_root_realloc(nv_handles, sizeof(NvHandleSlot) * (size_t)nv_handle_cap,
                                                     sizeof(NvHandleSlot) * (size_t)cap);
        nv_handle_cap = cap;
    }
    nv_handles[nv_handle_len].kind = kind;
    nv_handles[nv_handle_len].ptr = ptr;
    return ++nv_handle_len;
}

static void *nv_handle_of(nv v, unsigned char kind, const char *what) {
    long long i = nv_type_of(v) == NV_INT ? nv_ival(v) : -1;
    void *p = 0;
    nv_conc_init();
    NV_MUTEX_LOCK(&nv_rt_lock);
    if (i >= 1 && i <= nv_handle_len && nv_handles[i - 1].kind == kind) {
        p = nv_handles[i - 1].ptr;
    }
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    if (!p) {
        nv_error("not a %s handle: %s", what, nv_display(v));
    }
    return p;
}

/* --- tasks --------------------------------------------------------- */

/* Tasks, channels and the argument records of spawned threads hold values
 * outside any stack, so they are root blocks (nv_root_alloc): the collector
 * scans them like it scans a stack. */
typedef struct NvTask {
    int done;
    nv result;
    NvPark park;
} NvTask;

static void nv_task_complete(int handle, nv result) {
    NvTask *t;
    NV_MUTEX_LOCK(&nv_rt_lock);
    t = (NvTask *)nv_handles[handle - 1].ptr;
    t->result = result;
    t->done = 1;
    nv_unpark_all(&t->park);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
}

typedef struct NvOsTask {
    NvSpawnFn fn;
    nv *args;
    int nargs;
    int task;
} NvOsTask;

#ifdef _WIN32
static DWORD WINAPI nv_os_task_main(LPVOID arg) {
#else
static void *nv_os_task_main(void *arg) {
#endif
    volatile char marker = 0;
    NvOsTask *a = (NvOsTask *)arg;
    int task = a->task;
    nv result;
    nv_gc_thread_attach((char *)&marker + 256);
    result = a->fn(a->args, a->nargs);
    nv_root_free(a);
    nv_task_complete(task, result); /* the result is in a root block before this stack goes */
    nv_gc_thread_detach();
    return 0;
}

/* Everything the runtime builds on first use - the field layout of a class,
 * its fields in name order, its defaults, which constructor a subclass
 * inherits. Built here, on one thread, so that no two threads race to build
 * the same cache once a program has more than one. */
static void nv_class_warm_all(void) {
    int i;
    for (i = 0; i < nv_nclasses; i++) {
        NvClass *c = nv_classes[i];
        nv_class_layout(c);
        nv_field_order(c, nv_class_field_count(c));
        nv_class_defaults(c);
        if (!c->ctorResolved) {
            NvClass *walk = c;
            int guard = 0;
            while (walk && guard++ < 64) {
                if (walk->ctor) {
                    c->resolvedCtor = walk->ctor;
                    break;
                }
                walk = nv_class_base(walk);
            }
            c->ctorResolved = 1;
        }
    }
}

/* The first spawn of a program happens while it is still single threaded,
 * which is the moment to build everything the runtime would otherwise build
 * on first use - class layouts, field orders, defaults - so that no two
 * threads ever race to build the same cache. */
static void nv_conc_boot(void) {
    static int booted = 0;
    nv_conc_init();
    if (booted) {
        return;
    }
    booted = 1;
    nv_class_warm_all();
}

/* Called with c->lock held. */
static NvVThread *nv_vt_take(NvCarrier *c) {
    NvVThread *vt = c->pool;
    if (vt) {
        c->pool = vt->next;
        vt->next = 0;
        return vt;
    }
    vt = (NvVThread *)nv_root_alloc(sizeof(NvVThread)); /* its context and arguments are roots */
    vt->stackSize = nv_vstack_size;
    vt->carrier = c;
    if (!nv_ctx_start(vt)) {
        nv_root_free(vt);
        return 0;
    }
    vt->allNext = nv_all_vts;
    nv_all_vts = vt;
    return vt;
}

/* `thread f(...)` and `virtual f(...)`. The arguments were evaluated by the
 * thread that spawns and are handed to the new one; the result is a task
 * handle to await. */
static nv nv_spawn(int wantVirtual, NvSpawnFn fn, int nargs, ...) {
    va_list ap;
    NvTask *task;
    nv *args = 0;
    int handle, i;
    nv_conc_boot();
    if (nargs > 0) {
        args = (nv *)nv_alloc(sizeof(nv) * (size_t)nargs);
        va_start(ap, nargs);
        for (i = 0; i < nargs; i++) {
            args[i] = va_arg(ap, nv);
        }
        va_end(ap);
    }
    task = (NvTask *)nv_root_alloc(sizeof(NvTask));
    task->result = nv_nil;
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_TASK, task);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
#if NV_HAVE_FIBERS
    if (wantVirtual) {
        NvCarrier *c = 0;
        NvVThread *vt;
        NV_MUTEX_LOCK(&nv_sched_lock);
        /* one carrier to begin with, another whenever the virtual threads
         * outnumber the carriers, up to the parallelism of the machine */
        if (nv_carriers_running == 0 || nv_vt_live >= nv_carriers_running) {
            nv_carrier_add();
        }
        if (nv_carriers_running > 0) {
            c = nv_carrier_table[nv_spawn_turn++ % nv_carriers_running];
            nv_vt_live++;
        }
        NV_MUTEX_UNLOCK(&nv_sched_lock);
        if (c) {
            NV_MUTEX_LOCK(&c->lock);
            vt = nv_vt_take(c);
            if (vt) {
                vt->fn = fn;
                vt->args = args;
                vt->nargs = nargs;
                vt->task = handle;
                nv_runq_push(c, vt);
                NV_MUTEX_UNLOCK(&c->lock);
                return nv_int(handle);
            }
            NV_MUTEX_UNLOCK(&c->lock);
            NV_MUTEX_LOCK(&nv_sched_lock);
            nv_vt_live--;
            NV_MUTEX_UNLOCK(&nv_sched_lock);
            /* no stack to be had: an operating system thread will do */
        }
    }
#else
    (void)wantVirtual;
#endif
    {
        NvOsTask *a = (NvOsTask *)nv_root_alloc(sizeof(NvOsTask));
        a->fn = fn;
        a->args = args;
        a->nargs = nargs;
        a->task = handle;
        if (!nv_thread_start(nv_os_task_main, a)) {
            /* the system refused a thread: run the work here rather than
             * leave a task that is never going to complete */
            nv_root_free(a);
            nv_task_complete(handle, fn(args, nargs));
        }
    }
    return nv_int(handle);
}

/* await: the value of a task, once it has one. Awaiting anything that is not
 * a task is that value, so `await` may be written wherever a result is
 * expected without knowing whether the call it came from was async. */
static nv nv_await(nv v) {
    NvTask *t;
    nv result;
    long long i = nv_type_of(v) == NV_INT ? nv_ival(v) : -1;
    if (i < 1 || !nv_conc_ready) {
        return v;
    }
    NV_MUTEX_LOCK(&nv_rt_lock);
    if (i > nv_handle_len || nv_handles[i - 1].kind != NV_H_TASK) {
        NV_MUTEX_UNLOCK(&nv_rt_lock);
        return v;
    }
    t = (NvTask *)nv_handles[i - 1].ptr;
    while (!t->done) {
        nv_park_self(&t->park, &nv_rt_lock);
    }
    result = t->result;
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return result;
}

static nv nv_task_is_done(nv v) {
    NvTask *t = (NvTask *)nv_handle_of(v, NV_H_TASK, "task");
    int done;
    NV_MUTEX_LOCK(&nv_rt_lock);
    done = t->done;
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_bool(done);
}

static nv nv_await_all(nv tasks) {
    nv out;
    int i;
    if (nv_type_of(tasks) != NV_ARR) {
        nv_error("awaitAll expects an array of tasks");
    }
    out = nv_new(NV_ARR);
    out->a = nv_arr_new_cap(tasks->a->len);
    for (i = 0; i < tasks->a->len; i++) {
        nv_arr_push(out->a, nv_await(tasks->a->items[i]));
    }
    return out;
}

/* --- giving up the processor --------------------------------------- */

static nv nv_thread_yield(void) {
    NvVThread *vt = nv_cur_vt;
    if (!vt) {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
        return nv_nil;
    }
    vt->state = NV_VT_READY;
    nv_vt_switch_out(vt);
    return nv_nil;
}

/* Sleeping a virtual thread parks it on its carrier's timer queue; the
 * carrier goes on running other virtual threads and wakes it when due. */
static nv nv_thread_sleep(nv msValue) {
    long long ms = nv_as_int(msValue);
    NvVThread *vt = nv_cur_vt;
    NvCarrier *c;
    NvVThread **at;
    if (ms <= 0) {
        return nv_thread_yield();
    }
    if (!vt) {
        return nv_os_sleep(msValue);
    }
    c = vt->carrier;
    NV_MUTEX_LOCK(&c->lock);
    vt->wakeAt = nv_now_ms() + ms;
    at = &c->timers;
    while (*at && (*at)->wakeAt <= vt->wakeAt) {
        at = &(*at)->next;
    }
    vt->next = *at;
    *at = vt;
    nv_vt_park(&c->lock);
    NV_MUTEX_UNLOCK(&c->lock);
    return nv_nil;
}

static nv nv_thread_cpus(void) { return nv_int(nv_cpu_count()); }

static nv nv_thread_parallelism(void) {
    nv_conc_init();
    return nv_int(nv_carriers_wanted);
}

static nv nv_thread_set_parallelism(nv n) {
    long long want = nv_as_int(n);
    nv_conc_init();
    if (want < 1) {
        want = 1;
    }
    if (want > NV_MAX_CARRIERS) {
        want = NV_MAX_CARRIERS;
    }
    NV_MUTEX_LOCK(&nv_sched_lock);
    nv_carriers_wanted = (int)want;
    NV_MUTEX_UNLOCK(&nv_sched_lock);
    return nv_nil;
}

static nv nv_thread_is_virtual(void) { return nv_bool(nv_cur_vt != 0); }

static nv nv_thread_virtual_supported(void) { return nv_bool(NV_HAVE_FIBERS != 0); }

/* A number that is the same for every call from one runner and different for
 * every other runner. */
static nv nv_thread_self_id(void) {
    return nv_int((long long)(((uintptr_t)nv_runner() >> 4) & 0x7fffffff));
}

static nv nv_thread_running(void) {
    int n;
    nv_conc_init();
    NV_MUTEX_LOCK(&nv_sched_lock);
    n = nv_vt_live;
    NV_MUTEX_UNLOCK(&nv_sched_lock);
    return nv_int(n);
}

/* --- locks --------------------------------------------------------- */

typedef struct NvLock {
    NvMutexRaw m;
    void *owner;
    int count;
    NvPark park;
} NvLock;

static NvLock *nv_lock_make(void) {
    NvLock *l = (NvLock *)calloc(1, sizeof(NvLock));
    if (!l) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&l->m);
    return l;
}

static nv nv_lock_new(void) {
    NvLock *l;
    int handle;
    nv_conc_init();
    l = nv_lock_make();
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_LOCK, l);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

/* Re-entrant: the runner that holds a lock walks back into it, which is what
 * a `sync` block inside a method a `sync` block called needs. */
static void nv_lock_enter(NvLock *l) {
    void *me = nv_runner();
    NV_MUTEX_LOCK(&l->m);
    if (l->owner == me) {
        l->count++;
        NV_MUTEX_UNLOCK(&l->m);
        return;
    }
    while (l->owner) {
        nv_park_self(&l->park, &l->m);
    }
    l->owner = me;
    l->count = 1;
    NV_MUTEX_UNLOCK(&l->m);
}

static void nv_lock_leave(NvLock *l) {
    NV_MUTEX_LOCK(&l->m);
    if (l->count > 0 && --l->count == 0) {
        l->owner = 0;
        nv_unpark_one(&l->park);
    }
    NV_MUTEX_UNLOCK(&l->m);
}

static nv nv_lock_acquire(nv h) {
    nv_lock_enter((NvLock *)nv_handle_of(h, NV_H_LOCK, "lock"));
    return nv_nil;
}

static nv nv_lock_release(nv h) {
    nv_lock_leave((NvLock *)nv_handle_of(h, NV_H_LOCK, "lock"));
    return nv_nil;
}

static nv nv_lock_try_acquire(nv h) {
    NvLock *l = (NvLock *)nv_handle_of(h, NV_H_LOCK, "lock");
    void *me = nv_runner();
    int got = 0;
    NV_MUTEX_LOCK(&l->m);
    if (l->owner == me) {
        l->count++;
        got = 1;
    } else if (!l->owner) {
        l->owner = me;
        l->count = 1;
        got = 1;
    }
    NV_MUTEX_UNLOCK(&l->m);
    return nv_bool(got);
}

/* The lock behind a bare `sync { ... }`: one per program, made on first use.
 * `sync (lock) { ... }` names one instead. */
static NvLock *nv_sync_default = 0;

static NvLock *nv_sync_enter(nv lock) {
    NvLock *l;
    nv_conc_init();
    if (nv_type_of(lock) == NV_INT) {
        l = (NvLock *)nv_handle_of(lock, NV_H_LOCK, "lock");
    } else {
        NV_MUTEX_LOCK(&nv_rt_lock);
        if (!nv_sync_default) {
            nv_sync_default = nv_lock_make();
        }
        l = nv_sync_default;
        NV_MUTEX_UNLOCK(&nv_rt_lock);
    }
    nv_lock_enter(l);
    return l;
}

static void nv_sync_leave(NvLock *l) { nv_lock_leave(l); }

/* --- channels ------------------------------------------------------ */

typedef struct NvChan {
    NvMutexRaw m;
    nv *items;
    int cap;      /* slots in items */
    int declared; /* what the program asked for; 0 is unbuffered */
    int len;
    int head;
    int closed;
    NvPark senders;
    NvPark receivers;
} NvChan;

static nv nv_chan_new(nv capValue) {
    long long want = nv_as_int(capValue);
    NvChan *c;
    int handle;
    nv_conc_init();
    c = (NvChan *)nv_root_alloc(sizeof(NvChan));
    NV_MUTEX_INIT(&c->m);
    c->declared = want < 0 ? 0 : (int)want;
    c->cap = c->declared < 1 ? 1 : c->declared;
    c->items = (nv *)nv_root_alloc(sizeof(nv) * (size_t)c->cap);
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_CHAN, c);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

/* false when the channel was closed and the value could not be delivered. */
static nv nv_chan_send(nv h, nv value) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    NV_MUTEX_LOCK(&c->m);
    while (!c->closed && c->len == c->cap) {
        nv_park_self(&c->senders, &c->m);
    }
    if (c->closed) {
        NV_MUTEX_UNLOCK(&c->m);
        return nv_bool(0);
    }
    c->items[(c->head + c->len) % c->cap] = value;
    c->len++;
    nv_unpark_one(&c->receivers);
    if (c->declared == 0) {
        /* unbuffered: the send is over when the value has been taken */
        while (!c->closed && c->len > 0) {
            nv_park_self(&c->senders, &c->m);
        }
    }
    NV_MUTEX_UNLOCK(&c->m);
    return nv_bool(1);
}

/* A received value can be anything, the absence of one included, so the
 * receiving thread is told which of the two it got - the same way `net`
 * reports what its last call did. */
static NV_TLS int nv_chan_got = 0;

static nv nv_chan_received(void) { return nv_bool(nv_chan_got); }

static nv nv_chan_recv(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    nv v;
    nv_chan_got = 0;
    NV_MUTEX_LOCK(&c->m);
    while (c->len == 0 && !c->closed) {
        nv_park_self(&c->receivers, &c->m);
    }
    if (c->len == 0) {
        NV_MUTEX_UNLOCK(&c->m); /* closed and drained */
        return nv_nil;
    }
    nv_chan_got = 1;
    v = c->items[c->head];
    c->head = (c->head + 1) % c->cap;
    c->len--;
    nv_unpark_all(&c->senders);
    NV_MUTEX_UNLOCK(&c->m);
    return v;
}

static nv nv_chan_try_recv(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    nv v = nv_nil;
    nv_chan_got = 0;
    NV_MUTEX_LOCK(&c->m);
    if (c->len > 0) {
        nv_chan_got = 1;
        v = c->items[c->head];
        c->head = (c->head + 1) % c->cap;
        c->len--;
        nv_unpark_all(&c->senders);
    }
    NV_MUTEX_UNLOCK(&c->m);
    return v;
}

static nv nv_chan_close(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    NV_MUTEX_LOCK(&c->m);
    c->closed = 1;
    nv_unpark_all(&c->senders);
    nv_unpark_all(&c->receivers);
    NV_MUTEX_UNLOCK(&c->m);
    return nv_nil;
}

static nv nv_chan_is_closed(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    int closed;
    NV_MUTEX_LOCK(&c->m);
    closed = c->closed;
    NV_MUTEX_UNLOCK(&c->m);
    return nv_bool(closed);
}

static nv nv_chan_length(nv h) {
    NvChan *c = (NvChan *)nv_handle_of(h, NV_H_CHAN, "channel");
    int len;
    NV_MUTEX_LOCK(&c->m);
    len = c->len;
    NV_MUTEX_UNLOCK(&c->m);
    return nv_int(len);
}

/* --- counters ------------------------------------------------------ */

typedef struct NvCounter {
    NvMutexRaw m;
    long long value;
} NvCounter;

static nv nv_counter_new(nv initial) {
    NvCounter *a;
    int handle;
    nv_conc_init();
    a = (NvCounter *)calloc(1, sizeof(NvCounter));
    if (!a) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&a->m);
    a->value = nv_as_int(initial);
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_ATOMIC, a);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

static nv nv_counter_add(nv h, nv delta) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    long long v;
    NV_MUTEX_LOCK(&a->m);
    a->value += nv_as_int(delta);
    v = a->value;
    NV_MUTEX_UNLOCK(&a->m);
    return nv_int(v);
}

static nv nv_counter_get(nv h) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    long long v;
    NV_MUTEX_LOCK(&a->m);
    v = a->value;
    NV_MUTEX_UNLOCK(&a->m);
    return nv_int(v);
}

static nv nv_counter_set(nv h, nv value) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    NV_MUTEX_LOCK(&a->m);
    a->value = nv_as_int(value);
    NV_MUTEX_UNLOCK(&a->m);
    return nv_nil;
}

static nv nv_counter_swap(nv h, nv expect, nv value) {
    NvCounter *a = (NvCounter *)nv_handle_of(h, NV_H_ATOMIC, "counter");
    int swapped = 0;
    NV_MUTEX_LOCK(&a->m);
    if (a->value == nv_as_int(expect)) {
        a->value = nv_as_int(value);
        swapped = 1;
    }
    NV_MUTEX_UNLOCK(&a->m);
    return nv_bool(swapped);
}

/* --- groups -------------------------------------------------------- */

typedef struct NvGroup {
    NvMutexRaw m;
    int count;
    NvPark park;
} NvGroup;

static nv nv_group_new(void) {
    NvGroup *g;
    int handle;
    nv_conc_init();
    g = (NvGroup *)calloc(1, sizeof(NvGroup));
    if (!g) {
        nv_error("out of memory");
    }
    NV_MUTEX_INIT(&g->m);
    NV_MUTEX_LOCK(&nv_rt_lock);
    handle = nv_handle_new(NV_H_WG, g);
    NV_MUTEX_UNLOCK(&nv_rt_lock);
    return nv_int(handle);
}

static nv nv_group_add(nv h, nv n) {
    NvGroup *g = (NvGroup *)nv_handle_of(h, NV_H_WG, "group");
    NV_MUTEX_LOCK(&g->m);
    g->count += (int)nv_as_int(n);
    if (g->count <= 0) {
        nv_unpark_all(&g->park);
    }
    NV_MUTEX_UNLOCK(&g->m);
    return nv_nil;
}

static nv nv_group_done(nv h) {
    NvGroup *g = (NvGroup *)nv_handle_of(h, NV_H_WG, "group");
    NV_MUTEX_LOCK(&g->m);
    g->count--;
    if (g->count <= 0) {
        nv_unpark_all(&g->park);
    }
    NV_MUTEX_UNLOCK(&g->m);
    return nv_nil;
}

static nv nv_group_wait(nv h) {
    NvGroup *g = (NvGroup *)nv_handle_of(h, NV_H_WG, "group");
    NV_MUTEX_LOCK(&g->m);
    while (g->count > 0) {
        nv_park_self(&g->park, &g->m);
    }
    NV_MUTEX_UNLOCK(&g->m);
    return nv_nil;
}

/* --- what the collector needs to know ------------------------------ */

/* The stack of a virtual thread, for a collector that stopped the carrier
 * running it. */
static int nv_gc_vt_bounds(void *vtp, char **lo, char **hi) {
    NvVThread *vt = (NvVThread *)vtp;
    if (!vt || !vt->stackTop) {
        return 0;
    }
    *lo = vt->stackLo;
    *hi = vt->stackTop;
    return 1;
}

/* Every virtual thread's stack from where it last switched away. A running
 * one is scanned from its carrier's stopped stack pointer as well; what
 * lies between the two is stale and merely harmless. */
static void nv_gc_scan_fibers(void) {
    NvVThread *vt;
    for (vt = nv_all_vts; vt; vt = vt->allNext) {
        if (vt->parkSp && vt->stackTop && vt->parkSp < vt->stackTop) {
            nv_gc_scan_range(vt->parkSp, vt->stackTop);
        }
    }
}

#endif /* NV_THREADS_H */
