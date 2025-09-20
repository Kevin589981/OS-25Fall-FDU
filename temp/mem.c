#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <common/string.h>

RefCount kalloc_page_cnt;

typedef struct page_info{
    SpinLock page_lock;
    RefCount page_ref_count;
}page_info;

// 空闲页链表
ListNode *free_page_list=NULL;

// 总物理页数量
usize page_count;
// 空闲物理页数量
usize free_page_count=0;
SpinLock VMM_lock={false};
void *VMM=NULL;//页表使用情况报告表
// 页表所在位置
page_info* page_infos=NULL;

static usize page_infos_size_bytes = 0; 
static usize memory_size_byte=0; //内存大小
static usize page_infos_pages=0;

void kalloc_pools_init();
void kinit() {
    init_rc(&kalloc_page_cnt);
    // 先计算开始的位置
    
    extern char end[];
    // 进行物理页对齐
    usize user_start_link=(usize)(((usize)end + PAGE_SIZE-1) & (~(PAGE_SIZE-1llu)));
    usize user_end_link=P2K(PHYSTOP);
    printk("PHYSTOP is %x\n",PHYSTOP);
    printk("user_start_link is %llx\n",user_start_link);
    printk("user_end_link is %llx\n", user_end_link);
    // 共有page_count页
    page_count=((user_end_link-user_start_link)/(PAGE_SIZE));
    // 物理内存大小
    memory_size_byte=page_count*PAGE_SIZE;
    printk("memory size is %llu bytes.\n",memory_size_byte);
    // !操作系统内存很大，这个表会占用多页，不能先写kalloc_page，用kalloc_page分配导致页数不够
    
    page_infos_size_bytes=sizeof(page_info)*page_count;
    // 页表占用的页数
    page_infos_pages=(page_infos_size_bytes+PAGE_SIZE-1)/PAGE_SIZE;

    page_infos= (page_info*) user_start_link;
    
    // 从页表开始处清理字节给页表使用
    memset(page_infos, 0, page_infos_size_bytes);

    // 操作系统需要占据所有剩余空闲的内存方便托管
    // 不能使用malloc函数（这是用户态的函数）
    // usize page_addresses[page_count];
    void *free_page_start=(char*)page_infos+ page_infos_pages*PAGE_SIZE;
    acquire_spinlock(&VMM_lock);
    for (void *p =free_page_start;p+PAGE_SIZE<=(void *)user_end_link;p+=PAGE_SIZE){
        free_page_list = _insert_into_list(free_page_list, (ListNode *)p);
    }
    release_spinlock(&VMM_lock);
    
    // 将存放VMM的该页的引用计数增加
    for (usize i=0; i<page_count;i++){
        // 每一页页表的对应页表行
        // 32754是一个奇怪的数，发现每次i变成32754就会卡死，原来是逻辑写错导致内存溢出了
        page_info *this_page_info=&page_infos[i];
        // if (i!=32754)this_page_info->page_ref_count.count=0;
        if (i<page_infos_pages){
            increment_rc(&this_page_info->page_ref_count);
        }
        // printk("Record Page %llu\n", i);
    }
    printk("allocate %llu pages\n",page_infos_pages);

    kalloc_pools_init();
}
// static int times=0;
void* kalloc_page() {
    acquire_spinlock(&VMM_lock);
    increment_rc(&kalloc_page_cnt);
    // printk("allocate a new page\n");
    
    if (free_page_list==NULL){
        // if (times==0)printk("Great Error. First time failed.\n");
        decrement_rc(&kalloc_page_cnt);
        printk("Pages have been used out.\n");
        release_spinlock(&VMM_lock);
        return NULL;
    }
    // times+=1;
    //此时不应当出现free_page_list为空的情况，因为还有空页表
    ListNode *temp_node=free_page_list;
    // 接受返回的前一个节点（可能为空）
    free_page_list=_detach_from_list(temp_node);
    
    release_spinlock(&VMM_lock);

    memset(temp_node,0 ,PAGE_SIZE);
    
    // //计算出页号
    // 没有必要在这里增加引用计数，在kalloc和kfree里改就可以了
    // //usize this_page_number=(temp_node-page_infos)/PAGE_SIZE;
    // //(page_info *)temp_node
    return temp_node;

}

