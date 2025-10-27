#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <common/string.h>
// extern usize end;
// #define Kernel_end_link (usize)(((usize)end + 7) & (~((usize)0x7)))
// #define user_end_link P2K_WO(PHYSTOP)
// #define user_address_start (usize)((end + 7) & (~((usize)0x7)))
// #define user_address_end PHYSTOP
// #define page_count ((user_end_link-Kernel_end_link)/(PAGE_SIZE));


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

void kinit() {
    init_rc(&kalloc_page_cnt);
    // 先计算开始的位置
    
    // 利用K2P_WO(x)得到物理地址
    // usize user_address_start=K2P_WO(end);
    // usize user_address_end=PHYSTOP;
    // printk("user_address_start: %llu",user_address_start);
    // printk("user_address_end: %llu",user_address_end);
    // 使用 PAGE_SIZE计算可以分配的页数（向下取整）
    // usize page_count=(usize)((user_address_end-user_address_start)/(PAGE_SIZE));

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
    // // 向kalloc_page请求一页
    // ListNode* new_page=kalloc_page();
    // usize new_page_count= ((usize)new_page-user_start_link)/(PAGE_SIZE);
    // 在这一页上存放这个很大的表
    //// 向kalloc请求分配一块区域，这里改为手动完成
    // //2个字节足以完成记录这一个区域有多大(2字节是16位,2^16>4096)不足以
    // short* table_size=new_page;
    // *table_size=sizeof(page_info)*page_count;
    // // page_infos的起始位置
    // page_infos=new_page+sizeof(char);
    //
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
    // 这一部分初始化空闲页表的工作和kfree_page重合，放到kfree_page里
    // free_page_list=(ListNode *)user_start_link;
    // init_list_node(free_page_list);
    // ListNode *temp;
    // for (usize i=1;i< page_count; i++){
    //     temp=(ListNode *)(user_start_link+(i+1)*PAGE_SIZE);
    //     _insert_into_list(free_page_list,temp);
    // } 
    // for (usize i=0;i<page_count;i++){
    //     if (page_count==new_page_count){
    //         page_infos[page_count].page_ref_count.count=0;
    //         increment_rc(&page_infos[new_page_count].page_ref_count);    
    //     }
    //     else{
    //         page_infos[page_count].page_ref_count.count=0;
    //     }
    // }
    // 将存放VMM的该页的引用计数增加
    for (usize i=0; i<page_count;i++){
        // 每一页页表的对应页表行
        page_info *this_page_info=&page_infos[i];
        if (i!=32754)this_page_info->page_ref_count.count=0;
        if (i<page_infos_pages){
            increment_rc(&this_page_info->page_ref_count);
        }
        // printk("Record Page %llu\n", i);
    }
    printk("allocate %llu pages\n",page_infos_pages);
}
static int times=0;
void* kalloc_page() {
    acquire_spinlock(&VMM_lock);
    increment_rc(&kalloc_page_cnt);
    // printk("allocate a new page\n");
    
    if (free_page_list==NULL){
        if (times==0)printk("Great Error. First time failed.\n");
        decrement_rc(&kalloc_page_cnt);
        printk("Pages have been used out.\n");
        release_spinlock(&VMM_lock);
        return NULL;
    }
    times+=1;
    //此时不应当出现free_page_list为空的情况，因为还有空页表
    ListNode *temp_node=free_page_list;
    // 接受返回的前一个节点（可能为空）
    free_page_list=_detach_from_list(temp_node);
    
    release_spinlock(&VMM_lock);

    memset(temp_node,0 ,PAGE_SIZE);
    // 在kinit调用kalloc_page()时，这里不会被调用
    // if (page_infos!=NULL){
    //     ;
    // }
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

// typedef struct BlockHeader{
//     union{
//         struct BlockHeader *next_free;
//         struct SlabHeader *slab;
//     };
// }BlockHeader;

// 2字节的页上，只有页开头存放该页还有多少剩余
// typedef struct _n_bytes_page_header{
//     char typeofpool;
//     SpinLock _n_bytes_pool_lock;
//     // 记录这个n字节池的容量
//     short storage;
//     // 记录该页剩余n字节的数量
//     short count;

//     // 记录下一个n字节池的位置
//     struct _n_bytes_page_header* next;

//     // 下一块空闲的n字节所在的位置
//     short offset;
// }_n_bytes_page_header;
// _n_bytes_page_header *_2_bytes_pool=NULL;
// _n_bytes_page_header *_4_bytes_pool=NULL;
// _n_bytes_page_header *_8_bytes_pool=NULL;

// typedef struct _8_bytes_header{
//     short prev;
//     short next;
//     short length;// 这一块空区域的长度8*length
// }_8_bytes_header;

// void *query_for_n_bytes_pool(usize n){
//     void *p;
//     if (n<=2){
//         if (_2_bytes_pool==NULL){
//             _2_bytes_pool=kalloc_page();
//             _2_bytes_pool->typeofpool=(char)2;
//             _2_bytes_pool->_n_bytes_pool_lock.locked=false;
//             _2_bytes_pool->storage=(short)((PAGE_SIZE-sizeof(_n_bytes_page_header))/2);
//             _2_bytes_pool->count=0;
//             _2_bytes_pool->next=NULL;
//             _2_bytes_pool->offset=((sizeof(_n_bytes_page_header)+1)&(~1));
//             short offset=_2_bytes_pool->offset;
//             for (short *temp=((usize)_2_bytes_pool+_2_bytes_pool->offset);(usize)temp+sizeof(short)<=(usize)_2_bytes_pool+PAGE_SIZE;temp++){
//                 *temp=offset+2;
//                 offset+=2;
//             }
//         }
//         _n_bytes_page_header *temp=_2_bytes_pool;
//         acquire_spinlock(&temp->_n_bytes_pool_lock);
//         temp->count-=1;
//         if (temp->count==0){
//             _2_bytes_pool=temp->next;
//         }
//         p=(void *)((usize)temp+temp->offset);
//         temp->offset=*((short *)p);
//         release_spinlock(&temp->_n_bytes_pool_lock);
//         return p;
//     }else if (n<=4){
//         if (_4_bytes_pool==NULL){
//             _4_bytes_pool=kalloc_page();
//             _4_bytes_pool->typeofpool=(char)4;
//             _4_bytes_pool->_n_bytes_pool_lock.locked=false;
//             _4_bytes_pool->storage=(short)((PAGE_SIZE-sizeof(_n_bytes_page_header))/4);
//             _4_bytes_pool->count=0;
//             _4_bytes_pool->next=NULL;
//             _4_bytes_pool->offset=((sizeof(_n_bytes_page_header)+3)&(~3));
//             short offset=_4_bytes_pool->offset;
//             for (short *temp=((usize)_4_bytes_pool+_4_bytes_pool->offset);(usize)temp+4<=(usize)_4_bytes_pool+PAGE_SIZE;temp+=2){
//                 *temp=offset+4;
//                 offset+=4;
//             }
//         }
//         _n_bytes_page_header *temp=_4_bytes_pool;
//         acquire_spinlock(&temp->_n_bytes_pool_lock);
//         temp->count-=1;
//         if (temp->count==0){
//             _4_bytes_pool=temp->next;
//         }
//         p=(void *)((usize)temp+temp->offset);
//         temp->offset=*((short *)p);
//         release_spinlock(&temp->_n_bytes_pool_lock);
//         return p;
//     }
//     // 下面处理大于4字节的情况
//     // 首先向上对齐到8字节
//     n=(n+7)&(~7);
//     usize count=n/8+1; //预留8个字节的槽用于预留记录这一项使用了多少字节
//     _n_bytes_page_header *temp=_8_bytes_pool;
//     tag:
//     while (temp!=NULL){
//         acquire_spinlock(&temp->_n_bytes_pool_lock);
//         if (temp->count<count){
//             release_spinlock(&temp->_n_bytes_pool_lock);
//             continue;
//         }
//         usize pointer=(usize)temp+temp->offset;
//         _8_bytes_header *point=pointer;
//         while (point->length<n){
//             if (point->next==0) break;
//             pointer =(usize)temp+point->next;
//         }
//     }
//     if (p==NULL){
//         goto tag;
//     }

// }

// // 考虑到有的内存占用很大，
// void* kalloc(unsigned long long size) {
    
    
    
//     return NULL;

// }

// void kfree(void* ptr) {
//     return;
// }


// // ====================================================================
// // 2. 小块内存分配器 (Small Block Allocator) - 重写版本
// // ====================================================================

// /**
//  * @brief 内存块的布局设计 (Memory Block Layout)
//  * 
//  * 我们的分配器将一个物理页(Page)分割成许多固定大小的小块(Block)。
//  * 为了在释放(kfree)时能够管理它们，我们需要一些元数据。
//  *
//  * 关键设计：元数据只在块空闲时占用块自身的空间。
//  *
//  * 1. 当一个块是空闲的 (Free):
//  *    这个块的内存被用作一个链表节点，指向下一个空闲块。
//  *
//  *    +----------------------+
//  *    | BlockHeader          | <-- 指向这块内存的指针被保存在页头的 free_list 中
//  *    |----------------------|
//  *    | .next_free           | --> 指向此页内下一个空闲块的 BlockHeader
//  *    +----------------------+
//  *
//  * 2. 当一个块被分配后 (Allocated):
//  *    整个块的内存都可用于存储数据。kalloc() 返回的指针就是这个块的起始地址。
//  *    我们不需要在已分配的块中存储任何元数据。
//  *
//  *    +----------------------+
//  *    | User Data Area       | <-- kalloc() 返回这个地址
//  *    |                      |
//  *    | (整个块的空间)       |
//  *    +----------------------+
//  *
//  * 如何在 kfree(ptr) 时找到元数据？
//  * - 我们知道 ptr 是页内的一个地址。通过 `(ptr & ~(PAGE_SIZE - 1))` 我们可以得到该页的基地址。
//  * - 页的基地址存放着 PageSlab 结构，其中包含了所有管理信息。
//  */

// /**
//  * @brief 空闲内存块的头部结构。
//  * @note  这个结构只在块空闲时有意义。它将所有空闲块链接成一个单向链表。
//  *        在 AArch64 (64位) 架构下，一个指针是8字节，所以我们的最小分配单元是8字节。
//  *        这自动满足了所有对齐要求。
//  */
// typedef struct BlockHeader {
//     struct BlockHeader* next_free; // 指向页内下一个空闲块
// } BlockHeader;


// /**
//  * @brief 页级Slab头结构 (Page Slab Header)。
//  * @note  这个结构被放置在每个用于小块分配的物理页的最开始处。
//  *        它负责管理该页内所有小块的分配和回收。
//  *        会被 grow_cache() 创建和初始化。
//  *        会被 kalloc() 和 kfree() 使用。
//  */
// typedef struct PageSlab {
//     struct KmemCache* owner_cache; // 指针，指向管理这个Slab的KmemCache
//     BlockHeader* free_list;        // 指向此页内第一个空闲块的指针
//     usize alloc_count;             // 此页上已分配块的数量 (我们的引用计数)
//     ListNode list_node;            // 用于将此Slab链接到KmemCache的链表中
// } PageSlab;


// /**
//  * @brief 特定大小内存的缓存管理器 (KmemCache)。
//  * @note  这是Slab分配器的核心。每个KmemCache负责管理一种固定大小的内存块。
//  *        例如，一个KmemCache用于8字节的分配，另一个用于16字节的分配。
//  *        会被 kalloc_init() 初始化。
//  *        会被 kalloc() 和 kfree() 查找和使用。
//  */
// typedef struct KmemCache {
//     SpinLock lock;          // 保护此Cache的自旋锁
//     const char* name;       // Cache的名字，用于调试
//     usize block_size;       // 此Cache管理的每个内存块的大小 (包含头部)
    
//     ListNode partial_slabs; // 部分分配的Slab链表 (有空闲块，但非完全空闲)
//     ListNode full_slabs;    // 已满的Slab链表 (所有块都已分配)
// } KmemCache;


// // --- 定义我们的内存池 (Caches) ---

// // 我们支持的内存块大小。这个数组必须是升序的。
// // 最小为8字节，因为BlockHeader需要一个指针(8字节)。
// // 所有小于8字节的请求(如2, 4字节)都会从8字节池分配。
// const usize BLOCK_SIZES[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
// #define KMEM_CACHE_COUNT (sizeof(BLOCK_SIZES) / sizeof(usize))

// // KmemCache的全局静态数组。内存由编译器在.bss段中预留。
// KmemCache caches[KMEM_CACHE_COUNT];


// /**
//  * @brief 初始化所有KmemCache。
//  * @note  应在内核启动的早期，在kinit()之后被调用。
//  */
// void kalloc_init() {
//     for (usize i = 0; i < KMEM_CACHE_COUNT; i++) {
//         init_spinlock(&caches[i].lock);
//         // 动态生成名字，例如 "cache-8", "cache-16"
//         // 注意: 这需要一个简单的itoa或sprintf实现，这里为了简化，使用固定名字
//         // static char names[KMEM_CACHE_COUNT][16];
//         // snprintf(names[i], 16, "cache-%d", BLOCK_SIZES[i]);
//         // caches[i].name = names[i];
//         caches[i].name = "kmem_cache"; // 简化处理
//         caches[i].block_size = BLOCK_SIZES[i];
//         init_list_node(&caches[i].partial_slabs);
//         init_list_node(&caches[i].full_slabs);
//     }
// }

// /**
//  * @brief 为一个指定的Cache分配并初始化一个新的Slab页。
//  * @param cache 需要新页的KmemCache。
//  * @return成功返回true，失败(内存不足)返回false。
//  */
// static bool grow_cache(KmemCache* cache) {
//     // 1. 从底层页分配器获取一个干净的物理页
//     void* page = kalloc_page();
//     if (!page) {
//         return false;
//     }

//     // 2. 在页的起始位置放置PageSlab头
//     PageSlab* slab = (PageSlab*)page;
//     slab->owner_cache = cache;
//     slab->alloc_count = 0;
//     slab->free_list = NULL; // 准备构建空闲链表

//     // 3. 计算本页能容纳多少个小块
//     usize num_blocks = (PAGE_SIZE - sizeof(PageSlab)) / cache->block_size;
    
//     // 4. 将页的剩余空间切割成小块，并将它们全部加入到空闲链表
//     char* p_block = (char*)page + sizeof(PageSlab);
//     for (usize i = 0; i < num_blocks; i++) {
//         BlockHeader* block = (BlockHeader*)p_block;
//         block->next_free = slab->free_list; // 头插法
//         slab->free_list = block;
//         p_block += cache->block_size;
//     }

//     // 5. 将这个新的、非满的Slab页加入到Cache的partial_slabs链表中
//     _insert_into_list(&cache->partial_slabs, &slab->list_node);
//     return true;
// }

// /**
//  * @brief 分配一块指定大小的小块内存。
//  * @param size 用户请求的内存大小。
//  * @return 成功则返回一个至少8字节对齐的内存指针，失败则返回NULL。
//  */
// void* kalloc(usize size) {
//     // 1. 根据请求的大小，找到最合适的KmemCache
//     KmemCache* cache = NULL;
//     for (usize i = 0; i < KMEM_CACHE_COUNT; i++) {
//         // 找到第一个能容纳下请求大小的池
//         if (size <= caches[i].block_size) {
//             cache = &caches[i];
//             break;
//         }
//     }

//     // 2. 如果请求的大小超过了我们最大池的能力，则分配失败
//     if (!cache) {
//         printk("kalloc: requested size %llu too large for any cache\n", size);
//         return NULL;
//     }

//     acquire_spinlock(&cache->lock);

//     // 3. 检查是否有部分填充的Slab页。如果没有，就创建一个新的。
//     if ((ListNode *)(&cache->partial_slabs.next)==(ListNode *)&cache->partial_slabs) {
//         if (!grow_cache(cache)) {
//             release_spinlock(&cache->lock);
//             printk("kalloc: OOM failed to grow cache for size %llu\n", cache->block_size);
//             return NULL;
//         }
//     }

//     // 4. 从partial_slabs链表的第一个Slab中取出一个空闲块
//     PageSlab* slab = container_of(cache->partial_slabs.next, PageSlab, list_node);
//     BlockHeader* block = slab->free_list;
//     slab->free_list = block->next_free; // 从空闲链表头部移除
//     slab->alloc_count++;                // 引用计数加一

//     // 5. 如果这个Slab现在满了，将它移动到full_slabs链表
//     if (slab->free_list == NULL) {
//         _detach_from_list(&slab->list_node);
//         _insert_into_list(&cache->full_slabs, &slab->list_node);
//     }

//     release_spinlock(&cache->lock);

//     // 6. 返回指向块起始地址的指针 (即用户数据区)
//     return (void*)block;
// }

// /**
//  * @brief 释放由kalloc分配的内存块。
//  * @param ptr kalloc返回的内存指针。
//  */
// void kfree(void* ptr) {
//     if (ptr == NULL) {
//         return;
//     }

//     // 1. 根据指针ptr，计算出它所在的物理页的基地址
//     void* page_addr = (void*)((usize)ptr & ~(PAGE_SIZE - 1));
//     PageSlab* slab = (PageSlab*)page_addr;

//     // 2. 从Slab头中获取它所属的KmemCache
//     KmemCache* cache = slab->owner_cache;

//     acquire_spinlock(&cache->lock);

//     // 3. 将被释放的块重新加入到Slab的空闲链表头部
//     BlockHeader* block = (BlockHeader*)ptr;
//     block->next_free = slab->free_list;
//     slab->free_list = block;
//     slab->alloc_count--; // 引用计数减一

//     // 4. 检查Slab的状态变化
//     bool was_full = (block->next_free == NULL); // 如果free_list之前是空的，说明Slab是满的
//     if (was_full) {
//         // 从full_slabs移动到partial_slabs
//         _detach_from_list(&slab->list_node);
//         _insert_into_list(&cache->partial_slabs, &slab->list_node);
//     }
    
//     // 5. 【核心回收逻辑】如果引用计数为0，说明整个页都空闲了，释放它
//     if (slab->alloc_count == 0) {
//         // 从partial_slabs链表中移除
//         _detach_from_list(&slab->list_node);
//         // 将整个物理页归还给底层页分配器
//         kfree_page(page_addr); 
//     }

//     release_spinlock(&cache->lock);
// }

typedef struct PagePoolHeader {
    // 指向池中下一个页的指针，形成一个简单的单向链表。
    // 如果这是链表中最后一个页，则为 NULL。
    struct PagePoolHeader* next_page;

    // 保护该页内部状态（如 free_list_offset 和 free_count）的自旋锁
    SpinLock lock;
    
    // 该页管理的对象大小（例如 2, 4, 8, ..., 2048）。
    // 这个字段在 kfree 时至关重要，用于反向查找该页属于哪个池。
    short obj_size;

    // 该页最多可以容纳的小块的总数量。
    // 在页面初始化后，这个值是固定不变的。
    short storage;

    // 该页当前剩余的空闲小块数量。
    // 当 free_count == storage 时，该页完全空闲，可以被释放。
    short free_count;

    // 指向页内第一个空闲块的偏移量（相对于页起始地址）。
    // 值为 0 表示该页已满，没有空闲块了。
    short free_list_offset;

} PagePoolHeader;

/**
 * @brief 覆盖在空闲内存块上的视图
 * @note  当一个内存块是空闲状态时，我们将它的起始位置解释为这个结构体，
 *        用它来存储指向下一个空闲块的偏移量，形成一个侵入式空闲链表。
 *        它的大小必须小于等于它所在池的最小对象大小（即2字节）。
 */
typedef struct FreeBlock {
    // 下一个空闲块的偏移量。
    short next_offset;
} FreeBlock;

/**
 * @brief 内存池管理器
 * @note  这是特定大小内存块的全局管理器。
 */
typedef struct PoolManager {
    // 保护下面 pages 链表的全局自旋锁。
    // 当需要从链表中添加或删除页面时，必须持有此锁。
    SpinLock global_lock;

    // 指向该池管理的页面链表的头节点。
    PagePoolHeader* pages;

} PoolManager;


// --- 全局变量与配置 ---

// 定义支持的池的数量，从 2^1=2 到 2^11=2048
#define POOL_MIN_LOG 1  // 2^1 = 2 bytes
#define POOL_MAX_LOG 11 // 2^11 = 2048 bytes
#define POOL_COUNT (POOL_MAX_LOG - POOL_MIN_LOG + 1)

// 全局的内存池管理器数组
PoolManager pools[POOL_COUNT];

/**
 * @brief 初始化所有的小块内存池
 * @note  应该在内核初始化早期，在页分配器(kinit)之后调用。
 */
void kalloc_pools_init() {
    for (int i = 0; i < POOL_COUNT; i++) {
        init_spinlock(&pools[i].global_lock);
        pools[i].pages = NULL;
    }
}

/**
 * @brief 为一个指定的池分配并初始化一个新的专用页
 * @param pool_idx 需要扩容的池的索引
 * @return 成功则返回新页的头部指针，失败（内存不足）返回 NULL
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
 * @brief 分配一块指定大小的内存
 * @param size 用户请求的内存大小
 * @return 成功则返回指向用户数据区的指针，失败则返回NULL
 */
void* kalloc(usize size) {
    if (size == 0 || size > (1 << POOL_MAX_LOG)) {
        printk("kalloc: Invalid or unsupported size %llu\n", size);
        return NULL; // 或者分配一个整页
    }

    // 1. 根据请求大小，选择最合适的内存池
    int pool_idx = 0;
    usize pool_obj_size = 1 << POOL_MIN_LOG;
    while (pool_obj_size < size) {
        pool_obj_size <<= 1;
        pool_idx++;
    }
    PoolManager* pool = &pools[pool_idx];

    acquire_spinlock(&pool->global_lock);

    // 2. 遍历页面链表，查找有空闲块的页面
    PagePoolHeader* page_header = pool->pages;
    while (page_header != NULL && page_header->free_count == 0) {
        page_header = page_header->next_page;
    }

    // 3. 如果没有找到可用页面，则创建一个新页面
    if (page_header == NULL) {
        page_header = grow_pool(pool_idx);
        if (page_header == NULL) {
            release_spinlock(&pool->global_lock);
            printk("kalloc: out of memory for pool size %llu\n", pool_obj_size);
            return NULL;
        }
        // 将新页加入到池的链表头部
        page_header->next_page = pool->pages;
        pool->pages = page_header;
    }

    // 4. 从找到的页面中分配一个块
    acquire_spinlock(&page_header->lock);

    // 取出空闲链表的第一个块
    short offset = page_header->free_list_offset;
    void* ptr = (void*)((char*)page_header + offset);
    FreeBlock* block = (FreeBlock*)ptr;

    // 更新空闲链表头
    page_header->free_list_offset = block->next_offset;
    page_header->free_count--;
    
    release_spinlock(&page_header->lock);
    release_spinlock(&pool->global_lock);

    return ptr;
}

/**
 * @brief 释放由kalloc分配的内存块
 * @param ptr 指向由kalloc返回的数据区的指针
 */
// void kfree(void* ptr) {
//     if (ptr == NULL) return;

//     // 1. 根据用户指针，通过地址对齐反向计算出页头的地址
//     PagePoolHeader* page_header = (PagePoolHeader*)((usize)ptr & ~(PAGE_SIZE - 1));

//     // 2. 从页头中获取对象大小，并找到对应的内存池
//     int pool_idx = 0;
//     usize pool_obj_size = 1 << POOL_MIN_LOG;
//     while ((short)pool_obj_size < page_header->obj_size) {
//         pool_obj_size <<= 1;
//         pool_idx++;
//     }
//     // 安全检查，确保找到的池是正确的
//     if ((short)pool_obj_size != page_header->obj_size) {
//         printk("kfree: Memory corruption detected! Pointer points to an invalid page header.\n");
//         return;
//     }
//     PoolManager* pool = &pools[pool_idx];
    
//     acquire_spinlock(&page_header->lock);

//     // 3. 将释放的块以“头插法”插回到页的空闲链表中
//     FreeBlock* block_to_free = (FreeBlock*)ptr;
//     block_to_free->next_offset = page_header->free_list_offset;
//     page_header->free_list_offset = (short)((char*)ptr - (char*)page_header);
//     page_header->free_count++;
    
//     // 4. 检查该页是否已完全空闲
//     if (page_header->free_count == page_header->storage) {
//         // 此页已空，可以从池中移除并释放
//         release_spinlock(&page_header->lock); // 释放页锁，准备获取全局锁
        
//         acquire_spinlock(&pool->global_lock);
        
//         // 从池的单向链表中移除此页
//         PagePoolHeader* current = pool->pages;
//         PagePoolHeader* prev = NULL;
//         while (current != NULL && current != page_header) {
//             prev = current;
//             current = current->next_page;
//         }

//         if (current != NULL) { // 找到了
//             if (prev == NULL) { // 是头节点
//                 pool->pages = current->next_page;
//             } else {
//                 prev->next_page = current->next_page;
//             }
//         }
        
//         release_spinlock(&pool->global_lock);
        
//         // 最终，释放物理页
//         kfree_page(page_header);

//     } else {
//         // 如果没有完全空，只需释放页锁
//         release_spinlock(&page_header->lock);
//     }
// }
void kfree(void* ptr) {
    if (ptr == NULL) return;

    // 1. 根据用户指针，通过地址对齐反向计算出页头的地址
    PagePoolHeader* page_header = (PagePoolHeader*)((usize)ptr & ~(PAGE_SIZE - 1));

    // 2. 从页头中获取对象大小，并找到对应的内存池
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
    
    acquire_spinlock(&page_header->lock);

    // 3. 将释放的块以“头插法”插回到页的空闲链表中
    FreeBlock* block_to_free = (FreeBlock*)ptr;
    block_to_free->next_offset = page_header->free_list_offset;
    page_header->free_list_offset = (short)((char*)ptr - (char*)page_header);
    page_header->free_count++;
    
    bool should_free_page = (page_header->free_count == page_header->storage);
    
    release_spinlock(&page_header->lock);

    // 4. 如果页面可能已满，则尝试获取全局锁并移除它
    if (should_free_page) {
        acquire_spinlock(&pool->global_lock);
        
        // 重新获取页锁，准备进行最终检查
        acquire_spinlock(&page_header->lock);

        // 再次检查！可能在我们释放页锁和获取全局锁的间隙，
        // 另一个线程从这个页面分配了内存。
        if (page_header->free_count == page_header->storage) {
            // 确认该页仍然是空的，现在可以安全地移除了
            
            // 从池的单向链表中移除此页
            PagePoolHeader* current = pool->pages;
            PagePoolHeader* prev = NULL;
            while (current != NULL && current != page_header) {
                prev = current;
                current = current->next_page;
            }

            if (current != NULL) { // 找到了
                if (prev == NULL) { // 是头节点
                    pool->pages = current->next_page;
                } else {
                    prev->next_page = current->next_page;
                }

                // 释放锁之后再执行耗时操作
                release_spinlock(&page_header->lock);
                release_spinlock(&pool->global_lock);
        
                // 最终，释放物理页
                kfree_page(page_header);
                
                return; // 提前返回
            }
        }
        
        // 如果页不再是空的，或者在链表中没有找到（理论上不应该），
        // 那么就什么都不做，只需释放锁。
        release_spinlock(&page_header->lock);
        release_spinlock(&pool->global_lock);
    }
}