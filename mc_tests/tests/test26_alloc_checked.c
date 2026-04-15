/* Test: allocation functions properly checked -- no retval warnings expected */
#include <stdlib.h>
#include <string.h>

void test_alloc_checked(size_t n) {
    void *p = malloc(n);
    if (!p) return;

    void *q = calloc(n, sizeof(int));
    if (q == NULL) return;

    void *r = realloc(p, n * 2);
    if (r == NULL) return;

    char *s = strdup("hello");
    if (s == NULL) return;

    free(s);
    free(r);
    free(q);
}
