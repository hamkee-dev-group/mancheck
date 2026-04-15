#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fd1 = open("foo.txt", O_RDONLY);
    if (fd1 < 0) {
        return 1;
    }

    open("bar.txt", O_RDONLY);

    int fd2 = open("baz.txt", O_RDONLY);
    if (fd2 >= 0) {
        close(fd2);
    }

    return 0;
}
