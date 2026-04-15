#include <unistd.h>
#include <sys/types.h>

int main(void) {
    char buf[16];
    int fd = 0;
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        ssize_t w = write(1, buf, (size_t)n);
        if (w < 0) {
            return 1;
        }
    }
    return 0;
}
