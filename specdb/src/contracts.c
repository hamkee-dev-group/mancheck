#define _POSIX_C_SOURCE 200809L
#include "specdb.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void init_contract(struct specdb_contract *c)
{
    memset(c, 0, sizeof *c);
    c->format_index = c->truncation_size_index = -1;
}

static int ident(int c) { return isalnum((unsigned char)c) || c == '_'; }

/* No truncation: an incomplete declaration is not evidence of a signature. */
int specdb_extract_prototype(const char *syn, const char *name, char *out, size_t cap)
{
    if (!syn || !name || !*name || !out || !cap) return -1;
    out[0] = 0;
    size_t n = strlen(name);
    for (const char *p = syn; (p = strstr(p, name)); p += n) {
        if ((p > syn && ident(p[-1])) || ident(p[n])) continue;
        const char *args = p + n;
        while (isspace((unsigned char)*args)) args++;
        if (*args != '(') continue;
        const char *start = p;
        while (start > syn && start[-1] != '\n' && start[-1] != ';') start--;
        while (start < p && isspace((unsigned char)*start)) start++;
        /* Return types can be on the preceding line (BSD-style SYNOPSIS). */
        if (start == p && start > syn) {
            const char *end = start - 1;
            while (end > syn && isspace((unsigned char)*end)) end--;
            start = end;
            while (start > syn && start[-1] != '\n' && start[-1] != ';') start--;
            while (start < end && isspace((unsigned char)*start)) start++;
        }
        if (start == p || *start == '#' || memchr(start, ':', (size_t)(p - start))) continue;
        int depth = 0;
        const char *end = args;
        do {
            if (*end == '(') depth++;
            else if (*end == ')') depth--;
            if (!*end) break;
            end++;
        } while (depth);
        if (depth) continue;
        while (isspace((unsigned char)*end)) end++;
        if (*end != ';') continue;
        end++;
        size_t len = 0;
        for (const char *q = start; q < end; q++) {
            char ch = *q;
            if (isspace((unsigned char)ch)) {
                if (!len || out[len - 1] == ' ') continue;
                ch = ' ';
            }
            if (len + 1 >= cap) { out[0] = 0; return -1; }
            out[len++] = ch;
        }
        out[len] = 0;
        return 1;
    }
    return 0;
}

static int member(const char *names, const char *name)
{
    size_t n = strlen(name);
    const char *p = names;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !memcmp(p, name, n)) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

