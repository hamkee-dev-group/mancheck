#include <unistd.h>
#include <stdio.h>

void test_inline_comment_only_chain(int fd) {
    char buf[64];

    // mc:ignore
    // NOLINT(mancheck)
    read(fd, buf, sizeof buf);

    gets(buf);
}
