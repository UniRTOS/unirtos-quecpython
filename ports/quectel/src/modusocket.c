#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#include "shared/netutils/netutils.h"

#ifndef NO_QSTR
#include "qosa_sockets.h"
#include "qosa_datacall.h"
#include "qosa_ip_addr.h"
#include "qpy_compat_common.h"
#endif

#if MICROPY_QPY_MODULE_USOCKET

// Poll granularity used to translate Python-level timeouts into lwip socket
// receive/send timeouts. Matches the legacy QuecPython implementation.
#define SOCKET_POLL_US (100000)

typedef struct _socket_obj_t {
    mp_obj_base_t base;
    int fd;
    uint8_t domain;
    uint8_t type;
    uint8_t proto;
    bool peer_closed;
    bool connect_flag;
    bool listening;
    unsigned int retries;
    unsigned int timeout;
    unsigned int bind_port;
    char host[64];
    int port;
} socket_obj_t;

static void _socket_settimeout(socket_obj_t *sock, uint64_t timeout_ms);

static void _socket_check_pending(void) {
    mp_handle_pending(true);
}

static void exception_from_errno(int e) {
    mp_raise_OSError(e);
}

// Locate an active PDP context and bind the freshly created socket to the
// local IP so traffic is routed through the cellular data call, mirroring the
// behaviour of the legacy modsocket implementation.
static void _socket_bind_local(socket_obj_t *sock) {
    qosa_datacall_ip_info_t info = {0};
    bool found = false;
    for (qosa_uint8_t cid = 1; cid < 5; cid++) {
        if (!qosa_datacall_get_pdp_status(0, cid)) {
            continue;
        }
        memset(&info, 0, sizeof(info));
        if (qosa_datacall_get_pdp_ip_info(0, cid, &info) != QOSA_DATACALL_OK) {
            continue;
        }
        if ((sock->domain == AF_INET) && (info.ipv4_ip.ip_vsn == QOSA_PDP_IPV4)) {
            found = true;
            break;
        }
        if ((sock->domain == AF_INET6) && (info.ipv6_ip.ip_vsn == QOSA_PDP_IPV6)) {
            found = true;
            break;
        }
    }

    if (!found) {
        return;
    }

    if (sock->domain == AF_INET) {
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_port = 0;
        local.sin_addr.s_addr = info.ipv4_ip.apptcpip_ipv4_addr.s_addr;
        lwip_bind(sock->fd, (struct sockaddr *)&local, sizeof(local));
    } else if (sock->domain == AF_INET6) {
        struct sockaddr_in6 local;
        memset(&local, 0, sizeof(local));
        local.sin6_family = AF_INET6;
        local.sin6_port = 0;
        memcpy(&local.sin6_addr, &info.ipv6_ip.apptcpip_ipv6_addr, sizeof(struct in6_addr));
        lwip_bind(sock->fd, (struct sockaddr *)&local, sizeof(local));
    }
}

static mp_obj_t socket_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 3, false);

    socket_obj_t *sock = mp_obj_malloc_with_finaliser(socket_obj_t, type);
    sock->domain = AF_INET;
    sock->type = SOCK_STREAM;
    sock->proto = 0;
    sock->peer_closed = false;
    sock->connect_flag = false;
    sock->listening = false;
    sock->bind_port = 0;
    sock->host[0] = '\0';
    sock->port = 0;

    if (n_args > 0) {
        sock->domain = mp_obj_get_int(args[0]);
        if (n_args > 1) {
            sock->type = mp_obj_get_int(args[1]);
            if (n_args > 2) {
                sock->proto = mp_obj_get_int(args[2]);
            }
        }
    }

    sock->fd = lwip_socket(sock->domain, sock->type, sock->proto);
    if (sock->fd < 0) {
        exception_from_errno(errno);
    }

    _socket_bind_local(sock);
    _socket_settimeout(sock, UINT64_MAX);
    return MP_OBJ_FROM_PTR(sock);
}

// --- DNS -----------------------------------------------------------------

