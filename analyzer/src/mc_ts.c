#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mc_ts.h"
#include "mc_rules.h"
#include "report.h"
#include "mc_suppress.h"

/* Provided by vendor/tree-sitter-c/src/parser.c */
const TSLanguage *tree_sitter_c(void);

/* --- TSNode "null" helper ------------------------------------------- */

static TSNode
mc_ts_null_node(void)
{
    TSNode n;
    memset(&n, 0, sizeof n);
    return n;
}

/* --- helpers -------------------------------------------------------- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(f);
        return NULL;
    }

    long sz = ftell(f);
    if (sz < 0) {
        perror("ftell");
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        perror("fseek");
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        perror("malloc");
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    if (n != (size_t)sz) {
        perror("fread");
        free(buf);
        fclose(f);
        return NULL;
    }

    buf[n] = '\0';
    fclose(f);

    if (out_len) {
        *out_len = n;
    }
    return buf;
}

/* --- file lifecycle ------------------------------------------------- */

static bool mc_ts_file_init_source(mc_ts_file *f, const char *path,
                                   char *source, size_t source_len,
                                   const unsigned *line_map,
                                   size_t line_map_count) {
    memset(f, 0, sizeof(*f));
    f->path = path;
    f->source = source;
    f->source_len = source_len;
    f->line_map = line_map;
    f->line_map_count = line_map_count;

    f->parser = ts_parser_new();
    if (!f->parser) {
        fprintf(stderr, "mc_ts: ts_parser_new failed\n");
        f->source = NULL;  /* caller owns the buffer */
        return false;
    }

    const TSLanguage *lang = tree_sitter_c();
    if (!ts_parser_set_language(f->parser, lang)) {
        fprintf(stderr, "mc_ts: ts_parser_set_language failed\n");
        ts_parser_delete(f->parser);
        f->parser = NULL;
        f->source = NULL;
        return false;
    }

    f->tree = ts_parser_parse_string(
        f->parser,
        NULL,
        f->source,
        (uint32_t)f->source_len
    );

    if (!f->tree) {
        fprintf(stderr, "mc_ts: ts_parser_parse_string failed\n");
        ts_parser_delete(f->parser);
        f->parser = NULL;
        f->source = NULL;
        return false;
    }

    f->root = ts_tree_root_node(f->tree);
    return true;
}

bool mc_ts_file_init(mc_ts_file *f, const char *path) {
    memset(f, 0, sizeof(*f));
    f->path = path;

    f->source = read_file(path, &f->source_len);
    if (!f->source) {
        fprintf(stderr, "mc_ts: failed to read %s\n", path);
        return false;
    }

    f->parser = ts_parser_new();
    if (!f->parser) {
        fprintf(stderr, "mc_ts: ts_parser_new failed\n");
        free(f->source);
        f->source = NULL;
        return false;
    }

    const TSLanguage *lang = tree_sitter_c();
    if (!ts_parser_set_language(f->parser, lang)) {
        fprintf(stderr, "mc_ts: ts_parser_set_language failed\n");
        ts_parser_delete(f->parser);
        f->parser = NULL;
        free(f->source);
        f->source = NULL;
        return false;
    }

    f->tree = ts_parser_parse_string(
        f->parser,
        NULL,
        f->source,
        (uint32_t)f->source_len
    );
    if (!f->tree) {
        fprintf(stderr, "mc_ts: parsing failed for %s\n", path);
        ts_parser_delete(f->parser);
        f->parser = NULL;
        free(f->source);
        f->source = NULL;
        return false;
    }

    f->root = ts_tree_root_node(f->tree);
    if (ts_node_is_null(f->root)) {
        fprintf(stderr, "mc_ts: null root node for %s\n", path);
        ts_tree_delete(f->tree);
        f->tree = NULL;
        ts_parser_delete(f->parser);
        f->parser = NULL;
        free(f->source);
        f->source = NULL;
        return false;
    }

    return true;
}

void mc_ts_file_destroy(mc_ts_file *f) {
    if (f->tree) {
        ts_tree_delete(f->tree);
        f->tree = NULL;
    }
    if (f->parser) {
        ts_parser_delete(f->parser);
        f->parser = NULL;
    }
    if (f->source) {
        free(f->source);
        f->source = NULL;
    }
    f->source_len = 0;
}

/* --- status helpers ------------------------------------------------- */

static const char *mc_call_status_str(mc_call_status s) {
    switch (s) {
    case MC_CALL_STATUS_UNCHECKED:        return "unchecked";
    case MC_CALL_STATUS_CHECKED_COND:     return "checked_cond";
    case MC_CALL_STATUS_STORED:           return "stored";
    case MC_CALL_STATUS_STORED_UNCHECKED: return "stored_unchecked";
    case MC_CALL_STATUS_PROPAGATED:       return "propagated";
    case MC_CALL_STATUS_IGNORED_EXPLICIT: return "ignored_explicit";
    default:                              return "unknown";
    }
}

/* --- usage classification ------------------------------------------- */

/* Is this call effectively part of the condition of if/while/do/for?
 * We check that the node is in the "condition" field of the control
 * statement, not just anywhere inside its body. */
