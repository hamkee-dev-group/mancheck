#include <unistd.h>

static ssize_t my_read(int fd, void *buf, size_t len) {
    return read(fd, buf, len);
}

int main(void) {
    char buf[10];
    int fd = 3;
    my_read(fd, buf, sizeof buf);
    return 0;
}
