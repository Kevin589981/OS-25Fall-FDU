#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    if (argc != 2) {
        fprintf(stderr, "Usage: cat <filename>\n");
        return 1;
    }
    
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cat: cannot open %s\n", argv[1]);
        return 1;
    }
    
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            fprintf(stderr, "cat: write error\n");
            close(fd);
            return 1;
        }
    }
    
    if (n < 0) {
        fprintf(stderr, "cat: read error\n");
        close(fd);
        return 1;
    }
    
    close(fd);
    /* (Final) TODO END */
    return 0;
}