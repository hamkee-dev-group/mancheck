/* Test: stored-but-not-checked analysis */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

void test_stored_unchecked(void) {
    int fd = open("/tmp/x", 0);       /* stored but never checked */
    char buf[64];
    read(fd, buf, sizeof buf);
    close(fd);
}

void test_stored_checked(void) {
    int fd = open("/tmp/x", 0);       /* stored and checked: OK */
    if (fd < 0) return;
    char buf[64];
    read(fd, buf, sizeof buf);
    close(fd);
}

void test_stored_returned(int flag) {
    FILE *f = fopen("/tmp/x", "r");   /* stored and returned: OK */
    return;
}

void test_stored_void_ack(void) {
    void *p = malloc(100);            /* stored and (void)-acknowledged: OK */
    (void)p;
}

void test_stored_passed(void) {
    void *p = malloc(100);            /* stored and passed to free: OK */
    free(p);
}

void test_assign_unchecked(int fd) {
    char *p;
    p = malloc(100);                  /* assigned but never checked */
    free(p);
}
