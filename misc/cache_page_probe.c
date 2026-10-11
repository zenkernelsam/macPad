#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[3], "pread") && strcmp(argv[3], "map"))) {
        fprintf(stderr, "usage: %s <cache-file> <offset> <pread|map>\n", argv[0]);
        return 2;
    }
    char *end;
    unsigned long long offset = strtoull(argv[2], &end, 0);
    if (*end || (offset & 0x3fff)) return 2;
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { fprintf(stderr, "open errno=%d\n", errno); return 1; }
    struct stat st;
    if (fstat(fd, &st)) { fprintf(stderr, "fstat errno=%d\n", errno); return 1; }
    fprintf(stderr, "file size=0x%llx offset=0x%llx\n", (unsigned long long)st.st_size, offset);
    if (strcmp(argv[3], "pread") == 0) {
        unsigned char byte;
        ssize_t n = pread(fd, &byte, 1, (off_t)offset);
        fprintf(stderr, "pread n=%zd errno=%d byte=0x%02x\n", n, n < 0 ? errno : 0, n == 1 ? byte : 0);
        return n == 1 ? 0 : 1;
    }
    void *p = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, (off_t)offset);
    if (p == MAP_FAILED) { fprintf(stderr, "mmap errno=%d\n", errno); return 1; }
    fprintf(stderr, "mmap addr=%p, reading first byte\n", p);
    unsigned char byte = *(volatile unsigned char *)p;
    fprintf(stderr, "mmap byte=0x%02x\n", byte);
    munmap(p, 0x4000);
    close(fd);
    return 0;
}
