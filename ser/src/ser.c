#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "utils.h"
#include "vid_format.h"

// TODO: Redo properly.
#include "ser_content.h"
#include "ser_content.c"


#define PORT "61666"

// NOTE: This isn't meant to be a proper implementation of a HTTP/1.1
//       server. Mostly because the protocol is a piece of shit.

// TODO: In the final "product" the server shouldn't exit under almost
//       any circumstances.

static inline struct stat file_stat(const char *path)
{
    struct stat statbuf = {0};

    if (stat(path, &statbuf) < 0) {
        fprintf(stderr, "stat(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return statbuf;
}


static bool timespec_eq(struct timespec t1, struct timespec t2)
{
    return t1.tv_sec  == t2.tv_sec
        && t1.tv_nsec == t2.tv_nsec;
}


static int create_socket(void)
{
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_PASSIVE,
        .ai_protocol = 0,
    };

    struct addrinfo *result = NULL;
    int res = getaddrinfo(NULL, PORT, &hints, &result);
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

        if (bind(socket_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(socket_fd);
    }

    freeaddrinfo(result);

    if (!rp) {
        fprintf(stderr, "Could not bind.\n");
        exit(EXIT_FAILURE);
    }

    return socket_fd;
}


static int accept_connection(int socket_fd)
{
    struct sockaddr conn;
    int conn_len = sizeof(conn);

    int conn_fd = accept(socket_fd, &conn, &conn_len);
    if (conn_fd < 0) {
        fprintf(stderr, "accept(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return conn_fd;
}


static void read_request(int fd)
{
    char buffer[512];

    for (;;) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len < 0) {
            fprintf(stderr, "read(): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        fwrite(buffer, 1, len, stdout);

        if (len < sizeof(buffer))
            break;
    }
}


static void write_reply(int fd, const char *reply, size_t reply_len)
{
    if (write(fd, reply, reply_len) != reply_len) {
        fprintf(stderr, "write(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}


static void write_reply_content(int fd, sv_t header,
                                        const char *content_type,
                                        sv_t content)
{
    static arena_t scratch = {0};

    scratch.size = 0;
    
    size_t content_len = content.end - content.begin;

    char length_buffer[256];
    size_t len = snprintf(length_buffer, sizeof(length_buffer), 
                          "Content-Type: %s\r\n"
                          "Content-Length: %lu\r\n\r\n",
                          content_type, content_len);

    arena_append(&scratch, header.begin, header.end - header.begin);
    arena_append(&scratch, length_buffer, len);
    arena_append(&scratch, content.begin, content_len);

    write_reply(fd, scratch.data, scratch.size);
}


typedef enum
{
    MethodGet,

    MethodCount
} method_t;


#define MethodInvalid MethodCount

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

    return (request_t) { .method = MethodInvalid };
}


static channels_t channels = {0};
static arena_t response_arena = {0};

static arena_t css_arena = {0};
static struct timespec css_mod_time;

static void method_get(int fd, sv_t uri)
{
    if (sv_is(uri, "/")) {
        write_reply_content(fd, content.header_res, "text/html", content.page1_res);
    }
    else if (sv_is(uri, "/test.html")) {
        write_reply_content(fd, content.header_res, "text/html", content.page2_res);
    }
    else if (sv_is(uri, "/video.html")) {
        write_reply_content(fd, content.header_res, "text/html", content.page3_res);
    }
    else if (sv_is(uri, "/script.js")) {
        write_reply_content(fd, content.header_res, "text/javascript", content.script_res);
    }
    else if (sv_is(uri, "/style.css")) {
        struct stat s = file_stat("./style.css");
        if (!css_arena.data || !timespec_eq(s.st_mtim, css_mod_time)) {
            FILE *css_f = fopen("./style.css", "r");
            file_check(css_f, "./style.css");

            css_arena.size = 0;
            arena_append_file(&css_arena, css_f, s.st_size);

            css_mod_time = s.st_mtim;
            fclose(css_f);
        }

        sv_t res = { .begin = css_arena.data,
                     .end   = css_arena.data + css_arena.size };

        write_reply_content(fd, content.header_res, "text/css", res);
    }
    else if (sv_is(uri, "/vids.html")) {
        if (!channels.authors) {
            FILE *data_f = fopen("data.yt", "r");
            file_check(data_f, "data.yt");

            int data_fd = fileno(data_f);

            if (flock(data_fd, LOCK_EX) < 0) {
                fprintf(stderr, "flock(): %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            // NOTE: Right now not neccessary to empty.
            channels_empty(&channels);
            channels_load(&channels, data_f);

            content.generate_vids(&response_arena, &channels);

            if (flock(data_fd, LOCK_UN) < 0) {
                fprintf(stderr, "flock(): %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            fclose(data_f);
        }

        sv_t res = { .begin = response_arena.data,
                     .end   = response_arena.data + response_arena.size };

        write_reply_content(fd, content.header_res, "text/html", res);
    }
    else {
        write_reply(fd, content.error_404_res.begin, sv_length(content.error_404_res));

        printf("MethodGet: unknown uri: '");
        sv_fwrite(uri, stdout);
        printf("'\n");
    }
}


static void handle_request(int fd, request_t request)
{
    switch (request.method) {
        case MethodGet: {
            method_get(fd, request.uri);
        } break;
    }
}


static bool process_request(int fd)
{
    static arena_t arena = {0};
    arena.size = 0;

    char buffer[512];

    for (bool first = true;; first = false) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len < 0) {
            fprintf(stderr, "read(): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (first && len == 0)
            return false;

        arena_append(&arena, buffer, len);

        if (len < sizeof(buffer))
            break;
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

    handle_request(fd, request);

    return true;
}


#define MAX_CONNECTIONS 64
// NOTE:  Must be 0!
#define LISTEN_ID 0

int conn_count = 0;
struct pollfd connections[MAX_CONNECTIONS];
bool remove_marks[MAX_CONNECTIONS] = {0};

void close_handler(int s)
{
    (void)s;

    printf("losing...\n\\_/\n V\n");

    for (int i = conn_count; i >= 0; --i)
        close(connections[i].fd);

    exit(EXIT_SUCCESS); 
}


static void register_close_handler(void)
{
    struct sigaction sigIntHandler = {0};

    sigIntHandler.sa_handler = close_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
}


int main(void)
{
    register_close_handler();

    int listen_fd = create_socket();

    if  (listen(listen_fd, 5)) {
        fprintf(stderr, "listen(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    connections[LISTEN_ID] = (struct pollfd) {
        .fd = listen_fd,
        .events = POLLIN,
    };

    conn_count = 1;

    printf(" Serving...\n");

    for (;;) {
        int count = poll(connections, conn_count, -1);
        if (count < -1) {
            fprintf(stderr, "poll(): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        int new_conn_count = conn_count;

        if (connections[LISTEN_ID].revents & POLLIN) {
            connections[conn_count] = (struct pollfd) {
                .fd = accept_connection(listen_fd),
                .events = POLLIN,
            };

            remove_marks[conn_count] = false;

            ++new_conn_count;
        }

        for (int conn_id = LISTEN_ID + 1; conn_id < conn_count; ++conn_id) {
            struct pollfd conn = connections[conn_id];

            // TODO: We should handle all the other revents too.
            if (conn.revents & POLLIN) {
                remove_marks[conn_id] = !process_request(conn.fd);
                continue;
            }
        }

        int p = 1;

        for (int i = 1; i < new_conn_count; ++i) {
            if (!remove_marks[i]) {
                connections[p++] = connections[i];
                continue;
            }

            close(connections[i].fd);
        }

        conn_count = p;
    }

    return 0;
}
