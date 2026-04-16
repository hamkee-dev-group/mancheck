/*
 * Showcase: non-portable / Linux-specific functions used without guards.
 *
 * By diffing section 3 (Linux) vs 3posix (POSIX), mancheck can detect
 * functions that are Linux-specific or require feature test macros.
 * Neither cppcheck nor clang-tidy checks portability against POSIX.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>

/* Case 1: pipe2 is Linux-specific (not in POSIX) */
int use_pipe2(void) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) {  /* Linux-only, not in 3posix */
        return -1;
    }
    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
}

/* Case 2: epoll is Linux-specific */
#include <sys/epoll.h>

int use_epoll(void) {
    int efd = epoll_create1(EPOLL_CLOEXEC); /* Linux-only */
    if (efd == -1)
        return -1;
    close(efd);
    return 0;
}

/* Case 3: POSIX obsolescent functions — should use modern replacements */
#include <strings.h>

void use_obsolescent(char *dst, const char *src, size_t n) {
    bcopy(src, dst, n);     /* POSIX obsolescent: use memmove */
    bzero(dst, n);          /* POSIX obsolescent: use memset */
}

/* Case 4: getline requires _POSIX_C_SOURCE >= 200809L */
int use_getline(FILE *fp) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread = getline(&line, &len, fp); /* needs feature test macro */
    if (nread == -1) {
        free(line);
        return -1;
    }
    free(line);
    return 0;
}

/* Case 5: unshare is Linux-specific (namespaces) */
int use_unshare(void) {
    if (unshare(CLONE_NEWNS) == -1) {       /* Linux-only */
        return -1;
    }
    return 0;
}
