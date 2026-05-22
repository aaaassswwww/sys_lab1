#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

typedef struct {
    double real_sec;
    double user_sec;
    double sys_sec;
} timing_t;

typedef struct {
    int fd;
    unsigned char *buffer;
    size_t capacity;
    size_t begin;
    size_t end;
} my_file_t;

typedef struct {
    const char *input_path;
    const char *output_path;
    const char *write_dir;
    size_t *sizes;
    size_t size_count;
    size_t myfread_buffer_size;
    size_t write_total_bytes;
    bool quick_mode;
    bool sizes_overridden;
    bool myfread_buffer_overridden;
    bool write_total_overridden;
} config_t;

static const size_t k_default_sizes[] = {
    1, 2, 4, 8, 16, 32, 64, 128, 256, 512,
    1024, 2048, 4096, 8192, 16384, 32768, 65536, 16777216
};

static const size_t k_quick_sizes[] = {
    64, 256, 1024, 4096, 16384, 65536
};

static const size_t k_default_write_total_bytes = 8u * 1024u * 1024u;
static const size_t k_quick_write_total_bytes = 32u * 1024u * 1024u;

static void die_perror(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

static void die_usage(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    fprintf(stderr,
            "Usage: ./parta_bench --input <file> [--output results.csv]\n"
            "                     [--write-dir <dir>] [--sizes 1,2,4,...]\n"
            "                     [--myfread-buffer <bytes>] [--write-total-bytes <bytes>]\n");
    exit(EXIT_FAILURE);
}

static void print_help(void) {
    fprintf(stderr,
            "Usage: ./parta_bench --input <file> [--output results.csv]\n"
            "                     [--write-dir <dir>] [--sizes 1,2,4,...]\n"
            "                     [--myfread-buffer <bytes>] [--write-total-bytes <bytes>]\n"
            "                     [--quick]\n");
}

static double timeval_to_sec(const struct timeval *tv) {
    return (double) tv->tv_sec + (double) tv->tv_usec / 1000000.0;
}

static double timespec_to_sec(const struct timespec *ts) {
    return (double) ts->tv_sec + (double) ts->tv_nsec / 1000000000.0;
}

static timing_t timer_start(struct timespec *wall_start, struct rusage *usage_start) {
    if (clock_gettime(CLOCK_MONOTONIC, wall_start) != 0) {
        die_perror("clock_gettime");
    }
    if (getrusage(RUSAGE_SELF, usage_start) != 0) {
        die_perror("getrusage");
    }
    timing_t zero = {0};
    return zero;
}

static timing_t timer_end(const struct timespec *wall_start, const struct rusage *usage_start) {
    struct timespec wall_end;
    struct rusage usage_end;
    timing_t result;

    if (clock_gettime(CLOCK_MONOTONIC, &wall_end) != 0) {
        die_perror("clock_gettime");
    }
    if (getrusage(RUSAGE_SELF, &usage_end) != 0) {
        die_perror("getrusage");
    }

    result.real_sec = timespec_to_sec(&wall_end) - timespec_to_sec(wall_start);
    result.user_sec = timeval_to_sec(&usage_end.ru_utime) - timeval_to_sec(&usage_start->ru_utime);
    result.sys_sec = timeval_to_sec(&usage_end.ru_stime) - timeval_to_sec(&usage_start->ru_stime);
    return result;
}

static off_t file_size_of_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        die_perror("stat");
    }
    return st.st_size;
}

static size_t parse_size(const char *text) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (text[0] == '\0' || (end != NULL && *end != '\0')) {
        die_usage("invalid size value");
    }
    if (value == 0ULL) {
        die_usage("size value must be positive");
    }
    if (value > SIZE_MAX) {
        die_usage("size value is too large for this platform");
    }
    return (size_t) value;
}

