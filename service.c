#define _DEFAULT_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_MARK_SUFFIX "_LtH4Dk"
#define MARK_CONTENT "00000004 00000004\n"

typedef enum {
    ACTION_MARK,
    ACTION_REMOVE
} action_type;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} string_list;

typedef struct {
    string_list mark_dirs;
    string_list remove_dirs;
    string_list mark_patterns;
    string_list remove_patterns;
    const char *mark_suffix;
    const char *quarantine_dir;
    unsigned int interval_seconds;
    unsigned int passes;
    bool dry_run;
    bool use_syslog;
    bool verbose;
} config;

typedef struct {
    size_t dirs;
    size_t files;
    size_t mark_matches;
    size_t remove_matches;
    size_t marked;
    size_t removed;
    size_t quarantined;
    size_t skipped;
    size_t errors;
} run_stats;

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s [options]\n"
            "\n"
            "Safe file maintenance scanner. Dry-run is enabled by default.\n"
            "\n"
            "Options:\n"
            "  --mark-dir PATH          Recursively create marker files in PATH\n"
            "  --remove-dir PATH        Recursively remove or quarantine matches in PATH\n"
            "  --mark-pattern GLOB      Marker glob (default: test*.pdf)\n"
            "  --remove-pattern GLOB    Removal glob (defaults: test*.doc, test*.docx,\n"
            "                           test*.xls, test*.xlsx, test*.pdf)\n"
            "  --mark-suffix TEXT       Marker suffix (default: %s)\n"
            "  --quarantine-dir PATH    Move remove matches here instead of unlinking\n"
            "  --apply                  Apply changes. Without it, only report actions\n"
            "  --dry-run                Force report-only mode\n"
            "  --once                   Run exactly one scan pass\n"
            "  --passes N               Run N passes; 0 means forever (default: 1)\n"
            "  --interval SECONDS       Pause between passes (default: 60)\n"
            "  --syslog                 Also write messages to syslog\n"
            "  --verbose                Print every skipped/non-matching detail\n"
            "  -h, --help               Show this help\n",
            program,
            DEFAULT_MARK_SUFFIX);
}

static void log_msg(const config *cfg, int priority, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);
    va_end(args);

    if (cfg->use_syslog) {
        va_start(args, fmt);
        vsyslog(priority, fmt, args);
        va_end(args);
    }
}

static char *xstrdup(const char *value)
{
    char *copy = strdup(value);

    if (copy == NULL) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    return copy;
}

static void list_append(string_list *list, const char *value)
{
    char **items;

    if (list->count == list->capacity) {
        size_t next_capacity = list->capacity == 0 ? 4 : list->capacity * 2;

        items = realloc(list->items, next_capacity * sizeof(*items));
        if (items == NULL) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }

        list->items = items;
        list->capacity = next_capacity;
    }

    list->items[list->count++] = xstrdup(value);
}

static void list_free(string_list *list)
{
    size_t i;

    for (i = 0; i < list->count; i++) {
        free(list->items[i]);
    }

    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void config_init(config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mark_suffix = DEFAULT_MARK_SUFFIX;
    cfg->interval_seconds = 60;
    cfg->passes = 1;
    cfg->dry_run = true;

    list_append(&cfg->mark_patterns, "test*.pdf");
    list_append(&cfg->remove_patterns, "test*.doc");
    list_append(&cfg->remove_patterns, "test*.docx");
    list_append(&cfg->remove_patterns, "test*.xls");
    list_append(&cfg->remove_patterns, "test*.xlsx");
    list_append(&cfg->remove_patterns, "test*.pdf");
}

static void config_free(config *cfg)
{
    list_free(&cfg->mark_dirs);
    list_free(&cfg->remove_dirs);
    list_free(&cfg->mark_patterns);
    list_free(&cfg->remove_patterns);
}

static bool parse_uint(const char *value, unsigned int *out)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }

    *out = (unsigned int)parsed;
    return true;
}

static bool ends_with_slash(const char *path)
{
    size_t len = strlen(path);

    return len > 0 && path[len - 1] == '/';
}

