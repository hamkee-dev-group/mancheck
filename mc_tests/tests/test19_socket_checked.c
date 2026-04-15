/* Test: networking calls properly checked -- should produce no warnings */
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>

void test_socket_checked(void) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return;

    if (listen(fd, 5) < 0)
        return;

    int client = accept(fd, NULL, NULL);
    if (client < 0) return;

    ssize_t n = send(client, "hi", 2, 0);
    if (n < 0) return;

    if (close(client) != 0)
        fprintf(stderr, "close error\n");

    if (close(fd) != 0)
        fprintf(stderr, "close error\n");
}
