#include <string.h>

#include "mc_rules.h"

/*
 * Static baseline rules.
 * Later this can be generated from specdb or a config file;
 * the rest of the analyzer only calls mc_rules_lookup().
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

const mc_func_rule *mc_rules_lookup(const char *name) {
    size_t count = sizeof(mc_rules) / sizeof(mc_rules[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(name, mc_rules[i].name) == 0)
            return &mc_rules[i];
    }
    return NULL;
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
