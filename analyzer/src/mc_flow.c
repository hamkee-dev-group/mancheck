#include "mc_flow.h"
#include "mc_rules.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* Intraprocedural forward data flow. A definition is a call site or a parameter,
 * not a variable spelling. Branch joins retain unchecked obligations and forget
 * ambiguous aliases. Resources passed to unknown callees conservatively escape.
 * Work and memory limits fail explicitly rather than dropping AST nodes. */
enum { LIVE = 1, RELEASED = 2, ESCAPED = 4, INVALID = 8 };
enum { ZERO = -1, MINUS_ONE = -2, OTHER_CONST = -3 };
enum { EVAL, CONDITION, RETURN, END, JUMP, NOP, FORK, SWITCH };
typedef struct {
    TSNode node;
    char name[256];
    const struct specdb_contract *contract;
    unsigned flags;
    unsigned seen:1, stored:1, checked:1, propagated:1, ignored:1, pending:1;
} call_info;
typedef struct { TSNode node, scope; char name[256]; } variable;
typedef struct {
    int *value;
    unsigned char *pending, *resource;
} state;
typedef struct {
    TSNode ast;
    int kind, yes, no;
    state in;
    int reached, dirty;
} vertex;
struct mc_flow {
    const mc_ts_file *file;
    call_info *calls;
    size_t nc, cc;
    variable *vars;
    size_t nv, cv;
    mc_finding *findings;
    size_t nf, cf;
    int failed;
};
typedef struct {
    mc_flow *flow;
    vertex *nodes;
    size_t nn, cn;
    size_t first_call, end_call, first_var, end_var;
    size_t origins;
    int unsafe_cleanup;
    int emit;
} graph;

static int is(TSNode n, const char *type) { return !ts_node_is_null(n) && !strcmp(ts_node_type(n), type); }
static TSNode field(TSNode n, const char *key) { return ts_node_child_by_field_name(n, key, (uint32_t)strlen(key)); }
static TSNode unwrap(TSNode n) {
    while (is(n, "parenthesized_expression") || is(n, "cast_expression"))
        n = is(n, "cast_expression") ? field(n, "value") : ts_node_named_child(n, 0);
    return n;
}
static void spelling(const mc_flow *f, TSNode n, char *s, size_t cap) {
    size_t a = ts_node_start_byte(n), b = ts_node_end_byte(n);
    if (ts_node_is_null(n) || b > f->file->source_len || b - a >= cap) { s[0] = 0; return; }
    memcpy(s, f->file->source + a, b - a); s[b - a] = 0;
}
static int reserve(void **p, size_t *cap, size_t need, size_t item) {
    if (need <= *cap) return 1;
    size_t n = *cap ? *cap * 2 : 16;
    if (n < need) n = need;
    if (n > SIZE_MAX / item) return 0;
    void *q = realloc(*p, n * item);
    if (!q) return 0;
    *p = q; *cap = n; return 1;
}
static void finding(graph *g, TSNode node, const char *symbol, const char *kind, const char *msg) {
    if (!g->emit) return;
    mc_flow *f = g->flow;
    TSPoint p = ts_node_start_point(node);
    unsigned line = f->file->line_map && p.row < f->file->line_map_count ? f->file->line_map[p.row] : p.row + 1;
    const char *file = mc_ts_row_file(f->file, p.row);
    for (size_t i = 0; i < f->nf; i++)
        if (f->findings[i].line == line && f->findings[i].col == p.column + 1 &&
            !strcmp(f->findings[i].file, file) &&
            !strcmp(f->findings[i].category, kind) && !strcmp(f->findings[i].symbol, symbol)) return;
    if (!reserve((void **)&f->findings, &f->cf, f->nf + 1, sizeof *f->findings)) { f->failed = 1; return; }
    mc_finding *d = &f->findings[f->nf++];
    memset(d, 0, sizeof *d);
    d->file = file; d->line = line; d->col = p.column + 1;
    d->category = d->db_kind = kind;
    snprintf(d->symbol, sizeof d->symbol, "%.63s", symbol);
    snprintf(d->message, sizeof d->message, "%s", msg);
}

