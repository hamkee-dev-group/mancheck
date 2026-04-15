/* Test: a perfectly clean file -- zero warnings expected */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int process(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char buf[256];
    while (fgets(buf, sizeof buf, f) != NULL) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
        printf("%s\n", buf);
    }

    if (fclose(f) != 0)
        return -1;

    return 0;
}
