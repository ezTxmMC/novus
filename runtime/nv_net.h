/* nv_net.h - TCP sockets. Part of the Novus runtime; included by novus_rt.h. */
#ifndef NV_NET_H
#define NV_NET_H

/* ------------------------------------------------------------------ */
/* TCP sockets                                                         */
/* ------------------------------------------------------------------ */

/* Non-blocking sockets plus poll(): one thread drives every connection, which
 * is what a tick loop wants anyway. A socket is an integer handle in Novus;
 * net.status() reports what the last call did, because a Novus method cannot
 * return a value and an error at once. */
#ifdef _WIN32
typedef SOCKET nv_sock;
#define NV_BADSOCK INVALID_SOCKET
#define NV_SOCKERR(e) (WSAGetLastError() == (e))
#define NV_EWOULDBLOCK WSAEWOULDBLOCK
#else
typedef int nv_sock;
#define NV_BADSOCK (-1)
#define NV_SOCKERR(e) (errno == (e))
#define NV_EWOULDBLOCK EWOULDBLOCK
#endif

enum { NV_NET_OK = 0, NV_NET_AGAIN = 1, NV_NET_CLOSED = 2, NV_NET_ERROR = 3 };

static NV_TLS int nv_net_last = NV_NET_OK;
static NV_TLS char nv_net_message[256];

static void nv_net_fail(const char *what) {
    nv_net_last = NV_NET_ERROR;
#ifdef _WIN32
    snprintf(nv_net_message, sizeof(nv_net_message), "%s: winsock error %d", what, WSAGetLastError());
#else
    snprintf(nv_net_message, sizeof(nv_net_message), "%s: %s", what, strerror(errno));
#endif
}

static void nv_net_start(void) {
#ifdef _WIN32
    static int started = 0;
    if (!started) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        started = 1;
    }
#endif
}

