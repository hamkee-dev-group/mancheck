#include "maybe_gets.h"

void run_c11(void) {
    char buf[64];
    MAYBE_GETS(buf);
}
