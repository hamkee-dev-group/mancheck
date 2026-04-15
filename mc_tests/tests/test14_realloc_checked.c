#include <stdlib.h>

int main(void) {
    char *p = malloc(10);
    if (!p) {
        return 1;
    }
    p = realloc(p, 20);
    if (!p) {
        return 1;
    }
    free(p);
    return 0;
}
