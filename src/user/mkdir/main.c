#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    int i;

    if (argc < 2)
    {
        printf("Usage: mkdir <directory1> <directory2> ...\n");
        exit(1);
    }

    for (i = 1; i < argc; i++)
    {
        if (mkdir(argv[i], 0) < 0)
        {
            fprintf(stderr, "mkdir: failed to create directory '%s': %s\n", argv[i], strerror(errno));
            continue;
        }
        else
        {
            printf("Directory '%s' created successfully.\n", argv[i]);
        }
    }
    /* (Final) TODO END */
    exit(0);
}