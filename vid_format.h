#ifndef VID_FORMAT_H
#define VID_FORMAT_H

#include "utils.h"


typedef enum
{
    EntryId_Id,
    EntryId_Title,
    EntryId_Description,
    EntryId_Thumbnail,
    EntryId_Published,
    EntryId_Updated,

    ENTRY_ID_COUNT
} entry_id_t;

typedef struct
{
    rsv_t data[ENTRY_ID_COUNT];
} entry_t;

typedef struct
{
    sv_t data[ENTRY_ID_COUNT];
} entry_abs_t;

typedef enum
{
    AuthorId_Uri,
    AuthorId_Name,
    AuthorId_Published,

    AUTHOR_ID_COUNT
} author_id_t;

typedef struct
{
    rsv_t data[AUTHOR_ID_COUNT];
} author_t;

typedef struct
{
    sv_t data[AUTHOR_ID_COUNT];
} author_abs_t;

static inline bool author_is_valid(author_abs_t author)
{
    return !sv_empty(author.data[AuthorId_Uri])
        && !sv_empty(author.data[AuthorId_Name])
        && !sv_empty(author.data[AuthorId_Published]);
}

typedef struct
{
    author_t *authors;
    size_t author_count, author_capacity;

    // NOTE: Offsets are always one more than authors but whatever.
    //       Too lazy :P
    size_t *offsets;
    size_t offset_count, offset_capacity;

    entry_t *entries;
    size_t entry_count, entry_capacity;

    arena_t text;
} channels_t;

// TODO: Add some macro magic for generating serialization functions and reduce
//       the amount of code that we need to write.
//       This is very clearly just duplication of code.
static inline void channels_write(channels_t *channels, FILE *file)
{
    safe_write(&(channels->author_count), 1, file);
    safe_write(channels->authors, channels->author_count, file);

    safe_write(&(channels->offset_count), 1, file);
    safe_write(channels->offsets, channels->offset_count, file);

    safe_write(&(channels->entry_count), 1, file);
    safe_write(channels->entries, channels->entry_count, file);

    safe_write(&(channels->text.size), 1, file);
    safe_write(channels->text.data, channels->text.size, file);
}

static inline void channels_load(channels_t *channels, FILE *file)
{
    size_t count = 0;

    safe_read(&count, 1, file);
    safe_expand(channels->authors, channels->author_count, channels->author_capacity, count);
    safe_read(channels->authors, count, file);
    channels->author_count = count;

    safe_read(&count, 1, file);
    safe_expand(channels->offsets, channels->offset_count, channels->offset_capacity, count);
    safe_read(channels->offsets, count, file);
    channels->offset_count = count;

    safe_read(&count, 1, file);
    safe_expand(channels->entries, channels->entry_count, channels->entry_capacity, count);
    safe_read(channels->entries, count, file);
    channels->entry_count = count;

    safe_read(&count, 1, file);
    safe_expand(channels->text.data, channels->text.size, channels->text.capacity, count);
    safe_read(channels->text.data, count, file);
    channels->text.size = count;

    eof_check(file);
}

static inline void channels_empty(channels_t *channels)
{
    channels->author_count = 0;
    channels->offset_count = 0;
    channels->entry_count  = 0;
    channels->text.size    = 0;
}

static inline channels_t channels_create(int author_capacity)
{
    channels_t res = {0};

    safe_expand(res.authors, res.author_count, res.author_capacity, author_capacity);
    safe_expand(res.offsets, res.offset_count, res.offset_capacity, author_capacity + 1);

    res.offset_count = 1;
    res.offsets[0] = 0;

    return res;
}

static inline size_t channels_find_author(channels_t *channels, sv_t author_uri)
{
    for (size_t i = 0; i < channels->author_count; ++i) {
        sv_t uri = rsv_get(channels->text.data, channels->authors[i].data[AuthorId_Uri]);
        if (sv_eq(uri, author_uri))
            return i;
    }

    return -1;
}

static inline size_t channels_add_author(channels_t *channels, author_abs_t author_abs)
{
    author_t author = {0};

    for (author_id_t id = 0; id < AUTHOR_ID_COUNT; ++id)
        author.data[id] = arena_append_sv(&channels->text, author_abs.data[id]);

    safe_push(channels->authors, channels->author_count, channels->author_capacity, author);

    size_t offset = channels->offsets[channels->offset_count - 1];
    safe_push(channels->offsets, channels->offset_count, channels->offset_capacity, offset);

    return channels->author_count - 1;
}

static inline size_t channels_add_entry(channels_t *channels, entry_abs_t entry, size_t author_id)
{
    entry_t curr = {0};

    for (entry_id_t id = 0; id < ENTRY_ID_COUNT; ++id)
        curr.data[id] = arena_append_sv(&channels->text, entry.data[id]);

    size_t index = channels->offsets[author_id + 1];

    for (; index < channels->entry_count; ++index) {
        entry_t prev = channels->entries[index];
        channels->entries[index] = curr;
        curr = prev;
    }

    safe_push(channels->entries, channels->entry_count, channels->entry_capacity, curr);

    for (size_t i = author_id + 1; i < channels->offset_count; ++i)
        channels->offsets[i]++;

    return index;
}

#endif // VID_FORMAT_H
