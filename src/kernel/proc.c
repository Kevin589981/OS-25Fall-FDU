#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/debug.h>
#include <kernel/core.h>
#include <kernel/cpu.h>
#ifndef NCPU
#define NCPU 4
#endif
#include <kernel/paging.h>

Proc root_proc;

void kernel_entry();
u64 proc_entry();

SpinLock global_process_lock;
int prio_to_weight[40]={
    /* -20 */     88761,     71755,     56483,     46273,     36291,
    /* -15 */     29154,     23254,     18705,     14949,     11916,
    /* -10 */      9548,      7620,      6100,      4904,      3906,
    /*  -5 */      3121,      2501,      1991,      1586,      1277,
    /*   0 */      1024,       820,       655,       526,       423,
    /*   5 */       335,       272,       215,       172,       137,
    /*  10 */       110,        87,        70,        56,        45,
    /*  15 */        36,        29,        23,        18,        15,
}; 
// unsigned long pid_bitmap[BITMAP_SIZE];

// // 用于记录下一次开始搜索空闲PID的位置，以提高效率
// static int next_pid_to_check = 0;

// // 初始化PID分配器
// void init_pid_allocator() {
//     // 初始化位图，将所有位清零，表示所有PID都未被分配
//     memset(pid_bitmap, 0, sizeof(pid_bitmap));
//     next_pid_to_check = 0;
//     // 保留PID 0
//     // set_bit(0, pid_bitmap);
// }

// // 回收一个PID
// void pid_recycler(int pid) {
//     if (pid < 0 || pid >= MAX_PID) {
//         printk("WARNING: Attempted to recycle invalid PID %d.\n", pid);
//         return;
//     }

// #ifdef PID_DEBUG
//     printk("Recycling PID: %d\n", pid);
// #endif

//     // 使用原子操作或在锁保护下清除对应位
//     // 这里我们假设它会在 global_process_lock 的保护下被调用
//     int index = pid / BITS_PER_LONG;
//     int offset = pid % BITS_PER_LONG;
//     pid_bitmap[index] &= ~(1UL << offset);
// }

// // 分配一个PID
// int pid_allocator() {
//     // 假设此函数总是在 global_process_lock 保护下调用
    
//     // 从上次检查的位置开始循环查找
//     for (int i = 0; i < MAX_PID; ++i) {
//         int pid = (next_pid_to_check + i) % MAX_PID;
//         if (pid == 0) continue; // 跳过PID 0

//         int index = pid / BITS_PER_LONG;
//         int offset = pid % BITS_PER_LONG;

//         // 检查该位是否为0 (未被占用)
//         if (!(pid_bitmap[index] & (1UL << offset))) {
//             // 标记该位为1 (已占用)
//             pid_bitmap[index] |= (1UL << offset);
            
//             // 更新下一次开始搜索的位置
//             next_pid_to_check = pid + 1;
//             if (next_pid_to_check >= MAX_PID) {
//                 next_pid_to_check = 1; // 回到开头
//             }
            
//             return pid;
//         }
//     }

//     // 如果循环一圈都没有找到空闲PID，说明PID已耗尽
//     return -1; // 返回错误码
// }
unsigned long pid_bitmap[BITMAP_SIZE];

// 用于记录下一次开始搜索空闲PID的位置，以提高效率
static int next_pid_to_check = 1;

// 初始化PID分配器
void init_pid_allocator() {
    // 初始化位图，将所有位清零，表示所有PID都未被分配
    memset(pid_bitmap, 0, sizeof(pid_bitmap));
    next_pid_to_check = 1;
    pid_bitmap[0] |= 1UL; 
}

// 回收一个PID (此函数已是O(1)，无需修改)
void pid_recycler(int pid) {
    if (pid <= 0 || pid >= MAX_PID) {
        printk("WARNING: Attempted to recycle invalid PID %d.\n", pid);
        return;
    }

#ifdef PID_DEBUG
    printk("Recycling PID: %d\n", pid);
#endif

    int index = pid / BITS_PER_LONG;
    int offset = pid % BITS_PER_LONG;
    pid_bitmap[index] &= ~(1UL << offset);
}

