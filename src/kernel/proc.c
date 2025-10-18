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
static int allocated_pid;

typedef struct FreePidNode {
    ListNode node;
    int pid;
} FreePidNode;

static ListNode free_pid_list;

void init_pid_allocator(){
    allocated_pid=0;
    init_list_node(&free_pid_list);
}

void pid_recycler(int pid) {
#ifdef PID_DEBUG
    printk("recycle pid: %d\n", pid);
#endif
    FreePidNode *node = kalloc(sizeof(FreePidNode));
    if (node == NULL) {
        // 在内存不足的极端情况下，这个PID会丢失，但系统仍可运行
        printk("WARNING: Failed to allocate memory for PID recycling.\n");
        return;
    }
    node->pid = pid;
    _insert_into_list(&free_pid_list, &node->node);
}

int pid_allocator(){

    if (!_empty_list(&free_pid_list)) {
        ListNode *node = free_pid_list.next;
        _detach_from_list(node);
        FreePidNode *fpn = container_of(node, FreePidNode, node);
        int pid = fpn->pid;
        kfree(fpn); // 释放节点本身占用的内存
        return pid;
    } 

    ++allocated_pid;
    return allocated_pid;
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
    p->state = UNUSED;
    p->kstack = kalloc_page();
    init_spinlock(&p->lock);
    init_schinfo(&p->schinfo);
    
    if (p->kstack == NULL) {
        PANIC();
    }
    memset(p->kstack, 0, PAGE_SIZE);

    p->kcontext = (KernelContext *)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext));
    p->ucontext = (UserContext *)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));

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
        wait_sem(&parent->childexit);
    }
}

NO_RETURN void exit(int code)
{
    
     
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
    if (!p->parent){
        p->parent=&root_proc;
        _detach_from_list(&p->ptnode);
        _insert_into_list(root_proc.children.prev, &p->ptnode);
    }

    if ((u64)p->parent<0x20){
        printk("BUG at proc.c :325\n");
        PANIC();
    }
    
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