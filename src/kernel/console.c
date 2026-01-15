#include <kernel/console.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/uart.h>

#define INPUT_BUF_SIZE 128
#define BACKSPACE_CHAR 0x100
#define NEWLINE '\n'
#define CARRIAGE_RETURN '\r'
#define BACKSPACE '\x7f'

struct console cons;

void console_init()
{
    /* (Final) TODO BEGIN */
    init_spinlock(&cons.lock);
    init_sem(&cons.sem, 0);
    /* (Final) TODO END */
}

void uart_putchar(int c)
{
    if (c == BACKSPACE_CHAR)
    {
        uart_put_char('\b');
        uart_put_char(' ');
        uart_put_char('\b');
    }
    else
    {
        uart_put_char(c);
    }
}

/**
 * console_write - write to uart from the console buffer.
 * @ip: the pointer to the inode
 * @buf: the buffer
 * @n: number of bytes to write
 */
isize console_write(Inode *ip, char *buf, isize n)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&cons.lock);
    for (int i = 0; i < n; i++)
    {
        uart_putchar(buf[i]);
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
    isize i = n;
    acquire_spinlock(&cons.lock);
    while (i)
    {
        if (cons.write_idx == cons.read_idx)
        {
            release_spinlock(&cons.lock);
            if (!wait_sem(&cons.sem)) return -1;
            acquire_spinlock(&cons.lock);
        }
        cons.read_idx = (cons.read_idx + 1) % INPUT_BUF_SIZE;
        if (cons.buf[cons.read_idx] == C('D'))
        {
            if (i < n)
            {
                cons.read_idx = (cons.read_idx - 1) % INPUT_BUF_SIZE;
            }
            break;
        }
        *(dst++) = cons.buf[cons.read_idx];
        i--;
        if (cons.buf[cons.read_idx] == NEWLINE)
        {
            break;
        }
    }
    release_spinlock(&cons.lock);
    return n - i;
    /* (Final) TODO END */
}

void console_intr(char c)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&cons.lock);
    switch (c) {
    case C('U'):
        while (cons.edit_idx != cons.write_idx && cons.buf[(cons.edit_idx - 1) % INPUT_BUF_SIZE] != NEWLINE)
        {
            cons.edit_idx = (cons.edit_idx - 1) % INPUT_BUF_SIZE;
            uart_putchar(BACKSPACE_CHAR);
        }
        break;
    case BACKSPACE:
        if (cons.edit_idx != cons.write_idx)
        {
            cons.edit_idx = (cons.edit_idx - 1) % INPUT_BUF_SIZE;
            uart_putchar(BACKSPACE_CHAR);
        }
        break;
    default:
        if (c != 0 && cons.edit_idx - cons.read_idx < INPUT_BUF_SIZE)
        {
            if (c == CARRIAGE_RETURN)
            {
                c = NEWLINE;
            }
            cons.buf[++cons.edit_idx % INPUT_BUF_SIZE] = c;
            uart_putchar(c);
            if (c == NEWLINE || c == C('D'))
            {
                cons.write_idx = cons.edit_idx;
                post_sem(&cons.sem);
            }
        }
        break;
    }
    release_spinlock(&cons.lock);
    /* (Final) TODO END */
}