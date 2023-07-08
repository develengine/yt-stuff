#include "build.h"

/* shared */

const char *output = "program";

const char *prototype_source_files[] = {
#ifndef _WIN32
    "src/ser_lin.c",
#else
    "src/ser_win.c",
#endif

    NULL
};

const char *server_source_files[] = {
    "src/server.c",

#ifndef _WIN32
    "src/net_linux.c",
    "src/ctrl_c_linux.c",
#else
    "src/net_win32.c",
#endif

    NULL
};

const char *includes[] = {
    "src",
    "..",
    NULL
};

const char *defines[] = {
#ifndef _WIN32
    "_POSIX_C_SOURCE=200809L",
#else
    "_CRT_SECURE_NO_WARNINGS",
#endif
    NULL
};

const char *libs[] = {
#ifndef _WIN32
#else
    "Ws2_32.lib",
#endif
    NULL
};

/* debug */

const char *debug_defines[] = {
    "_DEBUG",
    NULL
};

const char *debug_raw[] = {
#ifndef _WIN32
    "-fno-omit-frame-pointer",
#endif
    NULL
};


int main(int argc, char *argv[])
{
    const char *build_files[] = { "build.c", NULL };
    int res = try_rebuild_self(build_files, argc, argv);
    if (res != -1)
        return res;

    int debug = contains("debug", argc, argv);

    const char **source_files = server_source_files;

    int arg_pos = find("target", argc, (const char **)argv);

    if (arg_pos != -1 && argc > arg_pos) {
        if (str_eq(argv[arg_pos+1], "prototype")) {
            source_files = prototype_source_files;
        }
        else if (str_eq(argv[arg_pos+1], "server")) {
            source_files = server_source_files;
        }
    }

    res = compile_w((compile_info_t) {
        .output = output,
        .std = "c17",
        .optimisations = debug ? DEBUG_FLAG : RELEASE_FLAG,

        .source_files = source_files,
        .includes = includes,
        .defines = debug ? merge(defines, debug_defines)
                         : defines,
        .libs = libs,
        .raw_params = debug ? debug_raw : NULL,

        .warnings = nice_warnings,
        .warnings_off = nice_warnings_off,
    });

    if (res)
        return res;

    if (contains("run", argc, argv)) {
        printf("./%s:\n", output);
#ifndef _WIN32
        return execute_w("./%s", output);
#else
        return execute_w("%s.exe", output);
#endif
    }

    return 0;
}

