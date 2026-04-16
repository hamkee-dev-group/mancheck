#include <unistd.h>
#include <stdio.h>

void test_nolint_next_warn(int fd) {
    char buf[64];

    // NOLINT(mancheck)

    // comment-only spacer
    read(fd, buf, sizeof buf);
    read(fd, buf, sizeof buf);
    read(fd, buf, sizeof buf); //   NOLINT(mancheck)
    gets(buf);
}