static size_t *parse_sizes_csv(const char *csv, size_t *count_out) {
    size_t capacity = 8;
    size_t count = 0;
    size_t *values = malloc(capacity * sizeof(*values));
    char *copy;
    char *token;
    char *rest;

    if (values == NULL) {
        die_perror("malloc");
    }

    copy = strdup(csv);
    if (copy == NULL) {
        die_perror("strdup");
    }

    rest = copy;
    while ((token = strtok(rest, ",")) != NULL) {
        size_t value = parse_size(token);
        rest = NULL;
        if (count == capacity) {
            capacity *= 2;
            values = realloc(values, capacity * sizeof(*values));
            if (values == NULL) {
                die_perror("realloc");
            }
        }
        values[count++] = value;
    }

    free(copy);

    if (count == 0) {
        die_usage("no sizes were parsed from --sizes");
    }

    *count_out = count;
    return values;
}

static void set_default_sizes(config_t *config) {
    size_t count = sizeof(k_default_sizes) / sizeof(k_default_sizes[0]);
    config->sizes = malloc(count * sizeof(*config->sizes));
    if (config->sizes == NULL) {
        die_perror("malloc");
    }
    memcpy(config->sizes, k_default_sizes, sizeof(k_default_sizes));
    config->size_count = count;
}

static void set_quick_sizes(config_t *config) {
    size_t count = sizeof(k_quick_sizes) / sizeof(k_quick_sizes[0]);
    free(config->sizes);
    config->sizes = malloc(count * sizeof(*config->sizes));
    if (config->sizes == NULL) {
        die_perror("malloc");
    }
    memcpy(config->sizes, k_quick_sizes, sizeof(k_quick_sizes));
    config->size_count = count;
}

static void parse_args(int argc, char **argv, config_t *config) {
    int i;

    memset(config, 0, sizeof(*config));
    config->output_path = "results.csv";
    config->write_dir = ".";
    set_default_sizes(config);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--input") == 0) {
            if (++i >= argc) {
                die_usage("missing value for --input");
            }
            config->input_path = argv[i];
        } else if (strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                die_usage("missing value for --output");
            }
            config->output_path = argv[i];
        } else if (strcmp(argv[i], "--write-dir") == 0) {
            if (++i >= argc) {
                die_usage("missing value for --write-dir");
            }
            config->write_dir = argv[i];
        } else if (strcmp(argv[i], "--sizes") == 0) {
            free(config->sizes);
            config->sizes = NULL;
            if (++i >= argc) {
                die_usage("missing value for --sizes");
            }
            config->sizes = parse_sizes_csv(argv[i], &config->size_count);
            config->sizes_overridden = true;
        } else if (strcmp(argv[i], "--myfread-buffer") == 0) {
            if (++i >= argc) {
                die_usage("missing value for --myfread-buffer");
            }
            config->myfread_buffer_size = parse_size(argv[i]);
            config->myfread_buffer_overridden = true;
        } else if (strcmp(argv[i], "--write-total-bytes") == 0) {
            if (++i >= argc) {
                die_usage("missing value for --write-total-bytes");
            }
            config->write_total_bytes = parse_size(argv[i]);
            config->write_total_overridden = true;
        } else if (strcmp(argv[i], "--quick") == 0) {
            config->quick_mode = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            exit(EXIT_SUCCESS);
        } else {
            die_usage("unknown argument");
        }
    }

    if (config->input_path == NULL) {
        die_usage("--input is required");
    }

    if (config->quick_mode) {
        if (!config->sizes_overridden) {
            set_quick_sizes(config);
        }
        if (!config->myfread_buffer_overridden) {
            config->myfread_buffer_size = 4096;
        }
        if (!config->write_total_overridden) {
            config->write_total_bytes = k_quick_write_total_bytes;
        }
    }
}

static my_file_t *my_fopen_fd(int fd, size_t buffer_size) {
    my_file_t *file = malloc(sizeof(*file));
    if (file == NULL) {
        die_perror("malloc");
    }
    file->buffer = malloc(buffer_size);
    if (file->buffer == NULL) {
        free(file);
        die_perror("malloc");
    }
    file->fd = fd;
    file->capacity = buffer_size;
    file->begin = 0;
    file->end = 0;
    return file;
}

