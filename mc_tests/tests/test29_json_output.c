/* Test: used for verifying --json mode */
#include <unistd.h>
#include <stdlib.h>

void test_json(int fd) {
    char buf[10];
    read(fd, buf, 10);
    malloc(42);
}
