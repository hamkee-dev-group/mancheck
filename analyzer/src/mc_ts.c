#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mc_ts.h"
#include "mc_rules.h"
#include "mc_flow.h"
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

static const char *const *g_line_files;
static size_t g_line_files_count;
void mc_ts_set_line_files(const char *const *files, size_t count)
{
    g_line_files = files;
    g_line_files_count = files ? count : 0;
}

const char *mc_ts_row_file(const mc_ts_file *f, uint32_t row)
{
    if (f->line_files && row < f->line_map_count && f->line_files[row])
        return f->line_files[row];
    return f->path;
}

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
    if (line_map && g_line_files && g_line_files_count == line_map_count)
        f->line_files = g_line_files;

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
    if (ts_node_has_error(f->root))
        fprintf(stderr, "analyzer: C parse errors in %s; analysis is incomplete\n", path);
    f->flow = mc_flow_analyze(f);
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
    if (ts_node_has_error(f->root))
        fprintf(stderr, "analyzer: C parse errors in %s; analysis is incomplete\n", path);
    f->flow = mc_flow_analyze(f);
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
    mc_flow_destroy(f->flow);
    f->flow = NULL;
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

static bool g_input_complete = true;
void mc_ts_set_input_complete(bool complete) { g_input_complete = complete; }

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
    const char *file;
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

    const struct specdb_contract *contract = mc_rules_contract(info->func_name);
    if (!contract || contract->format_index < 0) return false;
    uint32_t fmt_index = (uint32_t)contract->format_index;

    if (fmt_index >= named_count)
        return false;

    TSNode fmt_node = ts_node_named_child(args, fmt_index);
    if (ts_node_is_null(fmt_node))
        return false;

    while (strcmp(ts_node_type(fmt_node), "parenthesized_expression") == 0)
        fmt_node = ts_node_named_child(fmt_node, 0);
    const char *t = ts_node_type(fmt_node);
    if (strcmp(t, "string_literal") == 0 || strcmp(t, "concatenated_string") == 0) {
        return false;
    }

    return true;
}

/* forward-declare (defined below, after finding adapters) */
static void mc_ts_node_text_copy(const mc_ts_file *f,
                                 TSNode node,
                                 char *buf,
                                 size_t bufsize);

/* --- generic call walker -------------------------------------------- */

