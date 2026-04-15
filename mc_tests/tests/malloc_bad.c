#include <stdlib.h>

struct Foo {
    int x;
    long y;
};

void test_malloc_bad(size_t n)
{
    struct Foo *p = malloc(sizeof(p));             /* BAD */
    struct Foo *q = malloc(n * sizeof(q));         /* BAD */

    struct Foo *ok1 = malloc(sizeof(*ok1));        /* OK  */
    struct Foo *ok2 = malloc(n * sizeof(*ok2));    /* OK  */
}
