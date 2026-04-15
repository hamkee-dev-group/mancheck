/* Test: networking calls with unchecked returns */
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

void test_socket_unchecked(void) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;

    socket(AF_INET, SOCK_STREAM, 0);        /* unchecked */
    bind(3, (struct sockaddr *)&addr,
         sizeof(addr));                      /* unchecked */
    listen(3, 5);                            /* unchecked */
    accept(3, NULL, NULL);                   /* unchecked */
    connect(3, (struct sockaddr *)&addr,
            sizeof(addr));                   /* unchecked */
    send(3, "x", 1, 0);                     /* unchecked */
    recv(3, &addr, sizeof(addr), 0);        /* unchecked */
    shutdown(3, SHUT_RDWR);                  /* unchecked */
}
