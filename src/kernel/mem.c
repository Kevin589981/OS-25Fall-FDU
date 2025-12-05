#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <common/string.h>

RefCount kalloc_page_cnt;

// 前向声明 PagePoolHeader 结构体，以便在 page_info 中使用
struct PagePoolHeader;

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


// 将 PagePoolHeader 的定义移到 page_info 之前
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
    short free_count;

    // 指向页内第一个空闲块的偏移量（相对于页起始地址）。
    // 值为 0 表示该页已满，没有空闲块了。
    short free_list_offset;

} PagePoolHeader;

typedef struct page_info{
    SpinLock page_lock;
    RefCount page_ref_count;
    PagePoolHeader pool_header;
}page_info;

// 空闲页链表
ListNode *free_page_list=NULL;

// 总物理页数量
usize page_count;
// 空闲物理页数量
usize free_page_count=0;
SpinLock VMM_lock={false};
// void *VMM=NULL;//页表使用情况报告表，没有被使用

// 定义物理内存的起始虚拟地址，用于计算页号索引
#define VMM_START P2K(EXTMEM)
// 根据内存布局计算出最大的物理页数量
#define TOTAL_PHYS_PAGES ((PHYSTOP - EXTMEM) / PAGE_SIZE)

// 将 page_infos 定义为全局定长数组，将被放入.bss段
page_info page_infos[TOTAL_PHYS_PAGES];

// 内存池管理器，管理特定大小内存块
typedef struct PoolManager {
    SpinLock global_lock;
    PagePoolHeader* pages;
} PoolManager;

// 定义一个结构体来显式配置每个内存池
typedef struct PoolInfo {
    const usize obj_size; // 该池管理的对象大小
    PoolManager manager;    // 对应的池管理器
} PoolInfo;

// 全局的内存池配置表。
// ! 必须严格按照 obj_size 从小到大排序
static PoolInfo g_pools[] = {
    { 2,    { .global_lock = {false}, .pages = NULL } },
    { 4,    { .global_lock = {false}, .pages = NULL } },
    { 8,    { .global_lock = {false}, .pages = NULL } },
    { 16,   { .global_lock = {false}, .pages = NULL } },
    { 24,   { .global_lock = {false}, .pages = NULL } }, // 自定义尺寸
    { 32,   { .global_lock = {false}, .pages = NULL } },
    { 40,   { .global_lock = {false}, .pages = NULL } }, // 自定义尺寸
    { 48,   { .global_lock = {false}, .pages = NULL } }, // 自定义尺寸
    { 56,   { .global_lock = {false}, .pages = NULL } },
    { 64,   { .global_lock = {false}, .pages = NULL } },
    { 80,   { .global_lock = {false}, .pages = NULL } },
    { 96,   { .global_lock = {false}, .pages = NULL } },
    { 112,  { .global_lock = {false}, .pages = NULL } },
    { 128,  { .global_lock = {false}, .pages = NULL } },
    { 160,  { .global_lock = {false}, .pages = NULL } },
    { 192,  { .global_lock = {false}, .pages = NULL } },
    { 224,  { .global_lock = {false}, .pages = NULL } },
    { 256,  { .global_lock = {false}, .pages = NULL } },
    { 384,  { .global_lock = {false}, .pages = NULL } },
    { 512,  { .global_lock = {false}, .pages = NULL } },
    { 768,  { .global_lock = {false}, .pages = NULL } },
    { 1024, { .global_lock = {false}, .pages = NULL } },
    { 2048, { .global_lock = {false}, .pages = NULL } },
};

// 根据配置表自动计算池的数量
#define POOL_COUNT (sizeof(g_pools) / sizeof(g_pools[0]))
// 定义内存池支持的最大分配尺寸
#define POOL_MAX_SIZE (g_pools[POOL_COUNT - 1].obj_size)