static bool is_in_condition_context(TSNode node) {
    TSNode cur = node;
    while (!ts_node_is_null(cur)) {
        TSNode parent = ts_node_parent(cur);
        if (ts_node_is_null(parent))
            break;

        const char *ptype = ts_node_type(parent);

        if (strcmp(ptype, "if_statement") == 0 ||
            strcmp(ptype, "while_statement") == 0 ||
            strcmp(ptype, "do_statement") == 0 ||
            strcmp(ptype, "for_statement") == 0) {
            /* Check that cur is the "condition" child, not the body */
            TSNode cond = ts_node_child_by_field_name(
                parent, "condition", (uint32_t)strlen("condition"));
            if (!ts_node_is_null(cond) &&
                ts_node_start_byte(cur) >= ts_node_start_byte(cond) &&
                ts_node_end_byte(cur) <= ts_node_end_byte(cond)) {
                return true;
            }
            /* cur is in the body, not the condition -- keep walking
             * up in case this is nested inside an outer condition. */
        }

        cur = parent;
    }
    return false;
}

/* Climb through expression wrappers, but STOP at assignments/initializers. */
static TSNode ascend_expression(TSNode node) {
    TSNode cur = node;
    for (;;) {
        TSNode parent = ts_node_parent(cur);
        if (ts_node_is_null(parent))
            break;

        const char *ptype = ts_node_type(parent);
        if (strcmp(ptype, "parenthesized_expression") == 0 ||
            strcmp(ptype, "cast_expression") == 0 ||
            strcmp(ptype, "unary_expression") == 0 ||
            strcmp(ptype, "binary_expression") == 0 ||
            strcmp(ptype, "subscript_expression") == 0 ||
            strcmp(ptype, "field_expression") == 0) {
            cur = parent;
            continue;
        }
        break;
    }
    return cur;
}

static bool is_void_cast(TSNode expr, const char *source, size_t len) {
    const char *type = ts_node_type(expr);
    if (strcmp(type, "cast_expression") != 0)
        return false;

    uint32_t child_count = ts_node_child_count(expr);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(expr, i);
        const char *ctype = ts_node_type(child);
        if (strcmp(ctype, "type_descriptor") == 0 ||
            strcmp(ctype, "primitive_type") == 0) {
            uint32_t sb = ts_node_start_byte(child);
            uint32_t eb = ts_node_end_byte(child);
            if (sb >= eb || eb > len)
                continue;
            size_t n = eb - sb;
            if (n >= 16) n = 15;
            char buf[16];
            memcpy(buf, source + sb, n);
            buf[n] = '\0';
            if (strstr(buf, "void") != NULL)
                return true;
        }
    }
    return false;
}

static mc_call_status classify_call_usage(TSNode call_node,
                                          const char *source,
                                          size_t source_len) {
    TSNode expr = ascend_expression(call_node);
    TSNode parent = ts_node_parent(expr);
    if (ts_node_is_null(parent))
        return MC_CALL_STATUS_UNCHECKED;

    const char *etype = ts_node_type(expr);
    const char *ptype = ts_node_type(parent);

    /* Result assigned or used to initialize a variable */
    if (strcmp(etype, "assignment_expression") == 0 ||
        strcmp(etype, "init_declarator") == 0 ||
        strcmp(ptype, "assignment_expression") == 0 ||
        strcmp(ptype, "init_declarator") == 0) {
        return MC_CALL_STATUS_STORED;
    }

    /* Bare expression statement: result discarded */
    if (strcmp(ptype, "expression_statement") == 0) {
        if (is_void_cast(expr, source, source_len)) {
            return MC_CALL_STATUS_IGNORED_EXPLICIT;
        }
        return MC_CALL_STATUS_UNCHECKED;
    }

    /* Used in a condition (if/while/for/do) */
    if (is_in_condition_context(expr)) {
        return MC_CALL_STATUS_CHECKED_COND;
    }

    /* Returned to caller */
    if (strcmp(ptype, "return_statement") == 0) {
        return MC_CALL_STATUS_PROPAGATED;
    }

    /* Used as an argument to another call, comma expression, etc.
     * The value is consumed by something, so treat as stored. */
    if (strcmp(ptype, "argument_list") == 0 ||
        strcmp(ptype, "comma_expression") == 0 ||
        strcmp(ptype, "conditional_expression") == 0) {
        return MC_CALL_STATUS_STORED;
    }

    /* Anything else we don't recognize: conservatively assume stored
     * so we don't false-positive on uncommon AST shapes. */
    return MC_CALL_STATUS_STORED;
}

/* --- call info + sinks ---------------------------------------------- */

typedef struct {
    const char *func_name;
    mc_call_status status;
    unsigned line;
    unsigned col;
    unsigned flags; /* mc_func_rule_flags */
    TSNode call_node;
} mc_call_info;

typedef void (*mc_call_sink)(const mc_ts_file *file,
                             const mc_call_info *info,
                             void *userdata);

/* --- format-string helper ------------------------------------------- */

