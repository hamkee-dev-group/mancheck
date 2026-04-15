#include <stdlib.h>
#include <unistd.h>

void test_double_close(int fd)
{
    char *p = malloc(16);
    if (!p) return;

    close(fd);
    close(fd);    /* should trigger */

    free(p);
    free(p);      /* should trigger */
}