static void my_fclose(my_file_t *file) {
    if (file == NULL) {
        return;
    }
    free(file->buffer);
    free(file);
}

static size_t my_fread_bytes(void *dest, size_t bytes_requested, my_file_t *file) {
    unsigned char *out = dest;
    size_t bytes_read = 0;

    while (bytes_read < bytes_requested) {
        size_t available = file->end - file->begin;
        if (available == 0) {
            ssize_t rv = read(file->fd, file->buffer, file->capacity);
            if (rv < 0) {
                if (errno == EINTR) {
                    continue;
                }
                die_perror("read");
            }
            if (rv == 0) {
                break;
            }
            file->begin = 0;
            file->end = (size_t) rv;
            available = file->end;
        }

        size_t to_copy = bytes_requested - bytes_read;
        if (to_copy > available) {
            to_copy = available;
        }
        memcpy(out + bytes_read, file->buffer + file->begin, to_copy);
        file->begin += to_copy;
        bytes_read += to_copy;
    }

    return bytes_read;
}

static void write_csv_header(FILE *out) {
    fprintf(out, "method,buffer_size,sync_mode,total_bytes,real_sec,user_sec,sys_sec,mib_per_sec,notes\n");
}

static void write_result(FILE *out,
                         const char *method,
                         size_t buffer_size,
                         const char *sync_mode,
                         uint64_t total_bytes,
                         timing_t timing,
                         const char *notes) {
    double mib_per_sec = 0.0;
    if (timing.real_sec > 0.0) {
        mib_per_sec = ((double) total_bytes / (1024.0 * 1024.0)) / timing.real_sec;
    }
    fprintf(out,
            "%s,%zu,%s,%" PRIu64 ",%.9f,%.9f,%.9f,%.6f,%s\n",
            method,
            buffer_size,
            sync_mode,
            total_bytes,
            timing.real_sec,
            timing.user_sec,
            timing.sys_sec,
            mib_per_sec,
            notes);
}

static uint64_t benchmark_read_syscall(const char *path, size_t chunk_size, timing_t *timing_out) {
    int fd = open(path, O_RDONLY | O_BINARY);
    unsigned char *buffer = malloc(chunk_size);
    uint64_t total = 0;
    struct timespec wall_start;
    struct rusage usage_start;

    if (fd < 0) {
        die_perror("open");
    }
    if (buffer == NULL) {
        close(fd);
        die_perror("malloc");
    }

    timer_start(&wall_start, &usage_start);
    for (;;) {
        ssize_t rv = read(fd, buffer, chunk_size);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            free(buffer);
            die_perror("read");
        }
        if (rv == 0) {
            break;
        }
        total += (uint64_t) rv;
    }
    *timing_out = timer_end(&wall_start, &usage_start);

    free(buffer);
    close(fd);
    return total;
}

static uint64_t benchmark_getc_like(const char *path, bool use_function, timing_t *timing_out) {
    FILE *fp = fopen(path, "rb");
    int ch;
    uint64_t total = 0;
    struct timespec wall_start;
    struct rusage usage_start;

    if (fp == NULL) {
        die_perror("fopen");
    }

    timer_start(&wall_start, &usage_start);
    if (use_function) {
        while ((ch = fgetc(fp)) != EOF) {
            (void) ch;
            ++total;
        }
    } else {
        while ((ch = getc(fp)) != EOF) {
            (void) ch;
            ++total;
        }
    }
    if (ferror(fp)) {
        fclose(fp);
        die_perror("getc/fgetc");
    }
    *timing_out = timer_end(&wall_start, &usage_start);

    fclose(fp);
    return total;
}