static char *join_path(const char *dir, const char *name)
{
    bool has_slash = ends_with_slash(dir);
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    size_t total = dir_len + (has_slash ? 0U : 1U) + name_len + 1U;
    char *path = malloc(total);

    if (path == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    snprintf(path, total, "%s%s%s", dir, has_slash ? "" : "/", name);
    return path;
}

static char *append_suffix(const char *path, const char *suffix)
{
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    size_t total = path_len + suffix_len + 1U;
    char *result = malloc(total);

    if (result == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    snprintf(result, total, "%s%s", path, suffix);
    return result;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash == NULL ? path : slash + 1;
}

static bool matches_any(const string_list *patterns, const char *name)
{
    size_t i;

    for (i = 0; i < patterns->count; i++) {
        if (fnmatch(patterns->items[i], name, 0) == 0) {
            return true;
        }
    }

    return false;
}

static int write_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t written = write(fd, data, len);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        data += written;
        len -= (size_t)written;
    }

    return 0;
}

static bool mkdir_p(const char *path)
{
    char buffer[PATH_MAX];
    size_t len = strlen(path);
    size_t i;

    if (len == 0 || len >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return false;
    }

    memcpy(buffer, path, len + 1U);

    for (i = 1; buffer[i] != '\0'; i++) {
        if (buffer[i] == '/') {
            buffer[i] = '\0';
            if (mkdir(buffer, 0700) < 0 && errno != EEXIST) {
                return false;
            }
            buffer[i] = '/';
        }
    }

    if (mkdir(buffer, 0700) < 0 && errno != EEXIST) {
        return false;
    }

    return true;
}

static char *quarantine_path(const config *cfg, const char *source_path)
{
    const char *name = base_name(source_path);
    unsigned int attempt;
    time_t now = time(NULL);

    for (attempt = 0; attempt < 1000U; attempt++) {
        char prefix[64];
        char *candidate;
        struct stat st;

        snprintf(prefix, sizeof(prefix), "%ld-%ld-%03u-", (long)now, (long)getpid(), attempt);
        candidate = join_path(cfg->quarantine_dir, prefix);

        {
            char *full_candidate = append_suffix(candidate, name);
            free(candidate);
            candidate = full_candidate;
        }

        if (lstat(candidate, &st) < 0 && errno == ENOENT) {
            return candidate;
        }

        free(candidate);
    }

    errno = EEXIST;
    return NULL;
}

static void mark_file(const config *cfg, const char *path, run_stats *stats)
{
    char *marker_path = append_suffix(path, cfg->mark_suffix);
    int fd;

    if (cfg->dry_run) {
        log_msg(cfg, LOG_INFO, "DRY-RUN mark %s -> %s", path, marker_path);
        free(marker_path);
        return;
    }

    fd = open(marker_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        if (errno == EEXIST) {
            stats->skipped++;
            if (cfg->verbose) {
                log_msg(cfg, LOG_INFO, "skip existing marker %s", marker_path);
            }
        } else {
            stats->errors++;
            log_msg(cfg, LOG_ERR, "failed to create marker %s: %s", marker_path, strerror(errno));
        }
        free(marker_path);
        return;
    }

    if (write_all(fd, MARK_CONTENT, strlen(MARK_CONTENT)) < 0) {
        stats->errors++;
        log_msg(cfg, LOG_ERR, "failed to write marker %s: %s", marker_path, strerror(errno));
    } else {
        stats->marked++;
        log_msg(cfg, LOG_INFO, "marked %s -> %s", path, marker_path);
    }

    if (close(fd) < 0) {
        stats->errors++;
        log_msg(cfg, LOG_ERR, "failed to close marker %s: %s", marker_path, strerror(errno));
    }

    free(marker_path);
}

static void remove_file(const config *cfg, const char *path, run_stats *stats)
{
    if (cfg->quarantine_dir != NULL) {
        char *target;

        if (cfg->dry_run) {
            log_msg(cfg, LOG_INFO, "DRY-RUN quarantine %s -> %s/", path, cfg->quarantine_dir);
            return;
        }

        if (!mkdir_p(cfg->quarantine_dir)) {
            stats->errors++;
            log_msg(cfg, LOG_ERR, "failed to create quarantine directory %s: %s",
                    cfg->quarantine_dir, strerror(errno));
            return;
        }

        target = quarantine_path(cfg, path);
        if (target == NULL) {
            stats->errors++;
            log_msg(cfg, LOG_ERR, "failed to allocate quarantine path for %s: %s", path, strerror(errno));
            return;
        }

        if (rename(path, target) < 0) {
            stats->errors++;
            log_msg(cfg, LOG_ERR, "failed to quarantine %s -> %s: %s", path, target, strerror(errno));
        } else {
            stats->quarantined++;
            log_msg(cfg, LOG_INFO, "quarantined %s -> %s", path, target);
        }

        free(target);
        return;
    }

    if (cfg->dry_run) {
        log_msg(cfg, LOG_INFO, "DRY-RUN remove %s", path);
        return;
    }

    if (unlink(path) < 0) {
        stats->errors++;
        log_msg(cfg, LOG_ERR, "failed to remove %s: %s", path, strerror(errno));
    } else {
        stats->removed++;
        log_msg(cfg, LOG_INFO, "removed %s", path);
    }
}

