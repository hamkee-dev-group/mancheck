/* Test: dup/pipe calls unchecked */
#include <unistd.h>
#include <fcntl.h>

void test_dup_pipe(int fd) {
    int pipefd[2];

    dup(fd);                     /* unchecked */
    dup2(fd, 1);                 /* unchecked */
    pipe(pipefd);                /* unchecked */
    fcntl(fd, F_GETFL);          /* unchecked */
}
