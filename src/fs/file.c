#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <fs/pipe.h>

// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    for (int i = 0; i < NFILE; i++)
    {
        ftable.files[i].ref = 0;
        ftable.files[i].type = FD_NONE;
        ftable.files[i].off = 0;
        ftable.files[i].readable = false;
        ftable.files[i].writable = false;
    }
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
    for (int i = 0; i < NOFILE; i++)
    {
        oftable->ofiles[i] = NULL;
    }
}

void free_oftable(struct oftable *oftable)
{
    for (int i = 0; i < NOFILE; i++)
    {
        if (oftable->ofiles[i])
        {
            file_close(oftable->ofiles[i]);
            oftable->ofiles[i] = NULL;
        }
    }
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* (Final) TODO BEGIN */
    for (int i = 0; i < NFILE; i++)
    {
        if (ftable.files[i].ref == 0)
        {
            ftable.files[i].ref = 1;
            return &ftable.files[i];
        }
    }
    printk("file_alloc: no free file\n");
    /* (Final) TODO END */
    return NULL;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* (Final) TODO BEGIN */
    f->ref++;
    /* (Final) TODO END */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* (Final) TODO BEGIN */
    if (f == NULL) return;

    f->ref--;
    if (f->ref == 0)
    {
        if (f->type == FD_INODE) 
        {
            inodes.put(NULL, f->ip);
        }
        else if (f->type == FD_PIPE) 
        {
            pipe_close(f->pipe, 1);
        }
        f->type = FD_NONE;
        f->off = 0;
        f->readable = false;
        f->writable = false;
    }
    /* (Final) TODO END */
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE)
    {
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    /* (Final) TODO END */
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    if (f->readable == false)
    {
        printk("file_read: file is not readable\n");
        return -1;
    }

    if (f->type == FD_INODE)
    {
        inodes.lock(f->ip);
        isize result = inodes.read(f->ip, (u8*)addr, f->off, n);
        if (result > 0)
        {
            f->off += result;
        }
        inodes.unlock(f->ip);
        return result;
    }
    else if (f->type == FD_PIPE)
    {
        isize result = pipe_read(f->pipe, (u64)addr, n);
        return result;
    }
    printk("file_read: unknown file type\n");
    /* (Final) TODO END */
    return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    isize total_written = 0;
    if (f->type == FD_INODE)
    {
        usize max_write_size = MIN(INODE_MAX_BYTES - f->off, (usize)n);
        usize bytes_written = 0;

        while (bytes_written < max_write_size)
        {
            usize write_chunk_size = MIN(max_write_size - bytes_written, (usize)((OP_MAX_NUM_BLOCKS - 2) * BLOCK_SIZE));

            OpContext op_context;
            bcache.begin_op(&op_context);

            inodes.lock(f->ip);

            if (inodes.write(&op_context, f->ip, (u8 *)(addr + bytes_written), f->off, write_chunk_size) != write_chunk_size)
            {
                inodes.unlock(f->ip);
                bcache.end_op(&op_context);
                return -1;
            }

            inodes.unlock(f->ip);
            bcache.end_op(&op_context);

            f->off += write_chunk_size;
            bytes_written += write_chunk_size;
            total_written += write_chunk_size;
        }

        return total_written;
    }
    else if (f->type == FD_PIPE)
    {
        isize result = pipe_write(f->pipe, (u64)addr, n);
        return result;
    }
    printk("file_write: unknown file type\n");
    /* (Final) TODO END */
    return 0;
}