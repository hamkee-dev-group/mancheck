#define _POSIX_C_SOURCE 200809L
#include "mc_preproc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static char *
mc_read_entire_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        (void)fclose(f);
        return NULL;
    }

    long len = ftell(f);
    if (len < 0)
    {
        (void)fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0)
    {
        (void)fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)len + 1);
    if (!buf)
    {
        (void)fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)len, f);
    if (n != (size_t)len && ferror(f))
    {
        free(buf);
        (void)fclose(f);
        return NULL;
    }

    (void)fclose(f);
    buf[n] = '\0';
    return buf;
}

int mc_load_file(const mc_file_meta *meta, mc_source_views *out)
{
    if (!meta || !out || !meta->abs_path)
        return -1;

    memset(out, 0, sizeof(*out));
    out->meta = *meta;

    out->src_raw = mc_read_entire_file(meta->abs_path);
    if (!out->src_raw)
        return -1;

    return 0;
}

static int
is_space_char(int c)
{
    return c == ' ' || c == '\t' || c == '\r' ||
           c == '\n' || c == '\f' || c == '\v';
}

/* Keep line boundaries and literals intact; comments become whitespace. */
int mc_preprocess_minimal(mc_source_views *views)
{
    if (!views || !views->src_raw) return -1;
    const char *in = views->src_raw;
    char *out = strdup(in);
    if (!out) return -1;
    enum { CODE, STRING, CHARACTER, LINE, BLOCK } state = CODE;
    for (size_t i = 0; in[i]; i++) {
        if (state == STRING || state == CHARACTER) {
            if (in[i] == '\\' && in[i + 1]) { i++; continue; }
            if ((state == STRING && in[i] == '"') ||
                (state == CHARACTER && in[i] == '\'')) state = CODE;
        } else if (state == LINE) {
            if (in[i] == '\n') {
                if (!i || in[i - 1] != '\\') state = CODE;
            } else out[i] = ' ';
        } else if (state == BLOCK) {
            if (in[i] == '*' && in[i + 1] == '/') {
                out[i] = out[i + 1] = ' '; i++; state = CODE;
            } else if (in[i] != '\n') out[i] = ' ';
        } else if (in[i] == '"') state = STRING;
        else if (in[i] == '\'') state = CHARACTER;
        else if (in[i] == '/' && (in[i + 1] == '/' || in[i + 1] == '*')) {
            state = in[i + 1] == '/' ? LINE : BLOCK;
            out[i] = out[i + 1] = ' '; i++;
        }
    }
    views->src_min = out;
    return 0;
}

typedef struct {
    char **argv;
    size_t argc;
    size_t cap;
} mc_argv_builder;

static void
mc_argv_builder_free(mc_argv_builder *builder)
{
    if (!builder)
        return;

    for (size_t i = 0; i < builder->argc; i++)
        free(builder->argv[i]);
    free(builder->argv);

    builder->argv = NULL;
    builder->argc = 0;
    builder->cap = 0;
}

static int
mc_argv_builder_push(mc_argv_builder *builder, const char *arg)
{
    if (!builder || !arg)
        return -1;

    if (builder->argc + 2 > builder->cap)
    {
        size_t new_cap = builder->cap ? builder->cap * 2 : 8;
        while (builder->argc + 2 > new_cap)
            new_cap *= 2;

        char **tmp = realloc(builder->argv, new_cap * sizeof(*tmp));
        if (!tmp)
            return -1;

        builder->argv = tmp;
        builder->cap = new_cap;
    }

    builder->argv[builder->argc] = strdup(arg);
    if (!builder->argv[builder->argc])
        return -1;

    builder->argc++;
    builder->argv[builder->argc] = NULL;
    return 0;
}

