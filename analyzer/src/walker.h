#ifndef MANCHECK_WALKER_H
#define MANCHECK_WALKER_H

#include "mancheck.h"

int mc_scan_path(const char *path,
                 int recurse,
                 struct mc_stats *stats,
                 int quiet,
                 int verbose);

#endif
