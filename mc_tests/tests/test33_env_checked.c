/* Test: env calls that are checked -- still flagged as env usage (advisory) */
#include <stdlib.h>

int test_env_checked(void) {
    const char *p = getenv("HOME");
    if (!p) return -1;

    if (setenv("MY_VAR", "val", 1) != 0)
        return -1;

    return 0;
}
