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

void test_reassigned_close(void)
{
    int fd = open("/tmp/a", 0);
    close(fd);
    fd = open("/tmp/b", 0);
    close(fd);    /* must NOT trigger: fd was reassigned */
}

void test_reassigned_free(void)
{
    char *p = malloc(16);
    free(p);
    p = malloc(16);
    free(p);      /* must NOT trigger: p was reassigned */
}
