#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

#include "vid_format.h"

static inline sv_t parse_contents(stream_t *stream)
{
    sv_t res = {0};

    while (!empty(stream) && bite(stream) != '>')
        ;;

    res.begin = stream->pos;

    while (!empty(stream) && bite(stream) != '<')
        ;;

    res.end = stream->pos - 1;

    return res;
}

static inline bool isnt(stream_t *stream, char c)
{
    if (empty(stream) || bite(stream) != c) {
        fprintf(stderr, "parse error: Expected '%c' at index: %ld!\n",
                c, stream->pos - stream->begin);
        return true;
    }

    return false;
}

static bool parse_find_attribute(stream_t *stream, const char *attribute)
{
    chew_space(stream);

    char c;
    while (!empty(stream) || (c = peek(stream)) != '/' || c != '>') {
        for (int i = 0; !empty(stream); ++i) {
            char attr_c = attribute[i];
            if (!attr_c)
                return true;

            if (attr_c != bite(stream))
                break;
        }

        while (!isspace(bite(stream)))
            ;;

        chew_space(stream);
    }

    return false;
}

static sv_t parse_attribute_contents(stream_t *stream)
{
    sv_t res = {0};

    chew_space(stream);

    if (isnt(stream, '='))
        return res;

    chew_space(stream);

    // NOTE: Works only with double quote attributes,
    //       but single quotes are also valid for XML.
    if (isnt(stream, '\"'))
        return res;

    res.begin = stream->pos;

    // NOTE: Only double quotes.
    while (!empty(stream) && bite(stream) != '\"')
        ;;

    res.end = stream->pos - 1;

    return res;
}

// NOTE: This doesn't validate the XML!
//       It expects the XML to be correct.
bool parse(stream_t *stream, channels_t *channels)
{
    entry_abs_t entry = {0};
    bool in_entry = false;

    author_abs_t author = {0};
    bool in_author = false;

    int author_id = -1;

    while (find(stream, '<')) {
        if (in_entry) {
            if (follows(stream, "title")) {
                entry.data[EntryId_Title] = parse_contents(stream);
            }
            else if (follows(stream, "published")) {
                entry.data[EntryId_Published] = parse_contents(stream);
            }
            else if (follows(stream, "updated")) {
                entry.data[EntryId_Updated] = parse_contents(stream);
            }
            else if (follows(stream, "yt:videoId")) {
                entry.data[EntryId_Id] = parse_contents(stream);
            }
            else if (follows(stream, "media:description")) {
                entry.data[EntryId_Description] = parse_contents(stream);
            }
            // NOTE: Thumbnail link can be constructed from the video id.
            //       We parse and store the link in the feed just as an example
            //       of extracting element attributes in XML.
            else if (follows(stream, "media:thumbnail")) {
                if (!parse_find_attribute(stream, "url")) {
                    fprintf(stderr, "parse error: Didn't find attribute 'url'!\n");
                    continue;
                }

                entry.data[EntryId_Thumbnail] = parse_attribute_contents(stream);
            }
            else if (follows(stream, "/entry")) {
                in_entry = false;

                if (author_id < 0) {
                    // NOTE: XML doesn't guarantee the order of elements,
                    //       but we rely on it for simplicity.
                    if (!author_is_valid(author)) {
                        fprintf(stderr, "parse error: Entry with invalid author!\n");
                        entry = (entry_abs_t) {0};
                        continue;
                    }

                    author_id = channels_find_author(channels, author.data[AuthorId_Uri]);
                    if (author_id < 0)
                        author_id = channels_add_author(channels, author);
                }

                channels_add_entry(channels, entry, author_id);

                entry = (entry_abs_t) {0};
            }
        }
        else {
            if (follows(stream, "entry")) {
                in_entry = true;
            }
            else if (!in_author && follows(stream, "author")) {
                in_author = true;
            }
            else if (in_author && follows(stream, "/author")) {
                in_author = false;
            }
            else if (in_author && follows(stream, "uri")) {
                author.data[AuthorId_Uri] = parse_contents(stream);
            }
            else if (in_author && follows(stream, "name")) {
                author.data[AuthorId_Name] = parse_contents(stream);
            }
            else if (follows(stream, "published")) {
                author.data[AuthorId_Published] = parse_contents(stream);
            }
        }
    }
}


size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    arena_t *arena = (arena_t*)userdata;

    size_t len = size * nmemb;

    arena_append(arena, ptr, len);

    return len;
}

void update_channel(CURL *curl, channels_t *channels, arena_t *arena, const char *url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, arena);

    if (curl_easy_perform(curl) != CURLE_OK) {
        fprintf(stderr, "Failed to perform a curl on '%s'.\n", url);
        return;
    }

    stream_t stream = {
        .begin = arena->data,
        .pos   = arena->data,
        .end   = arena->data + arena->size
    };

    parse(&stream, channels);

    arena->size = 0;
}

static inline void load_channel_file(CURL *curl, channels_t *channels, arena_t *arena, FILE *file)
{
    char buffer[512];
    size_t read, to_read, edge, start = 0, pos = 0;

    char *begin;

    do {
        to_read = sizeof(buffer) - pos;
        read = fread(buffer + pos, 1, to_read, file);
        edge = pos + read;
        pos = 0;

        for (;;) {
            while (pos < edge && isspace(buffer[pos]))
                ++pos;

            if (pos == edge)
                break;

            start = pos;
            begin = buffer + pos;
            
            while (pos < edge && !isspace(buffer[pos]))
                ++pos;

            if (pos == edge)
                break;

            buffer[pos] = 0;
            start = ++pos;

            update_channel(curl, channels, arena, begin);
        }

        pos = 0;

        for (; start < edge; ++start, ++pos)
            buffer[pos] = buffer[start];

    } while (read == to_read);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "No feed link file in argument!\n");
        exit(666);
    }

    const char *path = argv[1];

    CURL *curl = curl_easy_init();

    if(!curl) {
        fprintf(stderr, "Failed to init curl! KMS shortly.\n");
        exit(666);
    }

    // arena to store the curl results
    arena_t arena = {0};

    channels_t channels = channels_create(8);

    // update_channel(curl, &channels, &arena, url);

    FILE *feed_file = fopen(path, "r");
    file_check(feed_file, path);

    load_channel_file(curl, &channels, &arena, feed_file);

    fclose(feed_file);


    FILE *file = fopen("data.yt", "w");
    file_check(file, "data.yt");

    channels_write(&channels, file);

    fclose(file);


    curl_easy_cleanup(curl);

    return 0;
}