static uint64_t benchmark_fread_stdio(const char *path, size_t chunk_size, timing_t *timing_out) {
    FILE *fp = fopen(path, "rb");
    unsigned char *buffer = malloc(chunk_size);
    uint64_t total = 0;
    struct timespec wall_start;
    struct rusage usage_start;

    if (fp == NULL) {
        die_perror("fopen");
    }
    if (buffer == NULL) {
        fclose(fp);
        die_perror("malloc");
    }

    timer_start(&wall_start, &usage_start);
    for (;;) {
        size_t n = fread(buffer, 1, chunk_size, fp);
        total += (uint64_t) n;
        if (n < chunk_size) {
            if (ferror(fp)) {
                fclose(fp);
                free(buffer);
                die_perror("fread");
            }
            break;
        }
    }
    *timing_out = timer_end(&wall_start, &usage_start);

    free(buffer);
    fclose(fp);
    return total;
}

static uint64_t benchmark_my_fread(const char *path,
                                   size_t chunk_size,
                                   size_t internal_buffer_size,
                                   timing_t *timing_out) {
    int fd = open(path, O_RDONLY | O_BINARY);
    my_file_t *myf;
    unsigned char *buffer = malloc(chunk_size);
    uint64_t total = 0;
    struct timespec wall_start;
    struct rusage usage_start;

    if (fd < 0) {
        die_perror("open");
    }
    if (buffer == NULL) {
        close(fd);
        die_perror("malloc");
    }

    myf = my_fopen_fd(fd, internal_buffer_size);

    timer_start(&wall_start, &usage_start);
    for (;;) {
        size_t n = my_fread_bytes(buffer, chunk_size, myf);
        total += (uint64_t) n;
        if (n < chunk_size) {
            break;
        }
    }
    *timing_out = timer_end(&wall_start, &usage_start);

    my_fclose(myf);
    free(buffer);
    close(fd);
    return total;
}

static uint64_t benchmark_write_syscall(const char *dir_path,
                                        size_t chunk_size,
                                        size_t total_bytes,
                                        bool use_osync,
                                        timing_t *timing_out) {
    char template_path[4096];
    int fd;
    unsigned char *buffer = malloc(chunk_size);
    uint64_t written_total = 0;
    struct timespec wall_start;
    struct rusage usage_start;
    int flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY;
    size_t i;

    if (buffer == NULL) {
        die_perror("malloc");
    }

    for (i = 0; i < chunk_size; ++i) {
        buffer[i] = (unsigned char) (i & 0xffu);
    }

    if (snprintf(template_path,
                 sizeof(template_path),
                 "%s/parta_write_%s_%zu.tmp",
                 dir_path,
                 use_osync ? "osync" : "nosync",
                 chunk_size) >= (int) sizeof(template_path)) {
        free(buffer);
        die_usage("write path is too long");
    }

    if (use_osync) {
        flags |= O_SYNC;
    }

    fd = open(template_path, flags, 0644);
    if (fd < 0) {
        free(buffer);
        die_perror("open");
    }

    timer_start(&wall_start, &usage_start);
    while (written_total < total_bytes) {
        size_t remaining = total_bytes - written_total;
        size_t this_chunk = remaining < chunk_size ? remaining : chunk_size;
        ssize_t rv = write(fd, buffer, this_chunk);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            unlink(template_path);
            free(buffer);
            die_perror("write");
        }
        if (rv == 0) {
            close(fd);
            unlink(template_path);
            free(buffer);
            die_usage("write() returned 0 unexpectedly");
        }
        written_total += (uint64_t) rv;
    }
    if (close(fd) != 0) {
        unlink(template_path);
        free(buffer);
        die_perror("close");
    }
    *timing_out = timer_end(&wall_start, &usage_start);

    if (unlink(template_path) != 0) {
        free(buffer);
        die_perror("unlink");
    }

    free(buffer);
    return written_total;
}

static void print_progress(const char *method, size_t chunk_size) {
    fprintf(stderr, "Running %-12s buffer=%zu bytes\n", method, chunk_size);
}