int specdb_builtin_contract(const char *name, struct specdb_contract *c)
{
    init_contract(c);
    if (member("open openat creat socket accept accept4 dup dup2 dup3", name)) {
        c->failure = SPECDB_FAILURE_NEGATIVE; c->acquires = SPECDB_RESOURCE_FD;
    } else if (member("malloc calloc realloc reallocarray strdup strndup aligned_alloc", name)) {
        c->failure = SPECDB_FAILURE_NULL; c->acquires = SPECDB_RESOURCE_HEAP;
    } else if (member("fopen fdopen freopen tmpfile", name)) {
        c->failure = SPECDB_FAILURE_NULL; c->acquires = SPECDB_RESOURCE_FILE;
    } else if (!strcmp(name, "popen")) {
        c->failure = SPECDB_FAILURE_NULL; c->acquires = SPECDB_RESOURCE_PIPE;
    } else if (member("opendir fdopendir", name)) {
        c->failure = SPECDB_FAILURE_NULL; c->acquires = SPECDB_RESOURCE_DIR;
    } else if (!strcmp(name, "dlopen")) {
        c->failure = SPECDB_FAILURE_NULL; c->acquires = SPECDB_RESOURCE_DL;
    } else if (!strcmp(name, "mmap")) {
        c->failure = SPECDB_FAILURE_MAP_FAILED; c->acquires = SPECDB_RESOURCE_MAPPING;
    } else if (!strcmp(name, "free")) {
        c->failure = SPECDB_FAILURE_NONE; c->returns_void = 1; c->releases = SPECDB_RESOURCE_HEAP;
    } else if (!strcmp(name, "close")) {
        c->failure = SPECDB_FAILURE_NEGATIVE; c->releases = SPECDB_RESOURCE_FD;
    } else if (!strcmp(name, "fclose")) {
        c->failure = SPECDB_FAILURE_NEGATIVE; c->releases = SPECDB_RESOURCE_FILE;
    } else if (!strcmp(name, "pclose")) {
        c->failure = SPECDB_FAILURE_NEGATIVE; c->releases = SPECDB_RESOURCE_PIPE;
    } else if (!strcmp(name, "closedir")) {
        c->failure = SPECDB_FAILURE_NEGATIVE; c->releases = SPECDB_RESOURCE_DIR;
    } else if (!strcmp(name, "munmap")) {
        c->failure = SPECDB_FAILURE_NEGATIVE; c->releases = SPECDB_RESOURCE_MAPPING;
    } else if (!strcmp(name, "dlclose")) {
        c->failure = SPECDB_FAILURE_NONZERO; c->releases = SPECDB_RESOURCE_DL;
    } else if (member("pthread_create pthread_join pthread_mutex_init pthread_mutex_lock pthread_mutex_trylock pthread_mutex_unlock pthread_rwlock_init pthread_rwlock_rdlock pthread_rwlock_wrlock pthread_rwlock_unlock pthread_cond_init pthread_cond_wait pthread_cond_signal pthread_cond_broadcast posix_memalign getaddrinfo getnameinfo", name)) {
        c->failure = SPECDB_FAILURE_NONZERO;
    } else if (member("strtol strtoul strtoll strtoull", name)) {
        c->failure = SPECDB_FAILURE_STRTOL;
    } else if (member("read write pread pwrite readv writev send recv sendto recvfrom sendmsg recvmsg lseek fsync fdatasync pipe pipe2 ftruncate chmod fchmod chown fchown unlink unlinkat rename renameat chdir fchdir mkdir mkdirat rmdir link linkat symlink symlinkat readlink readlinkat stat fstat lstat access faccessat fork wait waitpid execve execl execlp execv execvp nanosleep clock_gettime sigaction setenv unsetenv putenv", name)) {
        c->failure = SPECDB_FAILURE_NEGATIVE;
    } else if (member("memset memcpy memmove strcpy strcat strncpy strncat srand qsort closelog openlog perror freeaddrinfo umask", name)) {
        c->failure = SPECDB_FAILURE_NONE;
        c->returns_void = member("srand qsort closelog openlog perror freeaddrinfo", name);
    }
    if (member("printf vprintf scanf vscanf warn warnx vwarn vwarnx", name)) c->format_index = 0;
    if (member("fprintf vfprintf dprintf vdprintf sprintf vsprintf fscanf vfscanf sscanf vsscanf asprintf vasprintf syslog vsyslog err errx verr verrx", name)) c->format_index = 1;
    if (member("snprintf vsnprintf", name)) {
        c->format_index = 2;
        c->truncation_size_index = 1;
        c->failure = SPECDB_FAILURE_NEGATIVE;
    }
    if (c->failure == SPECDB_FAILURE_UNKNOWN && c->format_index < 0) return 0;
    c->sets_errno = c->failure == SPECDB_FAILURE_NEGATIVE || c->failure == SPECDB_FAILURE_NULL ||
                    c->failure == SPECDB_FAILURE_MAP_FAILED || c->failure == SPECDB_FAILURE_STRTOL;
    if (!strcmp(name, "dlopen")) c->sets_errno = 0;
    snprintf(c->source, sizeof c->source, "reviewed C/POSIX contract: %s", name);
    const char *e = "return value is not an error indicator";
    switch (c->failure) {
    case SPECDB_FAILURE_NEGATIVE: e = "failure is negative; zero is a valid success value"; break;
    case SPECDB_FAILURE_NULL: e = "failure returns a null pointer"; break;
    case SPECDB_FAILURE_MAP_FAILED: e = "failure returns MAP_FAILED, not a null pointer"; break;
    case SPECDB_FAILURE_NONZERO: e = "failure returns a nonzero error code directly"; break;
    case SPECDB_FAILURE_STRTOL: e = "conversion requires end-pointer validation; range failure is reported through errno"; break;
    default: break;
    }
    snprintf(c->evidence, sizeof c->evidence, "%s", e);
    return 1;
}

int specdb_open_readonly(const char *path, sqlite3 **out)
{
    *out = NULL;
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "SELECT f.id FROM functions f JOIN function_sections s ON s.function_id=f.id LEFT JOIN function_aliases a ON a.function_id=f.id LIMIT 1", -1, &stmt, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW) { sqlite3_close(db); return -1; }
    *out = db;
    return 0;
}

static void signature_properties(const char *name, struct specdb_contract *c)
{
    const char *fn = strstr(c->prototype, name);
    if (!fn) return;
    const char *star = memchr(c->prototype, '*', (size_t)(fn - c->prototype));
    c->returns_void = !star && strstr(c->prototype, "void") && strstr(c->prototype, "void") < fn;
    if (c->returns_void) c->failure = SPECDB_FAILURE_NONE;
    if (!strstr(fn, "...") && !strstr(fn, "va_list")) return;
    const char *arg = strchr(fn, '(');
    if (!arg) return;
    arg++;
    int index = 0, depth = 0;
    for (const char *p = arg; *p; p++) {
        if (*p == '(' || *p == '[') depth++;
        if (*p == ']' || (*p == ')' && depth)) depth--;
        if ((!depth && (*p == ',' || *p == ')'))) {
            size_t len = (size_t)(p - arg);
            char param[512];
            if (len < sizeof param) {
                memcpy(param, arg, len); param[len] = 0;
                if (strstr(param, "char") && strchr(param, '*')) {
                    const char *end = p;
                    while (end > arg && isspace((unsigned char)end[-1])) end--;
                    const char *begin = end;
                    while (begin > arg && ident(begin[-1])) begin--;
                    if ((end - begin == 3 && !memcmp(begin, "fmt", 3)) ||
                        (end - begin == 6 && !memcmp(begin, "format", 6))) c->format_index = index;
                }
            }
            if (*p == ')') break;
            arg = p + 1; index++;
        }
    }
}