static bool is_nonliteral_format_call(const mc_ts_file *file,
                                      const mc_call_info *info) {
    (void)file;

    if (!(info->flags & MC_FUNC_RULE_FORMAT_STRING))
        return false;

    TSNode call = info->call_node;
    if (ts_node_is_null(call))
        return false;

    TSNode args = ts_node_child_by_field_name(call, "arguments",
                                              (uint32_t)strlen("arguments"));
    if (ts_node_is_null(args))
        return false;

    uint32_t named_count = ts_node_named_child_count(args);
    if (named_count == 0)
        return false;

    uint32_t fmt_index = 0;

    if (strcmp(info->func_name, "printf") == 0 ||
        strcmp(info->func_name, "vprintf") == 0 ||
        strcmp(info->func_name, "scanf") == 0) {
        fmt_index = 0;
    } else if (strcmp(info->func_name, "fprintf") == 0 ||
               strcmp(info->func_name, "vfprintf") == 0 ||
               strcmp(info->func_name, "fscanf") == 0) {
        if (named_count < 2)
            return false;
        fmt_index = 1;
    } else if (strcmp(info->func_name, "dprintf") == 0) {
        if (named_count < 2)
            return false;
        fmt_index = 1;
    } else if (strcmp(info->func_name, "snprintf") == 0 ||
               strcmp(info->func_name, "vsnprintf") == 0) {
        if (named_count < 3)
            return false;
        fmt_index = 2;
    } else if (strcmp(info->func_name, "sprintf") == 0 ||
               strcmp(info->func_name, "vsprintf") == 0 ||
               strcmp(info->func_name, "sscanf") == 0 ||
               strcmp(info->func_name, "asprintf") == 0 ||
               strcmp(info->func_name, "vasprintf") == 0) {
        if (named_count < 2)
            return false;
        fmt_index = 1;
    } else {
        /*
         * Unknown format-string function (discovered via specdb).
         * Heuristic: the format parameter is typically the last named
         * parameter before the variadic args.  For functions with 1 arg
         * it's index 0, for 2 args it's index 1, etc.  But we can't
         * easily distinguish the variadic args in the tree-sitter AST.
         * Fall back: scan all string-literal arguments; if none of them
         * are string literals, the format is likely non-literal.
         * Simpler: check each argument from 0..n-1 and report if any
         * const char* position is non-literal.
         *
         * Simplest practical heuristic: for a function with N args where
         * N >= 2, the format is likely at index N-2 (penultimate arg
         * before the variadic expansion). For N == 1, it's index 0.
         * For N == 0, bail.
         */
        if (named_count == 0)
            return false;
        if (named_count == 1)
            fmt_index = 0;
        else
            fmt_index = 1;  /* most format-string functions: fmt is 2nd arg */
    }

    if (fmt_index >= named_count)
        return false;

    TSNode fmt_node = ts_node_named_child(args, fmt_index);
    if (ts_node_is_null(fmt_node))
        return false;

    const char *t = ts_node_type(fmt_node);
    if (strcmp(t, "string_literal") == 0) {
        return false;
    }

    return true;
}

/* forward-declare (defined below, after text_warning_sink) */
static void mc_ts_node_text_copy(const mc_ts_file *f,
                                 TSNode node,
                                 char *buf,
                                 size_t bufsize);

/* --- stored-but-not-checked analysis -------------------------------- */

/* Check if text [text, text+text_len) contains `name` as a whole-word identifier. */
static bool
text_contains_ident(const char *text, size_t text_len,
                    const char *name, size_t name_len)
{
    if (name_len == 0 || name_len > text_len)
        return false;

    for (size_t i = 0; i + name_len <= text_len; i++) {
        if (memcmp(text + i, name, name_len) != 0)
            continue;
        /* word-boundary check */
        bool left_ok  = (i == 0) ||
                        !(isalnum((unsigned char)text[i - 1]) || text[i - 1] == '_');
        bool right_ok = (i + name_len >= text_len) ||
                        !(isalnum((unsigned char)text[i + name_len]) ||
                          text[i + name_len] == '_');
        if (left_ok && right_ok)
            return true;
    }
    return false;
}

/* Walk up from `node` until we find a direct child of a compound_statement. */
static TSNode
find_enclosing_block_stmt(TSNode node)
{
    TSNode child  = node;
    TSNode parent = ts_node_parent(node);
    while (!ts_node_is_null(parent)) {
        if (strcmp(ts_node_type(parent), "compound_statement") == 0)
            return child;
        child  = parent;
        parent = ts_node_parent(parent);
    }
    return mc_ts_null_node();
}

/* Extract the variable name when a call's return value is stored via
 * init_declarator (int fd = open(..)) or assignment_expression (fd = open(..)). */
static bool
extract_store_varname(const mc_ts_file *f, TSNode call_node,
                      char *buf, size_t bufsize)
{
    TSNode expr   = ascend_expression(call_node);
    TSNode parent = ts_node_parent(expr);
    if (ts_node_is_null(parent))
        return false;

    const char *etype = ts_node_type(expr);
    const char *ptype = ts_node_type(parent);

    TSNode var_node = mc_ts_null_node();

    /* Case 1: init_declarator  →  int fd = open(...) */
    if (strcmp(etype, "init_declarator") == 0 ||
        strcmp(ptype, "init_declarator") == 0) {
        TSNode init = (strcmp(etype, "init_declarator") == 0) ? expr : parent;
        var_node = ts_node_child_by_field_name(
            init, "declarator", (uint32_t)strlen("declarator"));
        /* Drill through pointer_declarator: int *p = malloc(...) */
        while (!ts_node_is_null(var_node) &&
               strcmp(ts_node_type(var_node), "pointer_declarator") == 0) {
            TSNode inner = mc_ts_null_node();
            uint32_t nc = ts_node_named_child_count(var_node);
            for (uint32_t i = 0; i < nc; i++) {
                TSNode ch = ts_node_named_child(var_node, i);
                if (strcmp(ts_node_type(ch), "identifier") == 0) {
                    inner = ch;
                    break;
                }
            }
            var_node = inner;
        }
    }
    /* Case 2: assignment_expression  →  fd = open(...) */
    else if (strcmp(etype, "assignment_expression") == 0 ||
             strcmp(ptype, "assignment_expression") == 0) {
        TSNode assign = (strcmp(etype, "assignment_expression") == 0) ? expr : parent;
        var_node = ts_node_child_by_field_name(
            assign, "left", (uint32_t)strlen("left"));
    }

    if (ts_node_is_null(var_node))
        return false;
    if (strcmp(ts_node_type(var_node), "identifier") != 0)
        return false;

    mc_ts_node_text_copy(f, var_node, buf, bufsize);
    return buf[0] != '\0';
}