static void analyze_calls(const mc_ts_file *f, mc_call_sink sink, void *userdata) {
    const TSLanguage *lang = tree_sitter_c();

    const char *query_src =
        "(call_expression "
        "  function: (_) @func "
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
                while (strcmp(ts_node_type(node), "parenthesized_expression") == 0)
                    node = ts_node_named_child(node, 0);
                if (strcmp(ts_node_type(node), "identifier") != 0) continue;
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

        st = mc_flow_status(f->flow, call_node, st);

        TSPoint pt = ts_node_start_point(call_node);
        mc_call_info info;
        info.func_name = func_name;
        info.status = st;

        /* Translate line via pp line map if available. */
        info.file = mc_ts_row_file(f, pt.row);
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

/* --- finding adapters (call → finding, finding → text) -------------- */

typedef struct {
    mc_finding_sink sink;
    void *userdata;
} finding_adapter_ctx;

static void call_to_finding_sink(const mc_ts_file *file,
                                 const mc_call_info *info,
                                 void *userdata)
{
    finding_adapter_ctx *ctx = userdata;
    mc_finding f;
    memset(&f, 0, sizeof f);

    f.file = info->file;
    f.line = info->line;
    f.col  = info->col;
    snprintf(f.symbol, sizeof f.symbol, "%s", info->func_name);

    if (info->flags & MC_FUNC_RULE_DANGEROUS) {
        snprintf(f.message, sizeof f.message,
                 "warning: use of dangerous function %s()",
                 info->func_name);
        f.category = "dangerous_function";
        f.db_kind  = "dangerous_function";
    } else if ((info->flags & MC_FUNC_RULE_FORMAT_STRING) &&
               is_nonliteral_format_call(file, info)) {
        snprintf(f.message, sizeof f.message,
                 "warning: non-literal format string in %s()",
                 info->func_name);
        f.category = "format_string";
        f.db_kind  = "format_string";
    } else if (info->flags & MC_FUNC_RULE_RETVAL_MUST_CHECK) {
        if (info->status == MC_CALL_STATUS_UNCHECKED ||
            info->status == MC_CALL_STATUS_IGNORED_EXPLICIT) {
            snprintf(f.message, sizeof f.message,
                     "unchecked return value of %s()",
                     info->func_name);
            f.category = "return_value_check";
            f.db_kind  = "retval_unchecked";
            f.use_issue_format = 1;
        } else if (info->status == MC_CALL_STATUS_STORED_UNCHECKED) {
            snprintf(f.message, sizeof f.message,
                     "stored but unchecked return value of %s()",
                     info->func_name);
            f.category = "return_value_check";
            f.db_kind  = "return_value_check";
        } else {
            return;
        }
    } else {
        return;
    }

    ctx->sink(&f, ctx->userdata);
}

static void text_finding_sink(const mc_finding *f, void *userdata)
{
    (void)userdata;
    if (f->use_issue_format) {
        mc_report_issue(f->file, f->line, f->col,
                        f->symbol, f->message, 0);
    } else {
        mc_report_fact_kind(f->file, f->line, f->col,
                            f->symbol, f->db_kind, f->message, 0);
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
    if (ts_node_is_null(node) || strcmp(ts_node_type(node), "identifier") != 0) {
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
                                    TSNode call,
                                    mc_finding_sink sink,
                                    void *userdata)
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

    mc_finding finding;
    memset(&finding, 0, sizeof finding);
    finding.file     = mc_ts_row_file(f, ts_node_start_point(call).row);
    finding.line     = mc_ts_node_start_line(f, call);
    finding.col      = mc_ts_node_start_col(call);
    snprintf(finding.symbol, sizeof finding.symbol, "%s", symbol);
    finding.category = "insecure_env_usage";
    finding.db_kind  = "insecure_env_usage";
    snprintf(finding.message, sizeof finding.message, "%s", msg);
    sink(&finding, userdata);
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
                                                       TSNode decl,
                                                       mc_finding_sink sink,
                                                       void *userdata)
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

        bool pointer = false, mismatch = false;
        TSTreeCursor cursor = ts_tree_cursor_new(declarator);
        for (;;) {
            if (!strcmp(ts_node_type(ts_tree_cursor_current_node(&cursor)), "pointer_declarator")) pointer = true;
            if (ts_tree_cursor_goto_first_child(&cursor)) continue;
            while (!ts_tree_cursor_goto_next_sibling(&cursor))
                if (!ts_tree_cursor_goto_parent(&cursor)) goto declarator_done;
        }
declarator_done:
        ts_tree_cursor_delete(&cursor);
        if (!pointer) continue;
        cursor = ts_tree_cursor_new(value);
        for (;;) {
            TSNode node = ts_tree_cursor_current_node(&cursor);
            if (!strcmp(ts_node_type(node), "sizeof_expression")) {
                TSNode arg = ts_node_child_by_field_name(node, "value", 5);
                while (!ts_node_is_null(arg) && !strcmp(ts_node_type(arg), "parenthesized_expression"))
                    arg = ts_node_named_child(arg, 0);
                char name[64]; mc_ts_identifier_name(f, arg, name, sizeof name);
                if (!strcmp(name, var_name)) mismatch = true;
            }
            if (ts_tree_cursor_goto_first_child(&cursor)) continue;
            while (!ts_tree_cursor_goto_next_sibling(&cursor))
                if (!ts_tree_cursor_goto_parent(&cursor)) goto sizeof_done;
        }
sizeof_done:
        ts_tree_cursor_delete(&cursor);
        if (!mismatch) continue;

        mc_finding finding;
        memset(&finding, 0, sizeof finding);
        finding.file     = mc_ts_row_file(f, ts_node_start_point(value).row);
        finding.line     = mc_ts_node_start_line(f, value);
        finding.col      = mc_ts_node_start_col(value);
        snprintf(finding.symbol, sizeof finding.symbol, "%s", var_name);
        finding.category = "malloc_size_mismatch";
        finding.db_kind  = "malloc_size_mismatch";
        snprintf(finding.message, sizeof finding.message,
                 "malloc_size_mismatch: allocation for '%.48s' uses sizeof(%.48s); did you mean sizeof(*%.48s) or sizeof(type)?",
                 var_name, var_name, var_name);
        sink(&finding, userdata);
    }
}

