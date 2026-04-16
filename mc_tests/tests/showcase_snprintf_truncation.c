/*
 * Showcase: snprintf truncation not detected.
 *
 * snprintf(3) returns the number of characters that WOULD have been
 * written (excluding null), even if output was truncated. Most code
 * ignores this, silently producing truncated strings. Neither cppcheck
 * nor clang-tidy checks for truncation handling.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Case 1: snprintf return value ignored — truncation undetected */
void bad_snprintf_ignored(char *buf, size_t sz, const char *name) {
    snprintf(buf, sz, "Hello, %s! Welcome to the system.", name);
    /* BUG: if output was truncated, buf has partial data */
}

/* Case 2: snprintf return stored but not compared to buffer size */
int bad_snprintf_stored(char *buf, size_t sz, int id, const char *msg) {
    int n = snprintf(buf, sz, "[%05d] %s", id, msg);
    if (n < 0) {
        return -1;
    }
    /* BUG: n >= sz means truncation occurred, not checked */
    return 0;
}

/* Case 3: chained snprintf without tracking remaining space */
void bad_snprintf_chain(char *buf, size_t sz, const char *a, const char *b) {
    int n = snprintf(buf, sz, "first=%s,", a);
    /* BUG: not checking n >= sz before second snprintf */
    /* BUG: not adjusting buf+n, sz-n */
    snprintf(buf + n, sz - n, "second=%s", b);
    /* if first snprintf truncated, n > sz, and sz-n underflows (size_t!) */
}

/* Case 4: using snprintf return as strlen — correct only if not truncated */
void bad_snprintf_as_strlen(char *out, size_t outsz) {
    int len = snprintf(out, outsz, "some long format string with data: %d", 42);
    /* BUG: if len >= outsz, actual strlen is outsz-1, not len */
    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, out, len + 1);  /* reads past buffer if truncated */
        free(copy);
    }
}

/* Correct pattern for reference: */
int good_snprintf(char *buf, size_t sz, const char *name) {
    int n = snprintf(buf, sz, "Hello, %s!", name);
    if (n < 0) {
        return -1;          /* encoding error */
    }
    if ((size_t)n >= sz) {
        return -1;          /* truncation detected */
    }
    return 0;
}
