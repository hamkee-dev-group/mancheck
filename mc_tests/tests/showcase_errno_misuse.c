/*
 * Showcase: errno protocol violations.
 *
 * Man pages document whether a function sets errno and how to use it.
 * Common bugs: checking errno without resetting it first, checking errno
 * for functions that don't set it, ignoring the documented errno protocol.
 * Neither cppcheck nor clang-tidy validates errno usage patterns.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

/* Case 1: strtol requires errno=0 before call, then check errno after.
 * Checking return value alone is insufficient per strtol(3). */
long bad_strtol_no_errno_reset(const char *s) {
    /* BUG: errno not set to 0 before call */
    long val = strtol(s, NULL, 10);
    /* stale errno from a prior call could cause a false error */
    if (errno == ERANGE) {
        return -1;
    }
    return val;
}

/* Case 2: strtod — same errno protocol as strtol.
 * Must set errno=0, call, then check errno. */
double bad_strtod(const char *s) {
    /* BUG: errno not reset */
    double val = strtod(s, NULL);
    if (val == 0.0) {       /* BUG: 0.0 is valid; should check errno */
        return -1.0;
    }
    return val;
}

/* Case 3: fopen sets errno but many devs use perror without checking
 * the return value first. */
void bad_fopen_errno(const char *path) {
    FILE *fp = fopen(path, "r");
    /* correct: checks return, but many codebases just check errno */
    if (errno != 0) {       /* BUG: errno may be stale; check fp==NULL instead */
        perror("fopen");
        return;
    }
    if (fp) fclose(fp);
}

/* Case 4: malloc does NOT reliably set errno on all platforms per POSIX.
 * Should check return==NULL, not errno. */
void bad_malloc_errno(size_t n) {
    void *p = malloc(n);
    if (errno == ENOMEM) {  /* BUG: check p==NULL, not errno */
        fprintf(stderr, "out of memory\n");
        return;
    }
    free(p);
}

/* Case 5: printf/fprintf return value is character count or negative.
 * Checking errno after printf is unreliable per the spec. */
void bad_printf_errno(void) {
    printf("hello\n");
    if (errno != 0) {       /* BUG: printf failure is via return value, not errno */
        perror("printf");
    }
}

/* Correct pattern for reference: */
long good_strtol(const char *s) {
    char *endptr;
    errno = 0;              /* reset before call */
    long val = strtol(s, &endptr, 10);
    if (errno != 0) {       /* now errno is meaningful */
        return -1;
    }
    if (endptr == s) {      /* no digits found */
        return -1;
    }
    return val;
}
