/*
 * Showcase: missing or wrong headers for function calls.
 *
 * Man page SYNOPSIS documents required #include for each function.
 * mancheck can verify the right header is present. Compilers may
 * still compile the code (implicit declarations in older C standards,
 * or transitive includes) but the code is technically incorrect.
 */
#include <stdio.h>
#include <stdlib.h>
/* deliberately NOT including: <string.h>, <unistd.h>, <fcntl.h> */

int main(void) {
    char buf[256];

    /* Case 1: memset requires <string.h> — not included */
    memset(buf, 0, sizeof buf);

    /* Case 2: read requires <unistd.h> — not included */
    ssize_t n = read(0, buf, sizeof buf);
    if (n < 0) return 1;

    /* Case 3: strlen requires <string.h> — not included */
    size_t len = strlen(buf);

    /* Case 4: open requires <fcntl.h> — not included */
    int fd = open("/dev/null", 0);
    if (fd >= 0) {
        /* close requires <unistd.h> — not included */
        close(fd);
    }

    (void)len;
    return 0;
}
