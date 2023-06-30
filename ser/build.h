#ifndef BUILD_H_
#define BUILD_H_

/* TODO:
 * [ ] file writing and loading
 * [X] timestamp file for auto updating
 * [X] add windows support
 * */

#ifndef COMMAND_BUFFER_SIZE
#define COMMAND_BUFFER_SIZE 1024
#endif // COMMAND_BUFFER_SIZE

#ifndef EXECUTE_BUFFER_SIZE
#define EXECUTE_BUFFER_SIZE 1024
#endif // EXECUTE_BUFFER_SIZE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>

#ifndef _WIN32
    #include <sys/wait.h>
    #include <sys/stat.h>
    #include <unistd.h>
#else
    #include <windows.h>
#endif // _WIN32


#define RELEASE_FLAG "2"

#ifndef _WIN32
    #define DEBUG_FLAG "g"
#else
    #define DEBUG_FLAG "d"
#endif // _WIN32


#ifndef BUILD_NO_DATA

#ifndef _WIN32
    const char *nice_warnings[] = {
        "all",
        "extra",
        "pedantic",
        NULL
    };

    const char *nice_warnings_off[] = {
        "deprecated-declarations",
        "missing-field-initializers",
        NULL
    };
#else
    const char *nice_warnings[] = {
        "4",
        NULL
    };

    const char *nice_warnings_off[] = {
        "d5105",
        "d4706",
        "44062",
        NULL
    };
#endif // _WIN32

#endif // NO_DATA


typedef struct
{
    const char *output;
    const char *std;
    const char *optimisations;
    int pedantic;
    int keep_source_info;
    const char *compiler;

    const char **source_files;
    const char **includes;
    const char **defines;
    const char **libs;
    const char **warnings;
    const char **warnings_off;
    const char **raw_params;

} compile_info_t;

// NOTE: Opaque because of platform reasons.
#ifndef _WIN32
    typedef struct timespec file_time_t;
#else
    typedef FILETIME file_time_t;
#endif // _WIN32

typedef struct
{
    const char **paths;
    file_time_t *times;

    int count, capacity;
} mod_data_t;

typedef struct
{
    int count;
    int str_size;
} mod_data_header_t;


static inline void mod_data_load(mod_data_t *mod_data, const char *path)
{
    if (!path)
        path = "build.data";

    FILE *file = fopen(path, "rb");

    if (!file) {
        *mod_data = (mod_data_t) {0};
        printf("No mod file '%s'. Ignoring...\n", path);
        return;
    }

    mod_data_header_t header;

    assert(fread(&header, 1, sizeof(header), file) == sizeof(header));

    mod_data->paths = malloc(header.count * sizeof(const char *));
    assert(mod_data->paths);

    int time_size = header.count * sizeof(file_time_t);

    mod_data->times = malloc(time_size);
    assert(mod_data->times);

    char *string_storage = malloc(header.str_size);
    assert(string_storage);

    mod_data->count = header.count;
    mod_data->capacity = header.count;

    assert(fread(mod_data->times, 1, time_size, file) == time_size);

    assert(fread(string_storage, 1, header.str_size, file) == header.str_size);

    for (int i = 0, pos = 0, start = 0; i < header.str_size; ++i) {
        if (string_storage[i] == '\0') {
            mod_data->paths[pos++] = string_storage + start;
            start = i + 1;
        }
    }

    if (fgetc(file) != EOF) {
        fprintf(stderr, "Could not parse mod data from file '%s'!\n", path);
        exit(666);
    }

    fclose(file);
}

static inline void mod_data_store(mod_data_t *mod_data, const char *path)
{
    if (!path)
        path = "build.data";

    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "fopen(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    mod_data_header_t header = { .count = mod_data->count };

    for (int i = 0; i < mod_data->count; ++i) {
        header.str_size += strlen(mod_data->paths[i]) + 1;
    }

    assert(fwrite(&header, 1, sizeof(header), file) == sizeof(header));

    int time_size = mod_data->count * sizeof(file_time_t);
    assert(fwrite(mod_data->times, 1, time_size, file) == time_size);

    for (int i = 0; i < mod_data->count; ++i) {
        const char *path = mod_data->paths[i];
        int len0 = strlen(path) + 1;

        assert(fwrite(path, 1, len0, file) == len0);
    }
}

