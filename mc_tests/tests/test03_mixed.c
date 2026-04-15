#include <unistd.h>
#include <stdlib.h>

int main(void) {
    char buf[10];
    int fd = 3;
    ssize_t n = read(fd, buf, sizeof buf);
    if (n < 0) {
        return 1;
    }
    if (write(fd, buf, (size_t)n) < 0) {
        return 1;
    }
    write(fd, buf, 5);
    return 0;
}
