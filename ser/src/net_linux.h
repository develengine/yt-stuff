/* TODO:
 * [ ] Make the errors not kill the whole program.
 * [ ] Malloc override.
 */

#ifndef NET_LINUX_H
#define NET_LINUX_H

#include <stdbool.h>
#include <poll.h>
#include <stdlib.h>
#include <stdint.h>

typedef enum
{
    net_domain_Inet,
    net_domain_Inet6,
} net_domain_t;


typedef struct
{
    int fd;
} net_socket_t;

static inline bool net_socket_eq(net_socket_t s1, net_socket_t s2)
{
    return s1.fd == s2.fd;
}


net_socket_t net_tcp_listener_create(int port, net_domain_t domain, bool blocking);

// TODO: Add a way to extract address info directly and non-blocking result.
net_socket_t net_tcp_listener_accept(net_socket_t listener, bool blocking);

void net_socket_close(net_socket_t socket);

#define NET_WOULD_BLOCK -1

int net_tcp_recv(net_socket_t socket, void *buffer, int len);
int net_tcp_send(net_socket_t socket, void *buffer, int len);


typedef unsigned net_poller_events_t;

#define NET_E_IN   POLLIN
#define NET_E_OUT  POLLOUT
#define NET_E_ERR  POLLERR
#define NET_E_HUP  POLLHUP
#define NET_E_NVAL POLLNVAL

#define NET_POLLER_NOLIMIT 0

typedef union
{
    void *ptr;
    uint32_t dword;
    uint64_t qword;
} net_poller_data_t;

typedef struct
{
    net_poller_events_t events;
    net_poller_data_t data;
} net_poller_info_t;

typedef struct
{
    struct pollfd *fds;
    net_poller_data_t *data;
    int fd_count;
    int fd_limit;
    int fd_capacity;

    int event_count;
    int event_pos;
} net_poller_t;

net_poller_t net_poller_create(net_socket_t *sockets, net_poller_info_t *infos, int count,
                               int count_limit);

void net_poller_add(net_poller_t *poller, net_socket_t socket, net_poller_info_t info);
void net_poller_remove_this(net_poller_t *poller);
void net_poller_modify_this(net_poller_t *poller, net_poller_info_t info);
net_poller_info_t net_poller_get_this(net_poller_t *poller);
void net_poller_free(net_poller_t *poller, bool close_sockets);


typedef enum
{
    net_poller_res_Event,
    net_poller_res_Timeout,
    net_poller_res_Error,
    net_poller_res_Interrupt,
} net_poller_res_t;

#define NET_POLLER_NOBLOCK 0
#define NET_POLLER_INFBLOCK -1

// NOTE: `timeout` is in milliseconds.
net_poller_res_t net_poller_wait(net_poller_t *poller, int timeout);

typedef struct
{
    net_socket_t socket;
    net_poller_events_t events;
    net_poller_data_t data;
} net_poller_message_t;

bool net_poller_next(net_poller_t *poller, net_poller_message_t *message);

#endif // NET_LINUX_H
