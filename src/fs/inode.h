#pragma once
#include <common/list.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <fs/cache.h>
#include <fs/defines.h>

/**
    @brief 根 inode 的编号（即 `/` 的 inode_no）。
 */
#define ROOT_INODE_NO 1

/**
    @brief 内存中的 inode。

    你可以将其与 `Block` 进行比较，因为它们有相似的操作方式。

    @see Block
 */
typedef struct {
    /**
        @brief 保护 inode 元数据及其内容的锁。

        @note 它**不**保护 `rc`、`node`、`valid` 等，因为它们是“运行时”变量，而不是 inode 的“文件系统”元数据或数据。
     */
    SleepLock lock;

    /**
        @brief 该 inode 的引用计数。

        与 `Block` 不同，一个 inode 可以被多个线程或进程共享，因此我们需要一个引用计数来跟踪该 inode 的引用数量。
     */
    RefCount rc;

    /**
        @brief 将此 inode 链接到一个链表中。
     */
    ListNode node;

    /**
        @brief 磁盘上对应的 inode 编号。

        @note 请将其与 `Block` 中的 `block_no`（即“块号”）区分开来。

        `inode_no` 应该是从 inode 区域起始处算起的块内偏移量。
     */
    usize inode_no;

    /**
        @brief `entry` 是否已从磁盘加载？
     */
    bool valid;

    /**
        @brief 磁盘上 inode 在内存中的真实副本。
     */
    InodeEntry entry; 
} Inode;

/**
    @brief inode 层接口。
 */
typedef struct {
    /**
        @brief 文件系统的根 inode。

        @see `init_inodes` 应当将其初始化为一个有效的 inode。
     */
    Inode* root;

    /**
        @brief 在磁盘上分配一个新的零初始化 inode。
        
        @param type 要分配的 inode 类型。

        @return 新分配的 inode 编号。

        @throw 如果分配失败（例如没有空闲 inode），则触发 panic。
     */
    usize (*alloc)(OpContext* ctx, InodeType type);

    /**
        @brief 获取 `inode` 的睡眠锁。
        
        在对 `inode` 及其文件内容进行任何写操作之前，应调用此方法。

        如果 inode 尚未加载，此方法应从磁盘加载它。

        @see `unlock` - 此方法的对应操作。
     */
    void (*lock)(Inode* inode);

    /**
        @brief 释放 `inode` 的睡眠锁。
        
        @see `lock` - 此方法的对应操作。
     */
    void (*unlock)(Inode* inode);

    /**
        @brief 在内存和磁盘之间同步 `inode` 的内容。
        
        与块缓存不同，此方法既可以读取也可以写入 inode。

        如果 `do_write` 为真且 inode 有效，将 `inode` 的内容写入磁盘。

        如果 `do_write` 为假且 inode 无效，从磁盘读取 `inode` 的内容。

        如果 `do_write` 为假且 inode 有效，则什么也不做。

        @note 这里的“写入磁盘”意味着“与块缓存同步”，而不是“直接写入底层的 SD 卡”。

        @note 调用者必须持有 `inode` 的锁。

        @throw 如果 `do_write` 为真且 `inode` 无效，则触发 panic。
     */
    void (*sync)(OpContext* ctx, Inode* inode, bool do_write);

    /**
        @brief 通过 inode 编号获取一个 inode。
        
        此方法应将该 inode 的引用计数加一。

        @note 它**不**需要从磁盘加载 inode！

        @see `sync` 将负责加载 inode 的内容。
        
        @return `inode_no` 对应的 `inode`。`inode->valid` 可能为假。

        @see `put` - 此方法的对应操作。
     */
    Inode* (*get)(usize inode_no);

    /**
        @brief 截断 `inode` 的所有内容。
        
        此方法移除（即“释放”）`inode` 的所有文件块。

        @note 不要忘记重置 `inode` 的相关元数据，例如 `inode->entry.num_bytes`。

        @note 调用者必须持有 `inode` 的锁。
     */
    void (*clear)(OpContext* ctx, Inode* inode);

    /**
        @brief 复制一个 inode。
        
        如果你想与他人共享一个 inode，请调用此方法。

        它应该将 `inode` 的引用计数加一。

        @return 复制的 inode（即可能直接返回 `inode`）。
     */
    Inode* (*share)(Inode* inode);

    /**
        @brief 通知你不再需要 `inode`。
        
        如果没人需要它，此方法还负责释放该 inode：

        “没人需要它”意味着它在内存中（`inode->rc == 0`）和磁盘上（`inode->entry.num_links == 0`）都已无用。

        “释放 inode”意味着释放所有相关的文件块以及 inode 本身。

        @note 完成所有这些操作后，不要忘记 `kfree(inode)`！

        @note 调用者必须**不**持有 `inode` 的锁。即调用者应该已经 `unlock` 了它。

        @see `get` - 此方法的对应操作。

        @see `clear` 可用于释放 `inode` 的所有文件块。
     */
    void (*put)(OpContext* ctx, Inode* inode);

    /**
        @brief 从 `inode` 读取 `count` 字节到 `dest`，从 `offset` 开始。
        
        @return 你实际读取了多少字节。

        @note 调用者必须持有 `inode` 的锁。
     */
    usize (*read)(Inode* inode, u8* dest, usize offset, usize count);

    /**
        @brief 将 `src` 中的 `count` 字节写入 `inode`，从 `offset` 开始。
        
        @return 你实际写入了多少字节。

        @note 调用者必须持有 `inode` 的锁。
     */
    usize (*write)(OpContext* ctx,
                   Inode* inode,
                   u8* src,
                   usize offset,
                   usize count);

    /**
        @brief 在目录 `inode` 中查找名为 `name` 的条目。

        @param[out] index 找到的条目在该目录中的索引。

        @return 对应 inode 的 inode 编号，如果未找到则为 0。
        
        @note 调用者必须持有 `inode` 的锁。

        @throw 如果 `inode` 不是目录，则触发 panic。
     */
    usize (*lookup)(Inode* inode, const char* name, usize* index);

    /**
        @brief 在目录 `inode` 中插入一个新的目录项。
        
        在 `inode` 中添加一个名为 `name` 的新目录项，指向编号为 `inode_no` 的 inode。

        @return 新目录项的索引，如果 `name` 已存在则为 -1。

        @note 如果目录 inode 已满，你应该增加目录 inode 的大小。

        @note 你**不**需要更改 `inode->entry.num_links`。我们最终实验中要完成的另一个函数将处理此操作。

        @note 调用者必须持有 `inode` 的锁。

        @throw 如果 `inode` 不是目录，则触发 panic。
     */
    usize (*insert)(OpContext* ctx,
                    Inode* inode,
                    const char* name,
                    usize inode_no);

    /**
        @brief 移除 `index` 处的目录项。
        
        如果对应的条目之前未被使用，`remove` 什么也不做。

        @note 如果移除了最后一个条目，你可以缩小目录 inode 的大小。
        如果你愿意，也可以移动条目来填补空洞。

        @note 调用者必须持有 `inode` 的锁。

        @throw 如果 `inode` 不是目录，则触发 panic。
     */
    void (*remove)(OpContext* ctx, Inode* inode, usize index);
} InodeTree;

/**
    @brief 全局 inode 层实例。
 */
extern InodeTree inodes;

/**
    @brief 初始化 inode 层。

    @note 不要忘记从磁盘读取根 inode！

    @param sblock 已加载的超级块。
    @param cache 已初始化的块缓存。
 */
void init_inodes(const SuperBlock* sblock, const BlockCache* cache);