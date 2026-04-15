/* Test: dangerous function warnings */
#include <stdio.h>
#include <string.h>

void test_dangerous(char *dst, const char *src) {
    gets(dst);                   /* dangerous */
    strcpy(dst, src);            /* dangerous */
    strcat(dst, src);            /* dangerous */
    strncpy(dst, src, 10);      /* dangerous */
    strncat(dst, src, 10);      /* dangerous */
    sprintf(dst, "%s", src);    /* dangerous + format_string (literal ok) */
    strtok(dst, ":");           /* dangerous */
}
