#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#define PRINT_CACHE_LOG 1
#ifdef PRINT_CACHE_LOG

    #include <kernel/printk.h>
#else
    #define printk(...) do { } while(0)
#endif

// ============ 中断状态检测宏 ============
#include <kernel/printk.h>



#include <kernel/proc.h>
static int num_cached_blocks=0;
/**
    @brief 超级块的私有引用。

    @note 我们需要这两个变量，因为我们允许调用者指定要使用的块设备和超级块。
            相应地，你**绝不**应该使用它们的全局实例，
            例如 `get_super_block`，`block_device`。

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief 对底层块设备的引用。
 */
static const BlockDevice *device; 

/**
    @brief 块缓存的全局锁。

    用它来保护任何你需要保护的东西。

    例如：已分配块的列表等。
 */
static SpinLock lock;

/**
    @brief 所有已分配的内存中块的列表。

    我们使用链表来管理所有已分配的缓存块。

    如果你想要更好的性能，可以实现你自己的数据结构。

    @see Block
 */
static ListNode head;

static LogHeader header; // 日志头块的内存副本。

/**
    @brief 用于维护其他日志状态的结构体。
    
    你可能想知道我们在哪里存储某些状态，例如：
    
    * 有多少原子操作正在运行？
    * 我们正在进行检查点操作（checkpointing）吗？
    * 如何通知 `end_op` 检查点已完成？

    把它们放在这里！

    @see cache_begin_op, cache_end_op, cache_sync
 */
struct {
    /* 在此放置你的字段 */
    SpinLock lock;
    int outstanding;
    bool committing;
    Semaphore sem;
} log;

// 从磁盘读取内容。
static INLINE void device_read(Block *block) {
    device->read(block->block_no, block->data);
}

// 将内容写回磁盘。
static INLINE void device_write(Block *block) {
    device->write(block->block_no, block->data);
}

// 从磁盘读取日志头。
static INLINE void read_header() {
    printk("read_header: calling device->read for block %lld\n", (u64)sblock->log_start);
    device->read(sblock->log_start, (u8 *)&header);
    printk("read_header: device->read completed\n");
}

// 将日志头写回磁盘。
static INLINE void write_header() {
    device->write(sblock->log_start, (u8 *)&header);
}

// 初始化一个块结构体。
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    acquire_spinlock(&lock);
    usize n=num_cached_blocks;
    release_spinlock(&lock);
    return n;
}

int note=0;

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // TODO
    // printk("!!!important\n");
    acquire_spinlock(&lock);
    // printk("acquire cache lock.\n");
    Block *b=NULL;

    _for_in_list(this_node, &head){
        if (this_node==&head){
            continue;
        }
        Block *current=container_of(this_node,Block,node);
        if (current->block_no==block_no){
            b=current;
            _detach_from_list(this_node);
            _insert_into_list(&head,this_node);
            b->acquired=TRUE;
            release_spinlock(&lock);
            // unalertable_acquire_sleeplock(&b->lock);
            if (acquire_sleeplock(&b->lock)){

            }
            if (b->block_no==93){
                // printk("acquired block 93.\n");
            }
            // printk("acquiring cache:147, block no is %lld.\n",b->block_no);
            return b;
        }


    }
    if (num_cached_blocks>=EVICTION_THRESHOLD){
        _for_in_list_reverse(node,&head){
            if (node==&head){
                continue;
            }
            Block *victim=container_of(node,Block,node);
            if (!victim->acquired && !victim->pinned){
                b=victim;
                _detach_from_list(&b->node);
                break;
            }

        }
    }
    if (b==NULL){
        b=(Block *)kalloc(sizeof(Block));
        if (b==NULL){
            PANIC();
        }
        init_block(b);
        num_cached_blocks++;
    }
    b->block_no=block_no;
    b->valid=FALSE;
    b->acquired=TRUE;
    b->pinned=FALSE;
    _insert_into_list(&head, &b->node);
    release_spinlock(&lock);
    // unalertable_acquire_sleeplock(&b->lock);
    if (acquire_sleeplock(&b->lock)){

    };
    // printk("acquiring cache.\n");
    // CHECK_IRQ();
    if (!b->valid){
        note=1;
        // printk("cache miss, reading from device.\n");
        device_read(b);
        b->valid=TRUE;
    }
    // printk("acquired cache:193.\n");
    return b;
}

