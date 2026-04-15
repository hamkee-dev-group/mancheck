/* Test: return values used in binary expressions / comparisons -- checked */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

void test_binary_check(int fd) {
    char buf[64];

    /* Comparisons in conditions -- should be recognized as checked */
    if (read(fd, buf, sizeof buf) < 0)
        return;

    if (malloc(10) != NULL)
        return;

    while (fgets(buf, sizeof buf, stdin) != NULL) {
        /* ok */
    }

    /* Ternary / conditional usage */
    int x = (read(fd, buf, 1) > 0) ? 1 : 0;
    (void)x;
}