/* Check if a subtree's source text contains the given identifier. */
static bool
subtree_has_ident(const mc_ts_file *f, TSNode node,
                  const char *name, size_t name_len)
{
    uint32_t sb = ts_node_start_byte(node);
    uint32_t eb = ts_node_end_byte(node);
    if (eb > f->source_len) eb = (uint32_t)f->source_len;
    if (sb >= eb) return false;
    return text_contains_ident(f->source + sb, eb - sb, name, name_len);
}

/* Scan siblings after `store_stmt` in the enclosing compound_statement.
 * Return true if `varname` is used in a condition, return, or (void) cast. */
static bool
is_var_checked_in_block(const mc_ts_file *f, TSNode store_stmt,
                        const char *varname)
{
    size_t vlen = strlen(varname);
    TSNode parent = ts_node_parent(store_stmt);
    if (ts_node_is_null(parent))
        return false;

    uint32_t n = ts_node_named_child_count(parent);
    bool found_self = false;

    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(parent, i);

        if (!found_self) {
            if (ts_node_start_byte(child) == ts_node_start_byte(store_stmt) &&
                ts_node_end_byte(child) == ts_node_end_byte(store_stmt))
                found_self = true;
            continue;
        }

        const char *ntype = ts_node_type(child);

        /* if / while / for / do / switch: check condition subtree */
        if (strcmp(ntype, "if_statement") == 0 ||
            strcmp(ntype, "while_statement") == 0 ||
            strcmp(ntype, "for_statement") == 0 ||
            strcmp(ntype, "do_statement") == 0 ||
            strcmp(ntype, "switch_statement") == 0) {
            TSNode cond = ts_node_child_by_field_name(
                child, "condition", (uint32_t)strlen("condition"));
            if (!ts_node_is_null(cond) && subtree_has_ident(f, cond, varname, vlen))
                return true;
        }

        /* return var; */
        if (strcmp(ntype, "return_statement") == 0) {
            if (subtree_has_ident(f, child, varname, vlen))
                return true;
        }

        /* (void)var; — explicit acknowledgment */
        if (strcmp(ntype, "expression_statement") == 0) {
            TSNode inner = ts_node_named_child(child, 0);
            if (!ts_node_is_null(inner) &&
                strcmp(ts_node_type(inner), "cast_expression") == 0) {
                if (subtree_has_ident(f, inner, varname, vlen))
                    return true;
            }
        }
    }

    return false;
}

/* --- generic call walker -------------------------------------------- */

static void analyze_calls(const mc_ts_file *f, mc_call_sink sink, void *userdata) {
    const TSLanguage *lang = tree_sitter_c();

    const char *query_src =
        "(call_expression "
        "  function: (identifier) @func "
        ") @call";

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery *query = ts_query_new(
        lang,
        query_src,
        (uint32_t)strlen(query_src),
        &error_offset,
        &error_type
    );
    if (!query) {
        fprintf(stderr, "mc_ts: ts_query_new failed at offset %u (error %d)\n",
                error_offset, (int)error_type);
        return;
    }

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, f->root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        char func_name[256];
        bool have_name = false;
        TSNode call_node;
        bool have_call = false;

        for (uint32_t i = 0; i < match.capture_count; i++) {
            TSQueryCapture capture = match.captures[i];
            TSNode node = capture.node;
            uint32_t cap_id = capture.index;

            uint32_t name_len = 0;
            const char *cap_name =
                ts_query_capture_name_for_id(query, cap_id, &name_len);
            if (!cap_name || name_len == 0)
                continue;

            if (name_len == 4 && strncmp(cap_name, "func", 4) == 0) {
                uint32_t sb = ts_node_start_byte(node);
                uint32_t eb = ts_node_end_byte(node);
                if (eb > f->source_len)
                    eb = (uint32_t)f->source_len;
                if (sb >= eb)
                    continue;
                size_t fn_len = eb - sb;
                if (fn_len >= sizeof(func_name))
                    fn_len = sizeof(func_name) - 1;
                memcpy(func_name, f->source + sb, fn_len);
                func_name[fn_len] = '\0';
                have_name = true;
            } else if (name_len == 4 && strncmp(cap_name, "call", 4) == 0) {
                call_node = node;
                have_call = true;
            }
        }

        if (!have_name || !have_call)
            continue;

        const mc_func_rule *rule = mc_rules_lookup(func_name);
        if (!rule)
            continue;

        mc_call_status st = classify_call_usage(call_node,
                                                f->source,
                                                f->source_len);

        /* Stored-but-not-checked: if the return value was stored in a
         * variable but the variable is never used in a condition, return,
         * or acknowledged via (void)var, reclassify as STORED_UNCHECKED. */
        if (st == MC_CALL_STATUS_STORED &&
            (rule->flags & MC_FUNC_RULE_RETVAL_MUST_CHECK)) {
            char varname[64];
            if (extract_store_varname(f, call_node, varname, sizeof varname)) {
                TSNode block_stmt = find_enclosing_block_stmt(call_node);
                if (!ts_node_is_null(block_stmt) &&
                    !is_var_checked_in_block(f, block_stmt, varname)) {
                    st = MC_CALL_STATUS_STORED_UNCHECKED;
                }
            }
        }

        TSPoint pt = ts_node_start_point(call_node);
        mc_call_info info;
        info.func_name = func_name;
        info.status = st;

        /* Translate line via pp line map if available. */
        if (f->line_map && pt.row < (uint32_t)f->line_map_count)
            info.line = f->line_map[pt.row];
        else
            info.line = pt.row + 1;

        info.col  = pt.column + 1;
        info.flags = rule->flags;
        info.call_node = call_node;

        if (sink)
            sink(f, &info, userdata);
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
}