void kfree_page(void* p) {
    if (p==NULL){
        printk("A NULL POINTER.");
        return;
    }
    acquire_spinlock(&VMM_lock);
    decrement_rc(&kalloc_page_cnt);
    // printk("free page\n");
    
    _insert_into_list(free_page_list, (ListNode *)p);
    release_spinlock(&VMM_lock);
    // printk("free page: %llx\n", (usize)p);
    return;
}

typedef struct PagePoolHeader {
    // 指向池中下一个和上一个非满页的指针，形成双向链表
    struct PagePoolHeader* next_page;
    struct PagePoolHeader* prev_page;
    // 保护该页内部状态锁
    SpinLock lock;
    
    // 该页管理的对象大小（例如 2, 4, 8, ..., 2048）
    // 该字段在 kfree 时用于反向查找该页属于哪个池
    short obj_size;

    // 该页最多可以容纳的小块的总数量
    // 在页面初始化后，这个值是固定不变的
    short storage;

    // 该页当前剩余的空闲小块数量。
    // 当 free_count == storage 时，该页完全空闲，可以被释放。
    // 和页表中的引用计数器其实是一样的
    short free_count;

    // 指向页内第一个空闲块的偏移量（相对于页起始地址）。
    // 值为 0 表示该页已满，没有空闲块了。
    short free_list_offset;

} PagePoolHeader;

/**
 * 覆盖在空闲内存块上的视图
 * 当一个内存块是空闲状态时，它的起始位置为这个结构体，
 * 用它来存储指向下一个空闲块的偏移量
 * 大小必须小于等于它所在池的最小对象大小（即2字节），故采用short
 */
typedef struct FreeBlock {
    // 下一个空闲块的偏移量
    short next_offset;
} FreeBlock;


// 内存池管理器，管理特定大小内存块

typedef struct PoolManager {
    // 保护下面 pages 链表的锁，其实和页表中的锁重复了
    SpinLock global_lock;

    // 指向该池管理的页面链表的头节点
    PagePoolHeader* pages;

} PoolManager;

// 支持的池的数量，从 2^1=2 到 2^11=2048
#define POOL_MIN_LOG 1  // 2^1 = 2 bytes
#define POOL_MAX_LOG 11 // 2^11 = 2048 bytes
#define POOL_COUNT (POOL_MAX_LOG - POOL_MIN_LOG + 1)

// 全局的内存池管理器数组
PoolManager pools[POOL_COUNT];


void kalloc_pools_init() {
    for (int i = 0; i < POOL_COUNT; i++) {
        init_spinlock(&pools[i].global_lock);
        pools[i].pages = NULL;
    }
}

/**
 * 为一个指定的池分配并初始化一个新的专用页
 * pool_idx 需要扩容的池的索引
 * 成功则返回新页的头部指针，失败（内存不足）返回 NULL
 */
static PagePoolHeader* grow_pool(int pool_idx) {
    // 1. 从底层页分配器申请一个干净的物理页
    void* page_addr = kalloc_page();
    if (page_addr == NULL) {
        return NULL;
    }

    // 2. 在页的起始位置建立 PagePoolHeader
    PagePoolHeader* header = (PagePoolHeader*)page_addr;
    header->obj_size = 1 << (pool_idx + POOL_MIN_LOG);
    init_spinlock(&header->lock);

    // 3. 将页的剩余空间切割成N个小块，并构建侵入式空闲链表
    header->storage = 0;
    header->free_count = 0;
    
    // 计算第一个块的对齐后偏移量
    short current_offset = (sizeof(PagePoolHeader) + header->obj_size - 1) & ~(header->obj_size - 1);
    header->free_list_offset = current_offset;

    // 遍历所有可能的块，将它们链接起来
    while (current_offset + header->obj_size <= PAGE_SIZE) {
        FreeBlock* block = (FreeBlock*)((char*)page_addr + current_offset);
        
        // 指向下一个块，如果已经是最后一个块，则指向0
        short next_block_offset = current_offset + header->obj_size;
        if (next_block_offset + header->obj_size <= PAGE_SIZE) {
            block->next_offset = next_block_offset;
        } else {
            block->next_offset = 0; // 链表结束
        }
        
        current_offset += header->obj_size;
        header->storage++;
    }
    header->free_count = header->storage;

    return header;
}

/**
 * 成功则返回指向用户数据区的指针，失败则返回NULL
 */
