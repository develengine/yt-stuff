/* TODO:
 * [ ] accept all connections at once
 * [ ] rewrite code to support non-blocking behaviour properly
 */

#include "utils.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <Ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define PORT 61666

#define SWA_MAJOR_V 2
#define SWA_MINOR_V 2

#define MAX_CONNECTIONS 64
// NOTE:  Must be 0!
#define LISTEN_ID 0

static int conn_count = 0;
static struct pollfd connections[MAX_CONNECTIONS];
static bool remove_marks[MAX_CONNECTIONS] = {0};


static char continue_message[] =
    "HTTP/1.1 100 Continue\r\n"
    "\r\n";

static char error_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "\r\n";

static char reply_header[] = 
    "HTTP/1.1 100 Continue\r\n"
    "\r\n"
    "HTTP/1.1 200 OK\r\n"
    "Server: Brugger\r\n"
    "Connection: keep-alive\r\n";

static char page1_content[] = 
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "    <head>\r\n"
    "        <meta charset=\"utf-8\">\r\n"
    "        <title>Buzerantopia</title>\r\n"
    "    </head>\r\n"
    "    <body>\r\n"
    "        <h1>This is gay</h1>\r\n"
    "        <p>Bruh</p>\r\n"
    "        <a href=\"test.html\">Click me (test)</a>\r\n"
    "    </body>\r\n"
    "</html>\r\n";

static char page2_content[] = 
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "    <head>\r\n"
    "        <meta charset=\"utf-8\">\r\n"
    "        <title>Buzerantopia</title>\r\n"
    "    </head>\r\n"
    "    <body>\r\n"
    "        <h1>Stupid shit</h1>\r\n"
    "        <p>OMEGALUL</p>\r\n"
    "        <a href=\"/\">Click me (/)</a>\r\n"
    "    </body>\r\n"
    "</html>\r\n";

static sv_t continue_res = { .begin = continue_message,
                             .end = continue_message + sizeof(continue_message) - 1 };
static sv_t error_404_res = { .begin = error_404,
                              .end = error_404 + sizeof(error_404) - 1 };
static sv_t header_res = { .begin = reply_header,
                           .end = reply_header + sizeof(reply_header) - 1 };
static sv_t page1_res = { .begin = page1_content,
                          .end = page1_content + sizeof(page1_content) - 1 };
static sv_t page2_res = { .begin = page2_content,
                          .end = page2_content + sizeof(page2_content) - 1 };

typedef enum
{
    MethodGet,

    METHOD_COUNT
} method_t;


#define MethodInvalid METHOD_COUNT

typedef struct
{
    method_t method;
    sv_t uri;
} request_t;


static request_t parse_request(stream_t *stream)
{
    if (follows(stream, "GET")) {
        chew_space(stream);

        sv_t uri = { .begin = stream->pos };

        // TODO: Maybe add some validation?

        while (!empty(stream) && bite(stream) != ' ')
            ;;

        uri.end = stream->pos - 1;

        return (request_t) {
            .method = MethodGet,
            .uri = uri,
        };
    }

    // TODO: Other thingaz.

    return (request_t) { .method = MethodInvalid };
}


