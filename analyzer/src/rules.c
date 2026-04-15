#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "rules.h"

static const struct mc_rule mc_builtin_rules[] = {
    { "read",   "Check return value of read()" },
    { "write",  "Check return value of write()" },
    { "close",  "Check return value of close()" },
    { "fread",  "Check return value of fread()" },
    { "fwrite", "Check return value of fwrite()" },
    { "fclose", "Check return value of fclose()" },
    { "open",   "Check return value of open()" },
    { "fopen",  "Check return value of fopen()" },
    { "malloc", "Check return value of malloc()" },
    { "calloc", "Check return value of calloc()" },
    { "realloc","Check return value of realloc()" },
};

const struct mc_rule *mc_find_rule(const char *name)
{
    size_t n = sizeof(mc_builtin_rules) / sizeof(mc_builtin_rules[0]);
    size_t i;

    for (i = 0; i < n; i++) {
        if (strcmp(mc_builtin_rules[i].name, name) == 0) {
            return &mc_builtin_rules[i];
        }
    }
    return NULL;
}

void mc_rules_dump(void)
{
    size_t n = sizeof(mc_builtin_rules) / sizeof(mc_builtin_rules[0]);
    size_t i;

    for (i = 0; i < n; i++) {
        printf("rule: %s - %s\n",
               mc_builtin_rules[i].name,
               mc_builtin_rules[i].description);
    }
}
