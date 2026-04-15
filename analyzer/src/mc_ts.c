#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mc_ts.h"
#include "mc_rules.h"
#include "report.h"

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
    case MC_CALL_STATUS_PROPAGATED:       return "propagated";
    case MC_CALL_STATUS_IGNORED_EXPLICIT: return "ignored_explicit";
    default:                              return "unknown";
    }
}

/* --- usage classification ------------------------------------------- */

/* Is this call effectively part of the condition of if/while/do/for? */
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
            return true;
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

    if (strcmp(etype, "assignment_expression") == 0 ||
        strcmp(etype, "init_declarator") == 0) {
        return MC_CALL_STATUS_STORED;
    }

    if (strcmp(ptype, "expression_statement") == 0) {
        if (is_void_cast(expr, source, source_len)) {
            return MC_CALL_STATUS_IGNORED_EXPLICIT;
        }
        return MC_CALL_STATUS_UNCHECKED;
    }

    if (is_in_condition_context(expr)) {
        return MC_CALL_STATUS_CHECKED_COND;
    }

    if (strcmp(ptype, "return_statement") == 0) {
        return MC_CALL_STATUS_PROPAGATED;
    }

    if (strcmp(ptype, "assignment_expression") == 0 ||
        strcmp(ptype, "init_declarator") == 0) {
        return MC_CALL_STATUS_STORED;
    }

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
        return false;
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

        TSPoint pt = ts_node_start_point(call_node);
        mc_call_info info;
        info.func_name = func_name;
        info.status = st;
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
        mc_report_warning(file->path,
                          info->line,
                          info->col,
                          info->func_name,
                          msg,
                          NULL,
                          0);
        return;
    }

    if (info->flags & MC_FUNC_RULE_FORMAT_STRING) {
        if (is_nonliteral_format_call(file, info)) {
            snprintf(msg, sizeof(msg),
                     "warning: non-literal format string in %s()",
                     info->func_name);
            mc_report_warning(file->path,
                              info->line,
                              info->col,
                              info->func_name,
                              msg,
                              NULL,
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
                            NULL,
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

static uint32_t mc_ts_node_start_line(TSNode node)
{
    TSPoint p = ts_node_start_point(node);
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

static TSNode mc_ts_first_argument(TSNode call)
{
    TSNode args = ts_node_child_by_field_name(call, "arguments",
                                              (uint32_t)strlen("arguments"));
    if (ts_node_is_null(args))
        return mc_ts_null_node();

    uint32_t named_count = ts_node_named_child_count(args);
    if (named_count == 0)
        return mc_ts_null_node();

    return ts_node_named_child(args, 0);
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

    uint32_t line = mc_ts_node_start_line(call);
    uint32_t col  = mc_ts_node_start_col(call);

    mc_report_warning(f->path,
                      line,
                      col,
                      symbol,
                      msg,
                      NULL,
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

        uint32_t line = mc_ts_node_start_line(value);
        uint32_t col  = mc_ts_node_start_col(value);
        char msg[256];
        snprintf(msg, sizeof msg,
                 "malloc_size_mismatch: allocation for '%s' uses sizeof(%s); did you mean sizeof(*%s) or sizeof(type)?",
                 var_name, var_name, var_name);

        mc_report_warning(f->path,
                          line,
                          col,
                          var_name,
                          msg,
                          NULL,
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

    for (size_t i = 0; i < table_len; i++) {
        if (!table[i].used)
            continue;
        if (strcmp(table[i].var, var) == 0) {
            uint32_t line = mc_ts_node_start_line(call);
            uint32_t col  = mc_ts_node_start_col(call);
            char msg[256];
            snprintf(msg, sizeof msg,
                     "double_close: second call to %s(%s); first at line %u",
                     name, var, table[i].first_line);

            mc_report_warning(f->path,
                              line,
                              col,
                              var,
                              msg,
                              NULL,
                              0);
            return;
        }
    }

    for (size_t i = 0; i < table_len; i++) {
        if (!table[i].used) {
            table[i].used       = 1;
            table[i].first_line = mc_ts_node_start_line(call);
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

typedef struct {
    bool first_call;
} json_state;

static void json_sink(const mc_ts_file *file,
                      const mc_call_info *info,
                      void *userdata) {
    (void)file;

    json_state *st = (json_state *)userdata;

    if (!st->first_call) {
        printf(",\n");
    } else {
        st->first_call = false;
    }

    const char *category = mc_rules_category(info->flags);

    printf("      {\"function\":\"%s\","
           "\"status\":\"%s\","
           "\"category\":\"%s\","
           "\"line\":%u,"
           "\"column\":%u}",
           info->func_name,
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

    printf("  {\"path\":\"%s\",\"calls\":[\n", path);
    analyze_calls(&f, json_sink, &st);
    printf("\n  ]}");

    mc_ts_file_destroy(&f);
    return true;
}
