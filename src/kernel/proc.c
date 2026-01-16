#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <aarch64/intrinsic.h> // <--- 修正1：添加此头文件以声明 flush_tlb_all
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
#include <fs/file.h>
#include <fs/inode.h>

Proc root_proc;
Proc idle_procs[NCPU];

// 全局进程锁，用于保护进程树结构和非调度状态
SpinLock global_process_lock;

// nice值到权重的映射表
int prio_to_weight[40] = {
    /* -20 */     88761,     71755,     56483,     46273,     36291,
    /* -15 */     29154,     23254,     18705,     14949,     11916,
    /* -10 */      9548,      7620,      6100,      4904,      3906,
    /*  -5 */      3121,      2501,      1991,      1586,      1277,
    /*   0 */      1024,       820,       655,       526,       423,
    /*   5 */       335,       272,       215,       172,       137,
    /*  10 */       110,        87,        70,        56,        45,
    /*  15 */        36,        29,        23,        18,        15,
}; 

// PID 位图管理
unsigned long pid_bitmap[BITMAP_SIZE];
static int next_pid_to_check = 1;
SpinLock pid_lock;

void init_pid_allocator() {
    init_spinlock(&pid_lock);
    memset(pid_bitmap, 0, sizeof(pid_bitmap));
    next_pid_to_check = 1;
}

int allocate_pid() {
    acquire_spinlock(&pid_lock);
    for (int i = 0; i < MAX_PID; ++i) {
        int pid = next_pid_to_check++;
        if (next_pid_to_check >= MAX_PID) {
            next_pid_to_check = 1;
        }
        if (pid == 0) continue; // PID 0 is reserved

        int index = pid / BITS_PER_LONG;
        int offset = pid % BITS_PER_LONG;
        if ((pid_bitmap[index] & (1UL << offset)) == 0) {
            pid_bitmap[index] |= (1UL << offset);
            release_spinlock(&pid_lock);
            return pid;
        }
    }
    release_spinlock(&pid_lock);
    return -1; // PID exhausted
}

void deallocate_pid(int pid) {
    if (pid <= 0 || pid >= MAX_PID) return;
    acquire_spinlock(&pid_lock);
    int index = pid / BITS_PER_LONG;
    int offset = pid % BITS_PER_LONG;
    pid_bitmap[index] &= ~(1UL << offset);
    release_spinlock(&pid_lock);
}


void kernel_entry();
void trap_return();

void init_kproc()
{
    init_spinlock(&global_process_lock);
    init_pid_allocator();
    
    // 初始化 root_proc
    init_proc(&root_proc);
    root_proc.parent = &root_proc; // 自己是自己的父进程
    start_proc(&root_proc, kernel_entry, 0);
}

void init_proc(Proc *p)
{
    memset(p, 0, sizeof(Proc));

    p->pid = allocate_pid();
    if (p->pid == -1) PANIC(); // No free PIDs

    p->state = UNUSED;
    p->killed = false;
    p->idle = false;
    p->exitcode = 0;
    p->parent = NULL;

    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_schinfo(&p->schinfo);
    init_pgdir(&p->pgdir);

    p->kstack = kalloc_page();
    if (!p->kstack) PANIC();
    memset(p->kstack, 0, PAGE_SIZE);

    // 在内核栈顶部分配用户和内核上下文
    p->ucontext = (UserContext *)(p->kstack + PAGE_SIZE - sizeof(UserContext));
    p->kcontext = (KernelContext *)((char*)p->ucontext - sizeof(KernelContext));

    // 初始化文件描述符表和当前工作目录
    init_oftable(&p->oftable);
    if (inodes.root) p->cwd = inodes.share(inodes.root);
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    if (!p) PANIC();
    
    // create_proc 不应该加锁，让调用者决定锁的粒度
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
    acquire_spinlock(&global_process_lock);
    if (p->parent == NULL && p != &root_proc) {
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
    }
    release_spinlock(&global_process_lock);

    p->kcontext->lr = (u64)proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = arg;

    activate_proc(p);
    return p->pid;
}

