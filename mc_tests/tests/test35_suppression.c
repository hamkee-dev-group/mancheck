#include <unistd.h>
#include <stdio.h>

void test_suppress(int fd) {
    char buf[64];
    read(fd, buf, sizeof buf);
    gets(buf);
}
