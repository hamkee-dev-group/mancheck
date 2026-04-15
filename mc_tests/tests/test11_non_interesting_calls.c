#include <stdio.h>
#include <string.h>

int main(void) {
    char buf[16] = "hello";
    printf("%s\n", buf);
    memcpy(buf, "world", 6);
    return 0;
}