int main(int argc, char **argv) {
    config_t config;
    FILE *csv;
    size_t i;
    off_t input_size;
    size_t myfread_buffer_size;
    size_t write_total_bytes;

    parse_args(argc, argv, &config);
    input_size = file_size_of_path(config.input_path);
    if (input_size < 0) {
        die_usage("input file size is invalid");
    }
    if ((uint64_t) input_size > (uint64_t) SIZE_MAX) {
        die_usage("input file is too large for size_t-based write benchmark");
    }
    myfread_buffer_size = config.myfread_buffer_size == 0 ? 8192 : config.myfread_buffer_size;
    write_total_bytes = config.write_total_bytes == 0 ? k_default_write_total_bytes : config.write_total_bytes;

    csv = fopen(config.output_path, "w");
    if (csv == NULL) {
        die_perror("fopen");
    }
    write_csv_header(csv);

    for (i = 0; i < config.size_count; ++i) {
        timing_t timing;
        uint64_t total;
        size_t chunk_size = config.sizes[i];

        print_progress("read", chunk_size);
        total = benchmark_read_syscall(config.input_path, chunk_size, &timing);
        write_result(csv, "read", chunk_size, "n/a", total, timing, "read()");
    }

    {
        timing_t timing;
        uint64_t total;

        fprintf(stderr, "Running %-12s char-by-char stdio macro\n", "getc");
        total = benchmark_getc_like(config.input_path, false, &timing);
        write_result(csv, "getc", 1, "n/a", total, timing, "getc() macro path");

        fprintf(stderr, "Running %-12s char-by-char stdio function\n", "fgetc");
        total = benchmark_getc_like(config.input_path, true, &timing);
        write_result(csv, "fgetc", 1, "n/a", total, timing, "fgetc() function path");
    }

    for (i = 0; i < config.size_count; ++i) {
        timing_t timing;
        uint64_t total;
        size_t chunk_size = config.sizes[i];

        print_progress("fread", chunk_size);
        total = benchmark_fread_stdio(config.input_path, chunk_size, &timing);
        write_result(csv, "fread", chunk_size, "n/a", total, timing, "fread()");
    }

    for (i = 0; i < config.size_count; ++i) {
        timing_t timing;
        uint64_t total;
        size_t chunk_size = config.sizes[i];

        print_progress("my_fread", chunk_size);
        total = benchmark_my_fread(config.input_path, chunk_size, myfread_buffer_size, &timing);
        write_result(csv, "my_fread", chunk_size, "n/a", total, timing, "custom buffered read()");
    }

    for (i = 0; i < config.size_count; ++i) {
        timing_t timing;
        uint64_t total;
        size_t chunk_size = config.sizes[i];

        if (chunk_size < 32) {
            continue;
        }

        print_progress("write", chunk_size);
        total = benchmark_write_syscall(config.write_dir, chunk_size, write_total_bytes, false, &timing);
        write_result(csv, "write", chunk_size, "off", total, timing, "write() without O_SYNC");
    }

    for (i = 0; i < config.size_count; ++i) {
        timing_t timing;
        uint64_t total;
        size_t chunk_size = config.sizes[i];

        if (chunk_size < 32) {
            continue;
        }

        print_progress("write", chunk_size);
        total = benchmark_write_syscall(config.write_dir, chunk_size, write_total_bytes, true, &timing);
        write_result(csv, "write", chunk_size, "on", total, timing, "write() with O_SYNC");
    }

    fclose(csv);
    free(config.sizes);

    fprintf(stderr, "Finished. Results written to %s\n", config.output_path);
    fprintf(stderr, "quick mode: %s\n", config.quick_mode ? "on" : "off");
    fprintf(stderr, "my_fread internal buffer size: %zu bytes\n", myfread_buffer_size);
    fprintf(stderr, "write benchmark total bytes: %zu\n", write_total_bytes);
    return 0;
}