// see `cache.h`.
static void cache_release(Block *block) {
    // TODO
    acquire_spinlock(&lock);
    block->acquired=FALSE;
    release_spinlock(&lock);
    // post_all_sem(&block->lock);
    release_sleeplock(&block->lock);
}

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    // printk("init_bcache: starting\n");
    sblock = _sblock;
    device = _device;

    // TODO
    // printk("init_bcache: initializing locks and lists\n");
    init_spinlock(&lock);
    init_list_node(&head);
    init_spinlock(&log.lock);
    init_sem(&log.sem,0);
    log.outstanding=0;
    log.committing=FALSE;
    
    // printk("init_bcache: calling read_header\n");
    read_header();
    // printk("init_bcache: read_header done, num_blocks=%lld\n", (u64)header.num_blocks);
    
    if (header.num_blocks>0){
        // printk("init_bcache: recovering %lld blocks\n", (u64)header.num_blocks);
        for (usize i=0;i<header.num_blocks;i++){
            Block buf_block;
            buf_block.block_no=sblock->log_start+1+i;
            device_read(&buf_block);
            buf_block.block_no=header.block_no[i];
            device_write(&buf_block);
        }
        header.num_blocks=0;
        write_header();
        // printk("init_bcache: recovery complete\n");
    }
    
    // printk("init_bcache: completed\n");
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    acquire_spinlock(&log.lock);
    while (1){
        if (log.committing){
            release_spinlock(&log.lock);
            unalertable_wait_sem(&log.sem);
            acquire_spinlock(&log.lock);
        }else if (header.num_blocks+(log.outstanding+1)*OP_MAX_NUM_BLOCKS>LOG_MAX_SIZE){
            release_spinlock(&log.lock);
            unalertable_wait_sem(&log.sem);
            acquire_spinlock(&log.lock);
        }else{
            log.outstanding++;
            release_spinlock(&log.lock);
            break;
        }
    }
    ctx->rm=OP_MAX_NUM_BLOCKS;
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    if (ctx==NULL){
        device_write(block);
        return;
    }
    acquire_spinlock(&log.lock);
    // 如果这个块已经在当前事务的日志里了，什么都不用做，直接返回
    for (usize i = 0; i < header.num_blocks; i++) {
        if (header.block_no[i] == block->block_no) {
            release_spinlock(&log.lock);
            return; // <--- 关键：直接返回，不扣 rm
        }
    }

    // 2. 如果没找到，说明这是一个新加入事务的块，此时才检查配额
    if (ctx->rm <= 0) {
        // 此时释放锁再 panic 比较好，虽然 panic 会停机，但保持代码整洁
        release_spinlock(&log.lock);
        PANIC();
    }
    ctx->rm--; 
    acquire_spinlock(&lock);
    block->pinned = TRUE;
    release_spinlock(&lock);


    header.block_no[header.num_blocks] = block->block_no;
    header.num_blocks++;
    
    release_spinlock(&log.lock);
}
void commit(){
    if (header.num_blocks>0){
        for (usize i=0;i<header.num_blocks;i++){
            Block *b=cache_acquire(header.block_no[i]);
            usize dest=b->block_no;
            b->block_no=sblock->log_start+1+i;
            device_write(b);
            b->block_no=dest;
            cache_release(b);
            
        }
        write_header();
        for (usize i = 0; i < header.num_blocks; i++) {
            Block buf_block;
            buf_block.block_no = sblock->log_start + 1 + i;
            // printk("committing and read.\n");
            device_read(&buf_block); 
            buf_block.block_no = header.block_no[i];
            device_write(&buf_block);
        }
        header.num_blocks=0;
        write_header();
    }
}
// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    if (ctx==NULL)return;
    bool do_commit=FALSE;
    acquire_spinlock(&log.lock);
    log.outstanding--;
    if (log.committing){
        PANIC();
    }
    if (log.outstanding==0){
        do_commit=TRUE;
        log.committing=TRUE;
    }else{
        post_all_sem(&log.sem);
    }
    release_spinlock(&log.lock);
    if (do_commit){
        commit();
        acquire_spinlock(&lock);
        _for_in_list(node,&head){
            if (node==&head){
                continue;
            }
            Block *b=container_of(node,Block,node);
            if (b->pinned){
                b->pinned=FALSE;
            }

        }
        release_spinlock(&lock);
        acquire_spinlock(&log.lock);
        log.committing=FALSE;
        post_all_sem(&log.sem);
        release_spinlock(&log.lock);
    }
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    usize bpb=BLOCK_SIZE<<3;
    for (usize b=0;b<sblock->num_blocks;b+=bpb){
        usize bmap_block_no=sblock->bitmap_start+(b/bpb);
        Block *bp=cache_acquire(bmap_block_no);
        for (usize bi=0;bi<bpb&&b+bi<sblock->num_blocks;bi++){
            usize mask = 1u<<(bi%8);
            if ((bp->data[bi/8]&mask)==0){
                bp->data[bi/8]|=mask;
                cache_sync(ctx,bp);
                cache_release(bp);
                usize allocated_block=b+bi;
                Block *new_b=cache_acquire(allocated_block);
                memset(new_b->data,0,BLOCK_SIZE);
                cache_sync(ctx,new_b);
                cache_release(new_b);
                return allocated_block;
            }
        }
        cache_release(bp);
    }
    PANIC();
    return 0;
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no) {
    // TODO
    usize bpb=BLOCK_SIZE<<3;
    usize bmap_block_no=sblock->bitmap_start+(block_no/bpb);
    usize bi=block_no%bpb;
    Block *bp=cache_acquire(bmap_block_no);
    usize mask=1<<(bi%8);
    if ((bp->data[bi/8]&mask)==0){
        printk("freeing free block.\n");
        PANIC();
    }
    else{
        bp->data[bi/8]&=~mask;
        cache_sync(ctx,bp);
    }
    cache_release(bp);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};