/* --- text warnings (for humans + tests) ----------------------------- */

static void text_warning_sink(const mc_ts_file *file,
                              const mc_call_info *info,
                              void *userdata) {
    (void)userdata;

    char msg[256];

    if (info->flags & MC_FUNC_RULE_DANGEROUS) {
        snprintf(msg, sizeof(msg),
                 "warning: use of dangerous function %s()",
                 info->func_name);
        mc_report_fact_kind(file->path,
                            info->line,
                            info->col,
                            info->func_name,
                            "dangerous_function",
                            msg,
                            0);
        return;
    }

    if (info->flags & MC_FUNC_RULE_FORMAT_STRING) {
        if (is_nonliteral_format_call(file, info)) {
            snprintf(msg, sizeof(msg),
                     "warning: non-literal format string in %s()",
                     info->func_name);
            mc_report_fact_kind(file->path,
                                info->line,
                                info->col,
                                info->func_name,
                                "format_string",
                                msg,
                                0);
            return;
        }
    }

    if (info->flags & MC_FUNC_RULE_RETVAL_MUST_CHECK) {
        if (info->status == MC_CALL_STATUS_UNCHECKED ||
            info->status == MC_CALL_STATUS_IGNORED_EXPLICIT) {
            snprintf(msg, sizeof(msg),
                     "unchecked return value of %s()",
                     info->func_name);

            mc_report_issue(file->path,
                            info->line,
                            info->col,
                            info->func_name,
                            msg,
                            0);
        } else if (info->status == MC_CALL_STATUS_STORED_UNCHECKED) {
            snprintf(msg, sizeof(msg),
                     "stored but unchecked return value of %s()",
                     info->func_name);

            mc_report_fact_kind(file->path,
                                info->line,
                                info->col,
                                info->func_name,
                                "return_value_check",
                                msg,
                                0);
        }
    }
}

/* --- generic TS helpers for extra checks ---------------------------- */

static void mc_ts_node_text_copy(const mc_ts_file *f,
                                 TSNode node,
                                 char *buf,
                                 size_t bufsize)
{
    if (!buf || bufsize == 0)
        return;

    uint32_t sb = ts_node_start_byte(node);
    uint32_t eb = ts_node_end_byte(node);
    if (eb > f->source_len)
        eb = (uint32_t)f->source_len;
    if (sb >= eb) {
        buf[0] = '\0';
        return;
    }

    size_t len = eb - sb;
    if (len >= bufsize)
        len = bufsize - 1;

    memcpy(buf, f->source + sb, len);
    buf[len] = '\0';
}

static uint32_t mc_ts_node_start_line(const mc_ts_file *f, TSNode node)
{
    TSPoint p = ts_node_start_point(node);
    if (f->line_map && p.row < (uint32_t)f->line_map_count)
        return f->line_map[p.row];
    return p.row + 1;
}

static uint32_t mc_ts_node_start_col(TSNode node)
{
    TSPoint p = ts_node_start_point(node);
    return p.column + 1;
}

static void mc_ts_identifier_name(const mc_ts_file *f,
                                  TSNode node,
                                  char *buf,
                                  size_t bufsize)
{
    if (bufsize == 0)
        return;
    if (strcmp(ts_node_type(node), "identifier") != 0) {
        buf[0] = '\0';
        return;
    }
    mc_ts_node_text_copy(f, node, buf, bufsize);
}

static TSNode mc_ts_find_identifier(TSNode node)
{
    if (ts_node_is_null(node))
        return mc_ts_null_node();

    if (strcmp(ts_node_type(node), "identifier") == 0)
        return node;

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(node, i);
        TSNode r = mc_ts_find_identifier(child);
        if (!ts_node_is_null(r))
            return r;
    }
    return mc_ts_null_node();
}

/* --- insecure_env_usage --------------------------------------------- */

static bool mc_is_env_function_name(const char *name)
{
    return strcmp(name, "getenv")        == 0 ||
           strcmp(name, "secure_getenv") == 0 ||
           strcmp(name, "putenv")        == 0 ||
           strcmp(name, "setenv")        == 0 ||
           strcmp(name, "unsetenv")      == 0 ||
           strcmp(name, "clearenv")      == 0;
}

static void mc_ts_string_literal_contents(const mc_ts_file *f,
                                          TSNode node,
                                          char *buf,
                                          size_t bufsize)
{
    if (bufsize == 0)
        return;

    if (strcmp(ts_node_type(node), "string_literal") != 0) {
        buf[0] = '\0';
        return;
    }

    uint32_t sb = ts_node_start_byte(node);
    uint32_t eb = ts_node_end_byte(node);
    if (eb > f->source_len)
        eb = (uint32_t)f->source_len;
    if (sb >= eb) {
        buf[0] = '\0';
        return;
    }

    const char *p = f->source + sb;
    const char *q = f->source + eb;

    if (*p == '"' && q > p + 1)
        p++;
    if (q > p && q[-1] == '"')
        q--;

    size_t len = (size_t)(q - p);
    if (len >= bufsize)
        len = bufsize - 1;

    memcpy(buf, p, len);
    buf[len] = '\0';
}

