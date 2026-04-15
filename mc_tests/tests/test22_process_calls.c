/* Test: process/system calls unchecked */
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

void test_process_unchecked(void) {
    fork();                  /* unchecked */
    wait(NULL);              /* unchecked */
    waitpid(-1, NULL, 0);   /* unchecked */
    execve("/bin/sh", NULL, NULL);  /* unchecked */
    system("ls");            /* unchecked */
    chdir("/tmp");           /* unchecked */
}
