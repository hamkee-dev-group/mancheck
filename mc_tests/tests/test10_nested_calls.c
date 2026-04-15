#include <unistd.h>

static ssize_t total_read(int fd, void *buf, size_t len) {
    return read(fd, buf, len);
}

int main(void) {
    char buf[10];
    int fd = 3;

    ssize_t n = total_read(fd, buf, sizeof buf);
    if (n > 0)
        write(fd, buf, (size_t)n);

    return 0;
}