//==============================================================================
//           分配一个PID (使用 AArch64 内联汇编优化)
//==============================================================================
int pid_allocator() {
    // 假设此函数总是在锁保护下调用

    // 从上次检查的字开始，遍历整个位图
    usize start_index = (next_pid_to_check / BITS_PER_LONG) % BITMAP_SIZE;

    for (usize i = 0; i < BITMAP_SIZE; ++i) {
        int index = (start_index + i) % BITMAP_SIZE;
        unsigned long word = pid_bitmap[index];

        // 优化: 如果这个字的所有位都已设置 (全满)，则快速跳过。
        if (word == ~0UL) {
            continue;
        }

        // 找到了一个不满的字，现在使用硬件指令来快速找到第一个0位。
        unsigned long inverted_word = ~word;
        unsigned long offset;

        // 使用AArch64内联汇编计算尾随零的数量(Count Trailing Zeros)
        // 相当于找到 inverted_word 中第一个为1的位的索引
        asm volatile (
            "rbit %0, %1\n\t"  // 1. 将位的顺序颠倒
            "clz  %0, %0\n\t"  // 2. 计算颠倒后数字的前导零数量
            : "=r" (offset)    // 输出: offset ('='表示写入, 'r'表示通用寄存器)
            : "r" (inverted_word) // 输入: inverted_word ('r'表示通用寄存器)
        );

        // 计算最终的PID
        int pid = index * BITS_PER_LONG + offset;

        // 边界检查: 确保计算出的PID没有超过最大限制
        // 这在最后一个字不是完整填充的情况下很重要
        if (pid >= MAX_PID) {
            continue; // 继续搜索下一个可能的字
        }
        
        // 找到了一个有效的、空闲的PID

        // 标记该位为1 (已占用)
        pid_bitmap[index] |= (1UL << offset);

        // 更新下一次开始搜索的位置，以实现轮转效果
        next_pid_to_check = pid + 1;
        if (next_pid_to_check >= MAX_PID) {
            next_pid_to_check = 1; // 回到开头 (跳过PID 0)
        }

        return pid;
    }

    // 如果循环一圈都没有找到空闲PID，说明PID已耗尽
    return -1; // 返回错误码
}




Proc idle_procs[NCPU];

void init_kproc()
{
    init_spinlock(&global_process_lock);
    init_pid_allocator();
    
    // 2. init the root_proc (finished)

    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc *p)
{   
    acquire_spinlock(&global_process_lock);
    memset(p, 0, sizeof(Proc));

    init_sem(&p->childexit, 1);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->idle = FALSE;
    p->exitcode = 0;
    p->killed = FALSE;
    p->parent = NULL;
    p->pid = pid_allocator();
    if (p->pid==-1){
        kfree(p);
        release_spinlock(&global_process_lock);
        printk("PID used out\n");
        PANIC();
    }
    init_pgdir(&p->pgdir);
    p->state = UNUSED;
    p->kstack = kalloc_page();
    init_spinlock(&p->lock);
    init_schinfo(&p->schinfo);
    
    if (p->kstack == NULL) {
        PANIC();
    }
    memset(p->kstack, 0, PAGE_SIZE);
    p->ucontext = (UserContext *)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
    p->kcontext = (KernelContext *)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(UserContext)- sizeof(KernelContext));
    

    release_spinlock(&global_process_lock);
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    if (p==NULL){
        PANIC();
    }
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    acquire_spinlock(&global_process_lock);
    Proc *parent=thisproc();
    _detach_from_list(&proc->ptnode);
    proc->parent=parent;
    _insert_into_list(parent->children.prev,&proc->ptnode);
    release_spinlock(&global_process_lock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    acquire_spinlock(&global_process_lock);
    if (p->parent==NULL){
        
        p->parent=&root_proc;
        _detach_from_list(&p->ptnode);
        _insert_into_list(root_proc.children.prev, &p->ptnode);
        
    }
    
    p->kcontext->lr=(u64)&proc_entry;
    p->kcontext->x0=(u64)entry;
    p->kcontext->x1=(u64)arg;
    
    p->state=UNUSED;
    int id =p->pid;
    activate_proc(p);//activate函数内部有锁
    // NOTE: be careful of concurrency
#ifdef PID_DEBUG
    printk("cpuid: %lld, start pid: %d, its parent is %d\n",cpuid(),p->pid,p->parent->pid);
#endif
    release_spinlock(&global_process_lock);
    return id;
}

