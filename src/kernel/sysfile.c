//
// File-system system calls implementation.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <stddef.h>

#include "syscall.h"
#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/string.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <fs/pipe.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

struct iovec {
    void *iov_base; /* Starting address. */
    usize iov_len; /* Number of bytes to transfer. */
};

/** 
 * Get the file object by fd. Return null if the fd is invalid.
 */
static struct file *fd2file(int fd)
{
    /* (Final) TODO BEGIN */
    Proc *p=thisproc();
    if (fd<0||fd>=NOFILE){
        return NULL;
    }
    return p->oftable.files[fd];
    /* (Final) TODO END */
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
int fdalloc(struct file *f)
{
    /* (Final) TODO BEGIN */
    Proc *p=thisproc();

    for (int fd=0;fd<NOFILE;fd++){
        if (p->oftable.files[fd]==NULL){
            p->oftable.files[fd]=f;
            return fd;
        }
    }
    /* (Final) TODO END */
    return -1;
}

define_syscall(ioctl, int fd, u64 request)
{
    // 0x5413 is TIOCGWINSZ (I/O Control to Get the WINdow SIZe, a magic request
    // to get the stdin terminal size) in our implementation. Just ignore it.
    ASSERT(request == 0x5413);
    (void)fd;
    return 0;
}

define_syscall(mmap, void *addr, int length, int prot, int flags, int fd,
               int offset)
{
    /* (Final) TODO BEGIN */
    // return 2147483647;
    if (length <= 0)
        return (u64)-1;
    
    // 对齐到页边界
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    
    // 分配新的 section
    Section *sec = (Section *)kalloc(sizeof(Section));
    if (!sec)
        return (u64)-1;
    
    init_section(sec);
    
    // 查找可用的虚拟地址空间
    acquire_spinlock(&pd->lock);
    
    u64 va_start;
    if (addr && (flags & MAP_FIXED)) {
        // 使用指定地址
        va_start = (u64)addr;
    } else {
        // 自动分配地址：查找一个空闲区域
        // 从用户空间高地址开始查找（避免与堆冲突）
        va_start = 0x40000000; // 起始地址
        
        // 检查是否与现有 section 冲突
        bool found = false;
        for (u64 try_addr = va_start; try_addr < 0x80000000; try_addr += length) {
            bool conflict = false;
            _for_in_list(node, &pd->section_head) {
                if (node == &pd->section_head) continue;
                Section *s = container_of(node, Section, stnode);
                // 检查是否重叠
                if (!(try_addr + length <= s->begin || try_addr >= s->end)) {
                    conflict = true;
                    break;
                }
            }
            if (!conflict) {
                va_start = try_addr;
                found = true;
                break;
            }
        }
        
        if (!found) {
            release_spinlock(&pd->lock);
            kfree(sec);
            return (u64)-1;
        }
    }
    
    sec->begin = va_start;
    sec->end = va_start + length;
    
    // 如果是文件映射
    if (!(flags & MAP_ANONYMOUS) && fd >= 0) {
        struct file *f = fd2file(fd);
        if (!f) {
            release_spinlock(&pd->lock);
            kfree(sec);
            return (u64)-1;
        }
        
        // 检查文件权限和映射类型
        if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && !f->writable) {
            // MAP_SHARED 且请求写权限，但文件不可写
            release_spinlock(&pd->lock);
            kfree(sec);
            return (u64)-1;
        }
        
        sec->fp = file_dup(f);
        sec->offset = offset;
        sec->length = length;
        sec->flags = ST_FILE;
        
        // MAP_PRIVATE 时，即使文件只读，也可以请求写权限（COW）
        // 只有在真正写入时才会复制页面
    } else {
        // 匿名映射
        sec->fp = NULL;
        sec->offset = 0;
        sec->length = 0;
        sec->flags = ST_HEAP; // 匿名映射使用 HEAP 标志（延迟分配）
    }
    
    // 将 section 添加到链表
    _insert_into_list(&pd->section_head, &sec->stnode);
    
    release_spinlock(&pd->lock);
    
    printk("mmap: va=[%llx, %llx) len=%d flags=%llx fd=%d fp=%p\n", 
           (unsigned long long)va_start, (unsigned long long)sec->end, 
           length, sec->flags, fd, sec->fp);
    
    // 验证 section 已插入
    acquire_spinlock(&pd->lock);
    Section *verify = lookup_section(pd, va_start);
    release_spinlock(&pd->lock);
    printk("mmap: verify lookup_section(%llx) = %p\n", 
           (unsigned long long)va_start, verify);
    
    return va_start;
    /* (Final) TODO END */
}

