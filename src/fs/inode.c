#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock* sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache* cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;


// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    // printk("Initing a new inode.\n");
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    // TODO
    for (usize i=1;i<sblock->num_inodes;i++){
        usize blockid=to_block_no(i);
        Block *block=cache->acquire(blockid);
        InodeEntry *entry=get_entry(block,i);
        if (entry->type==INODE_INVALID){
            memset(entry,0,sizeof(InodeEntry));
            entry->type=type;
            entry->num_links=0;
            entry->num_bytes=0;
            
            cache->sync(ctx, block);
            cache->release(block);
            return i;
        }
        cache->release(block);
        
        
    }
    // 这里是否手动需要panic?
    printk("disk is full.\n");
    PANIC();
    
    return 0;
}
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write);
// static int useless=0;
// see `inode.h`.
static void inode_lock(Inode* inode) {
    // printk("Inode %lld's ref count is %lld\n",inode->inode_no,inode->rc.count);
    ASSERT(inode->rc.count > 0);
    // TODO
    // if (acquire_sleeplock(&inode->lock)){
    //     useless=0;
    // }
    ASSERT(acquire_sleeplock(&inode->lock));
    if (!inode->valid) {
        // 读出数据，inode自动变成有效的
        inode_sync(NULL, inode, false);
    }

}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    // TODO
    release_sleeplock(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    usize block_no=to_block_no(inode->inode_no);
    Block *block=cache->acquire(block_no);
    InodeEntry *entry=get_entry(block, inode->inode_no);


    if (do_write){
        if (!inode->valid)PANIC();
        ASSERT(ctx!=NULL);
        *entry=inode->entry;
        cache->sync(ctx, block);
    }else if (!inode->valid){
        inode->entry=*entry;
        inode->valid=true;
    }
    cache->release(block);
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    // printk("Getting inode: %llu\n", inode_no);
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    // printk("152:\n");
    acquire_spinlock(&lock);
    // printk("154:\n");
    // TODO
    Inode *empty=NULL;
    _for_in_list(node,&head){
        if (node==&head)continue;
        Inode *temp=container_of(node,Inode,node);
        if (temp->inode_no==inode_no){
            temp->rc.count++;
            release_spinlock(&lock);
            // printk("163\n");
            // inode_lock(temp);
            // printk("165\n");
            // if (!temp->valid){
            //     inode_sync(NULL,temp,false);
            // }
            // printk("169\n");
            return temp;

        }

    }
    if (empty==NULL){
        empty=(Inode *)kalloc(sizeof(Inode));
        if (empty==NULL){
            PANIC();
        }
    
    }
    //删去复用逻辑，不然put会有bug

    init_inode(empty);
    _insert_into_list(head.prev,&empty->node);
    empty->inode_no=inode_no;
    empty->rc.count=1;
    // empty->valid=false;
    release_spinlock(&lock);

    return empty;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    
    InodeEntry *entry=&inode->entry;
    // printk("Clearing inode: %lld\n",inode->inode_no);
    for (usize i=0;i<INODE_NUM_DIRECT;i++){
        if (entry->addrs[i]!=0){
            cache->free(ctx, entry->addrs[i]);
            entry->addrs[i]=0;
        }
    }
    usize indirect_block_no=entry->indirect;
    if (indirect_block_no){
        Block *block=cache->acquire(indirect_block_no);
        u32 *indirect_addrs=get_addrs(block);
        for (usize i=0;i<INODE_NUM_INDIRECT;i++){
            if (indirect_addrs[i]){
                cache->free(ctx,indirect_addrs[i]);
            }
        }
        cache->release(block);
        cache->free(ctx,indirect_block_no);
        entry->indirect=0;
    }
    entry->num_bytes=0;
    inode_sync(ctx,inode,true);

}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    acquire_spinlock(&lock);
    ASSERT(inode->rc.count>0);
    inode->rc.count++;
    release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    acquire_spinlock(&lock);

    // 第一轮检查（粗略检查）
    // 这里即使 valid 为 false 也可以放行，因为我们会在里面 load
    if (inode->rc.count == 1 && (!inode->valid || inode->entry.num_links == 0)) {
        
        release_spinlock(&lock); // --- 开启竞态窗口 ---

        // 获取 inode 锁
        // 如果 valid=false，这里会触发磁盘读取 (Lazy Load)
        // 这一步之后，inode->valid 必然为 true
        inode_lock(inode);

        // --- 竞态修复核心开始 ---
        
        // 重新获取全局锁，进行“二次确认”
        acquire_spinlock(&lock);
        
        // 必须同时满足三个条件才能真正删除：
        // 1. rc 依然是 1（没有其他线程在窗口期 get 了）
        // 2. num_links 是 0（确实被 unlink 了）
        // 3. valid 是 true (inode_lock 保证了这点，防御性检查)
        if (inode->rc.count == 1 && inode->entry.num_links == 0) {
            
            // 只有确认安全了，才释放全局锁去进行 I/O
            release_spinlock(&lock);
            
            // 执行删除操作
            inode_clear(ctx, inode);
            inode->entry.type = INODE_INVALID;
            inode_sync(ctx, inode, true);
            
        } else {
            // 如果进到这里，说明在窗口期有别的线程 get 了这个 inode (rc > 1)
            // 或者读完磁盘发现 num_links > 0
            // 此时什么都不能做，直接释放全局锁
            release_spinlock(&lock);
        }
        

        inode_unlock(inode);
        
        // 重新获取锁进入常规引用递减流程
        acquire_spinlock(&lock);
    }

    // 此时持有全局 lock
    inode->rc.count--;

    if (inode->rc.count == 0) {
        _detach_from_list(&inode->node);
        release_spinlock(&lock);
        kfree(inode);
    } else {
        release_spinlock(&lock);
    }
}
/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
/**
@brief 获取 inode 的偏移量所在的块。

例如 `inode_map(ctx, my_inode, 1234, &modified)` 将返回 block_no
包含文件第 1234 个字节的块
由 `my_inode` 表示。

如果该字节尚未分配块，`inode_map` 将
分配一个新块并更新 `my_inode`，此时，`modified`
将被设置为 true。

然而，如果 `ctx == NULL`，`inode_map` 将不会尝试分配任何新块，
当它发现该块未被分配时，将返回 0。
    
@param[out] modified 如果分配了新的块，则为 true，并且 `inode`
已经被更改。

@return usize 该区块的区块编号，如果 `ctx == NULL` 则返回 0 和
所需块未被分配。

@注意 调用者必须持有`inode`的锁。
*/
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    usize data_block_no=offset/BLOCK_SIZE;
    InodeEntry *entry=&inode->entry;
    if (data_block_no<INODE_NUM_DIRECT){
        if (!entry->addrs[data_block_no]){
            if (ctx==NULL){
                return 0;
            }
            entry->addrs[data_block_no]=cache->alloc(ctx);
            *modified=true;
        }
        return entry->addrs[data_block_no];
    }
    usize data_block_no_indirect=data_block_no-INODE_NUM_DIRECT;
    if (data_block_no_indirect>=INODE_NUM_INDIRECT){
        PANIC();
        return 0;
    }
    usize indirect_block_no=entry->indirect;
    if (indirect_block_no==0){
        if (ctx==NULL)return 0;
        indirect_block_no=entry->indirect=cache->alloc(ctx);
        *modified=true;
    }
    Block *indirect_block=cache->acquire(indirect_block_no);
    u32 *indirect_addrs=get_addrs(indirect_block);
    usize result_block=indirect_addrs[data_block_no_indirect];
    if (!result_block){
        if (ctx==NULL){
            cache->release(indirect_block);
            return 0;
        }
        result_block=cache->alloc(ctx);
        indirect_addrs[data_block_no_indirect]=result_block;
        cache->sync(ctx,indirect_block);

    }
    cache->release(indirect_block);
    return result_block;
    // return 0;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    usize total_read=0;
    usize current_off=offset;
    u8*current_dest=dest;
    while (total_read<count){
        usize off_in_block=current_off%BLOCK_SIZE;
        usize bytes_left_in_block=BLOCK_SIZE-off_in_block;
        usize bytes_to_copy=count-total_read;
        if (bytes_to_copy>bytes_left_in_block){
            bytes_to_copy=bytes_left_in_block;
        }
        bool modified=false;
        usize block_no=inode_map(NULL,inode,current_off,&modified);
        if (block_no==0){
            memset(current_dest,0,bytes_to_copy);
        }else{
            Block *block=cache->acquire(block_no);
            memcpy(current_dest,block->data+off_in_block,bytes_to_copy);
            cache->release(block);
        }
        total_read+=bytes_to_copy;
        current_off+=bytes_to_copy;
        current_dest+=bytes_to_copy;
    }
    return total_read;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    usize total_write=0;
    usize current_offset=offset;
    usize current_source=(usize)src;
    while (total_write<count){
        usize off_in_block=current_offset%BLOCK_SIZE;
        usize bytes_left_in_block=BLOCK_SIZE-off_in_block;
        usize bytes_to_write=count-total_write;
        if (bytes_to_write>bytes_left_in_block){
            bytes_to_write=bytes_left_in_block;
        }
        bool modified=false;
        usize block_no=inode_map(ctx,inode,current_offset,&modified);
        if (block_no==0){
            printk("Unable to write.\n");
            PANIC();
        }
        Block *block=cache->acquire(block_no);
        memcpy(block->data+off_in_block,(const void *)current_source,bytes_to_write);
        cache->sync(ctx,block);
        cache->release(block);
        total_write+=bytes_to_write;
        current_offset+=bytes_to_write;
        current_source+=bytes_to_write;
    }
    if (current_offset>entry->num_bytes){
        entry->num_bytes=current_offset;
        inode_sync(ctx,inode,true);
    }
    return total_write;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry dir;
    
    usize step=sizeof(DirEntry);
    for (usize off=0;off<entry->num_bytes;off+=step){
        usize n=inode_read(inode,(u8 *)&dir,off,step);
        if (n!=step){
            break;//这里是否需要PANIC()?
        }
        if (dir.inode_no==0){
            continue;
        }
        if (strncmp(name,dir.name,FILE_NAME_MAX_LENGTH)==0){
            if (index!=NULL){
                *index=off/step;
            }
            return dir.inode_no;
        }
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    // cache->begin_op(&ctx);
    DirEntry dir;
    usize step=sizeof(DirEntry);
    // bool found_slot=false;
    if (inode_lookup(inode,name,NULL)){
        return -1;
    }
    usize off;
    for (off=0;off<entry->num_bytes;off+=step){
        usize n=inode_read(inode,(u8 *)&dir,off,step);
        if (n!=step){
            break;//是否应该panic
        }
        if (dir.inode_no==0){
            // found_slot=true;
            break;
        }
    }
    // if (!found_slot){
    //     cache->begin_op(ctx);
    //     entry->num_bytes+=step;
    //     bool modified;
    //     inode_map(ctx,inode,off,&modified);
    //     cache->end_op(ctx);
    // }

    memset(&dir,0,step);
    
    memcpy(dir.name,name,FILE_NAME_MAX_LENGTH);
    dir.inode_no=inode_no;
    
    inode_write(ctx,inode,(u8 *)&dir,off,step);
    
    // if (!found_slot){
    //     inode_sync(ctx, inode, true);
    // }
    
    // cache->end_op(&ctx);
    return off/step;
}

