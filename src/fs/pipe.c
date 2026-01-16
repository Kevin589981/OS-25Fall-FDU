#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/syscall.h>
void init_pipe(Pipe *pi)
{
    /* (Final) TODO BEGIN */
    init_spinlock(&pi->lock);
    init_sem(&pi->wlock, 0);
    init_sem(&pi->rlock, 0);
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;
    /* (Final) TODO END */
}

void init_read_pipe(File *readp, Pipe *pipe)
{
    /* (Final) TODO BEGIN */
    readp->type = FD_PIPE;
    readp->readable = true;
    readp->writable = false;
    readp->pipe = pipe;
    readp->off = 0;
    /* (Final) TODO END */
}

void init_write_pipe(File *writep, Pipe *pipe)
{
    /* (Final) TODO BEGIN */
    writep->type = FD_PIPE;
    writep->readable = false;
    writep->writable = true;
    writep->pipe = pipe;
    writep->off = 0;
    /* (Final) TODO END */
}

int pipe_alloc(File **f0, File **f1)
{
    /* (Final) TODO BEGIN */
    Pipe *pi = NULL;
    *f0 = *f1 = NULL;
    
    // 分配两个文件对象
    if ((*f0 = file_alloc()) == NULL || (*f1 = file_alloc()) == NULL)
        goto bad;
    
    // 分配管道结构体
    if ((pi = (Pipe *)kalloc(sizeof(Pipe))) == NULL)
        goto bad;
    
    // 初始化管道
    init_pipe(pi);
    
    // 初始化读端和写端
    init_read_pipe(*f0, pi);
    init_write_pipe(*f1, pi);
    
    return 0;

bad:
    if (pi)
        kfree(pi);
    if (*f0)
        file_close(*f0);
    if (*f1)
        file_close(*f1);
    return -1;
    /* (Final) TODO END */
}

void pipe_close(Pipe *pi, int writable)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pi->lock);
    
    if (writable) {
        pi->writeopen = 0;
        post_all_sem(&pi->rlock);  // 唤醒所有等待读取的进程
    } else {
        pi->readopen = 0;
        post_all_sem(&pi->wlock);  // 唤醒所有等待写入的进程
    }
    
    // 如果读端和写端都关闭了，释放管道
    if (pi->readopen == 0 && pi->writeopen == 0) {
        release_spinlock(&pi->lock);
        kfree(pi);
    } else {
        release_spinlock(&pi->lock);
    }
    /* (Final) TODO END */
}

int pipe_write(Pipe *pi, u64 addr, int n)
{
    /* (Final) TODO BEGIN */
    int i = 0;
    Proc *pr = thisproc();
    char ch;
    
    acquire_spinlock(&pi->lock);
    while (i < n) {
        // 如果读端已关闭或进程被杀死，返回错误
        if (pi->readopen == 0 || pr->killed) {
            release_spinlock(&pi->lock);
            return -1;
        }
        
        // 如果管道已满，等待
        if (pi->nwrite == pi->nread + PIPE_SIZE) {
            post_all_sem(&pi->rlock);  // 唤醒读取进程
            release_spinlock(&pi->lock);
            wait_sem(&pi->wlock);  // 等待写入位置
            acquire_spinlock(&pi->lock);
        } else {
            // 从用户空间读取一个字符
            if (!user_readable((void *)(addr + i), 1)) {
                release_spinlock(&pi->lock);
                return -1;
            }
            ch = *(char *)(addr + i);
            
            // 写入管道
            pi->data[pi->nwrite++ % PIPE_SIZE] = ch;
            i++;
        }
    }
    
    post_all_sem(&pi->rlock);  // 唤醒等待读取的进程
    release_spinlock(&pi->lock);
    
    return i;
    /* (Final) TODO END */
}

int pipe_read(Pipe *pi, u64 addr, int n)
{
    /* (Final) TODO BEGIN */
    int i;
    Proc *pr = thisproc();
    char ch;
    
    acquire_spinlock(&pi->lock);
    
    // 等待管道中有数据或写端关闭
    while (pi->nread == pi->nwrite && pi->writeopen) {
        if (pr->killed) {
            release_spinlock(&pi->lock);
            return -1;
        }
        release_spinlock(&pi->lock);
        wait_sem(&pi->rlock);  // 等待数据
        acquire_spinlock(&pi->lock);
    }
    
    // 读取数据
    for (i = 0; i < n; i++) {
        if (pi->nread == pi->nwrite)
            break;
        
        ch = pi->data[pi->nread++ % PIPE_SIZE];
        
        // 写入用户空间
        if (!user_writeable((void *)(addr + i), 1)) {
            release_spinlock(&pi->lock);
            return -1;
        }
        *(char *)(addr + i) = ch;
    }
    
    post_all_sem(&pi->wlock);  // 唤醒等待写入的进程
    release_spinlock(&pi->lock);
    
    return i;
    /* (Final) TODO END */
}