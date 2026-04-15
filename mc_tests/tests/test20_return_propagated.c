/* Test: return value propagated to caller -- not unchecked */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

ssize_t my_read(int fd, void *buf, size_t n) {
    return read(fd, buf, n);    /* propagated: ok */
}

FILE *my_fopen(const char *path) {
    return fopen(path, "r");    /* propagated: ok */
}

void *my_malloc(size_t n) {
    return malloc(n);           /* propagated: ok */
}