static void handle_file(const config *cfg, const char *path, const char *name,
                        action_type action, run_stats *stats)
{
    if (action == ACTION_MARK) {
        if (matches_any(&cfg->mark_patterns, name)) {
            stats->mark_matches++;
            mark_file(cfg, path, stats);
        } else if (cfg->verbose) {
            stats->skipped++;
            log_msg(cfg, LOG_DEBUG, "skip mark pattern mismatch %s", path);
        }
        return;
    }

    if (matches_any(&cfg->remove_patterns, name)) {
        stats->remove_matches++;
        remove_file(cfg, path, stats);
    } else if (cfg->verbose) {
        stats->skipped++;
        log_msg(cfg, LOG_DEBUG, "skip remove pattern mismatch %s", path);
    }
}

static void scan_dir(const config *cfg, const char *path, action_type action, run_stats *stats)
{
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (dir == NULL) {
        stats->errors++;
        log_msg(cfg, LOG_ERR, "failed to open %s: %s", path, strerror(errno));
        return;
    }

    stats->dirs++;

    while ((entry = readdir(dir)) != NULL) {
        char *child_path;
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        child_path = join_path(path, entry->d_name);

        if (lstat(child_path, &st) < 0) {
            stats->errors++;
            log_msg(cfg, LOG_ERR, "failed to stat %s: %s", child_path, strerror(errno));
            free(child_path);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            scan_dir(cfg, child_path, action, stats);
        } else if (S_ISREG(st.st_mode)) {
            stats->files++;
            handle_file(cfg, child_path, entry->d_name, action, stats);
        } else {
            stats->skipped++;
            if (cfg->verbose) {
                log_msg(cfg, LOG_DEBUG, "skip non-regular path %s", child_path);
            }
        }

        free(child_path);
    }

    if (closedir(dir) < 0) {
        stats->errors++;
        log_msg(cfg, LOG_ERR, "failed to close %s: %s", path, strerror(errno));
    }
}

static bool sleep_interval(unsigned int seconds)
{
    struct timespec remaining;
    struct timespec requested;

    requested.tv_sec = (time_t)seconds;
    requested.tv_nsec = 0;

    while (nanosleep(&requested, &remaining) < 0) {
        if (errno != EINTR) {
            return false;
        }
        requested = remaining;
    }

    return true;
}