void kalloc_pools_init();
void kinit() {
    init_rc(&kalloc_page_cnt);
    // 先计算开始的位置
    extern char end[];

    // 进行物理页对齐
    void *user_start_link=(void*)(((usize)end + PAGE_SIZE-1) & (~(PAGE_SIZE-1llu)));
    usize user_end_link=P2K(PHYSTOP);
    
    // printk("PHYSTOP is %x\n",PHYSTOP);
    // printk("Kernel end symbol is at %p\n", end);
    // printk("Free memory starts at %p\n", user_start_link);
    // printk("Physical memory ends at %llu\n", user_end_link);
    
    // 共有page_count页
    page_count = TOTAL_PHYS_PAGES;
    usize memory_size_byte = (user_end_link - (usize)user_start_link);
    printk("Available memory size is %llu bytes.\n", memory_size_byte);

    // page_infos是全局数组，无需动态分配
    // // !操作系统内存很大，这个表会占用多页，不能先写kalloc_page，用kalloc_page分配导致页数不够
    memset(page_infos, 0, sizeof(page_infos));

    // 操作系统需要占据所有剩余空闲的内存方便托管
    // 不能使用malloc函数（这是用户态的函数）
    acquire_spinlock(&VMM_lock);
    for (void *p = user_start_link; p+PAGE_SIZE <= (void *)user_end_link; p+=PAGE_SIZE){
        free_page_list = _insert_into_list(free_page_list, (ListNode *)p);
        free_page_count++;
    }
    release_spinlock(&VMM_lock);
    
    // 标记所有内核代码、数据和BSS段占用的页为已使用
    // 通过增加它们的引用计数，防止被分配器错误地分配出去
    usize kernel_used_pages = ((usize)user_start_link - VMM_START) / PAGE_SIZE;
    for (usize i = 0; i < kernel_used_pages; i++) {
        increment_rc(&page_infos[i].page_ref_count);
    }
    
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
    return;
}


