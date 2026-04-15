#include <stdlib.h>

int main(void) {
    char *p = malloc(10);
    if (!p) {
        return 1;
    }
    realloc(p, 20);
    free(p);
    return 0;
}
