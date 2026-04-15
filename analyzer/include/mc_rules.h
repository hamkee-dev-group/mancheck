#ifndef MC_RULES_H
#define MC_RULES_H

#include <stddef.h>

/* Flags describing what rules apply to a function.
 *
 * RETVAL_MUST_CHECK: ignoring the return value is suspicious.
 * DANGEROUS: function is inherently risky; using it at all may warrant a warning.
 * FORMAT_STRING: function uses a printf-style format string; non-literal format is suspicious.
 *
 * More flags can be added over time without changing the rest of the analyzer.
 */
typedef enum {
    MC_FUNC_RULE_RETVAL_MUST_CHECK = 1u << 0,
    MC_FUNC_RULE_DANGEROUS         = 1u << 1,
    MC_FUNC_RULE_FORMAT_STRING     = 1u << 2
} mc_func_rule_flags;

typedef struct {
    const char *name;
    unsigned flags; /* mc_func_rule_flags */
} mc_func_rule;

/* Look up rules for a function name.
 * Returns NULL if the function is not interesting (yet).
 *
 * Today: implemented via a static compiled-in table.
 * Future: implemented via SQLite/specdb, config, etc.
 */
const mc_func_rule *mc_rules_lookup(const char *name);

/* Map flags to a JSON-friendly category string.
 * e.g. "dangerous_function", "format_string", "return_value_check", "other"
 */
const char *mc_rules_category(unsigned flags);

#endif /* MC_RULES_H */