int wait(int *exitcode)
{
    while (1){
        Proc *parent=thisproc();
        acquire_spinlock(&global_process_lock);
        
        bool has_children = !_empty_list(&parent->children);
        if (!has_children){
            release_spinlock(&global_process_lock);
#ifdef PID_DEBUG
            printk("194: proc.c: no children, parent pid is %d\n",parent->pid);
#endif
            return -1;
        }

        Proc *zombie_child=NULL;
        _for_in_list(node, &parent->children) {
            if (node==&parent->children){
                continue;
            }
            Proc *p = container_of(node, Proc, ptnode);
            if (is_zombie(p)) {
                zombie_child = p;
#ifdef PID_DEBUG
                printk("Found zombie child pid: %d\n", p->pid);
#endif
                break;
            }
        }
        
        if (zombie_child){
            if (exitcode!=NULL){
                *exitcode=zombie_child->exitcode;
            }
            int child_pid=zombie_child->pid;
            pid_recycler(child_pid);
            if (zombie_child->kstack){
                kfree_page(zombie_child->kstack);
            }
            _detach_from_list(&zombie_child->ptnode);
            kfree(zombie_child);
            release_spinlock(&global_process_lock);
            return child_pid;
        }

        release_spinlock(&global_process_lock);
        if (!wait_sem(&parent->childexit)){
            return -1;
        }
    }
}

NO_RETURN void exit(int code)
{
    
     
        // TODO:
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency

    acquire_spinlock(&global_process_lock);
    Proc *p=thisproc();
#ifdef debug_sched
    printk("proc.c:224, p->state is %d\n",p->state);
#endif
    p->exitcode=code;
    
    // 将所有子进程过继给root_proc
    ListNode orphaned_children;
    init_list_node(&orphaned_children);
    while (!_empty_list(&p->children)) {
        ListNode *child_node = p->children.next;
        _detach_from_list(child_node);
        _insert_into_list(orphaned_children.prev, child_node);
    }
    while (!_empty_list(&orphaned_children)) {
        ListNode *child_node = orphaned_children.next;
        _detach_from_list(child_node);
        _insert_into_list(root_proc.children.prev, child_node);
        Proc *child = container_of(child_node, Proc, ptnode);
        child->parent = &root_proc;
        if (is_zombie(child)) {
            if ((u64)&root_proc<0x20){
                printk("Error proc.c: 312\n");
            }
            post_sem(&root_proc.childexit);
        }
    }
    ASSERT(p->parent!=NULL);
    if (!p->parent){
        p->parent=&root_proc;
        _detach_from_list(&p->ptnode);
        _insert_into_list(root_proc.children.prev, &p->ptnode);
    }

    if ((u64)p->parent<0x20){
        printk("BUG at proc.c :325\n");
        PANIC();
    }
    free_pgdir(&p->pgdir);
    post_sem(&p->parent->childexit);

    // 不在此处获取调度锁，sched()函数会自己处理
    acquire_sched_lock(); 
    
    // acquire_spinlock(&p->lock);

    release_spinlock(&global_process_lock);
    
    // printk("Ready to Enter zombie, Proc %d, cpu %lld\n",p->pid,cpuid());
    sched(ZOMBIE);
    printk("exit: sched(ZOMBIE) should not return");
    PANIC();
}

Proc *find_to_kill(int pid, Proc *parent){
    if (parent->pid==pid){
        if (parent->state==UNUSED){
            return NULL;
        }
        return parent;
    }
    _for_in_list(node,&parent->children){
        if (node==&parent->children)continue;
        Proc *child=container_of(node, Proc, ptnode);
        Proc *find=find_to_kill(pid, child);
        if (find) return find;
    }
    return NULL;
}