// see `inode.h`.
static INLINE usize bytes_to_blocks(usize bytes) {
    if (bytes == 0) return 0;
    return (bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    InodeEntry *entry = &inode->entry;
    usize total_entries = entry->num_bytes / sizeof(DirEntry);
    
    // 1. 边界检查
    if (index >= total_entries) return;

    // 2. 将目标位置写入全0 (标记为删除)
    DirEntry dir;
    memset(&dir, 0, sizeof(DirEntry));
    usize n = inode_write(ctx, inode, (u8 *)&dir, index * sizeof(DirEntry), sizeof(DirEntry));
    if (n != sizeof(DirEntry)) {
        PANIC();
    }

    // 3. 检查是否需要缩容 (是否删除了最后一个有效项)
    // 如果删除的不是最后一个项，直接返回 (留作空洞复用)
    if (index + 1 != total_entries) {
        return; 
    }

    // 4. 向前回溯，找到新的文件末尾 (处理尾部连续空洞的情况)
    // 例如 [A, hole, B(deleted)] -> 此时 index 指向 B
    // 我们需要循环检查，发现前一个是 hole，继续前移，直到找到 A 或开头
    usize new_num_bytes = entry->num_bytes;
    usize scan_idx = index; // 当前已知的末尾是 index (已被清零)
    
    // 从当前删除的位置向前扫描，直到找到一个非空的条目或者到达头部
    while (scan_idx > 0) {
        scan_idx--; // 看前一个
        DirEntry temp_dir;
        inode_read(inode, (u8*)&temp_dir, scan_idx * sizeof(DirEntry), sizeof(DirEntry));
        if (temp_dir.inode_no != 0) {
            // 找到了有效项，新大小应该是这个有效项之后
            new_num_bytes = (scan_idx + 1) * sizeof(DirEntry);
            break;
        }
        // 如果读出来是 0，说明也是空洞，继续向前
        if (scan_idx == 0) {
            // 扫描到了第0项还是空的，说明整个目录都空了
            new_num_bytes = 0;
        }
    }
    // 特殊情况：如果原先 index=0 且被删了，上面循环不会执行，size 直接设为 0
    if (index == 0) {
        new_num_bytes = 0;
    }

    // 5. 如果大小没有变化（例如中间删除），由于前面的 check 这里一般不会进
    if (new_num_bytes == entry->num_bytes) return;

    // 6. 核心逻辑：释放不再使用的物理块
    usize old_blocks_count = bytes_to_blocks(entry->num_bytes);
    usize new_blocks_count = bytes_to_blocks(new_num_bytes);

    // 遍历所有需要释放的块号 (从后往前释放)
    for (usize b = old_blocks_count; b > new_blocks_count; b--) {
        usize block_idx_to_free = b - 1; // 转换为 0-based 索引

        if (block_idx_to_free < INODE_NUM_DIRECT) {
            // A. 释放直接块
            if (entry->addrs[block_idx_to_free]) {
                cache->free(ctx, entry->addrs[block_idx_to_free]);
                entry->addrs[block_idx_to_free] = 0;
            }
        } else {
            // B. 释放间接块指向的数据块
            usize indirect_idx = block_idx_to_free - INODE_NUM_DIRECT;
            if (entry->indirect) {
                Block *idx_block = cache->acquire(entry->indirect);
                u32 *addrs = get_addrs(idx_block);
                
                if (addrs[indirect_idx]) {
                    cache->free(ctx, addrs[indirect_idx]);
                    addrs[indirect_idx] = 0;
                    // 这里必须 sync 间接块，因为我们修改了它的内容
                    cache->sync(ctx, idx_block);
                }
                cache->release(idx_block);
            }
        }
    }

    // 7. 处理间接索引块本身的释放 (User 提到的重点)
    // 如果新的大小已经不需要间接块了 (即完全装在直接块里)，但 entry->indirect 还在
    // 说明间接块现在是空的（或者是无用的），应该释放它。
    if (new_blocks_count <= INODE_NUM_DIRECT && entry->indirect != 0) {
        cache->free(ctx, entry->indirect);
        entry->indirect = 0;
    }

    // 8. 更新 inode 元数据并同步
    entry->num_bytes = new_num_bytes;
    inode_sync(ctx, inode, true);
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};

/**
    @brief read the next path element from `path` into `name`.
    
    @param[out] name next path element.

    @return const char* a pointer offseted in `path`, without leading `/`. If no
    name to remove, return NULL.

    @example 
    skipelem("a/bb/c", name) = "bb/c", setting name = "a",
    skipelem("///a//bb", name) = "bb", setting name = "a",
    skipelem("a", name) = "", setting name = "a",
    skipelem("", name) = skipelem("////", name) = NULL, not setting name.
 */
static const char* skipelem(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/**
    @brief look up and return the inode for `path`.

    If `nameiparent`, return the inode for the parent and copy the final
    path element into `name`.
    
    @param path a relative or absolute path. If `path` is relative, it is
    relative to the current working directory of the process.

    @param[out] name the final path element if `nameiparent` is true.

    @return Inode* the inode for `path` (or its parent if `nameiparent` is true), 
    or NULL if such inode does not exist.

    @example
    namex("/a/b", false, name) = inode of b,
    namex("/a/b", true, name) = inode of a, setting name = "b",
    namex("/", true, name) = NULL (because "/" has no parent!)
 */
static Inode* namex(const char* path,
                    bool nameiparent,
                    char* name,
                    OpContext* ctx) {
    /* (Final) TODO BEGIN */
    Inode *inode;
    if (*path=='/'){
        inode=inodes.get(ROOT_INODE_NO);
        while (*path=='/'){
            path++;
        }
    }else{
        inode=inodes.share(thisproc()->cwd);
    }
    while ((path=skipelem(path,name))!=NULL){
        inodes.lock(inode);
        if (nameiparent&&*path=='\0'){
            inodes.unlock(inode);
            return inode;
        }
        if (inode->entry.type!=INODE_DIRECTORY){
            inodes.unlock(inode);
            inodes.put(ctx,inode);
            return NULL;
        }
        usize next_inode_no=inodes.lookup(inode,name,NULL);
        if (next_inode_no==0){
            inodes.unlock(inode);
            inodes.put(ctx,inode);
            return NULL;
        }
        inodes.unlock(inode);
        inodes.put(ctx,inode);
        Inode *next_inode=inodes.get(next_inode_no);
        
    }
    if (nameiparent){
        inodes.put(ctx,inode);
        return NULL;
    }
    return inode;
    /* (Final) TODO END */
    // return 0;
}

Inode* namei(const char* path, OpContext* ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, false, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx) {
    return namex(path, true, name, ctx);
}

/**
    @brief get the stat information of `ip` into `st`.
    
    @note the caller must hold the lock of `ip`.
 */
void stati(Inode* ip, struct stat* st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}