static void mc_extra_check_env_call(const mc_ts_file *f,
                                    TSNode call)
{
    TSNode func = ts_node_child_by_field_name(call, "function",
                                              (uint32_t)strlen("function"));
    if (ts_node_is_null(func))
        return;

    char name[32];
    mc_ts_node_text_copy(f, func, name, sizeof name);
    if (!mc_is_env_function_name(name))
        return;

    TSNode args = ts_node_child_by_field_name(call, "arguments",
                                              (uint32_t)strlen("arguments"));
    TSNode arg0 = ts_node_is_null(args) ? mc_ts_null_node()
                                        : ts_node_named_child(args, 0);

    char symbol[64];
    char msg[256];

    if (!ts_node_is_null(arg0) &&
        strcmp(ts_node_type(arg0), "string_literal") == 0) {
        mc_ts_string_literal_contents(f, arg0, symbol, sizeof symbol);
        if (symbol[0] == '\0') {
            strncpy(symbol, name, sizeof symbol - 1);
            symbol[sizeof symbol - 1] = '\0';
        }
        snprintf(msg, sizeof msg,
                 "insecure_env_usage: environment variable '%s' accessed via %s()",
                 symbol, name);
    } else {
        strncpy(symbol, name, sizeof symbol - 1);
        symbol[sizeof symbol - 1] = '\0';
        snprintf(msg, sizeof msg,
                 "insecure_env_usage: environment accessed via %s()", name);
    }

    uint32_t line = mc_ts_node_start_line(f, call);
    uint32_t col  = mc_ts_node_start_col(call);

    mc_report_fact_kind(f->path,
                        line,
                        col,
                        symbol,
                        "insecure_env_usage",
                        msg,
                        0);
}

/* --- malloc_size_mismatch ------------------------------------------- */

static bool mc_is_malloc_like_name(const char *name)
{
    return strcmp(name, "malloc") == 0 ||
           strcmp(name, "calloc") == 0;
}

/* String-based pattern: look for "sizeof(p)" or "sizeof (p)" in the initializer
   text, where p is the pointer variable we are declaring. */
static void mc_extra_check_declaration_malloc_mismatch(const mc_ts_file *f,
                                                       TSNode decl)
{
    uint32_t count = ts_node_named_child_count(decl);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(decl, i);
        if (strcmp(ts_node_type(child), "init_declarator") != 0)
            continue;

        TSNode declarator = ts_node_child_by_field_name(child,
                                                        "declarator",
                                                        (uint32_t)strlen("declarator"));
        TSNode value = ts_node_child_by_field_name(child,
                                                   "value",
                                                   (uint32_t)strlen("value"));
        if (ts_node_is_null(declarator) || ts_node_is_null(value))
            continue;

        if (strcmp(ts_node_type(value), "call_expression") != 0)
            continue;

        TSNode func = ts_node_child_by_field_name(value, "function",
                                                  (uint32_t)strlen("function"));
        if (ts_node_is_null(func))
            continue;

        char func_name[32];
        mc_ts_node_text_copy(f, func, func_name, sizeof func_name);
        if (!mc_is_malloc_like_name(func_name))
            continue;

        TSNode ident = mc_ts_find_identifier(declarator);
        if (ts_node_is_null(ident))
            continue;

        char var_name[64];
        mc_ts_identifier_name(f, ident, var_name, sizeof var_name);
        if (var_name[0] == '\0')
            continue;

        char init_text[256];
        mc_ts_node_text_copy(f, value, init_text, sizeof init_text);

        char pat1[96];
        char pat2[96];
        snprintf(pat1, sizeof pat1, "sizeof(%s)", var_name);
        snprintf(pat2, sizeof pat2, "sizeof (%s)", var_name);

        if (strstr(init_text, pat1) == NULL && strstr(init_text, pat2) == NULL)
            continue;

        uint32_t line = mc_ts_node_start_line(f, value);
        uint32_t col  = mc_ts_node_start_col(value);
        char msg[256];
        snprintf(msg, sizeof msg,
                 "malloc_size_mismatch: allocation for '%s' uses sizeof(%s); did you mean sizeof(*%s) or sizeof(type)?",
                 var_name, var_name, var_name);

        mc_report_fact_kind(f->path,
                            line,
                            col,
                            var_name,
                            "malloc_size_mismatch",
                            msg,
                            0);
    }
}

/* --- double_close --------------------------------------------------- */

typedef struct {
    char var[64];
    char api[32];
    uint32_t first_line;
    uint32_t first_col;
    int used;
    int terminal;  /* 1 = close is on a path that exits (return/exit follows) */
} mc_closed_entry;

#define MC_MAX_CLOSED_VARS 128

static bool mc_is_close_like_name(const char *name)
{
    return strcmp(name, "close")  == 0 ||
           strcmp(name, "fclose") == 0 ||
           strcmp(name, "pclose") == 0 ||
           strcmp(name, "free")   == 0 ||
           strcmp(name, "munmap") == 0;
}

/*
 * Check whether a close() call is "terminal" — i.e., the containing block
 * immediately returns or exits after the close.  Pattern:
 *     close(fd); return ...;     // terminal
 *     close(fd); exit(1);        // terminal
 *     close(fd); _exit(1);       // terminal
 *     close(fd); [more code]     // NOT terminal
 *
 * Walk up from the call to find the expression_statement, then check if the
 * next sibling in the parent block is return/exit.
 */
