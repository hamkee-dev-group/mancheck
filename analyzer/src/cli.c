#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cli.h"

int mc_parse_args(int argc, char **argv,
                  struct mc_options *opts,
                  int *first_path)
{
    opts->recurse = 1;
    opts->verbose = 0;
    opts->quiet   = 0;

    int i = 1;
    for (; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-') {
            break;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            mc_print_usage(argv[0]);
            return 1;
        } else if (strcmp(arg, "-r") == 0) {
            opts->recurse = 1;
        } else if (strcmp(arg, "--no-rec") == 0) {
            opts->recurse = 0;
        } else if (strcmp(arg, "-v") == 0) {
            opts->verbose = 1;
        } else if (strcmp(arg, "-q") == 0) {
            opts->quiet = 1;
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], arg);
            mc_print_usage(argv[0]);
            return -1;
        }
    }

    if (i >= argc) {
        fprintf(stderr, "%s: no input files or directories\n", argv[0]);
        mc_print_usage(argv[0]);
        return -1;
    }

    *first_path = i;
    return 0;
}

void mc_print_usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [OPTIONS] <file-or-dir> [<file-or-dir>...]\n"
            "\n"
            "Options:\n"
            "  -r           Recurse into directories (default)\n"
            "  --no-rec     Do not recurse into directories\n"
            "  -q           Quiet output (summary only)\n"
            "  -v           Verbose\n"
            "  -h, --help   Show this help message\n",
            progname);
}
