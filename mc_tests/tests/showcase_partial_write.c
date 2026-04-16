/*
 * Showcase: partial write / short read not handled.
 *
 * write(2) and read(2) man pages document that they may transfer fewer
 * bytes than requested (short write/read). Not looping is a bug that
 * causes data corruption or loss. Neither cppcheck nor clang-tidy checks
 * for correct write/read loop patterns.
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Case 1: single write without loop — may lose data on pipes, sockets,
 * or signals interrupting the call. write(2) says: "the number of bytes
 * written may be less than count". */
int bad_write(int fd, const char *data, size_t len) {
    ssize_t n = write(fd, data, len);
    if (n < 0) {
        return -1;
    }
    /* BUG: n < len is silently ignored — data truncated */
    return 0;
}

/* Case 2: single read to fill a buffer — same problem.
 * read(2) says: "it is not an error if this number is smaller than the
 * number of bytes requested". */
int bad_read(int fd, char *buf, size_t len) {
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
        return -1;
    }
    /* BUG: got fewer bytes than expected, caller assumes buf is full */
    return 0;
}

/* Case 3: send on a socket — same partial-write semantics.
 * send(2) says: "the call may ... transfer fewer bytes". */
#include <sys/socket.h>
int bad_send(int sockfd, const char *data, size_t len) {
    ssize_t n = send(sockfd, data, len, 0);
    if (n < 0) {
        return -1;
    }
    /* BUG: partial send not handled */
    return 0;
}

/* Case 4: fwrite — same issue with partial writes.
 * fwrite(3) says: "returns the number of items successfully written,
 * which may be less than nitems if an error occurs". */
int bad_fwrite(FILE *fp, const void *data, size_t len) {
    size_t n = fwrite(data, 1, len, fp);
    if (n == 0) {
        return -1;
    }
    /* BUG: 0 < n < len means partial write, data lost */
    return 0;
}

/* Correct write loop for reference: */
int good_write(int fd, const char *data, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, data + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;  /* interrupted, retry */
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}
