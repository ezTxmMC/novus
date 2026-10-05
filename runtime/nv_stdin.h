/* nv_stdin.h - io.readBytes and io.eof: reading standard input by byte count. Part of the Novus runtime; included by novus_rt.h.
 * Depends on: nv_memory.h (nv_alloc_atomic), nv_data.h (nv_str_own, nv_strn, nv_error). */
#ifndef NV_STDIN_H
#define NV_STDIN_H

/* ------------------------------------------------------------------ */
/* io.readBytes / io.eof                                               */
/* ------------------------------------------------------------------ */

/* A request that large is never a real message: the limit keeps a hostile
 * length from exhausting memory. It stays far below what a string can hold
 * (INT_MAX bytes), so no size computation below can overflow. */
#define NV_READ_MAX_BYTES (1LL << 30)

/* Up to this size one buffer of the announced size is allocated at once. A
 * sender that announces more than it delivers costs at most this much. */
#define NV_READ_DIRECT_LIMIT (1LL << 20)

/* The text of the first `got` bytes of `buffer`, which holds `capacity`
 * bytes (and one more for the terminator). A buffer that was not filled is
 * copied to an exact fit, so a short stream does not keep a large block alive. */
static nv nv_read_finish(char *buffer, size_t got, size_t capacity) {
    buffer[got] = 0;
    if (got == capacity) {
        return nv_str_own(buffer, (int)got);
    }
    return nv_strn(buffer, (int)got);
}

static nv nv_read_bytes_direct(size_t count) {
    char *buffer = (char *)nv_alloc_atomic(count + 1);
    size_t got = fread(buffer, 1, count, stdin);
    return nv_read_finish(buffer, got, count);
}

/* A bigger buffer holding the `used` bytes read so far. */
static char *nv_read_regrow(const char *buffer, size_t used, size_t capacity) {
    char *grown = (char *)nv_alloc_atomic(capacity + 1);
    memcpy(grown, buffer, used);
    return grown;
}

/* Large counts start small and double with what actually arrives, so a
 * length that promises more than the sender delivers costs only about twice
 * the bytes received. The data is read straight into the buffer: no chunk
 * buffer on the stack, which a virtual thread has little of. */
static nv nv_read_bytes_chunked(size_t count) {
    size_t capacity = (size_t)NV_READ_DIRECT_LIMIT;
    char *buffer = (char *)nv_alloc_atomic(capacity + 1);
    size_t got = 0;
    while (got < count) {
        size_t want;
        size_t received;
        if (got == capacity) {
            capacity = capacity * 2 < count ? capacity * 2 : count;
            buffer = nv_read_regrow(buffer, got, capacity);
        }
        want = capacity - got;
        received = fread(buffer + got, 1, want, stdin);
        got += received;
        if (received < want) {
            break;
        }
    }
    return nv_read_finish(buffer, got, capacity);
}

/* Exactly `count` bytes from stdin, NUL safe; fewer only at end of input.
 * Blocks until they arrived (the carrier thread of a virtual thread too). */
static nv nv_read_bytes(nv count) {
    long long wanted = nv_as_int(count);
    if (wanted <= 0) {
        return nv_str("");
    }
    if (wanted > NV_READ_MAX_BYTES) {
        nv_error("io.readBytes: %lld bytes is more than the limit of %lld", wanted, NV_READ_MAX_BYTES);
    }
    if (wanted <= NV_READ_DIRECT_LIMIT) {
        return nv_read_bytes_direct((size_t)wanted);
    }
    return nv_read_bytes_chunked((size_t)wanted);
}

/* Whether stdin is at its end. Looks at the next byte and puts it back, so
 * it waits when the input is open but idle - exactly as the next read would. */
static nv nv_io_eof(void) {
    int next = getc(stdin);
    if (next == EOF) {
        return nv_bool(1);
    }
    ungetc(next, stdin);
    return nv_bool(0);
}

#endif /* NV_STDIN_H */
