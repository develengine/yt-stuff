#define _GNU_SOURCE

#include "net_linux.h"

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <assert.h>


// NOTE: POSIX my ass.
static_assert(EAGAIN == EWOULDBLOCK);


static inline int net_domain_to_family(net_domain_t domain)
{
    switch (domain) {
        case net_domain_Inet:   return AF_INET;
        case net_domain_Inet6:  return AF_INET6;
    }

    __builtin_unreachable();
}


net_socket_t net_tcp_listener_create(int port, net_domain_t domain, bool blocking)
{
    struct addrinfo hints = {
        .ai_family = net_domain_to_family(domain),
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
        .ai_protocol = 0,
    };

    char port_buffer[16] = {0};
    snprintf(port_buffer, sizeof(port_buffer), "%d", port);

    struct addrinfo *result = NULL;
    int res = getaddrinfo(NULL, port_buffer, &hints, &result);
    if (res) {
        fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(res));
        exit(EXIT_FAILURE);
    }

    int socket_fd;
    struct addrinfo *rp;

    for (rp = result; rp; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd < 0)
            continue;

        res = ioctl(socket_fd, FIONBIO, (char *)(&(int){ !blocking }));
        if (res < 0) {
            fprintf(stderr, "ioctl(): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (bind(socket_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(socket_fd);
    }

    freeaddrinfo(result);

    if (!rp) {
        fprintf(stderr, "Could not bind.\n");
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, 5)) {
        fprintf(stderr, "listen(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return (net_socket_t) {
        .fd = socket_fd,
    };
}


net_socket_t net_tcp_listener_accept(net_socket_t listener, bool blocking)
{
    int flags = SOCK_CLOEXEC;

    if (!blocking) {
        flags |= SOCK_NONBLOCK;
    }

    int conn_fd = accept4(listener.fd, NULL, NULL, flags);
    if (conn_fd < 0) {
        fprintf(stderr, "accept(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return (net_socket_t) {
        .fd = conn_fd,
    };
}


void net_socket_close(net_socket_t socket)
{
    close(socket.fd);
}


int net_tcp_recv(net_socket_t socket, void *buffer, int len)
{
    ssize_t res = recv(socket.fd, buffer, len, 0);

    if (res < 0) {
        if (errno == EWOULDBLOCK)
            return NET_WOULD_BLOCK;

        fprintf(stderr, "recv(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return (int)res;
}


int net_tcp_send(net_socket_t socket, void *buffer, int len)
{
    ssize_t res = send(socket.fd, buffer, len, 0);

    if (res < 0) {
        if (errno == EWOULDBLOCK)
            return NET_WOULD_BLOCK;

        fprintf(stderr, "send(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return (int)res;
}


net_poller_t net_poller_create(net_socket_t *sockets, net_poller_info_t *infos, int count,
                               int count_limit)
{
    int capacity = count_limit ? count_limit : count * 2;

    struct pollfd *fds = malloc(capacity * sizeof(struct pollfd));
    assert(fds);

    net_poller_data_t *data = malloc(capacity * sizeof(net_poller_data_t));
    assert(data);

    assert(capacity >= count);

    for (int i = 0; i < count; ++i) {
        fds[i] = (struct pollfd) {
            .fd = sockets[i].fd,
            .events = (short)infos[i].events,
        };

        data[i] = infos[i].data;
    }

    return (net_poller_t) {
        .fds = fds,
        .data = data,
        .fd_count = count,
        .fd_limit = count_limit,
        .fd_capacity = capacity,
        .event_pos = -1,
    };
}


void net_poller_add(net_poller_t *poller, net_socket_t socket, net_poller_info_t info)
{
    int count = poller->fd_count;

    if (count == poller->fd_capacity) {
        assert(poller->fd_limit == poller->fd_capacity);

        if (poller->fd_limit) {
            fprintf(stderr, "net_poller_add(): Limit reached!\n");
            exit(EXIT_FAILURE);
        }

        int new_capacity = poller->fd_capacity ? poller->fd_capacity * 2
                                               : 8;

        poller->fds = realloc(poller->fds, new_capacity * sizeof(struct pollfd));
        assert(poller->fds);

        poller->data = realloc(poller->data, new_capacity * sizeof(net_poller_data_t));
        assert(poller->fds);

        poller->fd_capacity = new_capacity;
    }

    poller->fds[count] = (struct pollfd) {
        .fd = socket.fd,
        .events = (short)info.events,
    };

    poller->data[count] = info.data;

    poller->fd_count++;
}


void net_poller_remove_this(net_poller_t *poller)
{
    assert(poller->event_pos >= 0);

    int pos = poller->event_pos;

    if (pos < poller->fd_count - 1) {
        poller->fds [pos] = poller->fds [poller->fd_count - 1];
        poller->data[pos] = poller->data[poller->fd_count - 1];
    }

    poller->fd_count--;
}


void net_poller_modify_this(net_poller_t *poller, net_poller_info_t info)
{
    int pos = poller->event_pos;

    poller->fds [pos].events = (short)info.events;
    poller->data[pos]        = info.data;
}


net_poller_info_t net_poller_get_this(net_poller_t *poller)
{
    int pos = poller->event_pos;

    return (net_poller_info_t) {
        .events = poller->fds [pos].events,
        .data   = poller->data[pos],
    };
}


net_poller_res_t net_poller_wait(net_poller_t *poller, int timeout)
{
    int res = poll(poller->fds, poller->fd_count, timeout);

    if (res == 0)
        return net_poller_res_Timeout;

    if (res < 0) {
        if (errno == EINTR)
            return net_poller_res_Interrupt;

        fprintf(stderr, "poll(): %s\n", strerror(errno));
        return net_poller_res_Error;
    }

    poller->event_count = res;
    poller->event_pos = poller->fd_count;

    return net_poller_res_Event;
}


bool net_poller_next(net_poller_t *poller, net_poller_message_t *message)
{
    poller->event_pos--;

    for (; poller->event_pos >= 0 && poller->event_count > 0; poller->event_pos--) {
        struct pollfd fd = poller->fds[poller->event_pos];

        if (fd.revents) {
            *message = (net_poller_message_t) {
                .socket = { fd.fd },
                .events = fd.revents,
                .data = poller->data[poller->event_pos],
            };

            poller->event_count--;

            return true;
        }
    }

    poller->event_pos = -1;

    return false;
}


void net_poller_free(net_poller_t *poller, bool close_sockets)
{
    if (close_sockets) {
        for (int i = 0; i < poller->fd_count; ++i) {
            close(poller->fds[i].fd);
        }
    }

    free(poller->fds);
    free(poller->data);
}