static inline int buffer_append(char *buffer, int pos, const char *str)
{
    int str_len = 0;
    for (; str[str_len]; ++str_len)
        ;;

    if (pos + str_len >= COMMAND_BUFFER_SIZE) {
        fprintf(stderr, "build: COMMAND_BUFFER_SIZE '%d' too small!\n"
                        "       Increase the value of the macro in file '%s'.\n",
                        COMMAND_BUFFER_SIZE, __FILE__);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < str_len; ++i) {
        buffer[pos + i] = str[i];
    }

    return pos + str_len;
}

static inline int str_eq(const char *str_a, const char *str_b)
{
    for (; *str_a && *str_b; ++str_a, ++str_b) {
        if (*str_a != *str_b)
            return 0;
    }

    return *str_a == *str_b;
}

static inline int contains(const char *str, int argc, char *argv[])
{
    for (int i = 0; i < argc; ++i) {
        if (str_eq(argv[i], str))
            return 1;
    }

    return 0;
}

static inline int find(const char *str, int count, const char **list)
{
    for (int i = 0; i < count; ++i) {
        if (str_eq(list[i], str))
            return i;
    }

    return -1;
}

#ifndef _WIN32

static inline pid_t compile(compile_info_t info)
{
    /* construct command */

    char buffer[COMMAND_BUFFER_SIZE] = {0};
    int pos = 0;

    if (!info.compiler) {
        info.compiler = "cc";
    }

    pos = buffer_append(buffer, pos, info.compiler);

    if (info.output) {
        pos = buffer_append(buffer, pos, " -o ");
        pos = buffer_append(buffer, pos, info.output);
    }

    if (info.std) {
        pos = buffer_append(buffer, pos, " -std=");
        pos = buffer_append(buffer, pos, info.std);
    }

    if (info.optimisations) {
        pos = buffer_append(buffer, pos, " -O");
        pos = buffer_append(buffer, pos, info.optimisations);
    }

    if (info.pedantic) {
        pos = buffer_append(buffer, pos, " -pedantic");
    }

    if (info.keep_source_info) {
        pos = buffer_append(buffer, pos, " -g");
    }

    if (info.source_files) {
        for (int i = 0; info.source_files[i]; ++i) {
            pos = buffer_append(buffer, pos, " ");
            pos = buffer_append(buffer, pos, info.source_files[i]);
        }
    }

    if (info.includes) {
        for (int i = 0; info.includes[i]; ++i) {
            pos = buffer_append(buffer, pos, " -I");
            pos = buffer_append(buffer, pos, info.includes[i]);
        }
    }

    if (info.defines) {
        for (int i = 0; info.defines[i]; ++i) {
            pos = buffer_append(buffer, pos, " -D");
            pos = buffer_append(buffer, pos, info.defines[i]);
        }
    }

    if (info.libs) {
        for (int i = 0; info.libs[i]; ++i) {
            pos = buffer_append(buffer, pos, " -l");
            pos = buffer_append(buffer, pos, info.libs[i]);
        }
    }

    if (info.warnings) {
        for (int i = 0; info.warnings[i]; ++i) {
            pos = buffer_append(buffer, pos, " -W");
            pos = buffer_append(buffer, pos, info.warnings[i]);
        }
    }

    if (info.warnings_off) {
        for (int i = 0; info.warnings_off[i]; ++i) {
            pos = buffer_append(buffer, pos, " -Wno-");
            pos = buffer_append(buffer, pos, info.warnings_off[i]);
        }
    }

    if (info.raw_params) {
        for (int i = 0; info.raw_params[i]; ++i) {
            pos = buffer_append(buffer, pos, " ");
            pos = buffer_append(buffer, pos, info.raw_params[i]);
        }
    }

    /* execute command */

    pid_t pid = fork();

    if (pid == -1) {
        fprintf(stderr, "fork(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", buffer, NULL);

        fprintf(stderr, "execl(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return pid;
}

static inline int wait_on_exits(pid_t *pids, int count)
{
    siginfo_t info;

    for (int i = 0; i < count; ++i) {
        if (waitid(P_ALL, pids[i], &info, WEXITED) == -1) {
            fprintf(stderr, "waitid(): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (info.si_status == 0)
            continue;

        return info.si_status;
    }

    return 0;
}

static inline pid_t execute_vargs(const char *fmt, va_list args)
{
    /* construct command */

    char buffer[EXECUTE_BUFFER_SIZE];

    va_list args_copy;
    va_copy(args_copy, args);

    int to_write = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    int written = vsnprintf(buffer, EXECUTE_BUFFER_SIZE, fmt, args_copy);
    va_end(args_copy);

    if (written != to_write) {
        fprintf(stderr, "build: EXECUTE_BUFFER_SIZE '%d' too small!\n"
                        "       Increase the value of the macro in file '%s'.\n",
                        EXECUTE_BUFFER_SIZE, __FILE__);
        exit(EXIT_FAILURE);
    }

    /* execute command */

    pid_t pid = fork();

    if (pid == -1) {
        fprintf(stderr, "fork(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", buffer, NULL);

        fprintf(stderr, "execl(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return pid;
}

static inline int exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static inline pid_t execute_argv(char *argv[])
{
    pid_t pid = fork();

    if (pid == -1) {
        fprintf(stderr, "fork(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        execv(argv[0], argv);

        fprintf(stderr, "execv(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return pid;
}

static inline int file_time_cmp(file_time_t a, file_time_t b)
{
    int res = (a.tv_sec > b.tv_sec) - (a.tv_sec < b.tv_sec);

    if (res == 0)
        return (a.tv_nsec > b.tv_nsec) - (a.tv_nsec < b.tv_nsec);

    return res;
}

static inline file_time_t mod_time(const char *path)
{
    struct stat st = {0};

    if (lstat(path, &st)) {
        fprintf(stderr, "lstat(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return (file_time_t) { st.st_mtime };
}

#else

#define pid_t HANDLE


static inline pid_t compile(compile_info_t info)
{
    /* construct command */

    char buffer[COMMAND_BUFFER_SIZE] = {0};
    int pos = 0;

    pos = buffer_append(buffer, pos, "cmd.exe /c \"");

    if (info.compiler) {
        fprintf(stderr, "Compiler choice not yet implemented on windows!\n");
    }

    // NOTE: There mey be cases where these default flags may not be desired.
    //       /nologo: Makes the compiler shut up for the most part.
    //       /EHsc:   Doesn't generate stack unwinding code for c source code.
    pos = buffer_append(buffer, pos, "cl /nologo /EHsc");

    if (info.output) {
        pos = buffer_append(buffer, pos, " /Fe");
        pos = buffer_append(buffer, pos, info.output);
    }

    if (info.std) {
        pos = buffer_append(buffer, pos, " /std:");
        pos = buffer_append(buffer, pos, info.std);
    }

    if (info.optimisations) {
        pos = buffer_append(buffer, pos, " /O");
        pos = buffer_append(buffer, pos, info.optimisations);
    }

    if (info.pedantic) {
        fprintf(stderr, "Pedantic is not available on windows!\n");
    }

    if (info.keep_source_info) {
        // NOTE: Not sure if this is exactly analogous to '-g'.
        pos = buffer_append(buffer, pos, " /DEBUG");
    }

    if (info.source_files) {
        for (int i = 0; info.source_files[i]; ++i) {
            pos = buffer_append(buffer, pos, " ");
            pos = buffer_append(buffer, pos, info.source_files[i]);
        }
    }

    if (info.includes) {
        for (int i = 0; info.includes[i]; ++i) {
            pos = buffer_append(buffer, pos, " /I");
            pos = buffer_append(buffer, pos, info.includes[i]);
        }
    }

    if (info.defines) {
        for (int i = 0; info.defines[i]; ++i) {
            pos = buffer_append(buffer, pos, " /D");
            pos = buffer_append(buffer, pos, info.defines[i]);
        }
    }

    if (info.libs) {
        for (int i = 0; info.libs[i]; ++i) {
            // NOTE: This is different on windows and may need to be re-thought.
            pos = buffer_append(buffer, pos, " ");
            pos = buffer_append(buffer, pos, info.libs[i]);
        }
    }

    if (info.warnings) {
        for (int i = 0; info.warnings[i]; ++i) {
            pos = buffer_append(buffer, pos, " /W");
            pos = buffer_append(buffer, pos, info.warnings[i]);
        }
    }

    if (info.warnings_off) {
        for (int i = 0; info.warnings_off[i]; ++i) {
            pos = buffer_append(buffer, pos, " /w");
            pos = buffer_append(buffer, pos, info.warnings_off[i]);
        }
    }

    if (info.raw_params) {
        for (int i = 0; info.raw_params[i]; ++i) {
            pos = buffer_append(buffer, pos, " ");
            pos = buffer_append(buffer, pos, info.raw_params[i]);
        }
    }

    pos = buffer_append(buffer, pos, "\"");

    /* execute command */

    STARTUPINFO startup_info = {0};
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info = {0};

    if (!CreateProcessA(
        "C:\\Windows\\System32\\cmd.exe",
        buffer,
        NULL, NULL,
        FALSE,
        0,
        NULL, NULL,
        &startup_info,
        &process_info))
    {
        fprintf(stderr, "CreateProcessA(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    return process_info.hProcess;
}

static inline int wait_on_exits(pid_t *pids, int count)
{
    if (WaitForMultipleObjects(count, pids, TRUE, INFINITE) == WAIT_FAILED) {
        fprintf(stderr, "WaitForMultipleObjects(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < count; ++i) {
        int code;

        if (!GetExitCodeProcess(pids[i], &code)) {
            fprintf(stderr, "GetExitCodeProcess(): (%d)\n", GetLastError());
            exit(EXIT_FAILURE);
        }

        if (code)
            return code;
    }

    return 0;
}

static inline pid_t execute_vargs(const char *fmt, va_list args)
{
    /* construct command */

    char buffer[EXECUTE_BUFFER_SIZE] = {0};

    int pos = buffer_append(buffer, 0, "cmd.exe /c \"");

    char *buff = buffer + pos;

    va_list args_copy;
    va_copy(args_copy, args);

    int to_write = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    int written = vsnprintf(buff, EXECUTE_BUFFER_SIZE - pos - 1, fmt, args_copy);
    va_end(args_copy);

    if (written != to_write) {
        fprintf(stderr, "build: EXECUTE_BUFFER_SIZE '%d' too small!\n"
                        "       Increase the value of the macro in file '%s'.\n",
                        EXECUTE_BUFFER_SIZE, __FILE__);
        exit(EXIT_FAILURE);
    }

    buff[written] = '\"';

    /* execute command */

    STARTUPINFO startup_info = {0};
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info = {0};

    if (!CreateProcessA(
        "C:\\Windows\\System32\\cmd.exe",
        buffer,
        NULL, NULL,
        FALSE,
        0,
        NULL, NULL,
        &startup_info,
        &process_info))
    {
        fprintf(stderr, "CreateProcessA(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    return process_info.hProcess;
}

static inline int exists(const char *path)
{
    WIN32_FIND_DATAA data = {0};

    HANDLE handle = FindFirstFileA(path, &data);

    if (handle == INVALID_HANDLE_VALUE) {
        int err = GetLastError();

        if (err == ERROR_FILE_NOT_FOUND)
            return 0;

        fprintf(stderr, "FindFirstFileA(): (%d)\n", err);
        exit(EXIT_FAILURE);
    }

    if (!FindClose(handle)) {
        fprintf(stderr, "FindClose(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    return 1;
}

static inline pid_t execute_argv(char *argv[])
{
    int size = 0;
    int count = 0;

    for (int i = 0; argv[i]; ++i, ++count) {
        size += strlen(argv[i]) + 1;
    }

    char *buffer = malloc(size);
    assert(buffer);

    int pos = 0;

    for (int i = 0; i < count; ++i) {
        int len = strlen(argv[i]);
        memcpy(buffer + pos, argv[i], len);
        buffer[pos + len] = (i == count - 1 ? '\0' : ' ');
        pos += len + 1;
    }

    STARTUPINFO startup_info = {0};
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info = {0};

    if (!CreateProcessA(
        NULL,
        buffer,
        NULL, NULL,
        FALSE,
        0,
        NULL, NULL,
        &startup_info,
        &process_info))
    {
        fprintf(stderr, "CreateProcessA(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    return process_info.hProcess;
}

static inline int file_time_cmp(file_time_t a, file_time_t b)
{
    return CompareFileTime(&a, &b);
}

static inline file_time_t mod_time(const char *path)
{
    WIN32_FIND_DATAA data = {0};

    HANDLE handle = FindFirstFileA(path, &data);

    if (handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "FindFirstFileA(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    if (!FindClose(handle)) {
        fprintf(stderr, "FindClose(): (%d)\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    return data.ftLastWriteTime;
}

#endif // _WIN32

static inline int compile_w(compile_info_t info)
{
    pid_t pid = compile(info);
    return wait_on_exits(&pid, 1);
}

static inline int execute_argv_w(char *argv[])
{
    pid_t pid = execute_argv(argv);
    return wait_on_exits(&pid, 1);
}

static inline pid_t execute(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    pid_t pid = execute_vargs(fmt, args);

    va_end(args);

    return pid;
}

static inline int execute_w(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    pid_t pid = execute_vargs(fmt, args);

    va_end(args);

    return wait_on_exits(&pid, 1);
}

static inline const char **merge(const char *arr_1[], const char *arr_2[])
{
    int size = 1;

    for (int i = 0; arr_1[i]; ++i, ++size)
        ;;

    for (int i = 0; arr_2[i]; ++i, ++size)
        ;;

    const char **new_arr = malloc(size * sizeof(const char *));
    assert(new_arr);

    int pos = 0;

    for (int i = 0; arr_1[i]; ++i, ++pos) {
        new_arr[pos] = arr_1[i];
    }

    for (int i = 0; arr_2[i]; ++i, ++pos) {
        new_arr[pos] = arr_2[i];
    }

    new_arr[pos] = NULL;

    return new_arr;
}

static inline int updated(mod_data_t *mod_data, const char *path)
{
    int index = find(path, mod_data->count, mod_data->paths);
    file_time_t ft = mod_time(path);

    if (index != -1) {
        if (file_time_cmp(ft, mod_data->times[index]) == 0)
            return 0;

        mod_data->times[index] = ft;
        return 1;
    }

    int capacity = mod_data->capacity;

    if (mod_data->count == capacity) {
        capacity = capacity ? capacity * 2 : 8;

        size_t size = sizeof(const char *) * capacity;
        const char **new_paths = realloc(mod_data->paths, size);
        if (!new_paths) {
            new_paths = malloc(size);
            assert(new_paths);
            memcpy(new_paths, mod_data->paths, mod_data->count * sizeof(const char *));
            free(mod_data->paths);
        }
        mod_data->paths = new_paths;

        size = sizeof(file_time_t) * capacity;
        file_time_t *new_times = realloc(mod_data->times, size);
        if (!new_times) {
            new_times = malloc(size);
            assert(new_times);
            memcpy(new_times, mod_data->times, mod_data->count * sizeof(file_time_t));
            free(mod_data->times);
        }
        mod_data->times = new_times;

        mod_data->capacity = capacity;
    }

    mod_data->paths[mod_data->count] = path;
    mod_data->times[mod_data->count] = ft;
    mod_data->count++;

    return 1;
}

static inline int try_rebuild_self(const char *build_files[], int argc, char *argv[])
{
    int needs_rebuild = 0;
    file_time_t exe_time = mod_time(argv[0]);

    for (int i = 0; build_files[i]; ++i) {
        if (file_time_cmp(mod_time(build_files[i]), exe_time) > 0) {
            needs_rebuild = 1;
            break;
        }
    }

    if (!needs_rebuild)
        return -1;

    printf("recompiling build program\n");

    int exe_len = strlen(argv[0]);

    char *move_dest = malloc(exe_len + 5);
    assert(move_dest);

    memcpy(move_dest, argv[0], exe_len);

    move_dest[exe_len + 0] = '.';
    move_dest[exe_len + 1] = 'o';
    move_dest[exe_len + 2] = 'l';
    move_dest[exe_len + 3] = 'd';
    move_dest[exe_len + 4] = '\0';

    if (exists(move_dest)) {
        remove(move_dest);
    }

    rename(argv[0], move_dest);

    compile_info_t info = { .output = argv[0], .source_files = build_files };

    int res = compile_w(info);
    if (res)
        return res;

    return execute_argv_w(argv);
}

#endif // BUILD_H_
