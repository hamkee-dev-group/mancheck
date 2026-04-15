#ifndef MANCHECK_CLI_H
#define MANCHECK_CLI_H

#include "mancheck.h"

struct mc_options {
    int recurse;
    int verbose;
    int quiet;
};

int mc_parse_args(int argc, char **argv,
                  struct mc_options *opts,
                  int *first_path);

void mc_print_usage(const char *progname);

#endif