static bool mc_is_exit_call(const mc_ts_file *f, TSNode stmt)
{
    /* Check if stmt is an expression_statement containing a call to exit/_exit. */
    if (strcmp(ts_node_type(stmt), "expression_statement") != 0)
        return false;

    uint32_t nc = ts_node_named_child_count(stmt);
    for (uint32_t j = 0; j < nc; j++) {
        TSNode expr = ts_node_named_child(stmt, j);
        if (strcmp(ts_node_type(expr), "call_expression") != 0)
            continue;

        TSNode fn = ts_node_child_by_field_name(
            expr, "function", (uint32_t)strlen("function"));
        if (ts_node_is_null(fn))
            continue;
        if (strcmp(ts_node_type(fn), "identifier") != 0)
            continue;

        char name[16];
        mc_ts_node_text_copy(f, fn, name, sizeof name);
        if (strcmp(name, "exit")  == 0 ||
            strcmp(name, "_exit") == 0 ||
            strcmp(name, "_Exit") == 0 ||
            strcmp(name, "abort") == 0)
            return true;
    }
    return false;
}

static bool mc_is_terminal_close(const mc_ts_file *f, TSNode call)
{
    /* Walk up to the expression_statement containing this call. */
    TSNode stmt = call;
    while (!ts_node_is_null(stmt)) {
        if (strcmp(ts_node_type(stmt), "expression_statement") == 0)
            break;
        stmt = ts_node_parent(stmt);
    }
    if (ts_node_is_null(stmt))
        return false;

    TSNode parent = ts_node_parent(stmt);
    if (ts_node_is_null(parent))
        return false;

    /*
     * Check if any remaining sibling in this block is a return or exit.
     * This handles patterns like:
     *     free(buf); close(fd); return -1;
     * where free's immediate next sibling is close(), but the block
     * ends with return — so all statements in this block are terminal.
     */
    uint32_t n = ts_node_named_child_count(parent);
    bool found_self = false;
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(parent, i);
        if (!found_self) {
            if (ts_node_start_byte(child) == ts_node_start_byte(stmt) &&
                ts_node_end_byte(child)   == ts_node_end_byte(stmt))
                found_self = true;
            continue;
        }
        /* Check all subsequent siblings for return/exit. */
        const char *ntype = ts_node_type(child);
        if (strcmp(ntype, "return_statement") == 0)
            return true;
        if (mc_is_exit_call(f, child))
            return true;
    }
    return false;
}

static void mc_extra_check_double_close_call(const mc_ts_file *f,
                                             TSNode call,
                                             mc_closed_entry *table,
                                             size_t table_len)
{
    TSNode func = ts_node_child_by_field_name(call, "function",
                                              (uint32_t)strlen("function"));
    if (ts_node_is_null(func))
        return;

    char name[32];
    mc_ts_node_text_copy(f, func, name, sizeof name);
    if (!mc_is_close_like_name(name))
        return;

    TSNode args = ts_node_child_by_field_name(call, "arguments",
                                              (uint32_t)strlen("arguments"));
    if (ts_node_is_null(args))
        return;

    uint32_t named_count = ts_node_named_child_count(args);
    if (named_count == 0)
        return;

    TSNode arg0 = ts_node_named_child(args, 0);

    char var[64] = {0};
    const char *atype = ts_node_type(arg0);

    if (strcmp(atype, "identifier") == 0) {
        mc_ts_identifier_name(f, arg0, var, sizeof var);
    } else if (strcmp(atype, "unary_expression") == 0) {
        TSNode inner = ts_node_named_child(arg0, 0);
        if (!ts_node_is_null(inner) &&
            strcmp(ts_node_type(inner), "identifier") == 0) {
            mc_ts_identifier_name(f, inner, var, sizeof var);
        }
    }

    if (var[0] == '\0')
        return;

    /* Is this close on a terminal path (followed by return/exit)? */
    int this_is_terminal = mc_is_terminal_close(f, call) ? 1 : 0;

    for (size_t i = 0; i < table_len; i++) {
        if (!table[i].used)
            continue;
        if (strcmp(table[i].var, var) == 0) {
            /*
             * Skip if the first close was terminal — it's on a path
             * that exits, so a later close on a different path is fine.
             */
            if (table[i].terminal)
                continue;

            uint32_t line = mc_ts_node_start_line(f, call);
            uint32_t col  = mc_ts_node_start_col(call);
            char msg[256];
            snprintf(msg, sizeof msg,
                     "double_close: second call to %s(%s); first at line %u",
                     name, var, table[i].first_line);

            mc_report_fact_kind(f->path,
                                line,
                                col,
                                var,
                                "double_close",
                                msg,
                                0);
            return;
        }
    }

    for (size_t i = 0; i < table_len; i++) {
        if (!table[i].used) {
            table[i].used       = 1;
            table[i].terminal   = this_is_terminal;
            table[i].first_line = mc_ts_node_start_line(f, call);
            table[i].first_col  = mc_ts_node_start_col(call);
            strncpy(table[i].var, var, sizeof table[i].var - 1);
            table[i].var[sizeof table[i].var - 1] = '\0';
            strncpy(table[i].api, name, sizeof table[i].api - 1);
            table[i].api[sizeof table[i].api - 1] = '\0';
            return;
        }
    }
}

/* --- per-function DFS for extra checks ------------------------------ */

static void mc_extra_check_function_body(const mc_ts_file *f,
                                         TSNode func_def)
{
    TSNode body = ts_node_child_by_field_name(func_def, "body",
                                              (uint32_t)strlen("body"));
    if (ts_node_is_null(body))
        return;

    mc_closed_entry closed[MC_MAX_CLOSED_VARS];
    memset(closed, 0, sizeof closed);

    TSNode stack[256];
    unsigned sp = 0;
    stack[sp++] = body;

    while (sp > 0) {
        TSNode node = stack[--sp];
        const char *type = ts_node_type(node);

        if (strcmp(type, "call_expression") == 0) {
            mc_extra_check_double_close_call(f, node,
                                             closed, MC_MAX_CLOSED_VARS);
            mc_extra_check_env_call(f, node);
        } else if (strcmp(type, "declaration") == 0) {
            mc_extra_check_declaration_malloc_mismatch(f, node);
        }

        uint32_t child_count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            if (sp >= sizeof stack / sizeof stack[0])
                break;
            stack[sp++] = ts_node_named_child(node, child_count - 1 - i);
        }
    }
}