static char *
mc_capture_argv_output(char *const argv[], const char *workdir)
{
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return NULL;

    pid_t pid = fork();
    if (pid < 0)
    {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        return NULL;
    }

    if (pid == 0)
    {
        (void)close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(127);
        (void)close(pipefd[1]);

        if (workdir && workdir[0] != '\0' && chdir(workdir) != 0)
            _exit(127);

        execvp(argv[0], argv);
        _exit(127);
    }

    (void)close(pipefd[1]);

    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
    {
        (void)close(pipefd[0]);
        (void)waitpid(pid, NULL, 0);
        return NULL;
    }

    for (;;)
    {
        if (len + 1024 > cap)
        {
            size_t new_cap = cap * 2;
            char *tmp = realloc(buf, new_cap);
            if (!tmp)
            {
                free(buf);
                (void)close(pipefd[0]);
                (void)waitpid(pid, NULL, 0);
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }

        ssize_t n = read(pipefd[0], buf + len, 1024);
        if (n > 0)
        {
            len += (size_t)n;
            continue;
        }
        if (n == 0)
            break;
        if (errno == EINTR)
            continue;

        free(buf);
        (void)close(pipefd[0]);
        (void)waitpid(pid, NULL, 0);
        return NULL;
    }

    (void)close(pipefd[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return NULL;
    }

    if (len == cap)
    {
        char *tmp = realloc(buf, cap + 1);
        if (!tmp)
        {
            free(buf);
            return NULL;
        }
        buf = tmp;
    }

    buf[len] = '\0';
    return buf;
}

static const char *
mc_next_shell_token(const char *p, char *buf, size_t buf_size)
{
    size_t len = 0;
    int quote = 0;

    if (!p || !buf || buf_size == 0)
        return NULL;

    while (*p != '\0' && is_space_char((unsigned char)*p))
        p++;

    if (*p == '\0')
        return NULL;

    while (*p != '\0')
    {
        unsigned char c = (unsigned char)*p;

        if (quote != 0)
        {
            if (c == (unsigned char)quote)
            {
                quote = 0;
                p++;
                continue;
            }
        }
        else
        {
            if (is_space_char(c))
                break;
            if (c == '\'' || c == '"')
            {
                quote = (int)c;
                p++;
                continue;
            }
        }

        if (c == '\\' && p[1] == '\0' && !quote) {
            errno = EINVAL; return NULL;
        }
        if (c == '\\' && p[1] != '\0' && quote != '\'' &&
            (quote != '"' || p[1] == '"' || p[1] == '\\' ||
             p[1] == '$' || p[1] == '`' || p[1] == '\n'))
        {
            p++;
            c = (unsigned char)*p;
            if (c == '\n') { p++; continue; }
        }

        if (len + 1 < buf_size)
            buf[len++] = (char)c;
        p++;
    }

    if (quote) { errno = EINVAL; return NULL; }
    buf[len] = '\0';
    return p;
}

static int
mc_append_tokenized_flags(mc_argv_builder *builder, const char *flags)
{
    if (!flags || flags[0] == '\0')
        return 0;

    size_t tok_cap = strlen(flags) + 1;
    char *tok = malloc(tok_cap);
    if (!tok)
        return -1;

    int rc = 0;
    const char *p = flags;

    for (;;)
    {
        errno = 0;
        p = mc_next_shell_token(p, tok, tok_cap);
        if (!p) { if (errno) rc = -1; break; }

        if (mc_argv_builder_push(builder, tok) != 0)
        {
            rc = -1;
            break;
        }
    }

    free(tok);
    return rc;
}

static int
mc_compile_cmd_flag_has_value(const char *tok)
{
    const char *flags[] = {"-std", "-D", "-U", "-I", "-include", "-imacros",
        "-isystem", "-iquote", "-idirafter", "-isysroot", "--sysroot",
        "-target", "--target", "-x", "-resource-dir", "-iprefix",
        "-iwithprefix", "-iwithprefixbefore", NULL};
    for (size_t i = 0; flags[i]; i++) if (!strcmp(tok, flags[i])) return 1;
    return 0;
}

static int
mc_compile_cmd_flag_is_inline(const char *tok)
{
    const char *prefixes[] = {"-std=", "-D", "-U", "-I", "--sysroot=",
        "--target=", "-isystem", "-iquote", "-idirafter", "-O", "-m",
        "-fshort-", "-funsigned-char", "-fsigned-char", "-fms-", NULL};
    for (size_t i = 0; prefixes[i]; i++) {
        size_t n = strlen(prefixes[i]);
        if (!strncmp(tok, prefixes[i], n) && tok[n]) return 1;
    }
    return !strcmp(tok, "-pthread") || !strcmp(tok, "-nostdinc") ||
           !strcmp(tok, "-nostdinc++") || !strcmp(tok, "-undef") ||
           !strcmp(tok, "-ffreestanding") || !strcmp(tok, "-fno-builtin") ||
           !strcmp(tok, "-fopenmp");
}

static int
mc_append_compile_cmd_flags(mc_argv_builder *builder, const char *compile_cmd)
{
    if (!compile_cmd || compile_cmd[0] == '\0')
        return 0;

    size_t tok_cap = strlen(compile_cmd) + 1;
    char *tok = malloc(tok_cap);
    char *next = malloc(tok_cap);
    if (!tok || !next)
    {
        free(tok);
        free(next);
        return -1;
    }

    int rc = 0;
    const char *p = compile_cmd;

    for (;;)
    {
        errno = 0;
        p = mc_next_shell_token(p, tok, tok_cap);
        if (!p) { if (errno) rc = -1; break; }

        if (tok[0] == '@' || !strncmp(tok, "-Wp,", 4) || !strcmp(tok, "-Xpreprocessor")) {
            fprintf(stderr, "error: unsupported preprocessing argument: %s\n", tok);
            rc = -1; break;
        }
        if (mc_compile_cmd_flag_is_inline(tok))
        {
            if (mc_argv_builder_push(builder, tok) != 0)
            {
                rc = -1;
                break;
            }
            continue;
        }

        if (mc_compile_cmd_flag_has_value(tok))
        {
            const char *np = mc_next_shell_token(p, next, tok_cap);
            if (!np || next[0] == '\0')
            {
                rc = -1;
                break;
            }

            if (strcmp(tok, "-std") == 0)
            {
                size_t len = strlen(next) + strlen("-std=") + 1;
                char *joined = malloc(len);
                if (!joined)
                {
                    rc = -1;
                    break;
                }
                (void)snprintf(joined, len, "-std=%s", next);
                if (mc_argv_builder_push(builder, joined) != 0)
                    rc = -1;
                free(joined);
                if (rc != 0)
                    break;
            }
            else
            {
                if (mc_argv_builder_push(builder, tok) != 0 ||
                    mc_argv_builder_push(builder, next) != 0)
                {
                    rc = -1;
                    break;
                }
            }
            p = np;
        }
    }

    free(tok);
    free(next);
    return rc;
}

static int
mc_append_compile_argv_flags(mc_argv_builder *builder,
                             const char *const *compile_argv,
                             size_t compile_argc)
{
    if (!compile_argv || compile_argc == 0)
        return 0;

    int rc = 0;
    size_t start = 0;

    if (compile_argv[0] && compile_argv[0][0] != '-')
        start = 1;

    for (size_t i = start; i < compile_argc; i++)
    {
        const char *tok = compile_argv[i];
        if (!tok)
            continue;

        if (tok[0] == '@' || !strncmp(tok, "-Wp,", 4) || !strcmp(tok, "-Xpreprocessor")) {
            fprintf(stderr, "error: unsupported preprocessing argument: %s\n", tok);
            rc = -1; break;
        }
        if (mc_compile_cmd_flag_is_inline(tok))
        {
            if (mc_argv_builder_push(builder, tok) != 0)
            {
                rc = -1;
                break;
            }
            continue;
        }

        if (mc_compile_cmd_flag_has_value(tok))
        {
            const char *next = (i + 1 < compile_argc) ? compile_argv[i + 1] : NULL;
            if (!next || next[0] == '\0')
            {
                rc = -1;
                break;
            }

            if (strcmp(tok, "-std") == 0)
            {
                size_t len = strlen(next) + strlen("-std=") + 1;
                char *joined = malloc(len);
                if (!joined)
                {
                    rc = -1;
                    break;
                }
                (void)snprintf(joined, len, "-std=%s", next);
                if (mc_argv_builder_push(builder, joined) != 0)
                    rc = -1;
                free(joined);
                if (rc != 0)
                    break;
            }
            else
            {
                if (mc_argv_builder_push(builder, tok) != 0 ||
                    mc_argv_builder_push(builder, next) != 0)
                {
                    rc = -1;
                    break;
                }
            }

            i++;
        }
    }

    return rc;
}

/* A failed compiler must never produce a successful preprocessed view. */
int mc_preprocess_clang(mc_source_views *views,
                        const mc_pp_config *cfg)
{
    if (!views || !views->src_raw || !views->meta.abs_path)
        return -1;

    const char *clang = (cfg && cfg->clang_path) ? cfg->clang_path : "clang";
    const char *extra = (cfg && cfg->extra_flags) ? cfg->extra_flags : "";
    const char *path = views->meta.abs_path;
    mc_argv_builder argv = {0};

    if (mc_argv_builder_push(&argv, clang) != 0 ||
        mc_argv_builder_push(&argv, "-E") != 0 ||
        mc_append_tokenized_flags(&argv, extra) != 0 ||
        mc_append_compile_argv_flags(&argv,
                                     views->meta.compile_argv,
                                     views->meta.compile_argc) != 0 ||
        ((!views->meta.compile_argv || !views->meta.compile_argc) &&
         mc_append_compile_cmd_flags(&argv, views->meta.compile_cmd) != 0) ||
        mc_argv_builder_push(&argv, path) != 0)
    {
        mc_argv_builder_free(&argv);
        return -1;
    }

    char *out = mc_capture_argv_output(argv.argv, views->meta.compile_dir);
    mc_argv_builder_free(&argv);

    if (!out)
    {
        views->src_pp = NULL;
        return -1;
    }

    views->src_pp = out;
    return 0;
}

/* Intern a header path in views->pp_user_files; NULL on allocation failure. */
static const char *pp_user_intern_file(mc_source_views *views, const char *path)
{
    for (size_t i = 0; i < views->pp_user_file_count; i++)
        if (!strcmp(views->pp_user_files[i], path))
            return views->pp_user_files[i];
    char **tmp = realloc(views->pp_user_files,
                         (views->pp_user_file_count + 1) * sizeof(*tmp));
    if (!tmp)
        return NULL;
    views->pp_user_files = tmp;
    char *copy = strdup(path);
    if (!copy)
        return NULL;
    views->pp_user_files[views->pp_user_file_count++] = copy;
    return copy;
}

/* Build src_pp_user from src_pp using #line markers: the main file plus
 * project headers it includes. System headers (marker flag 3) are dropped.
 * Best-effort: if src_pp is NULL, leave src_pp_user NULL and succeed.
 */
int mc_preprocess_pp_trim_user(mc_source_views *views)
{
    if (!views)
        return -1;

    views->pp_user_line_map = NULL;
    views->pp_user_line_count = 0;
    views->pp_user_file_map = NULL;

    if (!views->src_pp || !views->meta.abs_path)
    {
        views->src_pp_user = NULL;
        return 0;
    }

    const char *orig = views->meta.abs_path;
    const char *pp = views->src_pp;

    size_t len = strlen(pp);
    char *out = malloc(len + 1);
    if (!out)
        return -1;

    /* Line map: grow dynamically. */
    size_t map_cap = 256;
    unsigned *map = malloc(map_cap * sizeof(*map));
    if (!map) {
        free(out);
        return -1;
    }
    const char **file_map = malloc(map_cap * sizeof(*file_map));
    if (!file_map) {
        free(out);
        free(map);
        return -1;
    }
    size_t map_len = 0;

    size_t oi = 0;
    const char *p = pp;
    const char *end = pp + len;

    char *current_file = NULL;
    int include_depth = 0, main_started = 0;
    /* Depth of the outermost system header being read; 0 = none. */
    int system_depth = 0;
    const char *header_file = NULL;  /* interned current header path */
    long main_line_offset = 0;
    unsigned current_orig_line = 1;  /* original source line from # marker */

    while (p < end)
    {
        const char *line_start = p;
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end)
            line_end = end;

        size_t line_len = (size_t)(line_end - line_start);

        if (line_len > 0 && line_start[0] == '#')
        {
            /* Parse: # <linenum> "<file>" [flags]
             * The line number tells us what original line the NEXT line is. */
            const char *q = line_start + 1;
            while (q < line_end && *q == ' ')
                q++;

            /* Parse the line number */
            unsigned marker_line = 0;
            while (q < line_end && *q >= '0' && *q <= '9') {
                marker_line = marker_line * 10 + (unsigned)(*q - '0');
                q++;
            }

            while (q < line_end && *q == ' ')
                q++;

            /* Parse the file path */
            if (q < line_end && *q == '"')
            {
                q++;
                const char *path_start = q;
                while (q < line_end && *q != '"')
                    q++;

                if (q < line_end)
                {
                    size_t path_len = (size_t)(q - path_start);
                    char *path = malloc(path_len + 1);
                    if (!path)
                    {
                        free(out);
                        free(current_file);
                        free(map);
                        free(file_map);
                        return -1;
                    }
                    memcpy(path, path_start, path_len);
                    path[path_len] = '\0';

                    int enter = 0, leave = 0, system = 0;
                    const char *flags = q + 1;
                    while (flags < line_end) {
                        while (flags < line_end && *flags == ' ') flags++;
                        if (flags < line_end && *flags == '1') enter = 1;
                        if (flags < line_end && *flags == '2') leave = 1;
                        if (flags < line_end && *flags == '3') system = 1;
                        while (flags < line_end && *flags != ' ') flags++;
                    }
                    if (enter) {
                        include_depth++;
                        if (system && !system_depth)
                            system_depth = include_depth;
                    }
                    if (leave && include_depth > 0) {
                        include_depth--;
                        if (include_depth < system_depth)
                            system_depth = 0;
                    }
                    header_file = NULL;
                    if (main_started && include_depth > 0 && !system_depth && path[0] != '<') {
                        header_file = pp_user_intern_file(views, path);
                        if (!header_file) {
                            free(path);
                            free(out);
                            free(current_file);
                            free(map);
                            free(file_map);
                            return -1;
                        }
                    }
                    if (!strcmp(path, orig) && include_depth == 0) {
                        main_started = 1;
                        main_line_offset = 0;
                    } else if (main_started && include_depth == 0 && !enter && !leave) {
                        /* A #line directive changes the logical filename, not
                         * membership in the main translation-unit source. */
                        main_line_offset += (long)current_orig_line + 1 - (long)marker_line;
                    }
                    free(current_file);
                    current_file = path;
                    current_orig_line = marker_line;
                }
            }
        }
        else
        {
            if (main_started && (include_depth == 0 || header_file))
            {
                /* Add to line map */
                if (map_len == map_cap) {
                    map_cap *= 2;
                    unsigned *tmp = realloc(map, map_cap * sizeof(*tmp));
                    if (tmp)
                        map = tmp;
                    const char **ftmp = tmp ? realloc(file_map, map_cap * sizeof(*ftmp)) : NULL;
                    if (!ftmp) {
                        free(out);
                        free(current_file);
                        free(map);
                        free(file_map);
                        return -1;
                    }
                    file_map = ftmp;
                }
                if (include_depth == 0) {
                    map[map_len] = (unsigned)((long)current_orig_line + main_line_offset);
                    file_map[map_len] = NULL;
                } else {
                    map[map_len] = current_orig_line;
                    file_map[map_len] = header_file;
                }
                map_len++;

                memcpy(out + oi, line_start, line_len);
                oi += line_len;
                if (line_end < end)
                {
                    out[oi++] = '\n';
                }
            }
            /* clang -E increments the original line for each output line */
            current_orig_line++;
        }

        if (line_end >= end)
            break;
        p = line_end + 1;
    }

    if (oi == 0)
        out[0] = '\0';
    else
        out[oi] = '\0';

    free(current_file);
    views->src_pp_user = out;
    views->pp_user_line_map = map;
    views->pp_user_line_count = map_len;
    views->pp_user_file_map = file_map;
    return 0;
}

void mc_free_source_views(mc_source_views *views)
{
    if (!views)
        return;

    free(views->src_raw);
    free(views->src_min);
    free(views->src_pp);
    free(views->src_pp_user);
    free(views->pp_user_line_map);
    free(views->pp_user_file_map);
    for (size_t i = 0; i < views->pp_user_file_count; i++)
        free(views->pp_user_files[i]);
    free(views->pp_user_files);

    memset(views, 0, sizeof(*views));
}

int mc_run_preproc_pipeline(const mc_file_meta *files, size_t file_count,
                            const mc_pp_config *pp_cfg, mc_preproc_hook *hook)
{
    if (!files) return -1;
    int result = 0;
    for (size_t i = 0; i < file_count; i++) {
        mc_source_views v;
        if (mc_load_file(&files[i], &v) != 0) {
            fprintf(stderr, "error: cannot read %s: %s\n", files[i].path, strerror(errno));
            result = -1;
            continue;
        }
        if (mc_preprocess_minimal(&v) != 0) result = -1;
        if (mc_preprocess_clang(&v, pp_cfg) != 0) {
            fprintf(stderr, "error: preprocessing failed for %s%s; analysis is incomplete\n",
                    files[i].path, (v.meta.compile_cmd || v.meta.compile_argc)
                    ? " (matched compilation database entry)" : "");
            result = -1;
        }
        if (mc_preprocess_pp_trim_user(&v) != 0) {
            fprintf(stderr, "error: source mapping failed for %s\n", files[i].path);
            result = -1;
        }
        if (hook && hook->on_views && hook->on_views(hook, &v) != 0) {
            mc_free_source_views(&v);
            result = -1;
            break;
        }
        mc_free_source_views(&v);
    }
    return result;
}
