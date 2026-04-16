#define _POSIX_C_SOURCE 200809L
#include "mc_preproc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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

static char *
mc_capture_command_output(const char *cmd)
{
    FILE *p = popen(cmd, "r");
    if (!p)
        return NULL;

    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
    {
        (void)pclose(p);
        return NULL;
    }

    for (;;)
    {
        if (len + 1024 > cap)
        {
            size_t new_cap = cap * 2;
            char *nb = realloc(buf, new_cap);
            if (!nb)
            {
                free(buf);
                (void)pclose(p);
                return NULL;
            }
            buf = nb;
            cap = new_cap;
        }

        size_t n = fread(buf + len, 1, 1024, p);
        len += n;

        if (n < 1024)
        {
            if (feof(p))
                break;
            if (ferror(p))
            {
                free(buf);
                (void)pclose(p);
                return NULL;
            }
        }
    }

    (void)pclose(p);

    if (len == 0)
    {
        free(buf);
        return NULL;
    }

    if (len == cap)
    {
        char *nb = realloc(buf, cap + 1);
        if (!nb)
        {
            free(buf);
            return NULL;
        }
        buf = nb;
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

static char *
mc_extract_compile_std(const char *compile_cmd)
{
    if (!compile_cmd)
        return NULL;

    size_t tok_cap = strlen(compile_cmd) + 1;
    char *tok = malloc(tok_cap);
    if (!tok)
        return NULL;

    char *std = NULL;
    const char *p = compile_cmd;

    for (;;)
    {
        p = mc_next_shell_token(p, tok, tok_cap);
        if (!p)
            break;

        if (strncmp(tok, "-std=", 5) == 0 && tok[5] != '\0')
        {
            free(std);
            std = strdup(tok + 5);
            if (!std)
                break;
            continue;
        }

        if (strcmp(tok, "-std") == 0)
        {
            p = mc_next_shell_token(p, tok, tok_cap);
            if (!p || tok[0] == '\0')
                break;

            free(std);
            std = strdup(tok);
            if (!std)
                break;
        }
    }

    free(tok);
    return std;
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
    char *compile_std = mc_extract_compile_std(views->meta.compile_cmd);

    /* Add " 2>/dev/null" to silence clang diagnostics */
    const char *redir = " 2>/dev/null";

    size_t len = strlen(clang) + 4 + strlen(path) + strlen(redir) + 1;
    if (extra[0] != '\0')
        len += strlen(extra) + 1;
    if (compile_std)
        len += 6 + strlen(compile_std);

    char *cmd = malloc(len);
    if (!cmd)
    {
        free(compile_std);
        return -1;
    }

    if (extra[0] != '\0' && compile_std)
        (void)snprintf(cmd, len, "%s -E %s -std=%s %s%s",
                       clang, extra, compile_std, path, redir);
    else if (extra[0] != '\0')
        (void)snprintf(cmd, len, "%s -E %s %s%s", clang, extra, path, redir);
    else if (compile_std)
        (void)snprintf(cmd, len, "%s -E -std=%s %s%s",
                       clang, compile_std, path, redir);
    else
        (void)snprintf(cmd, len, "%s -E %s%s", clang, path, redir);

    char *out = mc_capture_command_output(cmd);
    free(cmd);
    free(compile_std);

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