/* Independent syntax checks use a cursor: no fixed sibling limit. */
static void mc_extra_check_function_body(const mc_ts_file *f, TSNode func_def,
                                         mc_finding_sink sink, void *userdata)
{
    TSNode body = ts_node_child_by_field_name(func_def, "body", 4);
    TSTreeCursor cursor = ts_tree_cursor_new(body);
    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        const char *type = ts_node_type(node);
        if (!strcmp(type, "declaration"))
            mc_extra_check_declaration_malloc_mismatch(f, node, sink, userdata);
        /* Environment auditing is opt-in: the API itself is not a mistake. */
        if (getenv("MANCHECK_AUDIT_ENV") && !strcmp(type, "call_expression"))
            mc_extra_check_env_call(f, node, sink, userdata);
        if (ts_tree_cursor_goto_first_child(&cursor)) continue;
        while (!ts_tree_cursor_goto_next_sibling(&cursor))
            if (!ts_tree_cursor_goto_parent(&cursor)) goto done;
    }
done:
    ts_tree_cursor_delete(&cursor);
}

static void mc_run_extra_checks(const mc_ts_file *f,
                                mc_finding_sink sink,
                                void *userdata)
{
    mc_flow_findings(f->flow, sink, userdata);
    TSNode root = f->root;
    uint32_t n = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(node), "function_definition") == 0) {
            mc_extra_check_function_body(f, node, sink, userdata);
        }
    }
}

typedef struct {
    mc_finding *items;
    size_t count, cap;
    bool failed;
} finding_list;

static void gather_finding(const mc_finding *f, void *data)
{
    finding_list *list = data;
    if (list->failed) return;
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        mc_finding *items = realloc(list->items, cap * sizeof *items);
        if (!items) { list->failed = true; return; }
        list->items = items; list->cap = cap;
    }
    list->items[list->count++] = *f;
}

static int finding_order(const void *aa, const void *bb)
{
    const mc_finding *a = aa, *b = bb;
    int byfile = strcmp(a->file, b->file);
    if (byfile) return byfile;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    if (a->col != b->col) return a->col < b->col ? -1 : 1;
    return strcmp(a->category, b->category);
}

static bool mc_collect_all_findings(const mc_ts_file *f,
                                    mc_finding_sink sink, void *userdata)
{
    finding_list list = {0};
    finding_adapter_ctx ctx = { .sink = gather_finding, .userdata = &list };
    analyze_calls(f, call_to_finding_sink, &ctx);
    mc_run_extra_checks(f, gather_finding, &list);
    if (list.failed) { free(list.items); return false; }
    if (list.count > 1) qsort(list.items, list.count, sizeof *list.items, finding_order);
    for (size_t i = 0; i < list.count; i++) sink(&list.items[i], userdata);
    free(list.items);
    return true;
}

/* --- public entrypoints -------------------------------------------- */

bool mc_ts_report_unchecked_calls(const char *path) {
    mc_ts_file f;
    if (!mc_ts_file_init(&f, path))
        return false;

    bool collected = mc_collect_all_findings(&f, text_finding_sink, NULL);

    bool complete = collected && g_input_complete && mc_flow_ok(f.flow) && !ts_node_has_error(f.root);
    mc_ts_file_destroy(&f);
    return complete;
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
    const finding_list *accepted;
} json_state;

typedef finding_list sarif_state;

static void record_finding(const mc_finding *f)
{
    if (f->use_issue_format)
        mc_report_issue(f->file, f->line, f->col, f->symbol, f->message, 1);
    else
        mc_report_fact_kind(f->file, f->line, f->col, f->symbol, f->db_kind, f->message, 1);
}

