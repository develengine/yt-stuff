#ifndef SER_CONTENT_H
#define SER_CONTENT_H

#include "utils.h"
#include "vid_format.h"

#include <dlfcn.h>


typedef struct
{
    void *handle;

    sv_t continue_res,
         error_404_res,
         header_res,
         page1_res,
         page2_res,
         page3_res,
         script_res;

    void (*generate_vids)(arena_t *arena, channels_t *channels);

} ser_content_so_t;

static inline ser_content_so_t ser_content_load(const char *path)
{
    void *handle = dlopen(path, RTLD_NOW | RTLD_DEEPBIND);
    if (!handle) {
        fprintf(stderr, "dlopen(): Failed to open '%s'.\n", path);
        return (ser_content_so_t) { NULL };
    }

    void *object = dlsym(handle, "object");
    if (!object) {
        fprintf(stderr, "dlsym(): Failed to load symbol 'object'.\n");
        return (ser_content_so_t) { NULL };
    }

    ser_content_so_t lib = *((ser_content_so_t *)object);

    lib.handle = handle;

    return lib;
}

#endif // SER_CONTENT_H
