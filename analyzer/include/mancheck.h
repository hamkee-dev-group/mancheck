#ifndef MANCHECK_H
#define MANCHECK_H

#include <stddef.h>
#include <stdint.h>

struct mc_stats {
    uint64_t files_scanned;
    uint64_t issues_found;
};

#endif
