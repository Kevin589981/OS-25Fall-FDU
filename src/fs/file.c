#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <fs/cache.h>
#include <common/list.h>
#include <kernel/mem.h>
#include <fs/pipe.h>
// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    init_spinlock(&ftable.lock);
    for (usize i=0;i<NFILE;i++){
        ftable.files[i].ref=0;
        ftable.files[i].type=FD_NONE;
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
        stati(f->ip, st);
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
        read_bytes=pipe_read(f->pipe,addr,(int)n);
        if (read_bytes>0){
            f->off+=read_bytes;
        }
    }else{
        return -1;
    }
    /* (Final) TODO END */
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* (Final) TODO BEGIN */
    if (!f->writable){
        return -1;
    }
    isize bytes_written=0;
    if (f->type == FD_INODE) {
        OpContext ctx;
        // 假设 cache 是全局可访问的，并且有 begin_op/end_op 接口
        bcache.begin_op(&ctx); 
        inodes.lock(f->ip); // 写入前必须持有 inode 锁
        bytes_written = inodes.write(&ctx, f->ip, (u8*)addr, f->off, n);
        if (bytes_written > 0) {
            f->off += bytes_written; // 更新偏移量
        }
        inodes.unlock(f->ip);

        // 3. 结束并提交事务
        bcache.end_op(&ctx);
    } else if (f->type == FD_PIPE) {
        // bytes_written = pipe_write(f->pipe, addr, n); // 如果实现了管道
    }

    return bytes_written;
    /* (Final) TODO END */
    return 0;
}