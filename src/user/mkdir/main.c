#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    if (argc < 2) {
        fprintf(stderr, "Usage: mkdir <dir>...\n");
        exit(1);
    }
    
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) < 0) {
            fprintf(stderr, "mkdir: cannot create directory '%s'\n", argv[i]);
            exit(1);
        }
    }
    /* (Final) TODO END */
    exit(0);
}