static TSNode declaration_id(TSNode n) {
    while (!ts_node_is_null(n) && !is(n, "identifier")) {
        TSNode d = field(n, "declarator");
        if (ts_node_is_null(d)) return (TSNode){0};
        n = d;
    }
    return n;
}
/* Cursor traversal is dynamic internally and visits every sibling. */
static void collect(mc_flow *f, TSNode root) {
    TSTreeCursor cur = ts_tree_cursor_new(root);
    for (;;) {
        TSNode n = ts_tree_cursor_current_node(&cur);
        if (is(n, "call_expression")) {
            TSNode fn = unwrap(field(n, "function"));
            if (is(fn, "identifier")) {
                if (!reserve((void **)&f->calls, &f->cc, f->nc + 1, sizeof *f->calls)) { f->failed = 1; break; }
                call_info *c = &f->calls[f->nc++]; memset(c, 0, sizeof *c);
                c->node = n; spelling(f, fn, c->name, sizeof c->name);
                c->contract = mc_rules_contract(c->name);
                const mc_func_rule *r = mc_rules_lookup(c->name);
                c->flags = r ? r->flags : 0;
            }
        }
        if (is(n, "declaration") || is(n, "parameter_declaration")) {
            uint32_t count = ts_node_child_count(n);
            for (uint32_t i = 0; i < count; i++) {
                const char *key = ts_node_field_name_for_child(n, i);
                if (!key || strcmp(key, "declarator")) continue;
                TSNode id = declaration_id(ts_node_child(n, i));
                if (ts_node_is_null(id)) continue;
                TSNode scope = ts_node_parent(n);
                while (!ts_node_is_null(scope) && !is(scope, "compound_statement") &&
                       !is(scope, "for_statement") && !is(scope, "function_definition")) scope = ts_node_parent(scope);
                if (!reserve((void **)&f->vars, &f->cv, f->nv + 1, sizeof *f->vars)) { f->failed = 1; break; }
                variable *v = &f->vars[f->nv++]; v->node = id; v->scope = scope;
                spelling(f, id, v->name, sizeof v->name);
            }
        }
        if (f->failed) break;
        if (ts_tree_cursor_goto_first_child(&cur)) continue;
        while (!ts_tree_cursor_goto_next_sibling(&cur))
            if (!ts_tree_cursor_goto_parent(&cur)) goto done;
    }
done:
    ts_tree_cursor_delete(&cur);
}
static int var_id(graph *g, TSNode n) {
    if (!is(n, "identifier")) return -1;
    char name[256]; spelling(g->flow, n, name, sizeof name);
    unsigned pos = ts_node_start_byte(n), best_start = 0;
    int best = -1;
    for (size_t i = g->first_var; i < g->end_var; i++) {
        variable *v = &g->flow->vars[i];
        unsigned start = ts_node_start_byte(v->node);
        if (!strcmp(v->name, name) && start <= pos && pos <= ts_node_end_byte(v->scope) &&
            (best < 0 || start >= best_start)) { best = (int)(i - g->first_var); best_start = start; }
    }
    return best;
}
static int call_id(graph *g, TSNode n) {
    for (size_t i = g->first_call; i < g->end_call; i++)
        if (ts_node_eq(n, g->flow->calls[i].node)) return (int)(i - g->first_call + 1);
    return 0;
}
static call_info *origin_call(graph *g, int origin) {
    return origin > 0 && (size_t)origin <= g->end_call - g->first_call ? &g->flow->calls[g->first_call + (size_t)origin - 1] : NULL;
}
static int state_alloc(graph *g, state *s) {
    s->value = calloc(g->end_var - g->first_var + 1, sizeof *s->value);
    s->pending = calloc(g->origins + 1, 1);
    s->resource = calloc(g->origins + 1, 1);
    if (!s->value || !s->pending || !s->resource) { g->flow->failed = 1; return 0; }
    return 1;
}
static void state_free(state *s) { free(s->value); free(s->pending); free(s->resource); }
static void state_copy(graph *g, state *to, const state *from) {
    memcpy(to->value, from->value, (g->end_var - g->first_var) * sizeof *to->value);
    memcpy(to->pending, from->pending, g->origins + 1);
    memcpy(to->resource, from->resource, g->origins + 1);
}
static void acknowledge(graph *g, state *s, int value, int propagate) {
    if (value <= 0) return;
    s->pending[value] = 0;
    call_info *c = origin_call(g, value);
    if (c) { if (propagate) c->propagated = 1; else c->checked = 1; }
    if (propagate && s->resource[value]) s->resource[value] = ESCAPED;
}
static int eval(graph *, state *, TSNode, unsigned);
static void condition(graph *, state *, TSNode, unsigned);
static int constant(graph *g, TSNode n) {
    n = unwrap(n);
    char text[64]; spelling(g->flow, n, text, sizeof text);
    if (!strcmp(text, "NULL") || !strcmp(text, "nullptr")) return ZERO;
    if (!strcmp(text, "MAP_FAILED")) return MINUS_ONE;
    if (is(n, "number_literal")) {
        char *end; long v = strtol(text, &end, 0);
        if (end != text && (!*end || strspn(end, "uUlL") == strlen(end))) return v == 0 ? ZERO : v == -1 ? MINUS_ONE : OTHER_CONST;
    }
    if (is(n, "unary_expression")) {
        char op[8]; spelling(g->flow, field(n, "operator"), op, sizeof op);
        char arg[16]; spelling(g->flow, field(n, "argument"), arg, sizeof arg);
        if (!strcmp(op, "-") && !strcmp(arg, "1")) return MINUS_ONE;
    }
    return 0;
}
static void bad_check(graph *g, TSNode at, int value, int k, const char *op) {
    call_info *c = origin_call(g, value);
    if (!c || !c->contract) return;
    const char *reason = NULL;
    if (c->contract->failure == SPECDB_FAILURE_MAP_FAILED && k == ZERO)
        reason = "NULL comparison does not detect MAP_FAILED";
    if (c->contract->failure == SPECDB_FAILURE_NONZERO && !strncmp(c->name, "pthread_", 8) &&
        ((k == ZERO && (!strcmp(op, "<") || !strcmp(op, ">="))) || k == MINUS_ONE))
        reason = "pthread errors are positive error numbers; test the returned code against zero";
    if (c->contract->failure == SPECDB_FAILURE_STRTOL && k == MINUS_ONE)
        reason = "-1 is a valid conversion result; validate the end pointer and errno";
    if (reason) {
        char msg[256]; snprintf(msg, sizeof msg, "%.80s(): %s", c->name, reason);
        finding(g, at, c->name, "wrong_error_check", msg);
    }
}
static int eval(graph *g, state *s, TSNode n, unsigned depth) {
    if (ts_node_is_null(n)) return 0;
    if (depth > 512) { g->flow->failed = 1; return 0; }
    int k = constant(g, n);
    if (k) return k;
    if (is(n, "identifier")) { int i = var_id(g, n); return i < 0 ? 0 : s->value[i]; }
    if (is(n, "parenthesized_expression")) return eval(g, s, ts_node_named_child(n, 0), depth + 1);
    if (is(n, "cast_expression")) {
        int v = eval(g, s, field(n, "value"), depth + 1);
        char type[64]; spelling(g->flow, field(n, "type"), type, sizeof type);
        if (!strcmp(type, "void")) {
            call_info *c = origin_call(g, v); if (c) c->ignored = 1;
            /* A stored value explicitly acknowledged with (void)var is intentional. */
            if (!is(unwrap(field(n, "value")), "call_expression")) acknowledge(g, s, v, 0);
            return 0;
        }
        return v;
    }
    if (is(n, "assignment_expression") || is(n, "init_declarator")) {
        int init = is(n, "init_declarator");
        TSNode lhs = init ? declaration_id(field(n, "declarator")) : field(n, "left");
        int v = eval(g, s, field(n, init ? "value" : "right"), depth + 1);
        int id = var_id(g, lhs);
        if (id >= 0) { s->value[id] = v; call_info *c = origin_call(g, v); if (c) c->stored = 1; }
        else acknowledge(g, s, v, 1); /* Global/field store transfers ownership. */
        return v;
    }
    if (is(n, "call_expression")) {
        int id = call_id(g, n);
        call_info *c = origin_call(g, id);
        TSNode args = field(n, "arguments");
        uint32_t ac = ts_node_named_child_count(args);
        int first = 0;
        for (uint32_t i = 0; i < ac; i++) {
            int v = eval(g, s, ts_node_named_child(args, i), depth + 1);
            if (!i) first = v;
            if (v > 0 && (!c || !c->contract || (c->contract->failure == SPECDB_FAILURE_UNKNOWN && c->contract->format_index < 0)))
                acknowledge(g, s, v, 1);
            /* Helpers deliberately named check_* are explicit validation boundaries. */
            if (c && !strncmp(c->name, "check_", 6)) acknowledge(g, s, v, 0);
        }
        if (!c) return 0;
        const struct specdb_contract *ct = c->contract;
        if (ct && ct->releases && first > 0) {
            call_info *acq = origin_call(g, first);
            const struct specdb_contract *a = acq ? acq->contract : NULL;
            if (s->resource[first] == RELEASED) {
                char arg[80], msg[256];
                spelling(g->flow, ts_node_named_child(args, 0), arg, sizeof arg);
                snprintf(msg, sizeof msg, "double_close: second call to %.80s(%.79s); resource was already released", c->name, arg);
                finding(g, n, c->name, "double_close", msg);
            }
            if (a && a->acquires && a->acquires != ct->releases) {
                char msg[256]; snprintf(msg, sizeof msg, "%.80s() resource must not be released with %.80s()", acq->name, c->name);
                finding(g, n, c->name, "resource_mismatch", msg);
            }
            s->resource[first] = RELEASED;
        }
        if (ct && ct->acquires) {
            s->resource[id] = LIVE | INVALID;
            /* Transfer APIs have conditional ownership beyond this small model.
             * Escape the old resource to avoid claiming a leak after transfer. */
            if (first > 0 && (!strcmp(c->name, "realloc") || !strcmp(c->name, "reallocarray") ||
                             !strcmp(c->name, "fdopen") || !strcmp(c->name, "fdopendir")))
                s->resource[first] = ESCAPED;
        }
        c->seen = 1;
        s->pending[id] = (c->flags & MC_FUNC_RULE_RETVAL_MUST_CHECK) != 0;
        if (!strcmp(c->name, "exit") || !strcmp(c->name, "_exit") || !strcmp(c->name, "abort")) {
            for (size_t i = 1; i <= g->origins; i++) s->resource[i] = ESCAPED;
        }
        return id;
    }
    if (is(n, "binary_expression")) {
        char op[8]; spelling(g->flow, field(n, "operator"), op, sizeof op);
        if (!strcmp(op, "&&") || !strcmp(op, "||")) {
            condition(g, s, field(n, "left"), depth + 1);
            state right = {0};
            if (!state_alloc(g, &right)) { state_free(&right); return 0; }
            state_copy(g, &right, s);
            condition(g, &right, field(n, "right"), depth + 1);
            /* RHS is conditional. Preserve obligations from either outcome. */
            for (size_t i = 0; i < g->end_var - g->first_var; i++)
                if (s->value[i] != right.value[i]) s->value[i] = 0;
            for (size_t i = 1; i <= g->origins; i++) {
                s->pending[i] |= right.pending[i]; s->resource[i] |= right.resource[i];
            }
            state_free(&right); return 0;
        }
        int a = eval(g, s, field(n, "left"), depth + 1);
        int b = eval(g, s, field(n, "right"), depth + 1);
        if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
            if (a > 0 && b < 0) bad_check(g, n, a, b, op);
            if (b > 0 && a < 0) {
                const char *reverse = !strcmp(op, "<") ? ">" : !strcmp(op, ">") ? "<" : !strcmp(op, "<=") ? ">=" : !strcmp(op, ">=") ? "<=" : op;
                bad_check(g, n, b, a, reverse);
            }
            acknowledge(g, s, a, 0); acknowledge(g, s, b, 0);
            return 0;
        }
        return 0; /* Arithmetic is a use, not an error check. */
    }
    if (is(n, "unary_expression") || is(n, "pointer_expression")) {
        char op[8]; spelling(g->flow, field(n, "operator"), op, sizeof op);
        if (!strcmp(op, "!")) { condition(g, s, field(n, "argument"), depth + 1); return 0; }
        if (!strcmp(op, "&")) {
            int v = eval(g, s, field(n, "argument"), depth + 1);
            acknowledge(g, s, v, 1); return 0;
        }
        return eval(g, s, field(n, "argument"), depth + 1);
    }
    if (is(n, "update_expression")) {
        TSNode arg = field(n, "argument"); int i = var_id(g, arg);
        if (i >= 0) s->value[i] = 0;
        return 0;
    }
    if (is(n, "conditional_expression")) {
        condition(g, s, field(n, "condition"), depth + 1);
        state alt = {0}; if (!state_alloc(g, &alt)) { state_free(&alt); return 0; }
        state_copy(g, &alt, s);
        int a = eval(g, s, field(n, "consequence"), depth + 1);
        int b = eval(g, &alt, field(n, "alternative"), depth + 1);
        for (size_t i = 0; i < g->end_var - g->first_var; i++) if (s->value[i] != alt.value[i]) s->value[i] = 0;
        for (size_t i = 1; i <= g->origins; i++) { s->pending[i] |= alt.pending[i]; s->resource[i] |= alt.resource[i]; }
        state_free(&alt); return a == b ? a : 0;
    }
    if (is(n, "sizeof_expression") || is(n, "string_literal") || is(n, "concatenated_string")) return 0;
    int result = 0;
    for (uint32_t i = 0; i < ts_node_named_child_count(n); i++) result = eval(g, s, ts_node_named_child(n, i), depth + 1);
    return is(n, "comma_expression") ? result : 0;
}
static void condition(graph *g, state *s, TSNode n, unsigned depth) {
    int v = eval(g, s, n, depth + 1);
    bad_check(g, n, v, ZERO, "!=");
    acknowledge(g, s, v, 0);
}
/* Pure lookup for edge refinement: evaluating the condition a second time would
 * repeat assignments and acquisitions. */
