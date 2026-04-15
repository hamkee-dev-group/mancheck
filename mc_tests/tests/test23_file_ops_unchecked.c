/* Test: file operations unchecked */
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

void test_file_ops(void) {
    struct stat st;
    stat("/etc/passwd", &st);     /* unchecked */
    chmod("/tmp/x", 0644);        /* unchecked */
    chown("/tmp/x", 0, 0);        /* unchecked */
    unlink("/tmp/x");              /* unchecked */
    rename("/tmp/a", "/tmp/b");    /* unchecked */
    mkdir("/tmp/d", 0755);         /* unchecked */
    rmdir("/tmp/d");               /* unchecked */
    link("/tmp/a", "/tmp/b");      /* unchecked */
    symlink("/tmp/a", "/tmp/b");   /* unchecked */
}