int kill(int pid)
{
    // TODO:
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    acquire_spinlock(&global_process_lock);
    // printk("try to kill %d\n",pid);
    Proc *target=find_to_kill(pid, &root_proc);
    // printk("find kill target %d\n",pid);
    if (!target){
        release_spinlock(&global_process_lock);
        return -1;
    }
    target->killed=true;
    alert_proc(target);
    release_spinlock(&global_process_lock);
    
    return 0;
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();

Proc* create_child_proc(Proc *parent_proc)
{
    Proc *child_proc = create_proc();
    acquire_spinlock(&global_process_lock);
    child_proc->parent = parent_proc;
    _insert_into_list(&parent_proc->children, &child_proc->ptnode);
    release_spinlock(&global_process_lock);
    return child_proc;
}

void copy_page_directory(Proc *parent_proc, Proc *child_proc)
{
    acquire_spinlock(&parent_proc->pgdir.lock);
    ListNode *sections_head = &parent_proc->pgdir.section_head;

    _for_in_list(section_node, sections_head)
    {
        if (section_node == sections_head) continue;
        
        Section *sec = container_of(section_node, Section, stnode);
        Section *new_sec = (Section *)kalloc(sizeof(Section));
        init_section(new_sec);
        new_sec->begin = sec->begin;
        new_sec->end = sec->end;
        new_sec->flags = sec->flags;

        if (sec->fp)
        {
            new_sec->fp = file_dup(sec->fp);
            new_sec->offset = sec->offset;
            new_sec->length = sec->length;
        }
        _insert_into_list(&child_proc->pgdir.section_head, &new_sec->stnode);

        for (u64 va = PAGE_BASE(sec->begin); va < sec->end; va += PAGE_SIZE)
        {
            PTEntriesPtr pte = get_pte(&parent_proc->pgdir, va, false);
            if (pte && (*pte & PTE_VALID))
            {
                *pte |= PTE_RO;
                vmmap(&child_proc->pgdir, va, (void *)P2K(PTE_ADDRESS(*pte)), PTE_FLAGS(*pte));
                kshare_page(P2K(PTE_ADDRESS(*pte)));
            }
        }
    }
    release_spinlock(&parent_proc->pgdir.lock);
}

void copy_file_table(Proc *parent_proc, Proc *child_proc)
{
    memset((void *)&child_proc->oftable, 0, sizeof(struct oftable));
    for (int i = 0; i < NOFILE; i++)
    {
        if (parent_proc->oftable.ofiles[i])
        {
            child_proc->oftable.ofiles[i] = file_dup(parent_proc->oftable.ofiles[i]);
        }
        else break;
    }
}

void copy_working_directory(Proc *parent_proc, Proc *child_proc)
{
    if (child_proc->cwd != parent_proc->cwd)
    {
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, child_proc->cwd);
        bcache.end_op(&ctx);
        child_proc->cwd = inodes.share(parent_proc->cwd);
    }
}

int fork()
{
    /**
     * (Final) TODO BEGIN
     * 
     * 1. Create a new child process.
     * 2. Copy the parent's memory space.
     * 3. Copy the parent's trapframe.
     * 4. Set the parent of the new proc to current proc.
     * 5. Set the state of the new proc to RUNNABLE.
     * 6. Activate the new proc and return its pid.
     */
    Proc *parent_proc = thisproc();
    Proc *child_proc = create_child_proc(parent_proc);
    memcpy((void *)child_proc->ucontext, (void *)parent_proc->ucontext, sizeof(UserContext));
    child_proc->ucontext->x[0] = 0;
    copy_page_directory(parent_proc, child_proc);
    copy_file_table(parent_proc, child_proc);
    copy_working_directory(parent_proc, child_proc);
    start_proc(child_proc, trap_return, 0);
    return child_proc->pid;
    /* (Final) TODO END */
}