static void write_reply(SOCKET sock, const char *reply, size_t reply_len)
{
    if (send(sock, reply, (int)reply_len, 0) != (int)reply_len) {
        fprintf(stderr, "send(): (%d)\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }
}


static void write_reply_content(SOCKET sock,
                                sv_t header,
                                const char *content_type,
                                sv_t content)
{
    static arena_t scratch = {0};

    scratch.size = 0;
    
    size_t content_len = content.end - content.begin;

    char length_buffer[256];
    size_t len = snprintf(length_buffer, sizeof(length_buffer), 
                          "Content-Type: %s\r\n"
                          "Content-Length: %llu\r\n\r\n",
                          content_type, content_len);

    arena_append(&scratch, header.begin, header.end - header.begin);
    arena_append(&scratch, length_buffer, len);
    arena_append(&scratch, content.begin, content_len);

    write_reply(sock, scratch.data, scratch.size);
}


static void method_get(SOCKET sock, sv_t uri)
{
    if (sv_is(uri, "/")) {
        write_reply_content(sock, header_res, "text/html", page1_res);
    }
    else if (sv_is(uri, "/test.html")) {
        write_reply_content(sock, header_res, "text/html", page2_res);
    }
    else {
        write_reply(sock, error_404_res.begin, sv_length(error_404_res));

        printf("MethodGet: unknown uri: '");
        sv_fwrite(uri, stdout);
        printf("'\n");
    }
}


static void handle_request(SOCKET sock, request_t request)
{
    switch (request.method) {
        case MethodGet: {
            method_get(sock, request.uri);
        } break;

        case METHOD_COUNT: unreachable();
    }
}


static bool process_request(SOCKET sock)
{
    static arena_t arena = {0};
    arena.size = 0;

    char buffer[512];

    for (bool first = true;; first = false) {
        size_t len = recv(sock, buffer, sizeof(buffer), 0);

        if (len == SOCKET_ERROR) {
            int err = GetLastError();

            if (err == WSAEWOULDBLOCK)
                break;

            fprintf(stderr, "recv(): %d\n", err);
            WSACleanup();
            exit(EXIT_FAILURE);
        }

        // NOTE: Means the connection has been ended.
        if (first && len == 0)
            return false;

        arena_append(&arena, buffer, len);

        // if (len < sizeof(buffer))
            // break;
    }

    fwrite(arena.data, 1, arena.size, stdout);

    stream_t stream = {
        .begin = arena.data,
        .pos   = arena.data,
        .end   = arena.data + arena.size
    };

    request_t request = parse_request(&stream);

    if (request.method == MethodInvalid) {
        printf("Invalid Method!\n");
        return false;
    }

    handle_request(sock, request);

    return true;
}


static SOCKET accept_connection(SOCKET sock)
{
    struct sockaddr conn;
    int conn_len = sizeof(conn);

    SOCKET new_sock = accept(sock, &conn, &conn_len);
    if (new_sock == INVALID_SOCKET) {
        int err = WSAGetLastError();

        if (err == WSAEWOULDBLOCK)
            return INVALID_SOCKET;

        fprintf(stderr, "accept(): (%d)\n", err);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    return new_sock;
}


BOOL WINAPI close_handler(DWORD type)
{
    // NOTE: Turns out this procedure is called in separate thread...
    //       really cool. So basically we rely on the fact that WSACleanup()
    //       breaks out of the WSAPoll() function which makes it crash.
    //
    //       Maybe we should use timeout on the polling and do communication
    //       with the main thread.

    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
        if (closesocket(connections[LISTEN_ID].fd) == SOCKET_ERROR) {
            fprintf(stderr, "closesocket(LISTEN_ID): (%d)\n", WSAGetLastError());
        }

        for (int conn_id = LISTEN_ID + 1; conn_id < conn_count; ++conn_id) {
            if (closesocket(connections[conn_id].fd) == SOCKET_ERROR) {
                fprintf(stderr, "closesocket(%d): (%d)\n", conn_id, WSAGetLastError());
            }
        }

        // NOTE: Makes WSAPoll() fail instantly.
        WSACleanup();

        printf("\\_/\n V\n");

        return TRUE;
    }

    return FALSE;
}


int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    WSADATA wsa_data;
    int res = WSAStartup(MAKEWORD(SWA_MAJOR_V, SWA_MINOR_V), &wsa_data);
    if (res) {
        fprintf(stderr, "WSAStartup(): (%d)\n", res);
        exit(EXIT_FAILURE);
    }

    if (LOBYTE(wsa_data.wVersion) != SWA_MINOR_V || HIBYTE(wsa_data.wVersion) != SWA_MAJOR_V) {
        fprintf(stderr, "Wrong version of WSA: %d.%d\n",
                HIBYTE(wsa_data.wVersion), LOBYTE(wsa_data.wVersion));

        WSACleanup();
        exit(EXIT_FAILURE);
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "socket(): (%d)\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    res = ioctlsocket(listen_sock, FIONBIO, &(unsigned long){ 1 });
    if (res != NO_ERROR) {
        fprintf(stderr, "ioctlsocket(): (%d)\n", res);
    }

    IN_ADDR in_addr;
    res = inet_pton(AF_INET, "127.0.0.1", &in_addr);
    if (res != 1) {
        if (res == 0) {
            fprintf(stderr, "inet_pton(): No valid address!\n");
        }
        else {
            fprintf(stderr, "inet_pton(): (%d)\n", WSAGetLastError());
        }

        WSACleanup();
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in sock_addr = {
        .sin_family = AF_INET,
        .sin_addr   = in_addr,
        .sin_port   = htons(PORT),
    };

    res = bind(listen_sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (res == SOCKET_ERROR) {
        fprintf(stderr, "bind(): (%d)\n", res);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, 5) == SOCKET_ERROR) {
        fprintf(stderr, "listen(): (%d)\n", WSAGetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }


    connections[LISTEN_ID] = (struct pollfd) {
        .fd = listen_sock,
        .events = POLLIN,
    };

    conn_count = 1;


    if (!SetConsoleCtrlHandler(close_handler, true)) {
        fprintf(stderr, "SetConsoleCtrlHandler(): %d\n", GetLastError());
        WSACleanup();
        exit(EXIT_FAILURE);
    }


    printf(" Serving...\n");


    for (;;) {
        int count = WSAPoll(connections, conn_count, -1);
        if (count == SOCKET_ERROR) {
            fprintf(stderr, "WSAPoll(): (%d)\n", WSAGetLastError());
            WSACleanup();
            exit(EXIT_FAILURE);
        }

        printf("poll returned\n");

        int new_conn_count = conn_count;

        if (connections[LISTEN_ID].revents & POLLIN) {
            for (;;) {
                SOCKET new_sock = accept_connection(listen_sock);

                if (new_sock == INVALID_SOCKET)
                    break;

                connections[new_conn_count] = (struct pollfd) {
                    .fd = new_sock,
                    .events = POLLIN,
                };

                printf("creating a connection (%d)\n", new_conn_count);

                remove_marks[new_conn_count] = false;

                ++new_conn_count;
            }
        }

        for (int conn_id = LISTEN_ID + 1; conn_id < conn_count; ++conn_id) {
            struct pollfd conn = connections[conn_id];

            if (conn.revents & POLLIN) {
                remove_marks[conn_id] |= !process_request(conn.fd);
            }

            if (conn.revents & POLLERR) {
                printf("error on connection (%d)\n", conn_id);
                remove_marks[conn_id] = true;
            }

            if (conn.revents & POLLHUP) {
                remove_marks[conn_id] = true;
            }
        }

        int p = 1;

        for (int i = 1; i < new_conn_count; ++i) {
            if (!remove_marks[i]) {
                connections[p++] = connections[i];
                continue;
            }

            printf("closing a connection (%d)\n", i);

            if (closesocket(connections[i].fd) == SOCKET_ERROR) {
                fprintf(stderr, "closesocket(): (%d)\n", WSAGetLastError());
            }
        }

        conn_count = p;
    }
}
