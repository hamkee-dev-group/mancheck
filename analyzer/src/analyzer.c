#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "analyzer.h"
#include "report.h"

static const char *skip_space(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

/* Very naive heuristic: look for func(...) at start of a statement
 * not preceded by if/while/return/assignment.
 */
static void analyze_line(const char *path,
                         const char *line,
                         unsigned line_no,
                         struct mc_stats *stats,
                         int quiet)
{
    const char *p = skip_space(line);

    if (*p == '\0' || *p == '\n' || *p == '#') {
        return;
    }

    const char *start = p;
    const char *q = p;

    while (*q && (isalnum((unsigned char)*q) || *q == '_')) {
        q++;
    }

    if (q == start || *q != '(') {
        return;
    }

    char name[64];
    size_t len = (size_t)(q - start);
    if (len >= sizeof(name)) {
        return;
    }

    memcpy(name, start, len);
    name[len] = '\0';

    if (mc_find_rule(name) == NULL) {
        return;
    }

    mc_report_issue(path, line_no, (unsigned)(start - line + 1),
                    name, "return value appears to be ignored",
                    stats, quiet);
}

int mc_analyze_file(const char *path,
                    struct mc_stats *stats,
                    int quiet,
                    int verbose)
{
    FILE *f = fopen(path, "r");
    char buf[4096];
    unsigned line_no = 0;

    if (!f) {
        if (!quiet) {
            fprintf(stderr, "mancheck: cannot open %s\n", path);
        }
        return -1;
    }

    if (verbose) {
        fprintf(stderr, "mancheck: analyzing %s\n", path);
    }

    while (fgets(buf, sizeof(buf), f) != NULL) {
        line_no++;
        analyze_line(path, buf, line_no, stats, quiet);
    }

    fclose(f);

    if (stats) {
        stats->files_scanned++;
    }

    return 0;
}