define_syscall(munmap, void *addr, size_t length)
{
    /* (Final) TODO BEGIN */
    if (!addr || length <= 0)
        return -1;
    
    u64 va_start = (u64)addr;
    u64 va_end = va_start + ((length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    
    acquire_spinlock(&pd->lock);
    
    // 查找并删除覆盖该地址范围的 sections
    ListNode *node = pd->section_head.next;
    while (node != &pd->section_head) {
        Section *sec = container_of(node, Section, stnode);
        ListNode *next = node->next;
        
        // 检查是否有重叠
        if (!(va_end <= sec->begin || va_start >= sec->end)) {
            // 简化处理：如果完全覆盖，删除整个 section
            if (va_start <= sec->begin && va_end >= sec->end) {
                // 释放该 section 占用的物理页
                for (u64 va = sec->begin; va < sec->end; va += PAGE_SIZE) {
                    PTEntriesPtr pte = get_pte(pd, va, false);
                    if (pte && (*pte & PTE_VALID)) {
                        void *pa = (void *)P2K(PTE_ADDRESS(*pte));
                        kfree_page(pa);
                        *pte = 0;
                    }
                }
                
                // 关闭文件
                if (sec->fp) {
                    file_close(sec->fp);
                }
                
                // 从链表中移除
                _detach_from_list(&sec->stnode);
                kfree(sec);
            }
            // 部分覆盖的情况比较复杂，这里简化处理
            // 实际实现可能需要拆分 section
        }
        
        node = next;
    }
    
    release_spinlock(&pd->lock);
    arch_tlbi_vmalle1is();
    
    return 0;
    /* (Final) TODO END */
}

define_syscall(dup, int fd)
{
    struct file *f = fd2file(fd);
    if (!f)
        return -1;
    fd = fdalloc(f);
    if (fd < 0)
        return -1;
    file_dup(f);
    return fd;
}

define_syscall(read, int fd, char *buffer, int size)
{
    struct file *f = fd2file(fd);
    if (!f || size <= 0 || !user_writeable(buffer, size))
        return -1;
    return file_read(f, buffer, size);
}

define_syscall(write, int fd, char *buffer, int size)
{
    struct file *f = fd2file(fd);
    if (!f || size <= 0 || !user_readable(buffer, size))
        return -1;
    return file_write(f, buffer, size);
}

define_syscall(lseek, int fd, i64 offset, int whence)
{
    struct file *f = fd2file(fd);
    if (!f)
        return -1;
    
    // lseek is not meaningful for pipes
    if (f->type == FD_PIPE)
        return -1;
    
    i64 new_off;
    switch (whence) {
    case SEEK_SET:
        new_off = offset;
        break;
    case SEEK_CUR:
        new_off = f->off + offset;
        break;
    case SEEK_END:
        // For SEEK_END, we would need the file size
        // This requires inode operations
        if (f->type == FD_INODE) {
            inodes.lock(f->ip);
            new_off = f->ip->entry.num_bytes + offset;
            inodes.unlock(f->ip);
        } else {
            return -1;
        }
        break;
    default:
        return -1;
    }
    
    if (new_off < 0)
        return -1;
    
    f->off = new_off;
    return new_off;
}

define_syscall(writev, int fd, struct iovec *iov, int iovcnt)
{
    struct file *f = fd2file(fd);
    struct iovec *p;
    if (!f || iovcnt <= 0 || !user_readable(iov, sizeof(struct iovec) * iovcnt))
        return -1;
    usize tot = 0;
    for (p = iov; p < iov + iovcnt; p++) {
        if (!user_readable(p->iov_base, p->iov_len))
            return -1;
        tot += file_write(f, p->iov_base, p->iov_len);
    }
    return tot;
}

define_syscall(close, int fd)
{
    /* (Final) TODO BEGIN */
    File *f;
    Proc *p=thisproc();
    if ((f=fd2file(fd))==NULL){
        return -1;
    }
    p->oftable.files[fd]=NULL;
    file_close(f);
    /* (Final) TODO END */
    return 0;
}

define_syscall(fstat, int fd, struct stat *st)
{
    struct file *f = fd2file(fd);
    if (!f || !user_writeable(st, sizeof(*st)))
        return -1;
    return file_stat(f, st);
}

define_syscall(newfstatat, int dirfd, const char *path, struct stat *st,
               int flags)
{
    if (!user_strlen(path, 256) || !user_writeable(st, sizeof(*st)))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_fstatat: dirfd unimplemented\n");
        return -1;
    }
    if (flags != 0) {
        printk("sys_fstatat: flags unimplemented\n");
        return -1;
    }

    Inode *ip;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = namei(path, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    stati(ip, st);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);

    return 0;
}

static int isdirempty(Inode *dp)
{
    usize off;
    DirEntry de;

    for (off = 2 * sizeof(de); off < dp->entry.num_bytes; off += sizeof(de)) {
        if (inodes.read(dp, (u8 *)&de, off, sizeof(de)) != sizeof(de))
            PANIC();
        if (de.inode_no != 0)
            return 0;
    }
    return 1;
}

define_syscall(unlinkat, int fd, const char *path, int flag)
{
    ASSERT(fd == AT_FDCWD && flag == 0);
    Inode *ip, *dp;
    DirEntry de;
    char name[FILE_NAME_MAX_LENGTH];
    usize off;
    if (!user_strlen(path, 256))
        return -1;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((dp = nameiparent(path, name, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }

    inodes.lock(dp);

    // Cannot unlink "." or "..".
    if (strncmp(name, ".", FILE_NAME_MAX_LENGTH) == 0 ||
        strncmp(name, "..", FILE_NAME_MAX_LENGTH) == 0)
        goto bad;

    usize inumber = inodes.lookup(dp, name, &off);
    if (inumber == 0)
        goto bad;
    ip = inodes.get(inumber);
    inodes.lock(ip);

    if (ip->entry.num_links < 1)
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY && !isdirempty(ip)) {
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (inodes.write(&ctx, dp, (u8 *)&de, off, sizeof(de)) != sizeof(de))
        PANIC();
    if (ip->entry.type == INODE_DIRECTORY) {
        dp->entry.num_links--;
        inodes.sync(&ctx, dp, true);
    }
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    ip->entry.num_links--;
    inodes.sync(&ctx, ip, true);
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

bad:
    inodes.unlock(dp);
    inodes.put(&ctx, dp);
    bcache.end_op(&ctx);
    return -1;
}

/**
    @brief create an inode at `path` with `type`.

    If the inode exists, just return it.

    If `type` is directory, you should also create "." and ".." entries and link
   them with the new inode.

    @note BE careful of handling error! You should clean up ALL the resources
   you allocated and free ALL acquired locks when error occurs. e.g. if you
   allocate a new inode "/my/dir", but failed to create ".", you should free the
   inode "/my/dir" before return.

    @see `nameiparent` will find the parent directory of `path`.

    @return Inode* the created inode, or NULL if failed.
 */
Inode *create(const char *path, short type, short major, short minor,
              OpContext *ctx)
{
    /* (Final) TODO BEGIN */
    Inode *ip, *dp;
    char name[FILE_NAME_MAX_LENGTH];
    usize inode_no;

    // 1. 定位父目录。如果连父目录都找不到，直接失败。
    if ((dp = nameiparent(path, name, ctx)) == 0) {
        return NULL;
    }

    // 操作父目录前必须加锁
    inodes.lock(dp);

    // 2. 检查要创建的文件/目录是否已经存在
    if ((inode_no = inodes.lookup(dp, name, NULL)) != 0) {
        // 它已经存在
        inodes.unlock(dp);
        inodes.put(ctx, dp); // 释放对父目录的引用
        ip = inodes.get(inode_no); // 获取已存在的 Inode
        inodes.lock(ip); // 锁定它
        
        // （可选）可以检查类型是否匹配，但按题目要求，直接返回即可
        // if (ip->entry.type != type) { ... handle error ... }

        return ip; // 返回已存在的 Inode
    }

    // 3. 分配一个新的 Inode
    if ((inode_no = inodes.alloc(ctx, (InodeType)type)) == 0) {
        // 分配失败，必须清理并返回
        goto fail;
    }
    
    // 获取新分配 Inode 的内存结构
    ip = inodes.get(inode_no);
    inodes.lock(ip);

    // 4. 初始化新 Inode 的元数据
    ip->entry.major = major;
    ip->entry.minor = minor;
    ip->entry.num_links = 1; // 默认有一个来自父目录的链接

    // 5. 如果是目录，进行特殊处理
    if (type == INODE_DIRECTORY) {
        dp->entry.num_links++;       // 父目录的链接数+1 (因为有 '..')
        ip->entry.num_links++;       // 新目录的链接数+1 (因为有 '.')

        // 在新目录中创建 '.' (指向自己)
        if (inodes.insert(ctx, ip, ".", inode_no) == (usize)-1) {
            goto fail_creation;
        }
        // 在新目录中创建 '..' (指向父目录)
        if (inodes.insert(ctx, ip, "..", dp->inode_no) == (usize)-1) {
            goto fail_creation;
        }
    }

    // 6. 将新 Inode 链接到父目录中
    if (inodes.insert(ctx, dp, name, inode_no) == (usize)-1) {
        goto fail_creation;
    }
    
    // 7. 将所有变更（父目录和新inode）同步到磁盘
    if (type == INODE_DIRECTORY) {
        inodes.sync(ctx, dp, true);
    }
    inodes.sync(ctx, ip, true);

    // 成功！
    inodes.unlock(dp);
    inodes.put(ctx, dp);

    return ip; // 返回锁定的新 Inode，调用者负责解锁和释放

// --- 错误处理的回滚逻辑 ---
fail_creation:
    printk("!!!!fail_creation\n");
    // 如果创建过程中出错（例如插入 '.', '..' 或插入父目录失败）
    // 我们需要撤销所有操作，就像这个 Inode 从未被分配过一样
    ip->entry.num_links = 0;
    inodes.sync(ctx, ip, true); // 将 num_links=0 写回，以便 inode_put 能回收它
    inodes.unlock(ip);
    inodes.put(ctx, ip);

    if (type == INODE_DIRECTORY) {
        // 如果是目录创建失败，还要把父目录的链接数减回来
        dp->entry.num_links--;
        inodes.sync(ctx, dp, true);
    }
    // fall through

fail:
    // 通用的失败路径，释放对父目录的锁定和引用
    printk("!!!!fail\n");
    inodes.unlock(dp);
    inodes.put(ctx, dp);
    return NULL;

    /* (Final) TODO END */
    return 0;
}

define_syscall(openat, int dirfd, const char *path, int omode)
{
    int fd;
    struct file *f;
    Inode *ip;

    if (!user_strlen(path, 256))
        return -1;

    if (dirfd != AT_FDCWD) {
        printk("sys_openat: dirfd unimplemented\n");
        return -1;
    }

    OpContext ctx;
    bcache.begin_op(&ctx);
    if (omode & O_CREAT) {
        // FIXME: Support acl mode.
        ip = create(path, INODE_REGULAR, 0, 0, &ctx);
        if (ip == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
    } else {
        if ((ip = namei(path, &ctx)) == 0) {
            bcache.end_op(&ctx);
            return -1;
        }
        inodes.lock(ip);
    }

    if ((f = file_alloc()) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            file_close(f);
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

define_syscall(mkdirat, int dirfd, const char *path, int mode)
{
    Inode *ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mkdirat: dirfd unimplemented\n");
        return -1;
    }
    if (mode != 0) {
        printk("sys_mkdirat: mode unimplemented\n");
        return -1;
    }
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DIRECTORY, 0, 0, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(mknodat, int dirfd, const char *path, mode_t mode, dev_t dev)
{
    Inode *ip;
    if (!user_strlen(path, 256))
        return -1;
    if (dirfd != AT_FDCWD) {
        printk("sys_mknodat: dirfd unimplemented\n");
        return -1;
    }

    unsigned int ma = major(dev);
    unsigned int mi = minor(dev);
    printk("mknodat: path '%s', major:minor %u:%u\n", path, ma, mi);
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip = create(path, INODE_DEVICE, (short)ma, (short)mi, &ctx)) == 0) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;
}

define_syscall(chdir, const char *path)
{
    /**
     * (Final) TODO BEGIN 
     * 
     * Change the cwd (current working dictionary) of current process to 'path'.
     * You may need to do some validations.
     */
    
    Inode *ip;
    Proc *p=thisproc();
    OpContext ctx;
    if (!user_strlen(path,256)){
        return -1;
    }
    bcache.begin_op(&ctx);
    if ((ip=namei(path,&ctx))==NULL){
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    if (ip->entry.type!=INODE_DIRECTORY){
        inodes.unlock(ip);
        inodes.put(&ctx,ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    inodes.put(&ctx,p->cwd);
    p->cwd=ip;
    bcache.end_op(&ctx);
    return 0;
    /* (Final) TODO END */
}

define_syscall(pipe2, int pipefd[2], int flags)
{
    /* (Final) TODO BEGIN */
    File *f0, *f1;
    int fd0, fd1;
    
    // 检查用户空间指针是否可写
    if (!user_writeable(pipefd, sizeof(int) * 2))
        return -1;
    
    // 分配管道
    if (pipe_alloc(&f0, &f1) < 0)
        return -1;
    
    // 分配文件描述符
    fd0 = fdalloc(f0);
    if (fd0 < 0) {
        file_close(f0);
        file_close(f1);
        return -1;
    }
    
    fd1 = fdalloc(f1);
    if (fd1 < 0) {
        thisproc()->oftable.files[fd0] = NULL;
        file_close(f0);
        file_close(f1);
        return -1;
    }
    
    // 将文件描述符写入用户空间
    pipefd[0] = fd0;
    pipefd[1] = fd1;
    
    /* (Final) TODO END */
    (void)flags;
    return 0;
}