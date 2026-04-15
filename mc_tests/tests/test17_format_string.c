/* Test: non-literal format string warnings */
#include <stdio.h>

void test_format_ok(void) {
    printf("hello %d\n", 42);           /* literal format: no warning */
    fprintf(stderr, "err: %s\n", "x");  /* literal format: no warning */
}

void test_format_bad(const char *fmt) {
    printf(fmt);                         /* non-literal format */
    fprintf(stderr, fmt);               /* non-literal format */
    snprintf(NULL, 0, fmt, 1);          /* non-literal format (also retval) */
}