static void mc_run_extra_checks(const mc_ts_file *f)
{
    TSNode root = f->root;
    uint32_t n = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(node), "function_definition") == 0) {
            mc_extra_check_function_body(f, node);
        }
    }
}

/* --- public entrypoints -------------------------------------------- */

bool mc_ts_report_unchecked_calls(const char *path) {
    mc_ts_file f;
    if (!mc_ts_file_init(&f, path))
        return false;

    analyze_calls(&f, text_warning_sink, NULL);
    mc_run_extra_checks(&f);

    mc_ts_file_destroy(&f);
    return true;
}

/* --- JSON report (for training / tooling) --------------------------- */

static void
mc_json_escape(FILE *out, const char *s)
{
    if (!s) { fputs("null", out); return; }
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '\\': fputs("\\\\", out); break;
        case '"':  fputs("\\\"", out); break;
        case '\n': fputs("\\n",  out); break;
        case '\r': fputs("\\r",  out); break;
        case '\t': fputs("\\t",  out); break;
        default:
            if (*p < 0x20)
                fprintf(out, "\\u%04x", *p);
            else
                fputc((int)*p, out);
            break;
        }
    }
    fputc('"', out);
}

typedef struct {
    bool first_call;
} json_state;

/* Return true if this call info represents an actual diagnostic (i.e.,
 * text_warning_sink would emit a warning for it). */
static bool is_diagnostic(const mc_ts_file *file, const mc_call_info *info)
{
    if (info->flags & MC_FUNC_RULE_DANGEROUS)
        return true;
    if ((info->flags & MC_FUNC_RULE_FORMAT_STRING) &&
        is_nonliteral_format_call(file, info))
        return true;
    if (info->flags & MC_FUNC_RULE_RETVAL_MUST_CHECK) {
        if (info->status == MC_CALL_STATUS_UNCHECKED ||
            info->status == MC_CALL_STATUS_IGNORED_EXPLICIT ||
            info->status == MC_CALL_STATUS_STORED_UNCHECKED)
            return true;
    }
    return false;
}

static void json_sink(const mc_ts_file *file,
                      const mc_call_info *info,
                      void *userdata) {
    json_state *st = (json_state *)userdata;

    const char *category = mc_rules_category(info->flags);

    /* Only suppress entries that are actual diagnostics, matching
     * text_warning_sink semantics.  Checked/propagated calls pass
     * through even if their file+category is suppressed. */
    if (is_diagnostic(file, info)) {
        if (mc_suppress_check(file->path, category) ||
            mc_inline_suppress_check(info->line))
            return;
        mc_report_count_issue();
    }

    if (!st->first_call) {
        printf(",\n");
    } else {
        st->first_call = false;
    }

    printf("      {\"function\":");
    mc_json_escape(stdout, info->func_name);
    printf(",\"status\":\"%s\","
           "\"category\":\"%s\","
           "\"line\":%u,"
           "\"column\":%u}",
           mc_call_status_str(info->status),
           category,
           info->line,
           info->col);
}

bool mc_ts_report_file_json(const char *path) {
    mc_ts_file f;
    if (!mc_ts_file_init(&f, path))
        return false;

    json_state st = { .first_call = true };

    printf("  {\"path\":");
    mc_json_escape(stdout, path);
    printf(",\"calls\":[\n");
    analyze_calls(&f, json_sink, &st);
    printf("\n  ]}");

    mc_ts_file_destroy(&f);
    return true;
}

/* --- Extended versions using preprocessed source --------------------- */

bool mc_ts_report_unchecked_calls_ex(const char *path,
                                     const char *pp_source,
                                     size_t pp_source_len,
                                     const unsigned *line_map,
                                     size_t line_map_count)
{
    if (!pp_source || pp_source_len == 0)
        return mc_ts_report_unchecked_calls(path);

    mc_ts_file f;
    char *src_copy = malloc(pp_source_len + 1);
    if (!src_copy)
        return false;
    memcpy(src_copy, pp_source, pp_source_len);
    src_copy[pp_source_len] = '\0';

    if (!mc_ts_file_init_source(&f, path, src_copy, pp_source_len,
                                line_map, line_map_count)) {
        free(src_copy);
        return false;
    }

    analyze_calls(&f, text_warning_sink, NULL);
    mc_run_extra_checks(&f);

    mc_ts_file_destroy(&f);
    return true;
}

bool mc_ts_report_file_json_ex(const char *path,
                               const char *pp_source,
                               size_t pp_source_len,
                               const unsigned *line_map,
                               size_t line_map_count)
{
    if (!pp_source || pp_source_len == 0)
        return mc_ts_report_file_json(path);

    mc_ts_file f;
    char *src_copy = malloc(pp_source_len + 1);
    if (!src_copy)
        return false;
    memcpy(src_copy, pp_source, pp_source_len);
    src_copy[pp_source_len] = '\0';

    if (!mc_ts_file_init_source(&f, path, src_copy, pp_source_len,
                                line_map, line_map_count)) {
        free(src_copy);
        return false;
    }

    json_state st = { .first_call = true };

    printf("  {\"path\":");
    mc_json_escape(stdout, path);
    printf(",\"calls\":[\n");
    analyze_calls(&f, json_sink, &st);
    printf("\n  ]}");

    mc_ts_file_destroy(&f);
    return true;
}
