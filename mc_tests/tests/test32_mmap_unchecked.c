/* Test: mmap is not yet in rules table; ftruncate and lseek are */
#include <sys/mman.h>
#include <unistd.h>

void test_mmap(int fd) {
    /* mmap/munmap are not in the rules table, so no retval warning */
    mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    munmap(NULL, 4096);

    /* ftruncate and lseek ARE in the rules table */
    ftruncate(fd, 4096);        /* unchecked */
    lseek(fd, 0, SEEK_SET);     /* unchecked */
}
