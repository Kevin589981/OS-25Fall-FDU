#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <fs/cache.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <fs/pipe.h>
#include <kernel/printk.h>
// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    init_spinlock(&ftable.lock);
    for (usize i=0;i<NFILE;i++){
        ftable.files[i].ref=0;
        ftable.files[i].type=FD_NONE;
        ftable.files[i].off = 0;
        ftable.files[i].readable = false;
        ftable.files[i].writable = false;
    }
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
    for (usize i=0;i<NOFILE;i++){
        oftable->files[i]=0;
    }
}

void free_oftable(struct oftable *oftable){
    for (usize i=0;i<NOFILE;i++){
        if (oftable->files[i]){
            file_close(oftable->files[i]);
            oftable->files[i]=NULL;
        }
    }
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* (Final) TODO BEGIN */
    struct file *f;
    acquire_spinlock(&ftable.lock);
    for (f=ftable.files;f<ftable.files+NFILE;f++){
        if (f->ref==0){
            f->ref=1;
            release_spinlock(&ftable.lock);
            return f;
            
        }
    }
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    return NULL;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    if (f->ref < 1){
        PANIC();
    }
    f->ref++;
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* (Final) TODO BEGIN */
    struct file temp_f;
    acquire_spinlock(&ftable.lock);
    if (f->ref < 1){
        PANIC();
    }
    f->ref--;
    if (f->ref > 0){
        release_spinlock(&ftable.lock);
        return; 
    }
    temp_f=*f;
    f->type=FD_NONE;
    f->ref=0;
    f->readable=FALSE;
    f->writable=FALSE;
    f->off=0;
    release_spinlock(&ftable.lock);
    if (temp_f.type == FD_INODE){
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, temp_f.ip);
        bcache.end_op(&ctx);
        
    } else if (temp_f.type == FD_PIPE){
        pipe_close(temp_f.pipe, temp_f.writable);
    }
    /* (Final) TODO END */
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE){
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
    if (!f->readable){
        return -1;
    }
    isize read_bytes=0;
    if (f->type == FD_INODE){
        inodes.lock(f->ip);
        read_bytes=inodes.read(f->ip, (u8*)addr, f->off, (usize)n);
        if (read_bytes>0){
            f->off+=read_bytes;
        }
        inodes.unlock(f->ip);
    } else if (f->type == FD_PIPE){
        read_bytes=pipe_read(f->pipe,(u64)addr,(int)n);
        if (read_bytes>0){
            f->off+=read_bytes;
        }
    }else{
        return -1;
    }
    return read_bytes;
    /* (Final) TODO END */
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    
    // 1. 基本检查：文件是否可写
    if (!f->writable) {
        return -1;
    }
    if (n < 0) {
        return -1;
    }

    // 2. 处理管道写入 (Pipe)
    if (f->type == FD_PIPE) {
        return pipe_write(f->pipe, (u64)addr, n);
    }

    // 3. 处理 Inode 写入
    if (f->type == FD_INODE) {
        // 计算实际允许写入的最大字节数（受限于文件系统最大文件大小）
        // 如果 f->off 已经超过上限，则无法写入
        if (f->off >= INODE_MAX_BYTES) {
            return 0;
        }
        
        isize max_bytes = n;
        if (f->off + n > INODE_MAX_BYTES) {
            max_bytes = INODE_MAX_BYTES - f->off;
        }

        isize total_written = 0;
        
        // 循环分块写入
        while (total_written < max_bytes) {
            // 计算本次事务允许写入的最大块数
            // 减去 2 是为了预留块给 inode 自身更新和 bitmap 更新等元数据开销
            isize blocks_per_op = OP_MAX_NUM_BLOCKS - 2;
            isize limit = blocks_per_op * BLOCK_SIZE;
            
            isize bytes_left = max_bytes - total_written;
            isize write_chunk_size = bytes_left;
            
            if (write_chunk_size > limit) {
                write_chunk_size = limit;
            }

            OpContext ctx;
            bcache.begin_op(&ctx); // 开启事务
            inodes.lock(f->ip);    // 获取 inode 锁

            // 执行写入
            isize r = inodes.write(&ctx, f->ip, (u8*)(addr + total_written), f->off, write_chunk_size);

            if (r != write_chunk_size) {
                // 写入出错处理
                inodes.unlock(f->ip);
                bcache.end_op(&ctx);
                if (total_written > 0) return total_written;
                return -1;
            }

            // 更新偏移量
            f->off += r;
            
            inodes.unlock(f->ip);  // 释放 inode 锁
            bcache.end_op(&ctx);   // 提交事务

            total_written += r;
        }

        return total_written;
    }

    return -1;
    /* (Final) TODO END */
}