static int _socket_getaddrinfo2(const mp_obj_t host_in, const mp_obj_t port_in, uint8_t family, struct addrinfo **resp) {
    mp_obj_t port = port_in;
    if (mp_obj_is_small_int(port)) {
        port = mp_obj_str_binary_op(MP_BINARY_OP_MODULO, mp_obj_new_str("%s", 2), port);
    }

    const char *host_str = mp_obj_str_get_str(host_in);
    const char *port_str = mp_obj_str_get_str(port);
    if (host_str[0] == '\0') {
        host_str = "0.0.0.0";
    }

    struct addrinfo hints = {0};
    hints.ai_family = (family == AF_INET || family == AF_INET6) ? family : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    MP_THREAD_GIL_EXIT();
    int res = getaddrinfo(host_str, port_str, &hints, resp);
    MP_THREAD_GIL_ENTER();
    return res;
}

static void _socket_getaddrinfo(const mp_obj_t addrtuple, uint8_t family, struct addrinfo **resp) {
    mp_obj_t *elem;
    mp_obj_get_array_fixed_n(addrtuple, 2, &elem);
    int res = _socket_getaddrinfo2(elem[0], elem[1], family, resp);
    if (res != 0) {
        mp_raise_OSError(res);
    }
    if (*resp == NULL) {
        mp_raise_OSError(-2);
    }
}

static mp_obj_t socket_getaddrinfo(size_t n_args, const mp_obj_t *args) {
    struct addrinfo *res = NULL;
    uint8_t family = (n_args >= 3) ? (uint8_t)mp_obj_get_int(args[2]) : 0;
    int ret = _socket_getaddrinfo2(args[0], args[1], family, &res);
    if (ret != 0) {
        mp_raise_OSError(ret);
    }

    mp_obj_t ret_list = mp_obj_new_list(0, NULL);
    for (struct addrinfo *resi = res; resi; resi = resi->ai_next) {
        mp_obj_t addrinfo_objs[5] = {
            mp_obj_new_int(resi->ai_family),
            mp_obj_new_int(resi->ai_socktype),
            mp_obj_new_int(resi->ai_protocol),
            mp_obj_new_str(resi->ai_canonname ? resi->ai_canonname : "", resi->ai_canonname ? strlen(resi->ai_canonname) : 0),
            mp_const_none,
        };

        if (resi->ai_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)resi->ai_addr;
            char buf[16] = {0};
            inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
            mp_obj_t inaddr_objs[2] = {
                mp_obj_new_str(buf, strlen(buf)),
                mp_obj_new_int(lwip_ntohs(addr->sin_port)),
            };
            addrinfo_objs[4] = mp_obj_new_tuple(2, inaddr_objs);
        } else if (resi->ai_family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)resi->ai_addr;
            char buf[64] = {0};
            inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf));
            mp_obj_t inaddr_objs[2] = {
                mp_obj_new_str(buf, strlen(buf)),
                mp_obj_new_int(lwip_ntohs(addr->sin6_port)),
            };
            addrinfo_objs[4] = mp_obj_new_tuple(2, inaddr_objs);
        }
        mp_obj_list_append(ret_list, mp_obj_new_tuple(5, addrinfo_objs));
    }

    if (res) {
        lwip_freeaddrinfo(res);
    }
    return ret_list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_getaddrinfo_obj, 2, 6, socket_getaddrinfo);

// --- timeout / blocking --------------------------------------------------

static void _socket_settimeout(socket_obj_t *sock, uint64_t timeout_ms) {
    sock->retries = (timeout_ms == UINT64_MAX) ? UINT_MAX : (unsigned int)(timeout_ms * 1000 / SOCKET_POLL_US);
    sock->timeout = (unsigned int)(timeout_ms / 1000);

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = timeout_ms ? SOCKET_POLL_US : 0,
    };
    lwip_setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    lwip_setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    lwip_fcntl(sock->fd, F_SETFL, timeout_ms ? 0 : O_NONBLOCK);
}

