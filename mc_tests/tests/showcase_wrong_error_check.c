/*
 * Showcase: wrong error-checking patterns.
 *
 * Man pages document HOW to check errors, not just WHETHER to check.
 * These are real bugs that cppcheck and clang-tidy miss because they
 * only verify the return value is "used", not that the error protocol
 * is correct.
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

/* Case 1: strtol — must check errno, not just return value.
 * strtol(3) returns 0 on failure AND for "0" input.
 * Checking == -1 is completely wrong.  */
int bad_strtol(const char *s) {
    long n = strtol(s, NULL, 10);
    if (n == -1) {          /* BUG: strtol does not return -1 on error */
        return -1;
    }
    return (int)n;
}

/* Case 2: open — returns -1 on error, NOT NULL.
 * Comparing against NULL is a type confusion bug. */
int bad_open_check(void) {
    int fd = open("/tmp/showcase", O_RDONLY);
    if (fd == 0) {          /* BUG: 0 is a valid fd; should check == -1 */
        perror("open");
        return -1;
    }
    close(fd);
    return 0;
}

/* Case 3: pthread_create — returns error code directly, NOT via errno.
 * Checking errno after pthread_create is wrong. */
void bad_pthread_check(void) {
    pthread_t t;
    pthread_create(&t, NULL, (void*(*)(void*))bad_strtol, NULL);
    if (errno != 0) {       /* BUG: pthread_create doesn't set errno */
        fprintf(stderr, "thread failed\n");
    }
    pthread_join(t, NULL);
}

/* Case 4: fread — returns number of items read, not an error code.
 * Checking == -1 is wrong; should check < nmemb and then use feof/ferror. */
int bad_fread_check(FILE *fp) {
    char buf[128];
    size_t n = fread(buf, 1, sizeof buf, fp);
    if ((int)n == -1) {     /* BUG: fread never returns -1 */
        return -1;
    }
    return 0;
}

/* Case 5: getaddrinfo — returns its own error codes, NOT errno.
 * Using perror() after getaddrinfo failure is wrong. */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int bad_getaddrinfo_check(const char *host) {
    struct addrinfo *res;
    int ret = getaddrinfo(host, "80", NULL, &res);
    if (ret != 0) {
        perror("getaddrinfo");   /* BUG: should use gai_strerror(ret) */
        return -1;
    }
    freeaddrinfo(res);
    return 0;
}
