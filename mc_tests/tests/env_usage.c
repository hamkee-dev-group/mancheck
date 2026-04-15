#include <stdlib.h>

void test_env_simple(void)
{
    const char *p = getenv("PATH");
}

void test_env_nonliteral(const char *name)
{
    const char *v = getenv(name);
}

void test_env_mutate(void)
{
    setenv("LD_PRELOAD", "/tmp/x.so", 1);
    clearenv();
}