static mp_obj_t socket_settimeout(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    if (arg1 == mp_const_none) {
        _socket_settimeout(self, UINT64_MAX);
    } else {
        #if MICROPY_PY_BUILTINS_FLOAT
        _socket_settimeout(self, (uint64_t)(mp_obj_get_float(arg1) * 1000.0));
        #else
        _socket_settimeout(self, (uint64_t)mp_obj_get_int(arg1) * 1000);
        #endif
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_settimeout_obj, socket_settimeout);

static mp_obj_t socket_setblocking(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    _socket_settimeout(self, mp_obj_is_true(arg1) ? UINT64_MAX : 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_setblocking_obj, socket_setblocking);

// --- connection management -----------------------------------------------

static mp_obj_t socket_connect(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    if (self->connect_flag) {
        exception_from_errno(EISCONN);
    }
    self->connect_flag = true;

    mp_obj_t *elem;
    mp_obj_get_array_fixed_n(arg1, 2, &elem);
    const char *host = mp_obj_str_get_str(elem[0]);
    strncpy(self->host, host, sizeof(self->host) - 1);
    self->host[sizeof(self->host) - 1] = '\0';
    self->port = mp_obj_get_int(elem[1]);

    struct addrinfo *res = NULL;
    _socket_getaddrinfo(arg1, self->domain, &res);

    MP_THREAD_GIL_EXIT();
    int r;
    if (self->timeout > 0 && self->timeout <= 25) {
        int nbio = 1;
        lwip_ioctl(self->fd, FIONBIO, &nbio);
        r = lwip_connect(self->fd, res->ai_addr, res->ai_addrlen);
        int sock_error = errno;
        // UniRTOS lwIP can report an in-progress nonblocking connect without
        // setting errno.  Let select()/SO_ERROR determine the final result.
        if (r == -1 && sock_error != 0 && sock_error != EINPROGRESS) {
            MP_THREAD_GIL_ENTER();
            lwip_freeaddrinfo(res);
            exception_from_errno(sock_error);
        }

        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(self->fd, &read_fds);
        FD_SET(self->fd, &write_fds);
        struct timeval t = { .tv_sec = self->timeout, .tv_usec = 0 };
        r = lwip_select(self->fd + 1, &read_fds, &write_fds, NULL, &t);
        MP_THREAD_GIL_ENTER();
        lwip_freeaddrinfo(res);

        if (FD_ISSET(self->fd, &write_fds) || FD_ISSET(self->fd, &read_fds)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            lwip_getsockopt(self->fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (!(so_error == 0 || so_error == EISCONN || so_error == EINPROGRESS)) {
                nbio = 0;
                lwip_ioctl(self->fd, FIONBIO, &nbio);
                mp_raise_OSError(so_error);
            }
        } else {
            nbio = 0;
            lwip_ioctl(self->fd, FIONBIO, &nbio);
            mp_raise_OSError(MP_ETIMEDOUT);
        }
        nbio = 0;
        lwip_ioctl(self->fd, FIONBIO, &nbio);
    } else {
        r = lwip_connect(self->fd, res->ai_addr, res->ai_addrlen);
        int sock_error = errno;
        MP_THREAD_GIL_ENTER();
        lwip_freeaddrinfo(res);
        if (r != 0) {
            exception_from_errno(sock_error);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_connect_obj, socket_connect);

static mp_obj_t socket_bind(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);

    mp_obj_t *elem;
    mp_obj_get_array_fixed_n(arg1, 2, &elem);
    const char *addr_info = mp_obj_str_get_str(elem[0]);
    int server_port = mp_obj_get_int(elem[1]);
    self->bind_port = server_port;

    if (self->domain == AF_INET) {
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = lwip_htons(server_port);
        if (addr_info[0] == '\0') {
            server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            inet_pton(AF_INET, addr_info, &server_addr.sin_addr.s_addr);
        }
        if (lwip_bind(self->fd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
            exception_from_errno(errno);
        }
    } else if (self->domain == AF_INET6) {
        struct sockaddr_in6 local_v6;
        memset(&local_v6, 0, sizeof(local_v6));
        local_v6.sin6_family = AF_INET6;
        local_v6.sin6_port = lwip_htons(server_port);
        if (addr_info[0] != '\0') {
            inet_pton(AF_INET6, addr_info, &local_v6.sin6_addr);
        }
        if (lwip_bind(self->fd, (struct sockaddr *)&local_v6, sizeof(local_v6))) {
            exception_from_errno(errno);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_bind_obj, socket_bind);

static mp_obj_t socket_listen(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    int backlog = mp_obj_get_int(arg1);
    if (lwip_listen(self->fd, backlog) < 0) {
        exception_from_errno(errno);
    }
    self->listening = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_listen_obj, socket_listen);

static mp_obj_t socket_accept(const mp_obj_t arg0) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    char addr_buf[64] = {0};
    int port = 0;
    int new_fd = -1;

    for (unsigned int i = 0; i <= self->retries; i++) {
        if (self->domain == AF_INET) {
            struct sockaddr_in addr_v4;
            socklen_t addr_len = sizeof(addr_v4);
            MP_THREAD_GIL_EXIT();
            new_fd = lwip_accept(self->fd, (struct sockaddr *)&addr_v4, &addr_len);
            MP_THREAD_GIL_ENTER();
            if (new_fd >= 0) {
                inet_ntop(AF_INET, &addr_v4.sin_addr, addr_buf, sizeof(addr_buf));
                port = lwip_ntohs(addr_v4.sin_port);
            }
        } else {
            struct sockaddr_in6 addr_v6;
            socklen_t addr_len = sizeof(addr_v6);
            MP_THREAD_GIL_EXIT();
            new_fd = lwip_accept(self->fd, (struct sockaddr *)&addr_v6, &addr_len);
            MP_THREAD_GIL_ENTER();
            if (new_fd >= 0) {
                inet_ntop(AF_INET6, &addr_v6.sin6_addr, addr_buf, sizeof(addr_buf));
                port = lwip_ntohs(addr_v6.sin6_port);
            }
        }

        if (new_fd >= 0) {
            break;
        }
        int sock_error = errno;
        if (sock_error != EAGAIN && sock_error != EWOULDBLOCK) {
            exception_from_errno(sock_error);
        }
        _socket_check_pending();
    }

    if (new_fd < 0) {
        mp_raise_OSError(self->retries == 0 ? MP_EAGAIN : MP_ETIMEDOUT);
    }

    socket_obj_t *sock = mp_obj_malloc_with_finaliser(socket_obj_t, self->base.type);
    sock->fd = new_fd;
    sock->domain = self->domain;
    sock->type = self->type;
    sock->proto = self->proto;
    sock->peer_closed = false;
    sock->connect_flag = true;
    sock->bind_port = 0;
    strncpy(sock->host, addr_buf, sizeof(sock->host) - 1);
    sock->host[sizeof(sock->host) - 1] = '\0';
    sock->port = port;
    _socket_settimeout(sock, UINT64_MAX);

    mp_obj_t tuple[3] = {
        MP_OBJ_FROM_PTR(sock),
        mp_obj_new_str(addr_buf, strlen(addr_buf)),
        mp_obj_new_int(port),
    };
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_accept_obj, socket_accept);

// --- send / recv ----------------------------------------------------------

static int _socket_send(socket_obj_t *sock, const char *data, size_t datalen) {
    if (datalen == 0) {
        return 0;
    }
    int sentlen = 0;
    for (unsigned int i = 0; i <= sock->retries && sentlen < (int)datalen; i++) {
        if (sock->fd < 0) {
            mp_raise_OSError(MP_EBADF);
        }
        MP_THREAD_GIL_EXIT();
        int r = lwip_write(sock->fd, data + sentlen, datalen - sentlen);
        int sock_error = errno;
        MP_THREAD_GIL_ENTER();
        if (r < 0 && sock_error != EWOULDBLOCK && sock_error != EAGAIN) {
            exception_from_errno(sock_error);
        }
        if (r > 0) {
            sentlen += r;
        }
        _socket_check_pending();
    }
    if (sentlen == 0) {
        mp_raise_OSError(MP_ETIMEDOUT);
    }
    return sentlen;
}

static mp_obj_t socket_send(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *sock = MP_OBJ_TO_PTR(arg0);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(arg1, &bufinfo, MP_BUFFER_READ);
    int r = _socket_send(sock, bufinfo.buf, bufinfo.len);
    return mp_obj_new_int(r);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_send_obj, socket_send);

static mp_obj_t socket_sendall(const mp_obj_t arg0, const mp_obj_t arg1) {
    socket_obj_t *sock = MP_OBJ_TO_PTR(arg0);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(arg1, &bufinfo, MP_BUFFER_READ);
    int r = _socket_send(sock, bufinfo.buf, bufinfo.len);
    if (r < (int)bufinfo.len) {
        mp_raise_OSError(MP_ETIMEDOUT);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_sendall_obj, socket_sendall);

static mp_obj_t socket_sendto(mp_obj_t self_in, mp_obj_t data_in, mp_obj_t addr_in) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);

    if (self->domain == AF_INET) {
        struct sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = lwip_htons(netutils_parse_inet_addr(addr_in, (uint8_t *)&to.sin_addr, NETUTILS_BIG));
        for (unsigned int i = 0; i <= self->retries; i++) {
            MP_THREAD_GIL_EXIT();
            int ret = lwip_sendto(self->fd, bufinfo.buf, bufinfo.len, 0, (struct sockaddr *)&to, sizeof(to));
            int sock_error = errno;
            MP_THREAD_GIL_ENTER();
            if (ret > 0) {
                return mp_obj_new_int_from_uint(ret);
            }
            if (ret == -1 && sock_error != EWOULDBLOCK && sock_error != EAGAIN) {
                exception_from_errno(sock_error);
            }
            _socket_check_pending();
        }
    } else {
        mp_obj_t *elem;
        mp_obj_get_array_fixed_n(addr_in, 2, &elem);
        const char *addr_str = mp_obj_str_get_str(elem[0]);
        int server_port = mp_obj_get_int(elem[1]);
        struct sockaddr_in6 to;
        memset(&to, 0, sizeof(to));
        to.sin6_family = AF_INET6;
        to.sin6_port = lwip_htons(server_port);
        inet_pton(AF_INET6, addr_str, &to.sin6_addr);
        for (unsigned int i = 0; i <= self->retries; i++) {
            MP_THREAD_GIL_EXIT();
            int ret = lwip_sendto(self->fd, bufinfo.buf, bufinfo.len, 0, (struct sockaddr *)&to, sizeof(to));
            int sock_error = errno;
            MP_THREAD_GIL_ENTER();
            if (ret > 0) {
                return mp_obj_new_int_from_uint(ret);
            }
            if (ret == -1 && sock_error != EWOULDBLOCK && sock_error != EAGAIN) {
                exception_from_errno(sock_error);
            }
            _socket_check_pending();
        }
    }
    mp_raise_OSError(MP_ETIMEDOUT);
}
static MP_DEFINE_CONST_FUN_OBJ_3(socket_sendto_obj, socket_sendto);

static mp_uint_t _socket_read_data(mp_obj_t self_in, void *buf, size_t size, struct sockaddr *from, socklen_t *from_len, int *errcode) {
    socket_obj_t *sock = MP_OBJ_TO_PTR(self_in);
    if (sock->peer_closed) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }

    for (unsigned int i = 0; i <= sock->retries; i++) {
        if (sock->fd < 0) {
            *errcode = MP_EBADF;
            return MP_STREAM_ERROR;
        }
        MP_THREAD_GIL_EXIT();
        int r = lwip_recvfrom(sock->fd, buf, size, 0, from, from_len);
        int sock_error = errno;
        MP_THREAD_GIL_ENTER();

        if (r == 0) {
            sock->peer_closed = true;
        }
        if (r >= 0) {
            return r;
        }
        // On UniRTOS lwIP a receive with no data can return -1 without
        // updating errno.  Treat errno==0 like EAGAIN so blocking reads keep
        // polling until data arrives or the configured timeout expires.
        if (sock_error != 0 && sock_error != EWOULDBLOCK && sock_error != EAGAIN) {
            *errcode = sock_error;
            return MP_STREAM_ERROR;
        }
        _socket_check_pending();
    }

    *errcode = sock->retries == 0 ? MP_EWOULDBLOCK : MP_ETIMEDOUT;
    return MP_STREAM_ERROR;
}

static mp_obj_t _socket_recvfrom(mp_obj_t self_in, mp_obj_t len_in, struct sockaddr *from, socklen_t *from_len) {
    size_t len = mp_obj_get_int(len_in);
    vstr_t vstr;
    vstr_init_len(&vstr, len);
    int errcode = 0;
    mp_uint_t ret = _socket_read_data(self_in, vstr.buf, len, from, from_len, &errcode);
    if (ret == MP_STREAM_ERROR) {
        vstr_clear(&vstr);
        mp_raise_OSError(errcode);
    }
    vstr.len = ret;
    return mp_obj_new_bytes_from_vstr(&vstr);
}

static mp_obj_t socket_recv(mp_obj_t self_in, mp_obj_t len_in) {
    return _socket_recvfrom(self_in, len_in, NULL, NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recv_obj, socket_recv);

static mp_obj_t socket_recvfrom(mp_obj_t self_in, mp_obj_t len_in) {
    socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    struct sockaddr from;
    socklen_t fromlen = sizeof(from);
    memset(&from, 0, sizeof(from));

    mp_obj_t tuple[2];
    tuple[0] = _socket_recvfrom(self_in, len_in, &from, &fromlen);

    if (self->type == SOCK_STREAM) {
        mp_obj_t info[2] = {
            mp_obj_new_str(self->host, strlen(self->host)),
            mp_obj_new_int(self->port),
        };
        tuple[1] = mp_obj_new_tuple(2, info);
    } else {
        uint8_t *ip = (uint8_t *)&((struct sockaddr_in *)&from)->sin_addr;
        mp_uint_t port = lwip_ntohs(((struct sockaddr_in *)&from)->sin_port);
        tuple[1] = netutils_format_inet_addr(ip, port, NETUTILS_BIG);
    }
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recvfrom_obj, socket_recvfrom);

// --- options / misc -------------------------------------------------------

static mp_obj_t socket_setsockopt(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int opt = mp_obj_get_int(args[2]);

    switch (opt) {
        case SO_REUSEADDR: {
            int val = mp_obj_get_int(args[3]);
            if (lwip_setsockopt(self->fd, SOL_SOCKET, opt, &val, sizeof(int)) != 0) {
                exception_from_errno(errno);
            }
            break;
        }
        case TCP_KEEPALIVE: {
            if (self->type == SOCK_STREAM) {
                int val = mp_obj_get_int(args[3]);
                int optval = 1;
                lwip_setsockopt(self->fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(int));
                optval = val * 60;
                lwip_setsockopt(self->fd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(int));
                optval = 25;
                lwip_setsockopt(self->fd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(int));
                optval = 3;
                if (lwip_setsockopt(self->fd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(int)) != 0) {
                    exception_from_errno(errno);
                }
            }
            break;
        }
        default: {
            int val = mp_obj_get_int(args[3]);
            if (lwip_setsockopt(self->fd, SOL_SOCKET, opt, &val, sizeof(int)) != 0) {
                exception_from_errno(errno);
            }
            break;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_setsockopt_obj, 4, 4, socket_setsockopt);

// Legacy QuecPython exposes SO_ACCEPTCONN as a (is_listening, bind_port)
// tuple rather than the raw lwIP integer.
static mp_obj_t socket_getsockopt(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int opt = mp_obj_get_int(args[2]);

    if (opt == SO_ACCEPTCONN) {
        int value = 0;
        socklen_t optlen = sizeof(value);
        if (lwip_getsockopt(self->fd, SOL_SOCKET, opt, &value, &optlen) != 0) {
            return mp_obj_new_int(-1);
        }
        mp_obj_t result[2] = {
            mp_obj_new_int(value ? 1 : 0),
            mp_obj_new_int(self->bind_port),
        };
        return mp_obj_new_tuple(2, result);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_getsockopt_obj, 3, 3, socket_getsockopt);

static mp_obj_t socket_fileno(const mp_obj_t arg0) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    return mp_obj_new_int(self->fd);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_fileno_obj, socket_fileno);

// The legacy API reports lwIP TCP state values.  UniRTOS does not export
// lwip_getTcpState(), but these are the observable states maintained by this
// wrapper (CLOSED=0, LISTEN=1, ESTABLISHED=4); a closed descriptor is -1.
static mp_obj_t socket_getstate(const mp_obj_t arg0) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    if (self->fd < 0) {
        return mp_obj_new_int(-1);
    }
    if (self->listening) {
        return mp_obj_new_int(1);
    }
    return mp_obj_new_int(self->connect_flag ? 4 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_getstate_obj, socket_getstate);

static mp_obj_t socket_getsendacksize(const mp_obj_t arg0) {
    socket_obj_t *self = MP_OBJ_TO_PTR(arg0);
    if (self->fd < 0 || !self->connect_flag) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(lwip_getAckedSize(self->fd));
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_getsendacksize_obj, socket_getsendacksize);

static mp_obj_t socket_makefile(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_makefile_obj, 1, 3, socket_makefile);

// --- stream protocol ------------------------------------------------------

static mp_uint_t socket_stream_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    return _socket_read_data(self_in, buf, size, NULL, NULL, errcode);
}

static mp_uint_t socket_stream_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    socket_obj_t *sock = MP_OBJ_TO_PTR(self_in);
    for (unsigned int i = 0; i <= sock->retries; i++) {
        if (sock->fd < 0) {
            *errcode = MP_EBADF;
            return MP_STREAM_ERROR;
        }
        MP_THREAD_GIL_EXIT();
        int r = lwip_write(sock->fd, buf, size);
        int sock_error = errno;
        MP_THREAD_GIL_ENTER();
        if (r > 0) {
            return r;
        }
        // lwIP can report a would-block write as -1 with errno left at zero.
        if (r < 0 && sock_error != 0 && sock_error != EWOULDBLOCK && sock_error != EAGAIN) {
            *errcode = sock_error;
            return MP_STREAM_ERROR;
        }
        _socket_check_pending();
    }
    *errcode = sock->retries == 0 ? MP_EWOULDBLOCK : MP_ETIMEDOUT;
    return MP_STREAM_ERROR;
}

static mp_uint_t socket_stream_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    socket_obj_t *socket = MP_OBJ_TO_PTR(self_in);
    if (request == MP_STREAM_CLOSE) {
        if (socket->fd >= 0) {
            int ret = lwip_close(socket->fd);
            socket->connect_flag = false;
            if (ret != 0) {
                *errcode = errno;
                return MP_STREAM_ERROR;
            }
            socket->fd = -1;
        }
        return 0;
    }
    if (request == MP_STREAM_POLL) {
        if (socket->fd < 0) {
            return MP_STREAM_POLL_NVAL;
        }
        mp_uint_t ret = 0;
        fd_set rfds, wfds, efds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        if (arg & MP_STREAM_POLL_RD) {
            FD_SET(socket->fd, &rfds);
        }
        if (arg & MP_STREAM_POLL_WR) {
            FD_SET(socket->fd, &wfds);
        }
        FD_SET(socket->fd, &efds);
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
        int r = lwip_select(socket->fd + 1, &rfds, &wfds, &efds, &timeout);
        if (r < 0) {
            *errcode = errno;
            return MP_STREAM_ERROR;
        }
        if (FD_ISSET(socket->fd, &rfds)) {
            ret |= MP_STREAM_POLL_RD;
        }
        if (FD_ISSET(socket->fd, &wfds)) {
            ret |= MP_STREAM_POLL_WR;
        }
        if (FD_ISSET(socket->fd, &efds)) {
            ret |= MP_STREAM_POLL_ERR;
        }
        return ret;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static const mp_stream_p_t socket_stream_p = {
    .read = socket_stream_read,
    .write = socket_stream_write,
    .ioctl = socket_stream_ioctl,
};

static const mp_rom_map_elem_t socket_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&socket_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&socket_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_accept), MP_ROM_PTR(&socket_accept_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&socket_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&socket_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendall), MP_ROM_PTR(&socket_sendall_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendto), MP_ROM_PTR(&socket_sendto_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&socket_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_recvfrom), MP_ROM_PTR(&socket_recvfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_setsockopt), MP_ROM_PTR(&socket_setsockopt_obj) },
    { MP_ROM_QSTR(MP_QSTR_getsockopt), MP_ROM_PTR(&socket_getsockopt_obj) },
    { MP_ROM_QSTR(MP_QSTR_settimeout), MP_ROM_PTR(&socket_settimeout_obj) },
    { MP_ROM_QSTR(MP_QSTR_setblocking), MP_ROM_PTR(&socket_setblocking_obj) },
    { MP_ROM_QSTR(MP_QSTR_makefile), MP_ROM_PTR(&socket_makefile_obj) },
    { MP_ROM_QSTR(MP_QSTR_fileno), MP_ROM_PTR(&socket_fileno_obj) },
    { MP_ROM_QSTR(MP_QSTR_getsocketsta), MP_ROM_PTR(&socket_getstate_obj) },
    { MP_ROM_QSTR(MP_QSTR_getsendacksize), MP_ROM_PTR(&socket_getsendacksize_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
};
static MP_DEFINE_CONST_DICT(socket_locals_dict, socket_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    socket_type,
    MP_QSTR_socket,
    MP_TYPE_FLAG_NONE,
    make_new, socket_make_new,
    protocol, &socket_stream_p,
    locals_dict, &socket_locals_dict
    );

// --- module-level helpers -------------------------------------------------

static mp_obj_t mod_socket_inet_ntop(mp_obj_t family_in, mp_obj_t bin_addr_in) {
    int family = mp_obj_get_int(family_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(bin_addr_in, &bufinfo, MP_BUFFER_READ);
    char buf[64] = {0};
    if (inet_ntop(family, bufinfo.buf, buf, sizeof(buf)) == NULL) {
        mp_raise_OSError(errno);
    }
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_socket_inet_ntop_obj, mod_socket_inet_ntop);

static mp_obj_t mod_socket_inet_pton(mp_obj_t family_in, mp_obj_t addr_in) {
    int family = mp_obj_get_int(family_in);
    const char *addr = mp_obj_str_get_str(addr_in);
    size_t addr_len = (family == AF_INET6) ? sizeof(struct in6_addr) : sizeof(struct in_addr);
    vstr_t vstr;
    vstr_init_len(&vstr, addr_len);
    if (inet_pton(family, addr, vstr.buf) != 1) {
        vstr_clear(&vstr);
        mp_raise_OSError(MP_EINVAL);
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_socket_inet_pton_obj, mod_socket_inet_pton);

static mp_obj_t socket_initialize(void) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(socket_initialize_obj, socket_initialize);

static const mp_rom_map_elem_t mp_module_usocket_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_usocket) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&socket_initialize_obj) },
    { MP_ROM_QSTR(MP_QSTR_socket), MP_ROM_PTR(&socket_type) },
    { MP_ROM_QSTR(MP_QSTR_getaddrinfo), MP_ROM_PTR(&socket_getaddrinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_inet_ntop), MP_ROM_PTR(&mod_socket_inet_ntop_obj) },
    { MP_ROM_QSTR(MP_QSTR_inet_pton), MP_ROM_PTR(&mod_socket_inet_pton_obj) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET), MP_ROM_INT(AF_INET) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET6), MP_ROM_INT(AF_INET6) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_STREAM), MP_ROM_INT(SOCK_STREAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_DGRAM), MP_ROM_INT(SOCK_DGRAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_RAW), MP_ROM_INT(SOCK_RAW) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TCP), MP_ROM_INT(IPPROTO_TCP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TCP_SER), MP_ROM_INT(7) },
    { MP_ROM_QSTR(MP_QSTR_TCP_CUSTOMIZE_PORT), MP_ROM_INT(8) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_UDP), MP_ROM_INT(IPPROTO_UDP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_IP), MP_ROM_INT(IPPROTO_IP) },
    { MP_ROM_QSTR(MP_QSTR_SOL_SOCKET), MP_ROM_INT(SOL_SOCKET) },
    { MP_ROM_QSTR(MP_QSTR_SO_REUSEADDR), MP_ROM_INT(SO_REUSEADDR) },
    { MP_ROM_QSTR(MP_QSTR_SO_ACCEPTCONN), MP_ROM_INT(SO_ACCEPTCONN) },
    { MP_ROM_QSTR(MP_QSTR_TCP_KEEPALIVE), MP_ROM_INT(TCP_KEEPALIVE) },
    // Legacy EG800Z publishes 0x400 even though the UniRTOS lwIP internal
    // membership option number differs.
    { MP_ROM_QSTR(MP_QSTR_IP_ADD_MEMBERSHIP), MP_ROM_INT(0x400) },
};
static MP_DEFINE_CONST_DICT(mp_module_usocket_globals, mp_module_usocket_globals_table);

const mp_obj_module_t mp_module_usocket = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_usocket_globals,
};

MP_REGISTER_MODULE(MP_QSTR_usocket, mp_module_usocket);

#endif /* MICROPY_QPY_MODULE_USOCKET */
