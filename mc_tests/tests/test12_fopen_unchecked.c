#include <stdio.h>

int main(void) {
    FILE *f1 = fopen("a.txt", "r");
    if (!f1) {
        return 1;
    }

    fopen("b.txt", "r");

    return 0;
}