void kalloc_pools_init() {
    for (int i = 0; i < (int)POOL_COUNT; i++) {
        init_spinlock(&g_pools[i].manager.global_lock);
        g_pools[i].manager.pages = NULL;
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
    // 2. 计算页号，并从 page_infos 数组中获取对应的页头指针
    // 索引计算基于VMM_START
    usize page_info_idx = ((usize)page_addr - VMM_START) / PAGE_SIZE;
    PagePoolHeader* header = &page_infos[page_info_idx].pool_header;

    // 3. 在页信息中初始化页池头
    init_spinlock(&header->lock);
    // 从配置表中直接获取对象大小
    header->obj_size = g_pools[pool_idx].obj_size;
    
    header->storage = 0;
    header->free_count = 0;
    
    // 4. 将页的整个空间切割成N个小块，并构建侵入式空闲链表
    short current_offset = 0;
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
    // 不会等于0，也不会超过PAGE_SIZE/2，所以应该不会进入这里
    if (size == 0 || size > POOL_MAX_SIZE) {
        printk("kalloc: Invalid or unsupported size %llu\n", size);
        return NULL; 
    }

    // 根据请求大小，遍历配置表选择最合适的内存池
    int pool_idx = -1;
    for (int i = 0; i < (int)POOL_COUNT; i++) {
        if (g_pools[i].obj_size >= size) {
            pool_idx = i;
            break; // 找到第一个能容纳的池
        }
    }

    PoolManager* pool = &g_pools[pool_idx].manager;
    PagePoolHeader* page_header = NULL;

    // 1. 短暂加锁，查找并摘除一个可用页
    acquire_spinlock(&pool->global_lock);
    bool tag = false;
    page_header = pool->pages;
    while (page_header != NULL) {
        tag=try_acquire_spinlock(&page_header->lock);
        if (tag){
            break;
        }
        page_header = page_header->next_page;
    }

    if (page_header != NULL) {
        // 找到了，立即从链表中摘除
        if (page_header->prev_page != NULL) {
            page_header->prev_page->next_page = page_header->next_page;
        } else {
            pool->pages = page_header->next_page;
        }
        if (page_header->next_page != NULL) {
            page_header->next_page->prev_page = page_header->prev_page;
        }
        page_header->next_page = page_header->prev_page = NULL;
    }
    
    // 2. 立即释放全局锁
    release_spinlock(&pool->global_lock);

    // 3. 如果链表中没有可用页，则创建一个新页 (慢路径)
    if (page_header == NULL) {
        page_header = grow_pool(pool_idx);
        if (page_header == NULL) {
            return NULL;
        }
    }

    // 4. 在页级别上操作
    if (!tag) acquire_spinlock(&page_header->lock);

    // 计算页地址等信息
    page_info *p_info_base = 0;
    usize offset_of_header = (usize)((char*)&(p_info_base->pool_header) - (char*)p_info_base);
    page_info *info = (page_info*)((char*)page_header - offset_of_header);
    // 通过索引反向计算页地址时，基地址为VMM_START
    usize page_info_idx = info - page_infos;
    void* page_addr = (void*)(VMM_START + page_info_idx * PAGE_SIZE);

    // 从页内分配一个块
    short offset = page_header->free_list_offset;
    void* ptr = (void*)((char*)page_addr + offset);
    FreeBlock* block = (FreeBlock*)ptr;

    // 更新空闲链表头
    page_header->free_list_offset = block->next_offset;
    page_header->free_count--;
    
    // 检查此页在分配后是否已满
    bool page_still_has_space = (page_header->free_count > 0);

    // 为了遵守锁顺序，必须先释放低级锁，才能去获取高级锁
    release_spinlock(&page_header->lock);

    // 6. 如果页未满，则将其重新加入全局链表
    if (page_still_has_space) {

        acquire_spinlock(&pool->global_lock);
        
        // 将其插回链表头部
        page_header->next_page = pool->pages;
        page_header->prev_page = NULL;
        if (pool->pages != NULL) {
            pool->pages->prev_page = page_header;
        }
        pool->pages = page_header;
        
        release_spinlock(&pool->global_lock);
    }
    // 如果页变满了，它就自然地脱离了非满页链表，不必再加进去

    return ptr;
}

/**
 * 释放由kalloc分配的内存块，ptr指向由kalloc返回的数据区的指针
 */
void kfree(void* ptr) {
    if (ptr == NULL) return;

    // 根据用户指针，计算出页地址，再计算出页号，最终从 page_infos 中找到页头
    void* page_addr = (void*)((usize)ptr & ~(PAGE_SIZE - 1));
    usize page_info_idx = ((usize)page_addr - VMM_START) / PAGE_SIZE;
    PagePoolHeader* page_header = &page_infos[page_info_idx].pool_header;

    // 从配置信息中获取对象大小，并遍历配置表找到对应的内存池
    int pool_idx = -1;
    for (int i = 0; i < (int)POOL_COUNT; i++) {
        if ((short)g_pools[i].obj_size == page_header->obj_size) {
            pool_idx = i;
            break;
        }
    }
    
    if (pool_idx == -1) {
        printk("kfree: Memory corruption detected! Pointer points to a page with an invalid obj_size (%d).\n", page_header->obj_size);
        return;
    }
    
    PoolManager* pool = &g_pools[pool_idx].manager;
    
    acquire_spinlock(&pool->global_lock);
    acquire_spinlock(&page_header->lock);

    // 检查此页在释放前是否已满
    bool was_full = (page_header->free_count == 0);

    // 将释放的块插回到页的空闲链表中
    FreeBlock* block_to_free = (FreeBlock*)ptr;
    block_to_free->next_offset = page_header->free_list_offset;
    // 偏移量是相对于页的起始地址计算的
    page_header->free_list_offset = (short)((char*)ptr - (char*)page_addr);
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
        // 如果页之前是满的(was_full)，它不在任何链表中，无需移除

        release_spinlock(&page_header->lock);
        release_spinlock(&pool->global_lock);
        
        kfree_page(page_addr);
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