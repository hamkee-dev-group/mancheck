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

/* simple C comment stripper + whitespace normalizer */
int mc_preprocess_minimal(mc_source_views *views)
{
    if (!views || !views->src_raw)
        return -1;

    const char *in = views->src_raw;
    size_t len = strlen(in);
    char *out = malloc(len + 1);
    if (!out)
        return -1;

    enum
    {
        STATE_CODE,
        STATE_SLASH,
        STATE_LINE_COMMENT,
        STATE_BLOCK_COMMENT,
        STATE_BLOCK_COMMENT_STAR
    } state = STATE_CODE;

    size_t oi = 0;
    int last_was_space = 0;

    for (size_t i = 0; i < len; i++)
    {
        unsigned char uc = (unsigned char)in[i];
        char c = (char)uc;

        switch (state)
        {
        case STATE_CODE:
            if (c == '/')
            {
                state = STATE_SLASH;
            }
            else if (is_space_char(uc))
            {
                if (!last_was_space)
                {
                    out[oi++] = ' ';
                    last_was_space = 1;
                }
            }
            else
            {
                out[oi++] = c;
                last_was_space = 0;
            }
            break;

        case STATE_SLASH:
            if (c == '/')
            {
                state = STATE_LINE_COMMENT;
            }
            else if (c == '*')
            {
                state = STATE_BLOCK_COMMENT;
            }
            else
            {
                out[oi++] = '/';
                if (is_space_char(uc))
                {
                    if (!last_was_space)
                    {
                        out[oi++] = ' ';
                        last_was_space = 1;
                    }
                }
                else
                {
                    out[oi++] = c;
                    last_was_space = 0;
                }
                state = STATE_CODE;
            }
            break;

        case STATE_LINE_COMMENT:
            if (c == '\n')
            {
                if (!last_was_space)
                {
                    out[oi++] = ' ';
                    last_was_space = 1;
                }
                state = STATE_CODE;
            }
            break;

        case STATE_BLOCK_COMMENT:
            if (c == '*')
            {
                state = STATE_BLOCK_COMMENT_STAR;
            }
            break;

        case STATE_BLOCK_COMMENT_STAR:
            if (c == '/')
            {
                if (!last_was_space)
                {
                    out[oi++] = ' ';
                    last_was_space = 1;
                }
                state = STATE_CODE;
            }
            else if (c != '*')
            {
                state = STATE_BLOCK_COMMENT;
            }
            break;
        }
    }

    if (state == STATE_SLASH)
    {
        out[oi++] = '/';
    }

    out[oi] = '\0';
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
mc_capture_argv_output(char *const argv[])
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
        int devnull = open("/dev/null", O_WRONLY);

        (void)close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(127);
        (void)close(pipefd[1]);

        if (devnull >= 0)
        {
            if (dup2(devnull, STDERR_FILENO) < 0)
                _exit(127);
            if (devnull != STDERR_FILENO)
                (void)close(devnull);
        }

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
    while (waitpid(pid, NULL, 0) < 0)
    {
        if (errno != EINTR)
            break;
    }

    if (len == 0)
    {
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

        if (c == '\\' && p[1] != '\0')
        {
            p++;
            c = (unsigned char)*p;
        }

        if (len + 1 < buf_size)
            buf[len++] = (char)c;
        p++;
    }

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
        p = mc_next_shell_token(p, tok, tok_cap);
        if (!p)
            break;

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
    return strcmp(tok, "-std") == 0 ||
           strcmp(tok, "-D") == 0 ||
           strcmp(tok, "-U") == 0 ||
           strcmp(tok, "-I") == 0;
}

static int
mc_compile_cmd_flag_is_inline(const char *tok)
{
    return (strncmp(tok, "-std=", 5) == 0 && tok[5] != '\0') ||
           (strncmp(tok, "-D", 2) == 0 && tok[2] != '\0') ||
           (strncmp(tok, "-U", 2) == 0 && tok[2] != '\0') ||
           (strncmp(tok, "-I", 2) == 0 && tok[2] != '\0');
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
        p = mc_next_shell_token(p, tok, tok_cap);
        if (!p)
            break;

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

/* Best-effort clang -E:
 * - on success: fills views->src_pp
 * - on failure: leaves src_pp == NULL but returns 0 so pipeline continues
 */
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
        mc_append_compile_cmd_flags(&argv, views->meta.compile_cmd) != 0 ||
        mc_argv_builder_push(&argv, path) != 0)
    {
        mc_argv_builder_free(&argv);
        return -1;
    }

    char *out = mc_capture_argv_output(argv.argv);
    mc_argv_builder_free(&argv);

    if (!out)
    {
        /* clang -E failed: treat as "no preprocessed view", but not fatal */
        views->src_pp = NULL;
        return 0;
    }

    views->src_pp = out;
    return 0;
}

/* Build src_pp_user from src_pp using #line markers
 * Best-effort: if src_pp is NULL, leave src_pp_user NULL and succeed.
 */
int mc_preprocess_pp_trim_user(mc_source_views *views)
{
    if (!views)
        return -1;

    views->pp_user_line_map = NULL;
    views->pp_user_line_count = 0;

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
    size_t map_len = 0;

    size_t oi = 0;
    const char *p = pp;
    const char *end = pp + len;

    char *current_file = NULL;
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

                if (q <= line_end)
                {
                    size_t path_len = (size_t)(q - path_start);
                    char *path = malloc(path_len + 1);
                    if (!path)
                    {
                        free(out);
                        free(current_file);
                        free(map);
                        return -1;
                    }
                    memcpy(path, path_start, path_len);
                    path[path_len] = '\0';

                    free(current_file);
                    current_file = path;
                    current_orig_line = marker_line;
                }
            }
        }
        else
        {
            if (current_file && strcmp(current_file, orig) == 0)
            {
                /* Add to line map */
                if (map_len == map_cap) {
                    map_cap *= 2;
                    unsigned *tmp = realloc(map, map_cap * sizeof(*tmp));
                    if (!tmp) {
                        free(out);
                        free(current_file);
                        free(map);
                        return -1;
                    }
                    map = tmp;
                }
                map[map_len++] = current_orig_line;

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

    memset(views, 0, sizeof(*views));
}

int mc_run_preproc_pipeline(const mc_file_meta *files,
                            size_t file_count,
                            const mc_pp_config *pp_cfg,
                            mc_preproc_hook *hook)
{
    if (!files)
        return -1;

    for (size_t i = 0; i < file_count; i++)
    {
        mc_source_views v;

        if (mc_load_file(&files[i], &v) != 0)
        {
            continue;
        }

        (void)mc_preprocess_minimal(&v);

        /* best-effort: ignore rc; src_pp may be NULL */
        (void)mc_preprocess_clang(&v, pp_cfg);
        (void)mc_preprocess_pp_trim_user(&v);

        if (hook && hook->on_views)
        {
            if (hook->on_views(hook, &v) != 0)
            {
                mc_free_source_views(&v);
                break;
            }
        }

        mc_free_source_views(&v);
    }

    return 0;
}