static void json_sink(const mc_ts_file *file, const mc_call_info *info, void *userdata)
{
    json_state *st = userdata;
    finding_list diagnosis = {0};
    finding_adapter_ctx adapter = { .sink = gather_finding, .userdata = &diagnosis };
    call_to_finding_sink(file, info, &adapter);
    const char *category = diagnosis.count ? diagnosis.items[0].category : mc_rules_category(info->flags);
    if (diagnosis.count) {
        bool accepted = false;
        for (size_t i = 0; i < st->accepted->count; i++) {
            const mc_finding *d = &st->accepted->items[i];
            if (d->line == info->line && d->col == info->col && !strcmp(d->category, category) &&
                !strcmp(d->file, info->file)) accepted = true;
        }
        if (!accepted) { free(diagnosis.items); return; }
    }
    free(diagnosis.items);
    if (!st->first_call) printf(",\n");
    st->first_call = false;
    printf("      {\"function\":"); mc_json_escape(stdout, info->func_name);
    printf(",\"path\":"); mc_json_escape(stdout, info->file);
    printf(",\"status\":\"%s\",\"category\":\"%s\",\"line\":%u,\"column\":%u}",
           mc_call_status_str(info->status), category, info->line, info->col);
}

static bool mc_ts_report_file_json_impl(mc_ts_file *file)
{
    finding_list all = {0}, accepted = {0};
    bool collected = mc_collect_all_findings(file, gather_finding, &all);
    if (collected && !all.failed) {
        for (size_t i = 0; i < all.count; i++) {
            unsigned long long before = mc_report_get_run_issue_count();
            record_finding(&all.items[i]);
            if (before != mc_report_get_run_issue_count()) gather_finding(&all.items[i], &accepted);
        }
    }
    bool complete = collected && !all.failed && !accepted.failed && g_input_complete && mc_flow_ok(file->flow) && !ts_node_has_error(file->root);
    json_state state = { .first_call = true, .accepted = &accepted };
    printf("  {\"path\":"); mc_json_escape(stdout, file->path);
    printf(",\"calls\":[\n"); analyze_calls(file, json_sink, &state);
    printf("\n  ],\"diagnostics\":[\n");
    for (size_t i = 0; i < accepted.count; i++) {
        const mc_finding *f = &accepted.items[i];
        if (i) printf(",\n");
        printf("      {\"category\":"); mc_json_escape(stdout, f->category);
        printf(",\"symbol\":"); mc_json_escape(stdout, f->symbol);
        printf(",\"message\":"); mc_json_escape(stdout, f->message);
        printf(",\"path\":"); mc_json_escape(stdout, f->file);
        printf(",\"line\":%u,\"column\":%u}", f->line, f->col);
    }
    printf("\n  ],\"issue_count\":%llu,\"complete\":%s}", mc_report_get_run_issue_count(), complete ? "true" : "false");
    free(all.items); free(accepted.items);
    return complete;
}

static bool
mc_sarif_force_collect_failure(size_t next_index)
{
    const char *env = getenv("MANCHECK_SARIF_FAIL_AT");
    char *end = NULL;
    unsigned long target;

    if (!env || env[0] == '\0')
        return false;

    target = strtoul(env, &end, 10);
    if (end == env || *end != '\0' || target == 0)
        return false;

    return next_index == (size_t)target;
}

static void
sarif_collect_sink(const mc_finding *f, void *userdata)
{
    sarif_state *st = userdata;

    if (st->failed)
        return;

    if (mc_sarif_force_collect_failure(st->count + 1)) {
        st->failed = true;
        return;
    }

    if (st->count == st->cap) {
        size_t new_cap = st->cap ? st->cap * 2 : 8;
        mc_finding *tmp = realloc(st->items, new_cap * sizeof(*tmp));
        if (!tmp) {
            st->failed = true;
            return;
        }
        st->items = tmp;
        st->cap = new_cap;
    }

    st->items[st->count++] = *f;
}

