#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#define safe_read(buffer, count, stream)                                    \
do {                                                                        \
    if (fread(buffer, sizeof(*(buffer)), count, stream) != (size_t)count) { \
        fprintf(stderr, "%s:%d: fread failure! %s. exiting...\n",           \
                __FILE__, __LINE__, feof(stream) ? "EOF reached" : "Error");\
        exit(666);                                                          \
    }                                                                       \
} while (0)

#define safe_write(buffer, count, stream)                                           \
do {                                                                                \
    if (fwrite(buffer, sizeof(*(buffer)), count, stream) != (size_t)count) {        \
        fprintf(stderr, "%s:%d: fwrite failure! exiting...\n", __FILE__, __LINE__); \
        exit(666);                                                                  \
    }                                                                               \
} while (0)

#define file_check(file, path)                                          \
do {                                                                    \
    if (!(file)) {                                                      \
        fprintf(stderr, "%s:%d: fopen failure! path: \"%s\".\n",        \
                __FILE__, __LINE__, path);                              \
        exit(666);                                                      \
    }                                                                   \
} while (0)

#define eof_check(file)                                                             \
do {                                                                                \
    if (fgetc(file) != EOF) {                                                       \
        fprintf(stderr, "%s:%d: expected EOF! exiting...\n", __FILE__, __LINE__);   \
        exit(666);                                                                  \
    }                                                                               \
} while (0)

#define malloc_check(ptr)                                                           \
do {                                                                                \
    if (!(ptr)) {                                                                   \
        fprintf(stderr, "%s:%d: malloc failure! exiting...\n", __FILE__, __LINE__); \
        exit(666);                                                                  \
    }                                                                               \
} while (0)

/* arguments must be lvalues, except for `item` */
#define safe_push(buffer, size, capacity, item)                             \
do {                                                                        \
    if ((size) == (capacity)) {                                             \
        capacity = (capacity) ? (capacity) * 2 : 1;                         \
        void *new_ptr = realloc(buffer, sizeof(*(buffer)) * (capacity));    \
        if (!new_ptr) {                                                     \
            new_ptr = malloc(sizeof(*(buffer)) * (capacity));               \
            malloc_check(new_ptr);                                          \
            memcpy(new_ptr, buffer, sizeof(*(buffer)) * (size));            \
            free(buffer);                                                   \
        }                                                                   \
        buffer = new_ptr;                                                   \
    }                                                                       \
    buffer[size] = item;                                                    \
    ++(size);                                                               \
} while (0)

/* arguments must be lvalues, except for `amount` */
#define safe_expand(buffer, size, capacity, amount)                         \
do {                                                                        \
    capacity = (capacity) + (amount);                                       \
    void *new_ptr = realloc(buffer, sizeof(*(buffer)) * (capacity));        \
    if (!new_ptr) {                                                         \
        new_ptr = malloc(sizeof(*(buffer)) * (capacity));                   \
        malloc_check(new_ptr);                                              \
        memcpy(new_ptr, buffer, sizeof(*(buffer)) * (size));                \
        free(buffer);                                                       \
    }                                                                       \
    buffer = new_ptr;                                                       \
} while (0)


typedef struct
{
    char *begin, *end;
} sv_t;

static inline void sv_fwrite(sv_t sv, FILE *file)
{
    fwrite(sv.begin, 1, sv.end - sv.begin, file);
}

static inline bool sv_empty(sv_t sv)
{
    return sv.begin == sv.end;
}

static inline size_t sv_length(sv_t sv)
{
    return sv.end - sv.begin;
}

static inline bool sv_eq(sv_t a, sv_t b)
{
    int a_len = sv_length(a);

    if (a_len != sv_length(b))
        return false;

    for (int i = 0; i < a_len; ++i) {
        if (a.begin[i] != b.begin[i])
            return false;
    }

    return true;
}

static inline bool sv_is(sv_t sv, const char *str)
{
    int i = 0;
    for (; sv.begin + i < sv.end && str[i] != 0; ++i) {
        if (sv.begin[i] != str[i])
            return false;
    }

    return sv.begin + i >= sv.end && str[i] == 0;
}


typedef struct
{
    size_t begin, end;
} rsv_t;

static inline rsv_t rsv_make(char *origin, sv_t sv)
{
    return (rsv_t) { sv.begin - origin, sv.end - origin };
}

static inline sv_t rsv_get(char *origin, rsv_t rsv)
{
    return (sv_t) { origin + rsv.begin, origin + rsv.end };
}


typedef struct
{
    char *data;
    size_t size, capacity;
} arena_t;

static inline size_t arena_append(arena_t *arena, char *ptr, size_t len)
{
    if (!ptr)
        return 0;

    size_t new_len = arena->size + len;

    if (new_len > arena->capacity) {
        size_t mult = arena->capacity * 2;
        size_t new_capacity = mult > new_len ? mult
                                             : new_len * 2;

        arena->data = realloc(arena->data, new_capacity);

        if (!arena->data) {
            char *new_data = malloc(new_capacity);
            malloc_check(new_data);

            memcpy(new_data, arena->data, arena->size);

            free(arena->data);
            arena->data = new_data;
        }

        arena->capacity = new_capacity;
    }

    memcpy(arena->data + arena->size, ptr, len);

    size_t res = arena->size;
    arena->size = new_len;

    return res;
}

static inline size_t arena_append_file(arena_t *arena, FILE *file, size_t len)
{
    size_t new_len = arena->size + len;

    if (new_len > arena->capacity) {
        size_t mult = arena->capacity * 2;
        size_t new_capacity = mult > new_len ? mult
                                             : new_len * 2;

        arena->data = realloc(arena->data, new_capacity);

        if (!arena->data) {
            char *new_data = malloc(new_capacity);
            malloc_check(new_data);

            memcpy(new_data, arena->data, arena->size);

            free(arena->data);
            arena->data = new_data;
        }

        arena->capacity = new_capacity;
    }

    safe_read(arena->data + arena->size, len, file);

    size_t res = arena->size;
    arena->size = new_len;

    return res;
}

static inline int arena_append_str(arena_t *arena, char *str)
{
    size_t len = strlen(str);

    return arena_append(arena, str, len);
}

static inline rsv_t arena_append_sv(arena_t *arena, sv_t sv)
{
    return (rsv_t) { arena_append(arena, sv.begin, sv_length(sv)), arena->size };
}


typedef struct
{
    char *begin, *pos, *end;
} stream_t;

static inline bool empty(stream_t *stream)
{
    return stream->pos >= stream->end;
}

static inline char bite(stream_t *stream)
{
    return *(stream->pos++);
}

static inline char peek(stream_t *stream)
{
    return *(stream->pos);
}

static inline void chew_space(stream_t *stream)
{
    while (!empty(stream)) {
        if (!isspace(peek(stream)))
            return;

        stream->pos++;
    }
}

static inline bool follows(stream_t *stream, const char *str)
{
    int i = 0;
    int max_i = stream->end - stream->pos;

    for (; str[i]; ++i) {
        if (i >= max_i)
            return false;

        if (stream->pos[i] != str[i])
            return false;
    }

    stream->pos += i;

    return true;
}

static inline bool find(stream_t *stream, char c)
{
    while (!empty(stream)) {
        if (bite(stream) == c)
            return true;
    }

    return false;
}

#endif // UTILS_H
