#define _XOPEN_SOURCE 700
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include "walker.h"
#include "analyzer.h"

static int has_c_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 3) {
        return 0;
    }
    return (strcmp(name + len - 2, ".c") == 0);
}

static int scan_dir(const char *path,
                    int recurse,
                    struct mc_stats *stats,
                    int quiet,
                    int verbose)
{
    DIR *d = opendir(path);
    struct dirent *de;

    if (!d) {
        if (!quiet) {
            fprintf(stderr, "mancheck: cannot open directory %s\n", path);
        }
        return -1;
    }

    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        char full[4096];

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        snprintf(full, sizeof(full), "%s/%s", path, name);

        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (recurse) {
                scan_dir(full, recurse, stats, quiet, verbose);
            }
        } else if (S_ISREG(st.st_mode)) {
            if (has_c_extension(name)) {
                mc_analyze_file(full, stats, quiet, verbose);
            }
        }
    }

    closedir(d);
    return 0;
}

int mc_scan_path(const char *path,
                 int recurse,
                 struct mc_stats *stats,
                 int quiet,
                 int verbose)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        if (!quiet) {
            fprintf(stderr, "mancheck: cannot stat %s\n", path);
        }
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        return scan_dir(path, recurse, stats, quiet, verbose);
    } else if (S_ISREG(st.st_mode)) {
        return mc_analyze_file(path, stats, quiet, verbose);
    } else {
        if (!quiet) {
            fprintf(stderr, "mancheck: skipping non-regular %s\n", path);
        }
    }

    return 0;
}