void* kalloc(usize size) {
    if (size == 0 || size > (1 << POOL_MAX_LOG)) {
        // 不会等于0，也不会超过PAGE_SIZE/2，所以应该不会跑到这里
        printk("kalloc: Invalid or unsupported size %llu\n", size);
        return NULL; 
    }

    // 根据请求大小，选择最合适的内存池
    int pool_idx = 0;
    usize pool_obj_size = 1 << POOL_MIN_LOG;
    while (pool_obj_size < size) {
        pool_obj_size <<= 1;
        pool_idx++;
    }
    PoolManager* pool = &pools[pool_idx];

    acquire_spinlock(&pool->global_lock);

    // 遍历页面链表，查找有空闲块的页
    PagePoolHeader* page_header = pool->pages;
    while (page_header != NULL && page_header->free_count == 0) {
        page_header = page_header->next_page;
    }

    // 如果没有找到可用页面，则创建一个新页
    if (page_header == NULL) {
        page_header = grow_pool(pool_idx);
        if (page_header == NULL) {
            release_spinlock(&pool->global_lock);
            printk("kalloc: out of memory for pool size %llu\n", pool_obj_size);
            return NULL;
        }
        // 将新页加入到池的链表头部
        page_header->prev_page=NULL;
        page_header->next_page = pool->pages;
        pool->pages = page_header;
    }

    // 从找到的页面中分配一个块
    acquire_spinlock(&page_header->lock);

    // 取出空闲链表的第一个块
    short offset = page_header->free_list_offset;
    void* ptr = (void*)((char*)page_header + offset);
    FreeBlock* block = (FreeBlock*)ptr;

    // 更新空闲链表头
    page_header->free_list_offset = block->next_offset;
    page_header->free_count--;
    
    if (page_header->free_count == 0) {
        if (page_header->prev_page != NULL) {
            page_header->prev_page->next_page = page_header->next_page;
        } else { // 是头节点
            pool->pages = page_header->next_page;
        }
        if (page_header->next_page != NULL) {
            page_header->next_page->prev_page = page_header->prev_page;
        }
    }

    release_spinlock(&page_header->lock);
    release_spinlock(&pool->global_lock);

    return ptr;
}

/**
 * 释放由kalloc分配的内存块，ptr指向由kalloc返回的数据区的指针
 */
void kfree(void* ptr) {
    if (ptr == NULL) return;

    // 根据用户指针，通过地址对齐反向计算出页头的地址
    PagePoolHeader* page_header = (PagePoolHeader*)((usize)ptr & ~(PAGE_SIZE - 1));

    // 从页头中获取对象大小，并找到对应的内存池
    int pool_idx = 0;
    usize pool_obj_size = 1 << POOL_MIN_LOG;
    while ((short)pool_obj_size < page_header->obj_size) {
        pool_obj_size <<= 1;
        pool_idx++;
    }
    if ((short)pool_obj_size != page_header->obj_size) {
        printk("kfree: Memory corruption detected! Pointer points to an invalid page header.\n");
        return;
    }
    PoolManager* pool = &pools[pool_idx];
    
    acquire_spinlock(&pool->global_lock);
    acquire_spinlock(&page_header->lock);

    // 检查此页在释放前是否已满
    bool was_full = (page_header->free_count == 0);

    // 将释放的块插回到页的空闲链表中
    FreeBlock* block_to_free = (FreeBlock*)ptr;
    block_to_free->next_offset = page_header->free_list_offset;
    page_header->free_list_offset = (short)((char*)ptr - (char*)page_header);
    page_header->free_count++;
    
    // 如果页面完全空闲，则释放回系统
    if (page_header->free_count == page_header->storage) {
        // 如果页之前不是满的，它一定在非满页链表中，需要先移除
        if (!was_full) {
             if (page_header->prev_page != NULL) {
                page_header->prev_page->next_page = page_header->next_page;
            } else { // 是头节点
                pool->pages = page_header->next_page;
            }
            if (page_header->next_page != NULL) {
                page_header->next_page->prev_page = page_header->prev_page;
            }
        }
        // 注意：如果页之前是满的(was_full)，它不在任何链表中，无需移除

        release_spinlock(&page_header->lock);
        release_spinlock(&pool->global_lock);
        
        kfree_page(page_header);
        return;
    }
    
    // 如果此页之前是满的，现在有了一个空位，需要将它加回到非满页链表中
    if (was_full) {
        page_header->next_page = pool->pages;
        page_header->prev_page = NULL;
        if (pool->pages != NULL) {
            pool->pages->prev_page = page_header;
        }
        pool->pages = page_header;
    }
    
    release_spinlock(&page_header->lock);
    release_spinlock(&pool->global_lock);
}