#include <stdlib.h>

int main(void) {
    char *p = malloc(10);
    if (!p) {
        return 1;
    }
    malloc(20);
    return 0;
}
