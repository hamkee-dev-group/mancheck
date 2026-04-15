#ifndef MANCHECK_ANALYZER_H
#define MANCHECK_ANALYZER_H

#include "mancheck.h"
#include "rules.h"

int mc_analyze_file(const char *path,
                    struct mc_stats *stats,
                    int quiet,
                    int verbose);

#endif
