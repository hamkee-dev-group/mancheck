/* Test: return values stored but not necessarily checked -- not flagged */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

void test_stored(int fd) {
    char buf[64];
    ssize_t n = read(fd, buf, sizeof buf);   /* stored: ok */
    int rc = close(fd);                       /* stored: ok */
    void *p = malloc(100);                    /* stored: ok */
    FILE *f = fopen("/tmp/x", "r");           /* stored: ok */
    (void)n; (void)rc; (void)p; (void)f;
}
