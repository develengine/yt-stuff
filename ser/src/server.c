#include "net_linux.h"
#include "ctrl_c.h"
#include "dck.h"

#include <stdio.h>
#include <time.h>

#define MAX_CONNECTIONS 64
#define MAX_REQUEST_SIZE 4096
#define MAX_URL_SIZE 128

#define HOST "localhost"


static volatile bool running = true;

bool close_handler(void)
{
    running = false;
    return false;
}


typedef enum
{
    http_h_connection_KeepAlive,
    http_h_connection_Close,
} http_h_connection_t;

typedef struct
{
    http_h_connection_t connection;
    time_t if_modified_since;
    time_t if_unmodified_since;
} http_headers_t;

typedef enum
{
    http_method_Get,
    http_method_Head,
} http_method_t;

typedef enum
{
    parse_state_RequestLine = 0,
    parse_state_Headers,
    parse_state_Body,
    parse_state_Done,
} parse_state_t;

typedef struct
{
    parse_state_t state;

    http_method_t method;

    char url[MAX_URL_SIZE];
    int  url_size;

    http_headers_t headers;
} http_request_t;

typedef enum
{
    parse_status_Partial = 0,
    parse_status_Finished,
    parse_status_Error,
} parse_status_t;

typedef struct
{
    net_socket_t socket;
    int shitness;

    http_request_t request;
} http_connection_t;

static http_connection_t connections[MAX_CONNECTIONS];
static int connection_count = 0;


typedef struct
{
    int pos, size;
    char buffer[MAX_REQUEST_SIZE];
} request_buffer_t;

static request_buffer_t request_buffers[MAX_CONNECTIONS];


static inline int str_follows(char *buf, int buf_size, char *str)
{
    int i = 0;

    for (; str[i]; ++i) {
        if (i >= buf_size)
            return ~i;

        if (buf[i] != str[i])
            return 0;
    }

    return i;
}


static inline int str_follows_ci(char *buf, int buf_size, char *str)
{
    int i = 0;

    for (; str[i]; ++i) {
        if (i >= buf_size)
            return ~i;

        char c = buf[i];

        if (c != str[i]) {
            if (c < 'A' || c > 'Z' || c + 32 != str[i])
                return 0;
        }
    }

    return i;
}


static parse_status_t parse_request_line(http_request_t *request, request_buffer_t *buffer)
{
    char *buf = buffer->buffer + buffer->pos;
    int buf_size = buffer->size - buffer->pos;

    int res;

    /* method */
    http_method_t method;

    // NOTE: Need to be ordered ascending by length.
    if ((method = http_method_Get,  (res = str_follows(buf, buf_size, "GET "))  <= 0)
     && (method = http_method_Head, (res = str_follows(buf, buf_size, "HEAD ")) <= 0))
    {
        if (res)
            return parse_status_Partial

        return parse_status_Error;
    }

    buf      += res;
    buf_size -= res;

    /* request-uri */
    int  url_size;

    {
        int i = 0;

        for (; i < MAX_URL_SIZE; ++i) {
            if (i >= buf_size)
                return parse_status_Partial;

            if (buf[i] == ' ')
                break;

            request->url[i] = buf[i];
        }

        if (i == MAX_URL_SIZE)
            return parse_status_Error;

        url_size = i;

        // TODO: Maybe also add basic validation already here.
    }

    buf      += url_size;
    buf_size -= url_size;

    /* http-version + CRLF */
    if ((res = str_follows(buf, buf_size, " HTTP/1.1\r\n")) <= 0) {
        if (res)
            return parse_status_Partial;

        return parse_status_Error;
    }

    buf      += res;
    buf_size -= res;


    request->method = method;
    request->url_size = url_size;

    return parse_status_Finished;
}


// TODO: Check this coz im fucking tired.
static inline int eat_lws(char *buffer, int buf_size)
{
    if (buf_size == 0)
        return 0;

    int i = 1;

    char c = buf[0];

    if (c == '\r') {
        if (buf_size < 2)
            return 0;

        if (buf[1] != '\n')
            return -2;

        if (buf_size < 3)
            return 0;

        c = buf[2];
        i = 3;
    }

    if (c != ' ' && c != '\t')
        return 0;

    for (; i < buf_size; ++i) {
        c = buf[i];

        if (c != ' ' && c != '\t')
            break;
    }

    return i;
}


static inline int eat_lwss(char *buffer, int buf_size)
{
    int res;

    int i = 0;

    while ((res = eat_lws(buffer, buf_size))) {
        if (res < 0)
            return res;

        i += res;
    }

    return i;
}

