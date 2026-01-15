#include <kernel/console.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/uart.h>

struct console cons;

void console_init()
{
    /* (Final) TODO BEGIN */
    // 初始化自旋锁
    init_spinlock(&cons.lock);
    // 初始化信号量，初始值为0（表示当前没有可读数据）
    init_sem(&cons.sem, 0);
    // 初始化索引
    cons.read_idx = 0;
    cons.write_idx = 0;
    cons.edit_idx = 0;
    /* (Final) TODO END */
}

/**
 * console_write - write to uart from the console buffer.
 * @ip: the pointer to the inode (通常在控制台设备中忽略)
 * @buf: the buffer
 * @n: number of bytes to write
 */
isize console_write(Inode *ip, char *buf, isize n)
{
    /* (Final) TODO BEGIN */
    (void)ip; // 忽略 inode
    
    acquire_spinlock(&cons.lock);
    for (int i = 0; i < n; i++) {
        // 直接通过 UART 硬件接口输出字符
        uart_put_char(buf[i]);
    }
    release_spinlock(&cons.lock);
    
    return n;
    /* (Final) TODO END */
}

/**
 * console_read - read to the destination from the buffer
 * @ip: the pointer to the inode
 * @dst: the destination
 * @n: number of bytes to read
 */
isize console_read(Inode *ip, char *dst, isize n)
{
    /* (Final) TODO BEGIN */
    (void)ip;
    isize target = n;

    acquire_spinlock(&cons.lock);
    while (n > 0) {
        // 如果当前没有已经“提交”（即按下回车）的数据，则等待
        while (cons.read_idx == cons.write_idx) {
            release_spinlock(&cons.lock);
            wait_sem(&cons.sem); // 阻塞等待信号量
            acquire_spinlock(&cons.lock);
        }

        // 从缓冲区读取
        char c = cons.buf[cons.read_idx++ % IBUF_SIZE];

        // 处理 Ctrl-D (EOF)
        if (c == C('D')) {
            if (n < target) {
                // 如果已经读了一部分，把 Ctrl-D 放回去下次读
                cons.read_idx--;
            }
            break;
        }

        *dst++ = c;
        n--;

        // 如果读到了换行符，本次读取结束（行缓冲逻辑）
        if (c == '\n') {
            break;
        }
    }
    release_spinlock(&cons.lock);

    return target - n;
    /* (Final) TODO END */
}

void console_intr(char c)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&cons.lock);

    switch (c) {
    case C('U'): // Ctrl-U: 删除当前行
        while (cons.edit_idx != cons.write_idx) {
            cons.edit_idx--;
            uart_put_char('\b');
            uart_put_char(' ');
            uart_put_char('\b');
        }
        break;
    case C('H'): // Backspace
    case '\x7f':
        if (cons.edit_idx != cons.write_idx) {
            cons.edit_idx--;
            uart_put_char('\b');
            uart_put_char(' ');
            uart_put_char('\b');
        }
        break;
    default:
        if (c != 0 && cons.edit_idx - cons.read_idx < IBUF_SIZE) {
            // 回显字符（除了换行符可能需要转换）
            c = (c == '\r') ? '\n' : c;
            uart_put_char(c);

            // 存入编辑缓冲区
            cons.buf[cons.edit_idx++ % IBUF_SIZE] = c;

            // 如果是换行或缓冲区满，提交数据供 read 访问
            if (c == '\n' || c == C('D') || cons.edit_idx == cons.read_idx + IBUF_SIZE) {
                cons.write_idx = cons.edit_idx;
                post_sem(&cons.sem); // 唤醒正在等待读的进程
            }
        }
        break;
    }

    release_spinlock(&cons.lock);
    /* (Final) TODO END */
}