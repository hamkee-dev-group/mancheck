#include <unistd.h>

int main(void) {
    char buf[10];
    int fd = 3;
    (void)read(fd, buf, sizeof buf);
    return 0;
}