NO_RETURN void exit(int code)
{
    Proc *this = thisproc();

    // printk("[EXIT] PID=%d exiting with code %d\n", this->pid, code);
    
    // 1. 清理文件等资源
    if(this->cwd) {
        // decrement_rc(&this->cwd->rc);
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, this->cwd);
        bcache.end_op(&ctx);
        this->cwd = NULL;
    }
    
    // printk("[EXIT] PID=%d closing file descriptors\n", this->pid);
    for (int i = 0; i < NOFILE; i++) {
        if (this->oftable.files[i]) {
            // printk("[EXIT] PID=%d closing fd[%d]\n", this->pid, i);
            file_close(this->oftable.files[i]);
            this->oftable.files[i] = 0;
        }
    }
    // printk("[EXIT] PID=%d all fds closed\n", this->pid);
    
    acquire_spinlock(&global_process_lock);

    // 2. 将所有子进程过继给root_proc
    while (!_empty_list(&this->children)) {
        ListNode *child_node = this->children.next;
        Proc *child = container_of(child_node, Proc, ptnode);
        
        _detach_from_list(child_node);
        child->parent = &root_proc;
        _insert_into_list(&root_proc.children, &child->ptnode);
        
        if (is_zombie(child)) {
            post_sem(&root_proc.childexit);
        }
    }
    free_sections(&this->pgdir);

    free_pgdir(&this->pgdir);
    this->pgdir.pt = NULL; // 确保指针被清空
    // 3. 设置退出码并通知父进程
    this->exitcode = code;
    
    post_sem(&this->parent->childexit);

    acquire_sched_lock();
    release_spinlock(&global_process_lock);

    // 4. 进入僵尸状态并调度
    
    sched(ZOMBIE);

    PANIC(); // sched(ZOMBIE) should not return
}

int wait(int *exitcode)
{
    Proc *this = thisproc();
    
    while (1) {
        acquire_spinlock(&global_process_lock);

        if (_empty_list(&this->children)) {
            release_spinlock(&global_process_lock);
            return -1; // 没有子进程
        }

        Proc *zombie_child = NULL;
        _for_in_list(node, &this->children) {
            if (node==&this->children) continue;
            Proc *child = container_of(node, Proc, ptnode);
            if (is_zombie(child)) {
                zombie_child = child;
                break;
            }
        }

        if (zombie_child) {
            // 找到僵尸子进程，进行清理
            int pid = zombie_child->pid;
            if (exitcode) {
                *exitcode = zombie_child->exitcode;
            }

            _detach_from_list(&zombie_child->ptnode);
            
            // 在这里释放最后的资源
            free_pgdir(&zombie_child->pgdir);
            if (zombie_child->kstack) kfree_page(zombie_child->kstack);
            
            deallocate_pid(pid);
            kfree(zombie_child);

            release_spinlock(&global_process_lock);
            return pid;
        }

        // 没有僵尸子进程，准备睡眠等待
        release_spinlock(&global_process_lock);
        
        if (this->killed) return -1;
        
        wait_sem(&this->childexit);
    }
}

Proc *find_proc_by_pid(Proc* current, int pid)
{
    if (current->pid == pid && !is_unused(current)) return current;
    
    _for_in_list(node, &current->children) {
        Proc *child = container_of(node, Proc, ptnode);
        Proc *found = find_proc_by_pid(child, pid);
        if (found) return found;
    }
    return NULL;
}

int kill(int pid)
{
    acquire_spinlock(&global_process_lock);
    Proc *p = find_proc_by_pid(&root_proc, pid);
    if (!p) {
        release_spinlock(&global_process_lock);
        return -1;
    }
    
    p->killed = true;
    alert_proc(p);
    
    release_spinlock(&global_process_lock);
    return 0;
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
        if (parent_proc->oftable.files[i])
        {
            child_proc->oftable.files[i] = file_dup(parent_proc->oftable.files[i]);
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
    Proc *parent_proc = thisproc();
    Proc *child_proc = create_proc();
    if (!child_proc) return -1;

    // 1. 复制页目录和内存映射 (使用写时复制)
    copy_page_directory(parent_proc, child_proc);

    // 2. 复制用户上下文 (trapframe)
    memcpy(child_proc->ucontext, parent_proc->ucontext, sizeof(UserContext));
    child_proc->ucontext->x[0] = 0; // 子进程返回值为0

    // 3. 复制文件描述符表和当前工作目录
    copy_file_table(parent_proc, child_proc);
    copy_working_directory(parent_proc, child_proc);

    // 4. 设置父子关系
    acquire_spinlock(&global_process_lock);
    child_proc->parent = parent_proc;
    _insert_into_list(&parent_proc->children, &child_proc->ptnode);
    release_spinlock(&global_process_lock);
    
    // 5. 启动子进程
    start_proc(child_proc, trap_return, (u64)child_proc->ucontext);

    return child_proc->pid;
}