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

/* --- inline (comment-based) suppression ------------------------------ */

static unsigned *g_inline_same_line;
static size_t    g_inline_same_count;
static size_t    g_inline_same_cap;
static unsigned *g_inline_next_diag;
static size_t    g_inline_next_count;
static size_t    g_inline_next_cap;
static size_t    g_inline_next_pos;

static void inline_add(unsigned **items,
                       size_t *count,
                       size_t *cap,
                       unsigned line)
{
    if (*count == *cap) {
        size_t newcap = *cap ? *cap * 2 : 16;
        unsigned *tmp = realloc(*items, newcap * sizeof(*tmp));
        if (!tmp)
            return;
        *items = tmp;
        *cap = newcap;
    }
    (*items)[(*count)++] = line;
}

static void inline_add_same_line(unsigned line)
{
    inline_add(&g_inline_same_line, &g_inline_same_count, &g_inline_same_cap,
               line);
}

static void inline_add_next_diag(unsigned line)
{
    inline_add(&g_inline_next_diag, &g_inline_next_count, &g_inline_next_cap,
               line);
}

/* Check if the comment body (after //) starts with a suppression marker,
 * skipping leading whitespace. */
static int has_marker(const char *p, const char *eol)
{
    while (p < eol && (*p == ' ' || *p == '\t'))
        p++;
    if ((size_t)(eol - p) >= 9 && strncmp(p, "mc:ignore", 9) == 0)
        return 1;
    if ((size_t)(eol - p) >= 16 && strncmp(p, "NOLINT(mancheck)", 16) == 0)
        return 1;
    return 0;
}

/* Return 1 if everything before `comment_start` on the line starting
 * at `line_start` is only whitespace (i.e. a comment-only line). */
static int is_comment_only(const char *line_start, const char *comment_start)
{
    const char *p = line_start;
    while (p < comment_start) {
        if (*p != ' ' && *p != '\t')
            return 0;
        p++;
    }
    return 1;
}

void mc_inline_suppress_scan(const char *src_raw)
{
    if (!src_raw)
        return;

    /*
     * State machine that tracks C lexical context so we only recognise
     * // comments that are actual comments (not inside strings, char
     * literals, or block comments).
     */
    enum {
        ST_CODE,
        ST_STRING,          /* inside "..." */
        ST_STRING_ESC,      /* backslash inside string */
        ST_CHAR,            /* inside '...' */
        ST_CHAR_ESC,        /* backslash inside char literal */
        ST_SLASH,           /* seen '/' in code context */
        ST_LINE_COMMENT,    /* inside // ... */
        ST_BLOCK_COMMENT,   /* inside / * ... */
        ST_BLOCK_STAR       /* seen '*' inside block comment */
    } state = ST_CODE;

    unsigned lineno = 1;
    const char *line_start = src_raw;
    const char *comment_start = NULL; /* where // began */

    for (const char *p = src_raw; *p; p++) {
        char c = *p;

        switch (state) {
        case ST_CODE:
            if (c == '"')       state = ST_STRING;
            else if (c == '\'') state = ST_CHAR;
            else if (c == '/')  { state = ST_SLASH; comment_start = p; }
            break;

        case ST_STRING:
            if (c == '\\')      state = ST_STRING_ESC;
            else if (c == '"')  state = ST_CODE;
            else if (c == '\n') state = ST_CODE; /* unterminated string */
            break;

        case ST_STRING_ESC:
            state = ST_STRING;
            break;

        case ST_CHAR:
            if (c == '\\')      state = ST_CHAR_ESC;
            else if (c == '\'') state = ST_CODE;
            else if (c == '\n') state = ST_CODE;
            break;

        case ST_CHAR_ESC:
            state = ST_CHAR;
            break;

        case ST_SLASH:
            if (c == '/') {
                state = ST_LINE_COMMENT;
                /* p points at the second '/', comment body starts at p+1 */
                const char *eol = p + 1;
                while (*eol && *eol != '\n')
                    eol++;
                if (has_marker(p + 1, eol)) {
                    if (is_comment_only(line_start, comment_start))
                        inline_add_next_diag(lineno);
                    else
                        inline_add_same_line(lineno);
                }
            } else if (c == '*') {
                state = ST_BLOCK_COMMENT;
            } else {
                state = ST_CODE;
                /* Re-examine current char in CODE state */
                if (c == '"')       state = ST_STRING;
                else if (c == '\'') state = ST_CHAR;
                else if (c == '/')  { state = ST_SLASH; comment_start = p; }
            }
            break;

        case ST_LINE_COMMENT:
            /* handled at newline below */
            break;

        case ST_BLOCK_COMMENT:
            if (c == '*') state = ST_BLOCK_STAR;
            break;

        case ST_BLOCK_STAR:
            if (c == '/')      state = ST_CODE;
            else if (c != '*') state = ST_BLOCK_COMMENT;
            break;
        }

        if (c == '\n') {
            lineno++;
            line_start = p + 1;
            if (state == ST_LINE_COMMENT)
                state = ST_CODE;
        }
    }
}

int mc_inline_suppress_check(unsigned line)
{
    int suppressed = 0;

    while (g_inline_next_pos < g_inline_next_count &&
           g_inline_next_diag[g_inline_next_pos] < line) {
        g_inline_next_pos++;
        suppressed = 1;
    }

    for (size_t i = 0; i < g_inline_same_count; i++) {
        if (g_inline_same_line[i] == line)
            return 1;
    }

    return suppressed;
}

void mc_inline_suppress_clear(void)
{
    free(g_inline_same_line);
    g_inline_same_line = NULL;
    g_inline_same_count = 0;
    g_inline_same_cap   = 0;

    free(g_inline_next_diag);
    g_inline_next_diag = NULL;
    g_inline_next_count = 0;
    g_inline_next_cap   = 0;
    g_inline_next_pos   = 0;
}