static int parse_args(config *cfg, int argc, char **argv)
{
    enum {
        OPT_MARK_DIR = 1000,
        OPT_REMOVE_DIR,
        OPT_MARK_PATTERN,
        OPT_REMOVE_PATTERN,
        OPT_MARK_SUFFIX,
        OPT_QUARANTINE_DIR,
        OPT_APPLY,
        OPT_DRY_RUN,
        OPT_ONCE,
        OPT_PASSES,
        OPT_INTERVAL,
        OPT_SYSLOG,
        OPT_VERBOSE
    };

    static const struct option options[] = {
        {"mark-dir", required_argument, NULL, OPT_MARK_DIR},
        {"remove-dir", required_argument, NULL, OPT_REMOVE_DIR},
        {"mark-pattern", required_argument, NULL, OPT_MARK_PATTERN},
        {"remove-pattern", required_argument, NULL, OPT_REMOVE_PATTERN},
        {"mark-suffix", required_argument, NULL, OPT_MARK_SUFFIX},
        {"quarantine-dir", required_argument, NULL, OPT_QUARANTINE_DIR},
        {"apply", no_argument, NULL, OPT_APPLY},
        {"dry-run", no_argument, NULL, OPT_DRY_RUN},
        {"once", no_argument, NULL, OPT_ONCE},
        {"passes", required_argument, NULL, OPT_PASSES},
        {"interval", required_argument, NULL, OPT_INTERVAL},
        {"syslog", no_argument, NULL, OPT_SYSLOG},
        {"verbose", no_argument, NULL, OPT_VERBOSE},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    for (;;) {
        int opt = getopt_long(argc, argv, "h", options, NULL);

        if (opt == -1) {
            break;
        }

        switch (opt) {
        case OPT_MARK_DIR:
            list_append(&cfg->mark_dirs, optarg);
            break;
        case OPT_REMOVE_DIR:
            list_append(&cfg->remove_dirs, optarg);
            break;
        case OPT_MARK_PATTERN:
            list_append(&cfg->mark_patterns, optarg);
            break;
        case OPT_REMOVE_PATTERN:
            list_append(&cfg->remove_patterns, optarg);
            break;
        case OPT_MARK_SUFFIX:
            cfg->mark_suffix = optarg;
            break;
        case OPT_QUARANTINE_DIR:
            cfg->quarantine_dir = optarg;
            break;
        case OPT_APPLY:
            cfg->dry_run = false;
            break;
        case OPT_DRY_RUN:
            cfg->dry_run = true;
            break;
        case OPT_ONCE:
            cfg->passes = 1;
            break;
        case OPT_PASSES:
            if (!parse_uint(optarg, &cfg->passes)) {
                fprintf(stderr, "Invalid --passes value: %s\n", optarg);
                return 2;
            }
            break;
        case OPT_INTERVAL:
            if (!parse_uint(optarg, &cfg->interval_seconds)) {
                fprintf(stderr, "Invalid --interval value: %s\n", optarg);
                return 2;
            }
            break;
        case OPT_SYSLOG:
            cfg->use_syslog = true;
            break;
        case OPT_VERBOSE:
            cfg->verbose = true;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 1;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }

    if (optind != argc) {
        fprintf(stderr, "Unexpected positional argument: %s\n", argv[optind]);
        usage(stderr, argv[0]);
        return 2;
    }

    if (cfg->mark_dirs.count == 0 && cfg->remove_dirs.count == 0) {
        fprintf(stderr, "Nothing to scan: add --mark-dir and/or --remove-dir.\n");
        usage(stderr, argv[0]);
        return 2;
    }

    return 0;
}

int main(int argc, char **argv)
{
    config cfg;
    unsigned int pass = 0;
    int parse_result;
    int exit_code = EXIT_SUCCESS;

    config_init(&cfg);
    parse_result = parse_args(&cfg, argc, argv);
    if (parse_result != 0) {
        config_free(&cfg);
        return parse_result == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (cfg.use_syslog) {
        openlog("service-mvp", LOG_CONS | LOG_PID, LOG_USER);
    }

    log_msg(&cfg, LOG_INFO, "starting scan mode=%s passes=%u interval=%u",
            cfg.dry_run ? "dry-run" : "apply", cfg.passes, cfg.interval_seconds);

    while (cfg.passes == 0 || pass < cfg.passes) {
        size_t i;
        run_stats stats;

        memset(&stats, 0, sizeof(stats));
        pass++;

        for (i = 0; i < cfg.mark_dirs.count; i++) {
            scan_dir(&cfg, cfg.mark_dirs.items[i], ACTION_MARK, &stats);
        }

        for (i = 0; i < cfg.remove_dirs.count; i++) {
            scan_dir(&cfg, cfg.remove_dirs.items[i], ACTION_REMOVE, &stats);
        }

        log_msg(&cfg, LOG_INFO,
                "pass %u summary: dirs=%zu files=%zu matches(mark=%zu remove=%zu) "
                "applied(marked=%zu removed=%zu quarantined=%zu) skipped=%zu errors=%zu",
                pass, stats.dirs, stats.files, stats.mark_matches, stats.remove_matches,
                stats.marked, stats.removed, stats.quarantined, stats.skipped, stats.errors);

        if (stats.errors > 0) {
            exit_code = EXIT_FAILURE;
        }

        if (cfg.passes != 0 && pass >= cfg.passes) {
            break;
        }

        if (!sleep_interval(cfg.interval_seconds)) {
            log_msg(&cfg, LOG_ERR, "sleep failed: %s", strerror(errno));
            exit_code = EXIT_FAILURE;
            break;
        }
    }

    if (cfg.use_syslog) {
        closelog();
    }

    config_free(&cfg);
    return exit_code;
}
