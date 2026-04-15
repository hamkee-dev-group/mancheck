/* Test: stdio functions with unchecked returns */
#include <stdio.h>

void test_stdio(FILE *f, char *buf) {
    fread(buf, 1, 10, f);       /* unchecked */
    fwrite(buf, 1, 10, f);      /* unchecked */
    fgets(buf, 10, f);          /* unchecked */
    fputs("hello", f);          /* unchecked */
    fseek(f, 0, SEEK_SET);      /* unchecked */
    ftell(f);                    /* unchecked */
    fflush(f);                   /* unchecked */
    fclose(f);                   /* unchecked */
}