/* Infer only from a paragraph attributed to this function, or a single-function
 * page. Shared-page and conditional prose which cannot be resolved stays UNKNOWN. */
static void infer_return(const char *name, const char *text, int single, struct specdb_contract *c)
{
    if (!text || c->returns_void || c->failure != SPECDB_FAILURE_UNKNOWN) return;
    for (const char *p = text; *p;) {
        const char *end = strstr(p, "\n\n");
        if (!end) end = p + strlen(p);
        size_t len = (size_t)(end - p);
        char *para = malloc(len + 1);
        if (!para) return;
        for (size_t i = 0; i < len; i++) para[i] = isspace((unsigned char)p[i]) ? ' ' : (char)tolower((unsigned char)p[i]);
        para[len] = 0;
        char subject[256];
        snprintf(subject, sizeof subject, "%s()", name);
        int attributed = single;
        const char *start = para;
        while (*start == ' ') start++;
        if (!strncmp(start, "the ", 4)) start += 4;
        if (!strncmp(start, subject, strlen(subject))) attributed = 1;
        /* Shared paragraphs containing other calls are ambiguous. */
        if (!single) {
            for (const char *q = para; (q = strstr(q, "()")); q += 2) {
                const char *begin = q;
                while (begin > para && ident(begin[-1])) begin--;
                if ((size_t)(q - begin) != strlen(name) || strncmp(begin, name, strlen(name))) attributed = 0;
            }
        }
        if (strstr(para, "never") || strstr(para, "does not") || strstr(para, "cannot")) attributed = 0;
        if (attributed && (strstr(para, "on error") || strstr(para, "on failure") || strstr(para, "if an error"))) {
            const char *fn = strstr(c->prototype, name);
            int pointer = fn && memchr(c->prototype, '*', (size_t)(fn - c->prototype));
            if (pointer && strstr(para, "map_failed")) c->failure = SPECDB_FAILURE_MAP_FAILED;
            else if (pointer && strstr(para, "null")) c->failure = SPECDB_FAILURE_NULL;
            else if (strstr(para, "-1") || strstr(para, "−1")) c->failure = SPECDB_FAILURE_NEGATIVE;
            else if (strstr(para, "error number") || strstr(para, "error code")) c->failure = SPECDB_FAILURE_NONZERO;
            if (c->failure != SPECDB_FAILURE_UNKNOWN) {
                c->sets_errno = strstr(para, "errno") != NULL;
                snprintf(c->evidence, sizeof c->evidence, "%s", para);
            }
        }
        free(para);
        if (c->failure != SPECDB_FAILURE_UNKNOWN) return;
        p = *end ? end + 2 : end;
    }
}

int specdb_lookup_contract(sqlite3 *db, const char *name, struct specdb_contract *out)
{
    init_contract(out);
    if (!db || !name) return -1;
    const char *sql = "SELECT f.section,s.content,r.content FROM functions f "
        "JOIN function_sections s ON s.function_id=f.id AND s.section_name='SYNOPSIS' "
        "LEFT JOIN function_sections r ON r.function_id=f.id AND r.section_name LIKE '%RETURN VALUE%' "
        "WHERE (f.name=?1 OR f.id IN (SELECT function_id FROM function_aliases WHERE alias_name=?1)) "
        "AND (f.section='2' OR f.section='3' OR f.section='3posix' OR f.section='3p') "
        "ORDER BY (f.name=?1) DESC, CASE f.section WHEN '2' THEN 0 WHEN '3' THEN 1 ELSE 2 END";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    int rc, found = 0;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        const char *syn = (const char *)sqlite3_column_text(st, 1);
        if (!syn || specdb_extract_prototype(syn, name, out->prototype, sizeof out->prototype) != 1) continue;
        signature_properties(name, out);
        /* Counting declaration terminators is intentionally conservative. */
        int declarations = 0;
        for (const char *p = syn; *p; p++) if (*p == ';') declarations++;
        infer_return(name, (const char *)sqlite3_column_text(st, 2), declarations == 1, out);
        if (strstr(out->prototype, "[[deprecated")) out->dangerous = 1;
        snprintf(out->source, sizeof out->source, "man %s %s: SYNOPSIS / RETURN VALUE",
                 (const char *)sqlite3_column_text(st, 0), name);
        found = 1;
        break;
    }
    sqlite3_finalize(st);
    return rc == SQLITE_ROW || rc == SQLITE_DONE ? found : -1;
}
