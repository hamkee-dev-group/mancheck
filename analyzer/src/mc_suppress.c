#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>

#include "mc_suppress.h"

struct mc_suppress_rule {
    char *path;     /* canonical (realpath) */
    char *category;
};

static struct mc_suppress_rule *g_rules;
static size_t g_rule_count;
static size_t g_rule_cap;

static int add_rule(const char *resolved, const char *cat)
{
    if (g_rule_count == g_rule_cap) {
        size_t newcap = g_rule_cap ? g_rule_cap * 2 : 8;
        struct mc_suppress_rule *tmp =
            realloc(g_rules, newcap * sizeof(*tmp));
        if (!tmp)
            return -1;
        g_rules = tmp;
        g_rule_cap = newcap;
    }

    g_rules[g_rule_count].path     = strdup(resolved);
    g_rules[g_rule_count].category = strdup(cat);
    if (!g_rules[g_rule_count].path || !g_rules[g_rule_count].category) {
        free(g_rules[g_rule_count].path);
        free(g_rules[g_rule_count].category);
        return -1;
    }
    g_rule_count++;
    return 0;
}

int mc_suppress_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    /* Resolve the directory containing the suppressions file so that
     * relative entries are interpreted relative to it, not to CWD. */
    char abs_sup[PATH_MAX];
    if (!realpath(path, abs_sup)) {
        fclose(fp);
        return -1;
    }
    char *sup_dir_buf = strdup(abs_sup);
    const char *sup_dir = dirname(sup_dir_buf);

    char line[1024];
    int rc = 0;
    while (fgets(line, sizeof line, fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0')
            continue;

        char fpath[512], cat[128];
        if (sscanf(p, "%511s %127s", fpath, cat) != 2)
            continue;

        /* Build absolute path relative to the suppressions file dir. */
        char joined[PATH_MAX];
        if (fpath[0] != '/')
            snprintf(joined, sizeof joined, "%s/%s", sup_dir, fpath);
        else
            snprintf(joined, sizeof joined, "%s", fpath);

        char resolved[PATH_MAX];
        if (!realpath(joined, resolved))
            continue;

        if (add_rule(resolved, cat) != 0) {
            rc = -1;
            break;
        }
    }

    free(sup_dir_buf);
    fclose(fp);
    return rc;
}

int mc_suppress_check(const char *file, const char *category)
{
    if (!g_rules || !file || !category)
        return 0;

    char resolved[PATH_MAX];
    const char *norm = file;
    if (realpath(file, resolved))
        norm = resolved;

    for (size_t i = 0; i < g_rule_count; i++) {
        if (strcmp(g_rules[i].path, norm) == 0 &&
            strcmp(g_rules[i].category, category) == 0)
            return 1;
    }
    return 0;
}

void mc_suppress_free(void)
{
    for (size_t i = 0; i < g_rule_count; i++) {
        free(g_rules[i].path);
        free(g_rules[i].category);
    }
    free(g_rules);
    g_rules = NULL;
    g_rule_count = 0;
    g_rule_cap   = 0;
}
