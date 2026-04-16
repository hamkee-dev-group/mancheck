/*
 * Showcase: resource cleanup contracts from man pages.
 *
 * Man pages document which cleanup function matches which allocation.
 * Using the wrong cleanup function is UB or a resource leak. Neither
 * cppcheck nor clang-tidy checks allocation/deallocation pairing
 * beyond basic malloc/free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <netdb.h>
#include <sys/socket.h>

/* Case 1: opendir must be closed with closedir, not close or fclose.
 * opendir(3) says "The directory stream ... should later be closed
 * by closedir()". */
void bad_dir_cleanup(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    /* ... use d ... */
    fclose((FILE *)d);      /* BUG: should be closedir(d) */
}

/* Case 2: fopen must be closed with fclose, not close.
 * fclose(3) is for FILE*, close(2) is for fd. */
void bad_file_cleanup(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    /* ... read from fp ... */
    close(fileno(fp));      /* BUG: should be fclose(fp), this leaks the FILE */
}

/* Case 3: socket must be closed with close, not fclose.
 * socket(2) returns a file descriptor. */
void bad_socket_cleanup(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    /* ... use socket ... */
    free((void *)(long)fd); /* BUG: should be close(fd) */
}

/* Case 4: getaddrinfo must be freed with freeaddrinfo, not free.
 * getaddrinfo(3) says "use freeaddrinfo() to free the linked list". */
void bad_addrinfo_cleanup(const char *host) {
    struct addrinfo *res;
    if (getaddrinfo(host, "80", NULL, &res) != 0) return;
    /* ... use res ... */
    free(res);              /* BUG: should be freeaddrinfo(res) */
}

/* Case 5: dlopen must be closed with dlclose, not close or free.
 * dlopen(3) says "use dlclose() to close the handle". */
void bad_dl_cleanup(const char *lib) {
    void *handle = dlopen(lib, RTLD_LAZY);
    if (!handle) return;
    /* ... use handle ... */
    free(handle);           /* BUG: should be dlclose(handle) */
}

/* Case 6: popen must be closed with pclose, not fclose.
 * popen(3) says "must be closed by pclose()". */
void bad_popen_cleanup(void) {
    FILE *pp = popen("ls", "r");
    if (!pp) return;
    char buf[256];
    while (fgets(buf, sizeof buf, pp)) { /* use it */ }
    fclose(pp);             /* BUG: should be pclose(pp) */
}

/* Correct patterns for reference: */
void good_cleanup(const char *path, const char *host) {
    DIR *d = opendir(path);
    if (d) { closedir(d); }

    FILE *fp = fopen(path, "r");
    if (fp) { fclose(fp); }

    struct addrinfo *res;
    if (getaddrinfo(host, "80", NULL, &res) == 0) {
        freeaddrinfo(res);
    }
}
