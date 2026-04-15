#include <unistd.h>

#define CHECKED_READ(fd, buf, len)                \
    do {                                          \
        ssize_t _n = read((fd), (buf), (len));    \
        if (_n < 0) {                             \
            return 1;                             \
        }                                         \
    } while (0)

int main(void) {
    char buf[10];
    int fd = 3;

    CHECKED_READ(fd, buf, sizeof buf);
    read(fd, buf, sizeof buf);

    return 0;
}