char *week_day_strings[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
char *month_strings[]    = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static inline time_t parse_time(char *buffer, int buf_size)
{
    
}


typedef enum
{
    http_header_Unknown,
    http_header_Connection,
    http_header_IfModifiedSince,
    http_header_IfUnmodifiedSince,
} http_header_t;

static parse_status_t parse_header(http_request_t *request, request_buffer_t *buffer)
{
    char *buf = buffer->buffer + buffer->pos;
    int buf_size = buffer->size - buffer->pos;

    int res;

    http_header_t type;

    // NOTE: Need to be ordered ascending by length.
    if ((type = http_header_Connection,
         (res = str_follows_ci(buf, buf_size, "connection:")) > 0)
     || (type = http_header_IfModifiedSince,
         (res = str_follows_ci(buf, buf_size, "if-modified-since:")) > 0)
     || (type = http_header_IfUnmodifiedSince,
         (res = str_follows_ci(buf, buf_size, "if-unmodified-since:")) > 0))
    {
        buf      += res;
        buf_size -= res;

        res = eat_lwss(buf, buf_size);
        if (res < 0)
            return parse_status_Error;

        buf      += res;
        buf_size -= res;

        http_h_connection_t conn = {0};
        time_t              time = {0};

        if (type == http_header_Connection) {
            // NOTE: Need to be ordered ascending by length.
            if ((conn = http_h_connection_Close,
                 (res = str_follows_ci(buf, buf_size, "close")) <= 0)
             && (conn = http_h_connection_KeepAlive,
                 (res = str_follows_ci(buf, buf_size, "keep-alive")) <= 0))
            {
                if (res)
                    return parse_status_Partial;

                return parse_status_Error;
            }
        }
        else {
            assert(0 && "TODO: parse time");
        }

        buf      += res;
        buf_size -= res;

        res = eat_lwss(buf, buf_size);
        if (res < 0)
            return parse_status_Error;

        buf      += res;
        buf_size -= res;

        if (buf_size < 2)
            return parse_status_Partial;

        if (buf[0] != '\r' || buf[1] != '\n')
            return parse_status_Error;

        buf      += 2;
        buf_size -= 2;

        if (type == http_header_Connection) {
            request->headers.connection = conn;
        }
        else if (type == http_header_IfModifiedSince) {
            request->headers.if_modifier_since = time;
        }
        else {
            request->headers.if_unmodifier_since = time;
        }
    }
    else {
        type = http_header_Unknown;

        // NOTE: We don't validate what we don't care about.
        int i = 0;

        for (; i < buf_size - 2; ++i) {
            if (buf[i] == '\r') {
                if (buf[i + 1] != '\n')
                    return parse_status_Error;

                char c = buf[i + 3];

                if (c != ' ' && c != '\t') {
                    i += 2;
                    break;
                }
            }
        }

        if (i == buf_size - 2)
            return parse_status_Partial;

        buf      += i;
        buf_size -= i;
    }

}


static parse_status_t parse_request(http_request_t *request, request_buffer_t *buffer)
{
    assert(request->state != parse_state_Done);

    if (request->state == parse_state_RequestLine) {
        parse_status_t res = parse_request_line(request, buffer);

        if (res == parse_status_Partial || res == parse_status_Error)
            return res;

        request->state = parse_state_Headers;
    }

    if (request->state == parse_state_Headers) {
        for (;;) {
            if (str_follows(buffer->buffer, buffer->size, "\r\n") > 0)
                break;

            parse_status_t res = parse_header(request, buffer);

            if (res == parse_status_Partial || res == parse_status_Error)
                return res;
        }

        request->state = parse_state_Body;
    }
}


static bool process_connection(net_socket_t socket, int id)
{
    http_connection_t *connection = connections[id];
    request_buffer_t *req_buf = request_buffers + id;
    char *buffer = req_buf->buffer + req_buf->pos;

    int read_size = 0;

    for (;;) {
        int space_left = MAX_REQUEST_SIZE - req_buf->pos - read_size;

        if (space_left == 0)
            break;

        int res = net_tcp_recv(socket, buffer + read_size, space_left);

        if ((!res && !read_size) || res == NET_WOULD_BLOCK)
            break;

        read_size += res;
    }

    if (!read_size)
        return true;

    req_buf->size += read_size;

    fwrite(response_buffer, 1, read_size, stdout);

    parse_status_t status = parse_request(&connection->request, buffer, req_buf->size);

    return false;
}


int main(void)
{
    ctrl_c_register(close_handler);

    net_socket_t listener = net_tcp_listener_create(61666, net_domain_Inet, false);

    net_poller_info_t listener_info = {
        .events = NET_E_IN,
    };

    net_poller_t poller = net_poller_create(&listener, &listener_info, 1, MAX_CONNECTIONS);

    printf(" Running...\n");

    while (running) {
        net_poller_res_t res = net_poller_wait(&poller, 1000);

        if (res == net_poller_res_Error || !running)
            break;

        if (res != net_poller_res_Event)
            continue;

        net_poller_message_t message;

        while (net_poller_next(&poller, &message)) {
            if (net_socket_eq(message.socket, listener)) {
                if (message.events & ~NET_E_IN) {
                    fprintf(stderr, "Something wrong with listener: (%d)\n", message.events);
                    exit(EXIT_FAILURE);
                }

                net_socket_t conn = net_tcp_listener_accept(message.socket, false);

                net_poller_info_t info = {
                    .events = NET_E_IN,
                    .data = { .dword = connection_count },
                };

                net_poller_add(&poller, conn, info);

                connections[connection_count] = (http_connection_t) {
                    .socket = conn,
                };

                ++connection_count;
                continue;
            }

            if ((message.events & (NET_E_HUP | NET_E_ERR))
             || process_connection(message.socket, message.data.dword))
            {
                printf("closing connection\n");
                net_socket_close(message.socket);
                net_poller_remove_this(&poller);
            }
        }
    }

    net_poller_free(&poller, true);

    printf("losing...\n\\_/\n V\n");
    return 0;
}
