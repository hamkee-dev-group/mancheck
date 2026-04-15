/* Test: multiple rule categories firing in one file */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_mixed(const char *fmt, int fd) {
    char buf[256];

    /* dangerous */
    gets(buf);

    /* unchecked retval */
    read(fd, buf, sizeof buf);

    /* non-literal format string */
    printf(fmt);

    /* dangerous + format string (literal, so just dangerous) */
    sprintf(buf, "%s", "hi");
}
