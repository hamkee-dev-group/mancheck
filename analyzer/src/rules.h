#ifndef MANCHECK_RULES_H
#define MANCHECK_RULES_H

struct mc_rule {
    const char *name;
    const char *description;
};

const struct mc_rule *mc_find_rule(const char *name);
void mc_rules_dump(void);

#endif
