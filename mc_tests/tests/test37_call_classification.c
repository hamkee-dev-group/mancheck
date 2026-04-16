/* Test: call-classification branches for for/do/switch/comma/forwarding */
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

/* Direct call in for-statement condition: checked */
void test_for_condition(int fd) {
    char buf[64];
    for (; read(fd, buf, 1) > 0; ) {
        break;
    }
}

/* Direct call in do-while condition: checked */
void test_do_condition(int fd) {
    char buf[64];
    do {
        /* loop */
    } while (read(fd, buf, 1) > 0);
}

/* Stored variable checked in switch condition */
void test_switch_check(void) {
    int fd = open("/tmp/x", 0);
    switch (fd) {
    case -1: return;
    default: break;
    }
    close(fd);
}

/* Stored variable checked in for condition */
void test_for_stored_check(int fd) {
    char buf[64];
    ssize_t n = read(fd, buf, sizeof buf);
    for (int i = 0; i < n; i++) {
        /* use buf */
    }
}

/* Stored variable checked in do-while condition */
void test_do_stored_check(int fd) {
    char buf[64];
    ssize_t n = read(fd, buf, sizeof buf);
    do {
        /* loop */
    } while (n > 0);
}

/* Comma expression: call result consumed, treated as stored */
int test_comma_expr(int fd) {
    char buf[64];
    return (read(fd, buf, 1), 0);
}

/* Argument forwarding: value passed to another call, treated as stored */
void test_arg_forwarding(void) {
    free(malloc(100));
}

/* Pointer declarator storage: var name extracted through pointer_declarator */
void test_pointer_store_checked(void) {
    char *p = malloc(100);
    if (!p) return;
    free(p);
}

void test_pointer_store_unchecked(void) {
    char *p = malloc(100);
    free(p);
}
