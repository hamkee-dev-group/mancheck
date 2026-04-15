#include <unistd.h>

int main(void) {
    char buf[10];
    int fd = 3; read(fd, buf, sizeof buf); write(fd, buf, 5);
    return 0;
}
