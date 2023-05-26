#include "vid_format.h"

int main(void)
{
    channels_t channels = {0};

    FILE *file = fopen("data.yt", "r");
    file_check(file, "data.yt");

    channels_load(&channels, file);

    fclose(file);

    char *base = channels.text.data;

    for (int ai = 0; ai < channels.author_count; ++ai) {
        author_t author = channels.authors[ai];

        printf("\n\n-_----------------------------_-\n");
        sv_fwrite(rsv_get(base, author.uri), stdout); fputc('\n', stdout);
        sv_fwrite(rsv_get(base, author.name), stdout); fputc('\n', stdout);
        sv_fwrite(rsv_get(base, author.created), stdout); fputc('\n', stdout);
        printf("-_----------------------------_-\n");

        for (int ei = channels.offsets[ai]; ei < channels.offsets[ai + 1]; ++ei) {
            entry_t entry = channels.entries[ei];

            printf("\n\n");
            printf("https://www.youtube.com/watch?v=");
            sv_fwrite(rsv_get(base, entry.id), stdout); fputc('\n', stdout);
            sv_fwrite(rsv_get(base, entry.title), stdout); fputc('\n', stdout);
            sv_fwrite(rsv_get(base, entry.thumbnail), stdout); fputc('\n', stdout);
            printf("{{{\n");
            sv_fwrite(rsv_get(base, entry.description), stdout); fputc('\n', stdout);
            printf("}}}\n");
        }
    }

    return 0;
}
