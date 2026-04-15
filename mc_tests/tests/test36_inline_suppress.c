#include <unistd.h>
#include <stdio.h>

void test_inline_suppress(int fd) {
    char buf[64];

    read(fd, buf, sizeof buf); // mc:ignore

    // NOLINT(mancheck)
    read(fd, buf, sizeof buf);

    read(fd, buf, sizeof buf);

    gets(buf);

    /* false positive: marker in string literal must NOT suppress */
    char *s1 = "// mc:ignore";
    read(fd, buf, sizeof buf);

    /* false negative: marker after URL in string must still suppress */
    char *s2 = "http://example.com"; read(fd, buf, sizeof buf); // mc:ignore

    (void)s1; (void)s2;
}