static void
mc_sarif_emit_result(const mc_finding *f, int *first_result)
{
    if (!*first_result)
        printf(",\n");
    else
        *first_result = 0;

    printf("  {\"ruleId\":");
    mc_json_escape(stdout, f->category);
    printf(",\"level\":\"warning\",\"message\":{\"text\":");
    mc_json_escape(stdout, f->message);
    printf("},\"locations\":[{\"physicalLocation\":{\"artifactLocation\":{\"uri\":");
    mc_json_escape(stdout, f->file);
    printf("},\"region\":{\"startLine\":%u,\"startColumn\":%u}}}]}",
           f->line,
           f->col);
}

static bool
mc_ts_report_file_sarif_impl(mc_ts_file *f, int *first_result)
{
    sarif_state st;
    memset(&st, 0, sizeof st);

    bool collected = mc_collect_all_findings(f, sarif_collect_sink, &st);
    if (!collected || st.failed) {
        free(st.items);
        return false;
    }

    for (size_t i = 0; i < st.count; i++) {
        unsigned long long before = mc_report_get_run_issue_count();
        const mc_finding *finding = &st.items[i];

        if (finding->use_issue_format) {
            mc_report_issue(finding->file, finding->line, finding->col,
                            finding->symbol, finding->message, 1);
        } else {
            mc_report_fact_kind(finding->file, finding->line, finding->col,
                                finding->symbol, finding->db_kind,
                                finding->message, 1);
        }

        if (mc_report_get_run_issue_count() == before)
            continue;

        mc_sarif_emit_result(finding, first_result);
    }

    free(st.items);
    return true;
}

bool mc_ts_report_file_json(const char *path) {
    mc_ts_file f;
    if (!mc_ts_file_init(&f, path))
        return false;

    bool complete = mc_ts_report_file_json_impl(&f);
    mc_ts_file_destroy(&f);
    return complete;
}

static bool
mc_ts_report_file_sarif(const char *path, int *first_result)
{
    mc_ts_file f;
    if (!mc_ts_file_init(&f, path))
        return false;

    bool ok = mc_ts_report_file_sarif_impl(&f, first_result);
    ok = ok && g_input_complete && mc_flow_ok(f.flow) && !ts_node_has_error(f.root);
    mc_ts_file_destroy(&f);
    return ok;
}

/* --- Extended versions using preprocessed source --------------------- */

bool mc_ts_report_unchecked_calls_ex(const char *path,
                                     const char *pp_source,
                                     size_t pp_source_len,
                                     const unsigned *line_map,
                                     size_t line_map_count)
{
    if (!pp_source)
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

    bool collected = mc_collect_all_findings(&f, text_finding_sink, NULL);

    bool complete = collected && g_input_complete && mc_flow_ok(f.flow) && !ts_node_has_error(f.root);
    mc_ts_file_destroy(&f);
    return complete;
}

bool mc_ts_report_file_json_ex(const char *path,
                               const char *pp_source,
                               size_t pp_source_len,
                               const unsigned *line_map,
                               size_t line_map_count)
{
    if (!pp_source)
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

    bool complete = mc_ts_report_file_json_impl(&f);
    mc_ts_file_destroy(&f);
    return complete;
}

bool mc_ts_report_file_sarif_ex(const char *path,
                                const char *pp_source,
                                size_t pp_source_len,
                                const unsigned *line_map,
                                size_t line_map_count,
                                int *first_result)
{
    if (!pp_source)
        return mc_ts_report_file_sarif(path, first_result);

    mc_ts_file f;
    char *src_copy = malloc(pp_source_len + 1);
    bool ok;

    if (!src_copy)
        return false;
    memcpy(src_copy, pp_source, pp_source_len);
    src_copy[pp_source_len] = '\0';

    if (!mc_ts_file_init_source(&f, path, src_copy, pp_source_len,
                                line_map, line_map_count)) {
        free(src_copy);
        return false;
    }

    ok = mc_ts_report_file_sarif_impl(&f, first_result);
    ok = ok && g_input_complete && mc_flow_ok(f.flow) && !ts_node_has_error(f.root);
    mc_ts_file_destroy(&f);
    return ok;
}
