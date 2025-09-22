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
void *VMM=NULL;//页表使用情况报告表
// 页表所在位置
page_info* page_infos=NULL;

static usize page_infos_size_bytes = 0; 
static usize memory_size_byte=0; //内存大小
static usize page_infos_pages=0;

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
    // printk("allocate %llu pages\n",page_infos_pages);

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
    usize page_info_idx = ((usize)page_addr - (usize)page_infos) / PAGE_SIZE;
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
    // usize pool_obj_size = g_pools[pool_idx].obj_size;


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
            return NULL;
        }
        // 将新页加入到池的链表头部
        page_header->prev_page=NULL;
        page_header->next_page = pool->pages;
        if (pool->pages != NULL) {
            pool->pages->prev_page = page_header;
        }
        pool->pages = page_header;
    }

    // 从找到的页面中分配一个块
    acquire_spinlock(&page_header->lock);

    // 在编译时计算出pool_header这个成员变量在一个page_info结构体中的偏移量
    page_info *p_info_base = 0;
    usize offset_of_header = (usize)((char*)&(p_info_base->pool_header) - (char*)p_info_base);
    page_info *info = (page_info*)((char*)page_header - offset_of_header);
    usize page_info_idx = info - page_infos;
    void* page_addr = (void*)((char*)page_infos + page_info_idx * PAGE_SIZE);

    // 取出空闲链表的第一个块
    short offset = page_header->free_list_offset;
    void* ptr = (void*)((char*)page_addr + offset);
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

    // 根据用户指针，计算出页地址，再计算出页号，最终从 page_infos 中找到页头
    void* page_addr = (void*)((usize)ptr & ~(PAGE_SIZE - 1));
    usize page_info_idx = ((usize)page_addr - (usize)page_infos) / PAGE_SIZE;
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