static int value_of(graph *g, state *s, TSNode n) {
    n = unwrap(n);
    int k = constant(g, n); if (k) return k;
    if (is(n, "identifier")) { int i = var_id(g, n); return i < 0 ? 0 : s->value[i]; }
    if (is(n, "call_expression")) return call_id(g, n);
    if (is(n, "assignment_expression")) return value_of(g, s, field(n, "left"));
    return 0;
}
static int refine(graph *g, state *s, TSNode n, int truth, unsigned depth) {
    if (depth > 512) { g->flow->failed = 1; return 1; }
    n = unwrap(n);
    int k = constant(g, n); if (k) return (k != ZERO) == truth;
    int v = 0, failure = !truth;
    if (is(n, "unary_expression")) {
        char op[8]; spelling(g->flow, field(n, "operator"), op, sizeof op);
        if (!strcmp(op, "!")) return refine(g, s, field(n, "argument"), !truth, depth + 1);
    }
    if (is(n, "binary_expression")) {
        char op[8]; spelling(g->flow, field(n, "operator"), op, sizeof op);
        int a = value_of(g, s, field(n, "left")), b = value_of(g, s, field(n, "right"));
        if ((!strcmp(op, "&&") && truth) || (!strcmp(op, "||") && !truth))
            return refine(g, s, field(n, "left"), truth, depth + 1) && refine(g, s, field(n, "right"), truth, depth + 1);
        if (a < 0 && b > 0) { int t = a; a = b; b = t;
            if (!strcmp(op, "<")) strcpy(op, ">"); else if (!strcmp(op, ">")) strcpy(op, "<");
            else if (!strcmp(op, "<=")) strcpy(op, ">="); else if (!strcmp(op, ">=")) strcpy(op, "<="); }
        call_info *c = origin_call(g, a);
        if (!c || !c->contract) return 1;
        enum specdb_failure f = c->contract->failure;
        if ((f == SPECDB_FAILURE_NULL && b == ZERO) || (f == SPECDB_FAILURE_MAP_FAILED && b == MINUS_ONE) || (f == SPECDB_FAILURE_NEGATIVE && b == MINUS_ONE)) {
            if (strcmp(op, "==") && strcmp(op, "!=")) return 1;
            failure = !strcmp(op, "==") ? truth : !truth; v = a;
        } else if (f == SPECDB_FAILURE_NEGATIVE && b == ZERO && (!strcmp(op, "<") || !strcmp(op, ">="))) {
            failure = !strcmp(op, "<") ? truth : !truth; v = a;
        } else return 1;
    } else {
        v = value_of(g, s, n); call_info *c = origin_call(g, v);
        if (!c || !c->contract || c->contract->failure != SPECDB_FAILURE_NULL) return 1;
    }
    if (v > 0 && s->resource[v] && !(s->resource[v] & (ESCAPED | RELEASED))) {
        int mask = failure ? INVALID : LIVE;
        if (!(s->resource[v] & mask)) return 0;
        s->resource[v] = (unsigned char)mask;
    }
    return 1;
}
static int node(graph *g, TSNode ast, int kind, int yes, int no) {
    if (!reserve((void **)&g->nodes, &g->cn, g->nn + 1, sizeof *g->nodes)) { g->flow->failed = 1; return -1; }
    int id = (int)g->nn++;
    g->nodes[id] = (vertex){ .ast = ast, .kind = kind, .yes = yes, .no = no };
    return id;
}
static int build(graph *g, TSNode ast, int next, int brk, int cont, unsigned depth) {
    if (ts_node_is_null(ast)) return next;
    if (depth > 512) { g->flow->failed = 1; return next; }
    if (is(ast, "compound_statement") || is(ast, "else_clause")) {
        for (uint32_t i = ts_node_named_child_count(ast); i > 0; i--)
            next = build(g, ts_node_named_child(ast, i - 1), next, brk, cont, depth + 1);
        return next;
    }
    if (is(ast, "if_statement")) {
        int y = build(g, field(ast, "consequence"), next, brk, cont, depth + 1);
        int n = build(g, field(ast, "alternative"), next, brk, cont, depth + 1);
        return node(g, field(ast, "condition"), CONDITION, y, n);
    }
    if (is(ast, "while_statement") || is(ast, "for_statement") || is(ast, "do_statement")) {
        TSNode test = field(ast, "condition");
        int cond = node(g, test, ts_node_is_null(test) ? NOP : CONDITION, -1, next);
        int update = build(g, field(ast, "update"), cond, next, cond, depth + 1);
        int body = build(g, field(ast, "body"), update, next, update, depth + 1);
        if (cond >= 0) g->nodes[cond].yes = body;
        int entry = is(ast, "do_statement") ? body : cond;
        return build(g, field(ast, "initializer"), entry, next, update, depth + 1);
    }
    if (is(ast, "return_statement")) return node(g, ast, RETURN, -1, -1);
    if (is(ast, "break_statement")) return brk;
    if (is(ast, "continue_statement")) return cont;
    if (is(ast, "goto_statement")) return node(g, ast, JUMP, -1, -1);
    if (is(ast, "labeled_statement")) {
        int dest = build(g, ts_node_named_child(ast, ts_node_named_child_count(ast) - 1), next, brk, cont, depth + 1);
        return node(g, ast, NOP, dest, -1);
    }
    if (is(ast, "switch_statement")) {
        g->unsafe_cleanup = 1; /* Case-value constraints are not modeled yet. */
        TSNode body = field(ast, "body");
        int dispatch = -1, tail = -1, fallback = next, fallthrough = next;
        for (uint32_t i = ts_node_named_child_count(body); i > 0; i--) {
            TSNode c = ts_node_named_child(body, i - 1);
            if (!is(c, "case_statement")) continue;
            TSNode value = field(c, "value");
            int dest = fallthrough;
            for (uint32_t j = ts_node_named_child_count(c); j > 0; j--) {
                TSNode child = ts_node_named_child(c, j - 1);
                if (!ts_node_eq(child, value)) dest = build(g, child, dest, next, cont, depth + 1);
            }
            fallthrough = dest;
            if (ts_node_is_null(value)) fallback = dest;
            else {
                dispatch = node(g, (TSNode){0}, FORK, dest, dispatch);
                if (tail < 0) tail = dispatch;
            }
        }
        if (tail >= 0) g->nodes[tail].no = fallback;
        if (dispatch < 0) dispatch = fallback;
        return node(g, field(ast, "condition"), SWITCH, dispatch, -1);
    }
    return node(g, ast, EVAL, next, -1);
}
static void propagate(graph *g, int to, const state *s) {
    if (to < 0) return;
    vertex *v = &g->nodes[to];
    if (!v->reached) {
        if (!state_alloc(g, &v->in)) return;
        state_copy(g, &v->in, s); v->reached = v->dirty = 1; return;
    }
    for (size_t i = 0; i < g->end_var - g->first_var; i++)
        if (v->in.value[i] && v->in.value[i] != s->value[i]) { v->in.value[i] = 0; v->dirty = 1; }
    for (size_t i = 1; i <= g->origins; i++) {
        unsigned char p = v->in.pending[i] | s->pending[i], r = v->in.resource[i] | s->resource[i];
        if (p != v->in.pending[i] || r != v->in.resource[i]) v->dirty = 1;
        v->in.pending[i] = p; v->in.resource[i] = r;
    }
}
static void finish(graph *g, state *s, TSNode at) {
    (void)at;
    if (!g->emit) return;
    for (size_t i = 1; i <= g->end_call - g->first_call; i++) {
        call_info *c = origin_call(g, (int)i);
        if (s->pending[i]) c->pending = 1;
        if (!g->unsafe_cleanup && c->stored && (s->resource[i] & LIVE) && !(s->resource[i] & ESCAPED)) {
            char msg[256]; snprintf(msg, sizeof msg, "resource acquired by %.100s() can reach a function exit without release or ownership transfer", c->name);
            finding(g, c->node, c->name, "missing_cleanup", msg);
        }
    }
}
static void analyze_function(mc_flow *f, TSNode fn) {
    graph g = { .flow = f, .first_call = f->nc, .first_var = f->nv };
    collect(f, fn);
    g.end_call = f->nc; g.end_var = f->nv;
    g.origins = g.end_call - g.first_call + g.end_var - g.first_var;
    TSNode body = field(fn, "body");
    int end = node(&g, body, END, -1, -1);
    int entry = build(&g, body, end, -1, -1, 0);
    /* Bind ordinary goto targets after all labels have been built. */
    for (size_t i = 0; i < g.nn; i++) if (g.nodes[i].kind == JUMP) {
        char target[256]; spelling(f, field(g.nodes[i].ast, "label"), target, sizeof target);
        for (size_t j = 0; j < g.nn; j++) if (is(g.nodes[j].ast, "labeled_statement")) {
            char label[256]; spelling(f, field(g.nodes[j].ast, "label"), label, sizeof label);
            if (*target && !strcmp(target, label)) g.nodes[i].yes = (int)j;
        }
        if (g.nodes[i].yes < 0) f->failed = 1;
    }
    /* Explicit cap, including all per-node state. Avoid quadratic memory blowup. */
    if (g.nn > 100000 || g.origins > 100000 || (g.origins + 1) * (g.nn + 1) > 16000000) f->failed = 1;
    state initial = {0}, work = {0}, other = {0};
    if (!f->failed && state_alloc(&g, &initial) && state_alloc(&g, &work) && state_alloc(&g, &other)) {
        for (size_t i = 0; i < g.end_var - g.first_var; i++) {
            initial.value[i] = (int)(g.end_call - g.first_call + i + 1);
            initial.resource[initial.value[i]] = LIVE;
        }
        propagate(&g, entry, &initial);
        size_t steps = 0;
        int changed;
        do {
            changed = 0;
            for (size_t i = 0; i < g.nn && !f->failed; i++) {
                vertex *v = &g.nodes[i]; if (!v->dirty) continue;
                if (++steps > 200000) { f->failed = 1; break; }
                v->dirty = 0; changed = 1; state_copy(&g, &work, &v->in);
                if (v->kind == EVAL) eval(&g, &work, v->ast, 0);
                if (v->kind == SWITCH) condition(&g, &work, v->ast, 0);
                if (v->kind == FORK) propagate(&g, v->no, &work);
                if (v->kind == CONDITION) {
                    condition(&g, &work, v->ast, 0); state_copy(&g, &other, &work);
                    if (refine(&g, &work, v->ast, 1, 0)) propagate(&g, v->yes, &work);
                    if (refine(&g, &other, v->ast, 0, 0)) propagate(&g, v->no, &other);
                } else if (v->kind == RETURN || v->kind == END) {
                    if (v->kind == RETURN) acknowledge(&g, &work, eval(&g, &work, ts_node_named_child(v->ast, 0), 0), 1);
                    finish(&g, &work, v->ast);
                } else propagate(&g, v->yes, &work);
            }
        } while (changed && !f->failed);
        /* Diagnose only the fixed point, never a transient worklist state. */
        g.emit = 1;
        for (size_t i = 0; i < g.nn && !f->failed; i++) {
            vertex *v = &g.nodes[i]; if (!v->reached) continue;
            state_copy(&g, &work, &v->in);
            if (v->kind == EVAL) eval(&g, &work, v->ast, 0);
            if (v->kind == CONDITION || v->kind == SWITCH) condition(&g, &work, v->ast, 0);
            if (v->kind == RETURN) acknowledge(&g, &work, eval(&g, &work, ts_node_named_child(v->ast, 0), 0), 1);
            if (v->kind == RETURN || v->kind == END) finish(&g, &work, v->ast);
        }
    }
    state_free(&initial); state_free(&work); state_free(&other);
    for (size_t i = 0; i < g.nn; i++) state_free(&g.nodes[i].in);
    free(g.nodes);
}
mc_flow *mc_flow_analyze(const mc_ts_file *file) {
    mc_flow *f = calloc(1, sizeof *f); if (!f) return NULL;
    f->file = file;
    for (uint32_t i = 0; i < ts_node_named_child_count(file->root) && !f->failed; i++) {
        TSNode n = ts_node_named_child(file->root, i);
        if (is(n, "function_definition")) analyze_function(f, n);
    }
    if (f->failed) fprintf(stderr, "analyzer: data-flow analysis failed or exceeded its resource limit in %s\n", file->path);
    return f;
}
void mc_flow_destroy(mc_flow *f) { if (f) { free(f->calls); free(f->vars); free(f->findings); free(f); } }
bool mc_flow_ok(const mc_flow *f) { return f && !f->failed; }
mc_call_status mc_flow_status(const mc_flow *f, TSNode n, mc_call_status fallback) {
    if (!f || f->failed) return fallback;
    for (size_t i = 0; i < f->nc; i++) if (ts_node_eq(f->calls[i].node, n)) {
        const call_info *c = &f->calls[i];
        if (!c->seen) return MC_CALL_STATUS_STORED;
        if (c->pending) return c->stored ? MC_CALL_STATUS_STORED_UNCHECKED : c->ignored ? MC_CALL_STATUS_IGNORED_EXPLICIT : MC_CALL_STATUS_UNCHECKED;
        if (c->propagated) return MC_CALL_STATUS_PROPAGATED;
        if (c->checked) return c->stored ? MC_CALL_STATUS_STORED : MC_CALL_STATUS_CHECKED_COND;
        return fallback;
    }
    return fallback;
}
void mc_flow_findings(const mc_flow *f, mc_finding_sink sink, void *data) {
    if (f && !f->failed) for (size_t i = 0; i < f->nf; i++) sink(&f->findings[i], data);
}
