#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void cat(int fd)
{
    char buffer[4096];
    ssize_t bytesRead;

    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
    {
        if (write(STDOUT_FILENO, buffer, bytesRead) != bytesRead)
        {
            fprintf(stderr, "Error: Failed to write to stdout\n");
            exit(1);
        }
    }

    if (bytesRead < 0)
    {
        fprintf(stderr, "Error: Failed to read from file descriptor\n");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    int fd;
    if (argc == 1)
    {
        cat(STDIN_FILENO);
    }
    else
    {
        for (int i = 1; i < argc; ++i)
        {
            if ((fd = open(argv[i], O_RDONLY)) < 0)
            {
                fprintf(stderr, "cat: cannot open %s: %s\n", argv[i], strerror(errno));
                exit(1);
            }
            cat(fd);
            if (close(fd) < 0)
            {
                fprintf(stderr, "cat: error closing file %s: %s\n", argv[i], strerror(errno));
                exit(1);
            }
        }
    }
    /* (Final) TODO END */
    return 0;
}