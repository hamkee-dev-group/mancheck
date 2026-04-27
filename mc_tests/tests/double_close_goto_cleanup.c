#include <unistd.h>

void test_cleanup_label(int fd, int fail)
{
    if (fail) {
        close(fd);
        goto out;
    }
    close(fd);
out:
    return;
}