static int nv_net_would_block(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static void nv_net_nonblocking(nv_sock s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void nv_net_shutclose(nv_sock s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

/* Resolves host:port. An empty or "0.0.0.0" host binds every interface. */
static struct addrinfo *nv_net_resolve(const char *host, int port, int passive) {
    struct addrinfo hints;
    struct addrinfo *result = 0;
    char service[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo((host && host[0]) ? host : 0, service, &hints, &result) != 0) {
        return 0;
    }
    return result;
}

static nv nv_net_listen(nv host, nv port) {
    struct addrinfo *info;
    nv_sock s;
    int on = 1;
    nv_net_start();
    nv_net_last = NV_NET_OK;
    info = nv_net_resolve(nv_cstr(host), (int)nv_as_int(port), 1);
    if (!info) {
        nv_net_last = NV_NET_ERROR;
        snprintf(nv_net_message, sizeof(nv_net_message), "listen: cannot resolve host");
        return nv_int(-1);
    }
    s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (s == NV_BADSOCK) {
        nv_net_fail("socket");
        freeaddrinfo(info);
        return nv_int(-1);
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    if (bind(s, info->ai_addr, (int)info->ai_addrlen) != 0) {
        nv_net_fail("bind");
        nv_net_shutclose(s);
        freeaddrinfo(info);
        return nv_int(-1);
    }
    freeaddrinfo(info);
    if (listen(s, 128) != 0) {
        nv_net_fail("listen");
        nv_net_shutclose(s);
        return nv_int(-1);
    }
    nv_net_nonblocking(s);
    return nv_int((long long)s);
}

/* -1 and status AGAIN when no connection is waiting. */
static nv nv_net_accept(nv server) {
    nv_sock s = (nv_sock)nv_as_int(server);
    nv_sock c;
    int on = 1;
    nv_net_last = NV_NET_OK;
    c = accept(s, 0, 0);
    if (c == NV_BADSOCK) {
        nv_net_last = nv_net_would_block() ? NV_NET_AGAIN : NV_NET_ERROR;
        if (nv_net_last == NV_NET_ERROR) {
            nv_net_fail("accept");
        }
        return nv_int(-1);
    }
    nv_net_nonblocking(c);
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
    return nv_int((long long)c);
}

static nv nv_net_connect(nv host, nv port) {
    struct addrinfo *info;
    nv_sock s;
    int on = 1;
    nv_net_start();
    nv_net_last = NV_NET_OK;
    info = nv_net_resolve(nv_cstr(host), (int)nv_as_int(port), 0);
    if (!info) {
        nv_net_last = NV_NET_ERROR;
        snprintf(nv_net_message, sizeof(nv_net_message), "connect: cannot resolve host");
        return nv_int(-1);
    }
    s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (s == NV_BADSOCK) {
        nv_net_fail("socket");
        freeaddrinfo(info);
        return nv_int(-1);
    }
    if (connect(s, info->ai_addr, (int)info->ai_addrlen) != 0) {
        nv_net_fail("connect");
        nv_net_shutclose(s);
        freeaddrinfo(info);
        return nv_int(-1);
    }
    freeaddrinfo(info);
    nv_net_nonblocking(s);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
    return nv_int((long long)s);
}

/* Up to `max` bytes. "" with status AGAIN means nothing was ready, "" with
 * status CLOSED means the peer hung up. */
static nv nv_net_recv(nv sock, nv max) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    long long want = nv_as_int(max);
    char *buf;
    long long got;
    nv_net_last = NV_NET_OK;
    if (want <= 0) {
        return nv_str("");
    }
    if (want > 1024 * 1024) {
        want = 1024 * 1024;
    }
    buf = (char *)nv_alloc_atomic((size_t)want + 1);
    got = (long long)recv(s, buf, (int)want, 0);
    if (got == 0) {
        nv_net_last = NV_NET_CLOSED;
        return nv_str("");
    }
    if (got < 0) {
        nv_net_last = nv_net_would_block() ? NV_NET_AGAIN : NV_NET_ERROR;
        if (nv_net_last == NV_NET_ERROR) {
            nv_net_fail("recv");
        }
        return nv_str("");
    }
    buf[got] = 0;
    return nv_str_own(buf, (int)got);
}

/* Bytes actually handed to the kernel; the caller keeps the remainder in its
 * own out-buffer and retries (status AGAIN when the socket is full). */
static nv nv_net_send(nv sock, nv data) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    int len;
    const char *bytes = nv_bin(data, &len);
    long long sent;
    nv_net_last = NV_NET_OK;
    if (len == 0) {
        return nv_int(0);
    }
#ifdef MSG_NOSIGNAL
    sent = (long long)send(s, bytes, len, MSG_NOSIGNAL);
#else
    sent = (long long)send(s, bytes, len, 0);
#endif
    if (sent < 0) {
        nv_net_last = nv_net_would_block() ? NV_NET_AGAIN : NV_NET_ERROR;
        if (nv_net_last == NV_NET_ERROR) {
            nv_net_fail("send");
        }
        return nv_int(0);
    }
    return nv_int(sent);
}

static nv nv_net_close(nv sock) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    if ((long long)s >= 0) {
        nv_net_shutclose(s);
    }
    return nv_nil;
}

/* poll() that sleeps on after a collection's signal interrupted it, for
 * whatever remains of the timeout (-1 blocks, 0 only looks). */
#ifdef _WIN32
static int nv_net_poll_fds(WSAPOLLFD *fds, int n, long long timeoutMs) {
    return WSAPoll(fds, (ULONG)n, (INT)timeoutMs);
}
#else
static int nv_net_poll_fds(struct pollfd *fds, int n, long long timeoutMs) {
    long long deadline = timeoutMs < 0 ? 0 : nv_now_ms() + timeoutMs;
    int count;
    for (;;) {
        count = poll(fds, (nfds_t)n, (int)timeoutMs);
        if (count >= 0 || errno != EINTR) {
            return count;
        }
        if (timeoutMs >= 0) {
            long long left = deadline - nv_now_ms();
            timeoutMs = left < 0 ? 0 : left;
        }
    }
}
#endif

/* Waits until one of `sockets` is readable (or `timeoutMs` passes) and
 * returns those that are. A timeout of 0 polls, -1 blocks. */
static nv nv_net_poll(nv sockets, nv timeoutMs) {
    NvArr *a;
    nv out = nv_arr();
    int i;
    int count;
#ifdef _WIN32
    WSAPOLLFD *fds;
#else
    struct pollfd *fds;
#endif
    nv_net_last = NV_NET_OK;
    if (nv_type_of(sockets) != NV_ARR) {
        return out;
    }
    a = sockets->a;
    if (a->len == 0) {
        return out;
    }
#ifdef _WIN32
    fds = (WSAPOLLFD *)nv_alloc_atomic(sizeof(WSAPOLLFD) * (size_t)a->len);
#else
    fds = (struct pollfd *)nv_alloc_atomic(sizeof(struct pollfd) * (size_t)a->len);
#endif
    for (i = 0; i < a->len; i++) {
        fds[i].fd = (nv_sock)nv_as_int(a->items[i]);
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }
    count = nv_net_poll_fds(fds, a->len, nv_as_int(timeoutMs));
    if (count < 0) {
        if (!nv_net_would_block()) {
            nv_net_fail("poll");
        }
        return out;
    }
    for (i = 0; i < a->len; i++) {
        if (fds[i].revents != 0) {
            nv_arr_push(out->a, a->items[i]);
        }
    }
    return out;
}

/* Like poll(), but reports writability - used to drain a backed up send
 * buffer without spinning. */
static nv nv_net_poll_write(nv sockets, nv timeoutMs) {
    NvArr *a;
    nv out = nv_arr();
    int i;
    int count;
#ifdef _WIN32
    WSAPOLLFD *fds;
#else
    struct pollfd *fds;
#endif
    nv_net_last = NV_NET_OK;
    if (nv_type_of(sockets) != NV_ARR) {
        return out;
    }
    a = sockets->a;
    if (a->len == 0) {
        return out;
    }
#ifdef _WIN32
    fds = (WSAPOLLFD *)nv_alloc_atomic(sizeof(WSAPOLLFD) * (size_t)a->len);
#else
    fds = (struct pollfd *)nv_alloc_atomic(sizeof(struct pollfd) * (size_t)a->len);
#endif
    for (i = 0; i < a->len; i++) {
        fds[i].fd = (nv_sock)nv_as_int(a->items[i]);
        fds[i].events = POLLOUT;
        fds[i].revents = 0;
    }
    count = nv_net_poll_fds(fds, a->len, nv_as_int(timeoutMs));
    if (count < 0) {
        return out;
    }
    for (i = 0; i < a->len; i++) {
        if (fds[i].revents != 0) {
            nv_arr_push(out->a, a->items[i]);
        }
    }
    return out;
}

static nv nv_net_status(void) { return nv_int(nv_net_last); }
static nv nv_net_error(void) { return nv_str(nv_net_message); }

/* "1.2.3.4:56789" of the connected peer. */
static nv nv_net_peer(nv sock) {
    nv_sock s = (nv_sock)nv_as_int(sock);
    struct sockaddr_storage addr;
    char host[64];
    char out[96];
#ifdef _WIN32
    int len = (int)sizeof(addr);
#else
    socklen_t len = (socklen_t)sizeof(addr);
#endif
    if (getpeername(s, (struct sockaddr *)&addr, &len) != 0) {
        return nv_str("");
    }
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)&addr;
        if (!inet_ntop(AF_INET, &v4->sin_addr, host, sizeof(host))) {
            return nv_str("");
        }
        snprintf(out, sizeof(out), "%s:%d", host, (int)ntohs(v4->sin_port));
        return nv_str(out);
    }
    return nv_str("");
}


#endif /* NV_NET_H */
