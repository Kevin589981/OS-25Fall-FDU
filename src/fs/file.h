#pragma once

#include <common/defines.h>
#include <common/sem.h>
#include <fs/defines.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <sys/stat.h>
#include <common/list.h>
#include <common/spinlock.h>

// 整个系统中最大打开文件数量。
#define NFILE 65536  
// 加上定义每个进程可以打开的文件数量
#define NOFILE 128

typedef struct file {
    // 文件类型。
    // 注意：设备文件的类型也会是 FD_INODE。
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    // 引用计数。
    int ref;
    // 文件是否可读或可写。
    bool readable, writable;
    // 文件对应的底层对象。
    union {
        struct pipe* pipe;
        Inode* ip;
    };
    // 文件的字节偏移量。
    // 对于管道而言，该值表示已写入/读取的字节数。
    usize off;
} File;

struct ftable {
    // TODO: 系统中的文件对象表
    File files[NFILE];
    SpinLock lock;
    // 注意：你可能需要一个锁来防止对该表的并发访问！
};

struct oftable {
    // TODO: 进程中已打开的文件描述符表
    File* files[NOFILE];
};

// 初始化全局文件表。
void init_ftable();
// 初始化进程的已打开文件表。
void init_oftable(struct oftable*);
void free_oftable(struct oftable*);
/**
    @brief 在全局文件表中查找一个未使用的文件（即 ref == 0）并将其引用计数设为 1。
    
    @return struct file* 找到的文件对象。
 */
struct file* file_alloc();

/**
    @brief 通过增加引用计数来复制一个文件对象。
    
    @return struct file* 同一个文件对象。

    @see `inode_share` 函数为索引节点（inode）执行类似的操作。
 */

struct file* file_dup(struct file* f);

/**
    @brief 减少文件对象的引用计数。

    如果 f->ref == 0，则真正关闭该文件并释放索引节点（或关闭管道）。

    @note 由于 `cache.end_op` 可能会导致休眠，因此在调用 `end_op` 时，
    你不应该持有任何锁（指的是 `ftable` 的锁）！在释放索引节点之前，请先释放锁。

    @see `inode_put` 函数为索引节点（inode）执行类似的操作。
 */
void file_close(struct file* f);

/**
    @brief 读取文件的元数据。

    你无需自行完整实现此方法。只需调用 `stati` 函数即可。
    
    @param[out] st 待填充的 stat 结构体。
    @return int 成功返回 0，失败返回 -1。

    @see `stati` 函数会为索引节点填充 `st` 结构体。
 */
int file_stat(struct file* f, struct stat* st);

/**
    @brief 读取文件 `f` 中范围为 [f->off, f->off + n) 的内容。

    
    @param[out] addr 待填充的缓冲区。
    @param n 要读取的字节数。
    @return isize 实际读取的字节数。出错时返回 -1。
 */
isize file_read(struct file* f, char* addr, isize n);

/**
    @brief 向文件 `f` 中范围为 [f->off, f->off + n) 的位置写入内容。

    @param addr 待写入的缓冲区。
    @param n 要写入的字节数。
    @return isize 实际写入的字节数。出错时返回 -1。
*/
isize file_write(struct file* f, char* addr, isize n);