#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void cat_fd(int fd, const char *name)
{
    char buf[512];
    ssize_t n;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            fprintf(stderr, "cat: write error\n");
            exit(1);
        }
        // fprintf(stderr, "cat write succ\n");
    }
    
    if (n < 0) {
        fprintf(stderr, "cat: read error from %s\n", name);
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    /* (Final) TODO BEGIN */
    // 如果没有参数，从标准输入读取
    if (argc == 1) {
        cat_fd(STDIN_FILENO, "stdin");
        return 0;
    }
    
    // 遍历所有参数
    for (int i = 1; i < argc; i++) {
        // 如果参数是 "-"，从标准输入读取
        if (strcmp(argv[i], "-") == 0) {
            cat_fd(STDIN_FILENO, "stdin");
        } else {
            // 否则打开文件
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cat: cannot open %s\n", argv[i]);
                return 1;
            }
            cat_fd(fd, argv[i]);
            close(fd);
        }
    }
    /* (Final) TODO END */
    return 0;
}