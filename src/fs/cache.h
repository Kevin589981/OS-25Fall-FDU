#pragma once
#include <common/list.h>
#include <common/sem.h>
#include <fs/block_device.h>
#include <fs/defines.h>

/**
    @brief 一个原子操作可以持有的不同块的最大数量。
 */
#define OP_MAX_NUM_BLOCKS 10

/**
    @brief 块缓存开始驱逐（eviction）的阈值。

    如果已缓存块的数量不小于此阈值，我们可以在 `acquire` 中驱逐一些块，
    以保持块缓存较小。
 */
#define EVICTION_THRESHOLD 20

/**
    @brief 块缓存中的一个块。

    @note 你可以根据需要向此结构体添加任何成员。
 */
typedef struct {
    /**
        @brief 磁盘上对应的块号。

        @note 应受块缓存的全局锁保护。

        @note 我们的测试需要用到此字段。请勿删除。
     */
    usize block_no;

    /**
        @brief 将此块链接到一个链表中。

        @note 应受块缓存的全局锁保护。
     */
    ListNode node;

    /**
        @brief 该块是否已被某些线程或进程获取（占用）？

        @note 应受块缓存的全局锁保护。
     */
    bool acquired;

    /**
        @brief 该块是否被固定（pinned）？

        被固定的块不应从缓存中被驱逐。

        例如：它是脏块（dirty）。

        @note 应受块缓存的全局锁保护。
     */
    bool pinned;

    /**
        @brief 保护 `valid` 和 `data` 的睡眠锁（SleepLock）。
     */
    SleepLock lock;

    /**
        @brief 块的内容是否已从磁盘加载？

        你可能发现它没用，事实上确实如此。它只是我们的测试读取的一个测试标志。
        在你的代码中，你应该：

        * 当你分配一个新的 `Block` 结构体时，将 `valid` 设为 `false`。
        * 仅在从磁盘加载块内容后，才将 `valid` 设为 `true`。

        @note 我们的测试需要用到此字段。请勿删除。
     */
    bool valid;
    /**
        @brief 磁盘块在内存中的真实内容。
     */
    u8 data[BLOCK_SIZE];
} Block;

/**
    @brief 一个原子操作上下文。

    @note 你可以根据需要向此结构体添加任何成员。

    @see begin_op, end_op
 */
typedef struct {
    /**
        @brief 此原子操作中还剩余多少次操作？

        如果 `rm` 为 0，任何 **新** 的 `sync` 调用都会导致 panic。
     */
    usize rm;
    /**
        @brief 用于标识此原子操作的时间戳（即 ID）。

        @note 你的实现不必使用此字段，忽略它也没关系。

        @note 仅我们的测试需要用到。请勿删除。
     */
    usize ts;
} OpContext;


typedef struct {
    /**
        @return 返回此刻缓存块的数量。

        @note 仅我们的测试需要用到以打印统计信息。
     */
    usize (*get_num_cached_blocks)();

    /**
        @brief 声明一个块已被调用者获取。

        它从磁盘读取 `block_no` 处的块内容，并锁定该块，
        以便调用者可以独占修改它。

        @return 指向已锁定块的指针。

        @see `release` - 此函数的对应操作。
     */
    Block *(*acquire)(usize block_no);

    /**
        @brief 声明一个已获取的块已被调用者释放。

        它解锁该块，以便其他线程可以再次获取它。

        @note 它不需要将块内容写回磁盘。
     */
    void (*release)(Block *block);

    // # 原子操作说明
    //
    // 原子操作有三种状态：
    // * running（运行中）：此原子操作可能还有更多修改。
    // * committed（已提交）：此原子操作已结束。不再有更多修改。
    // * checkpointed（已检查点）：所有修改均已持久化到磁盘。
    //
    // `begin_op` 创建一个新的运行中原子操作。
    // `end_op` 提交一个原子操作，并等待其被 checkpointed（持久化）。

    /**
        @brief 开始一个新的原子操作并初始化 `ctx`。

        如果有太多正在运行的操作（即我们的日志太小，无法容纳所有操作），
        `begin_op` 应该睡眠，直到我们可以开始一个新的操作。

        @param[out] ctx 要被初始化的上下文。

        @throw panic 如果 `ctx` 为 NULL。

        @see `end_op` - 此函数的对应操作。
     */
    void (*begin_op)(OpContext *ctx);

    /**
        @brief 将 `block` 的内容同步到磁盘。

        如果 `ctx` 为 NULL，它会立即将 `block` 的内容写入磁盘。

        然而这非常危险，因为它可能会破坏并发原子操作的原子性。
        你应该谨慎使用此模式。

        @param ctx 此块所属的原子操作上下文。

        @note 调用者必须持有 `block` 的锁。

        @throw panic 如果在 `sync` 之后，与 `ctx` 关联的块数大于 `OP_MAX_NUM_BLOCKS`。
     */
    void (*sync)(OpContext *ctx, Block *block);

    /**
        @brief 结束由 `ctx` 管理的原子操作。

        它会睡眠直到所有关联的块都写入磁盘。

        @param ctx 要结束的原子操作上下文。

        @throw panic 如果 `ctx` 为 NULL。
     */
    void (*end_op)(OpContext *ctx);

    // # 位图说明
    //
    // 磁盘上的每个块在位图中都有一个位（bit），包括位图内部的块！
    //
    // 通常，MBR 块、超级块（super block）、inode 块、日志块和位图块
    // 在磁盘上是预分配的，即位图中对应的位已经设置好了。
    // 因此当我们分配一个新块时，它通常返回一个数据块。
    // 但是，没人能阻止你释放一个非数据块 :)

    /**
        @brief 分配一个新的零初始化块。

        它在位图中搜索一个空闲块，将其标记为已分配并返回块号。

        @param ctx 由于此函数可能写入磁盘上的位图，因此必须与原子操作关联。
                   调用者必须确保 `ctx` 处于 **running** 状态。

        @return 已分配块的块号。

        @note 你应该在此处使用 `acquire`、`sync` 和 `release` 进行磁盘 I/O。

        @throw panic 如果磁盘上没有空闲块。
     */
    usize (*alloc)(OpContext *ctx);

    /**
        @brief 在位图中释放 `block_no` 处的块。

        如果 `block_no` 已经是空闲的或无效的，它**不会** panic。

        @param ctx 由于此函数可能写入磁盘上的位图，因此必须与原子操作关联。
                   调用者必须确保 `ctx` 处于 **running** 状态。
        @param block_no 要释放的块号。

        @note 你应该在此处使用 `acquire`、`sync` 和 `release` 进行磁盘 I/O。
     */
    void (*free)(OpContext *ctx, usize block_no);
} BlockCache;

/**
    @brief 全局块缓存实例。
 */
extern BlockCache bcache;

/**
    @brief 初始化块缓存。

    此方法还负责在系统崩溃后恢复日志，

    即它应该从日志区读取未提交的块，并将它们写回其原始位置。

    @param sblock 已加载的超级块。
    @param device 已初始化的块设备。

    @note 你可能想把它放入 `*_init` 方法组中。
 */
void init_bcache(const SuperBlock *sblock, const BlockDevice *device);