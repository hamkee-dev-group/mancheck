#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "mc_rules.h"
#include "specdb.h"

/*
 * Static baseline rules.
 * The rest of the analyzer only calls mc_rules_lookup().
 * When specdb is loaded, functions not in this table are checked
 * against manpage data for RETVAL_MUST_CHECK inference.
 */

static const mc_func_rule mc_rules[] = {
    /* =====================================================================
     * POSIX I/O – must check return values
     * ===================================================================== */
    { "read",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "write",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pread",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pwrite",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "lseek",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fsync",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fdatasync",     MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* File descriptors / files */
    { "open",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "openat",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "close",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "dup",           MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "dup2",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "dup3",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pipe",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pipe2",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "ftruncate",     MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fchmod",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fchown",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fcntl",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "ioctl",         MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* C stdio */
    { "fopen",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "freopen",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fdopen",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fclose",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fflush",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fread",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fwrite",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fgetc",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fputc",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fgets",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fputs",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fseek",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "ftell",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "ferror",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "feof",          MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Memory allocation */
    { "malloc",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "calloc",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "realloc",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "strdup",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "strndup",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "posix_memalign",MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "aligned_alloc", MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Sockets / networking */
    { "socket",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "accept",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "accept4",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "connect",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "listen",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "bind",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "send",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "recv",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "sendto",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "recvfrom",      MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "sendmsg",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "recvmsg",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "getsockopt",    MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "setsockopt",    MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "shutdown",      MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Process / system calls */
    { "fork",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "vfork",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "wait",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "waitpid",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "waitid",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "system",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "execve",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "execl",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "execlp",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "execv",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "execvp",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "execvpe",       MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Directory / path operations */
    { "chdir",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fchdir",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "mkdir",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "mkdirat",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "rmdir",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "unlink",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "unlinkat",      MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "rename",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "renameat",      MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "link",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "linkat",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "symlink",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "symlinkat",     MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "readlink",      MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "readlinkat",    MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "realpath",      MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* File status / access */
    { "stat",          MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "fstat",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "lstat",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "access",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "faccessat",     MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Temp files */
    { "mkstemp",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "mkdtemp",       MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Line readers – must check for -1 */
    { "getline",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "getdelim",      MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Time / sleep */
    { "clock_gettime", MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "nanosleep",     MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Threads (POSIX) */
    { "pthread_create",     MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_join",       MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_mutex_init", MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_mutex_lock", MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_mutex_unlock", MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_rwlock_init",  MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_rwlock_rdlock",MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_rwlock_wrlock",MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_rwlock_unlock",MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_cond_init",    MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_cond_wait",    MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_cond_signal",  MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "pthread_cond_broadcast",MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Address resolution / networking helpers */
    { "getaddrinfo",   MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "getnameinfo",   MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "inet_pton",     MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "inet_ntop",     MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* Environment / configuration */
    { "setenv",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "unsetenv",      MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "putenv",        MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "chown",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "chmod",         MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "umask",         MC_FUNC_RULE_RETVAL_MUST_CHECK },

    /* =====================================================================
     * Dangerous / banned-ish functions
     * ===================================================================== */
    { "gets",          MC_FUNC_RULE_DANGEROUS },
    { "strcpy",        MC_FUNC_RULE_DANGEROUS },
    { "strcat",        MC_FUNC_RULE_DANGEROUS },
    { "strncpy",       MC_FUNC_RULE_DANGEROUS },
    { "strncat",       MC_FUNC_RULE_DANGEROUS },
    { "sprintf",       MC_FUNC_RULE_DANGEROUS | MC_FUNC_RULE_FORMAT_STRING },
    { "vsprintf",      MC_FUNC_RULE_DANGEROUS | MC_FUNC_RULE_FORMAT_STRING },
    { "tmpnam",        MC_FUNC_RULE_DANGEROUS },
    { "mktemp",        MC_FUNC_RULE_DANGEROUS },
    { "strtok",        MC_FUNC_RULE_DANGEROUS }, /* re-entrant/thread-safety concern */

    /* scanf-family: format-string + parsing pitfalls */
    { "scanf",         MC_FUNC_RULE_DANGEROUS | MC_FUNC_RULE_FORMAT_STRING },
    { "fscanf",        MC_FUNC_RULE_DANGEROUS | MC_FUNC_RULE_FORMAT_STRING },
    { "sscanf",        MC_FUNC_RULE_DANGEROUS | MC_FUNC_RULE_FORMAT_STRING },

    /* =====================================================================
     * Format string users (printf-family)
     * ===================================================================== */
    { "printf",        MC_FUNC_RULE_FORMAT_STRING },
    { "fprintf",       MC_FUNC_RULE_FORMAT_STRING },
    { "dprintf",       MC_FUNC_RULE_FORMAT_STRING },
    { "snprintf",      MC_FUNC_RULE_FORMAT_STRING | MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "vsnprintf",     MC_FUNC_RULE_FORMAT_STRING | MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "vprintf",       MC_FUNC_RULE_FORMAT_STRING },
    { "vfprintf",      MC_FUNC_RULE_FORMAT_STRING },
    { "asprintf",      MC_FUNC_RULE_FORMAT_STRING | MC_FUNC_RULE_RETVAL_MUST_CHECK },
    { "vasprintf",     MC_FUNC_RULE_FORMAT_STRING | MC_FUNC_RULE_RETVAL_MUST_CHECK }
};

/* =====================================================================
 * specdb integration: optional fallback for RETVAL_MUST_CHECK
 * ===================================================================== */

static sqlite3 *g_specdb = NULL;

/* Simple per-process cache of specdb-derived rules.
 * Entries have dynamically allocated names.
 * A flags value of 0 means "looked up but not interesting" (negative cache). */
struct specdb_cache_entry {
    char     *name;
    unsigned  flags;
};

static struct specdb_cache_entry *g_cache = NULL;
static size_t g_cache_len = 0;
static size_t g_cache_cap = 0;

/* Sentinel rule returned for specdb-derived matches. */
static mc_func_rule g_specdb_rule;

int mc_rules_init_specdb(const char *path)
{
    if (!path)
        return 0;

    if (specdb_open(path, &g_specdb) != 0) {
        fprintf(stderr, "mc_rules: cannot open specdb '%s'\n", path);
        g_specdb = NULL;
        return -1;
    }
    return 0;
}

void mc_rules_close_specdb(void)
{
    if (g_specdb) {
        specdb_close(g_specdb);
        g_specdb = NULL;
    }
    for (size_t i = 0; i < g_cache_len; i++)
        free(g_cache[i].name);
    free(g_cache);
    g_cache = NULL;
    g_cache_len = 0;
    g_cache_cap = 0;
}

static const mc_func_rule *specdb_lookup(const char *name)
{
    if (!g_specdb)
        return NULL;

    /* Check cache first */
    for (size_t i = 0; i < g_cache_len; i++) {
        if (strcmp(name, g_cache[i].name) == 0) {
            if (g_cache[i].flags == 0)
                return NULL;  /* negative cache hit */
            g_specdb_rule.name  = g_cache[i].name;
            g_specdb_rule.flags = g_cache[i].flags;
            return &g_specdb_rule;
        }
    }

    /* Query specdb for all three flag types */
    unsigned flags = 0;

    int has_rv = specdb_function_has_retval(g_specdb, name);
    if (has_rv == 1)
        flags |= MC_FUNC_RULE_RETVAL_MUST_CHECK;

    int is_dangerous = specdb_function_is_dangerous(g_specdb, name);
    if (is_dangerous == 1)
        flags |= MC_FUNC_RULE_DANGEROUS;

    int has_fmt = specdb_function_has_format_string(g_specdb, name);
    if (has_fmt == 1)
        flags |= MC_FUNC_RULE_FORMAT_STRING;

    /* Add to cache */
    if (g_cache_len == g_cache_cap) {
        size_t newcap = g_cache_cap ? g_cache_cap * 2 : 64;
        struct specdb_cache_entry *tmp =
            realloc(g_cache, newcap * sizeof(*tmp));
        if (!tmp)
            return NULL;
        g_cache = tmp;
        g_cache_cap = newcap;
    }

    char *dup = strdup(name);
    if (!dup)
        return NULL;

    g_cache[g_cache_len].name  = dup;
    g_cache[g_cache_len].flags = flags;
    g_cache_len++;

    if (flags == 0)
        return NULL;

    g_specdb_rule.name  = dup;
    g_specdb_rule.flags = flags;
    return &g_specdb_rule;
}

/* ===================================================================== */

const mc_func_rule *mc_rules_lookup(const char *name) {
    size_t count = sizeof(mc_rules) / sizeof(mc_rules[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(name, mc_rules[i].name) == 0)
            return &mc_rules[i];
    }
    return specdb_lookup(name);
}

const char *mc_rules_category(unsigned flags) {
    if (flags & MC_FUNC_RULE_DANGEROUS)
        return "dangerous_function";
    if (flags & MC_FUNC_RULE_FORMAT_STRING)
        return "format_string";
    if (flags & MC_FUNC_RULE_RETVAL_MUST_CHECK)
        return "return_value_check";
    return "other";
}
