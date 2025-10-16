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

void init_pid_allocator(){
    allocated_pid=0;
}

int pid_allocator(){
    ++allocated_pid;
    return allocated_pid;
}

Proc idle_procs[NCPU];
// static u8 idle_stacks[NCPU][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));


// typedef struct Proc {
//     bool killed;
//     bool idle;√
//     int pid;√
//     int exitcode;
//     enum procstate state;√
//     Semaphore childexit;
//     ListNode children;
//     ListNode ptnode;
//     struct Proc *parent;√
//     struct schinfo schinfo;
//     void *kstack;√
//     UserContext *ucontext;√
//     KernelContext *kcontext;√
// } Proc;


// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    init_spinlock(&global_process_lock);
    // pid计数器
    init_pid_allocator();
    
    // 2. init the root_proc (finished)

    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}
// 在 init_proc 中初始化 schinfo
void init_proc(Proc *p)
{
    memset(p, 0, sizeof(Proc));

    acquire_spinlock(&global_process_lock);

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
    // 初始化调度信息
    init_schinfo(&p->schinfo);
    
    if (p->kstack == NULL) {
        // release_spinlock(&global_process_lock);
        PANIC();
    }
    memset(p->kstack, 0, PAGE_SIZE);

    p->kcontext = (KernelContext *)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext));
    p->ucontext = (UserContext *)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));

    release_spinlock(&global_process_lock);
}

// wait 和 exit 函数保持不变

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
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    
    acquire_spinlock(&global_process_lock);
    Proc *parent=thisproc();
    proc->parent=parent;
// #ifdef debug_page_fault
//     printk("proc.c:144: proc's ptnode is %llx\n", (u64)&proc->ptnode);
// #endif
    _insert_into_list(&parent->children,&proc->ptnode);
    release_spinlock(&global_process_lock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    if (p->parent==NULL){
        acquire_spinlock(&global_process_lock);
        p->parent=&root_proc;

        _insert_into_list(&root_proc.children, &p->ptnode);

        release_spinlock(&global_process_lock);
    }
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // void *sp=(void *)p->kstack+PAGE_SIZE;
    // sp-=sizeof(KernelContext);
    // p->kcontext=(KernelContext *)sp;
    // memset(p->kcontext,0,sizeof(KernelContext));
    p->kcontext->lr=(u64)proc_entry;
    p->kcontext->x0=(u64)entry;
    p->kcontext->x1=(u64)arg;
    // 3. activate the proc and return its pid
    p->state=UNUSED;
    int id =p->pid;
    activate_proc(p);//activate函数内部有锁
    // NOTE: be careful of concurrency
    printk("cpuid: %lld, start pid: %d, its parent is %d\n",cpuid(),p->pid,p->parent->pid);
    return id;

}

int wait(int *exitcode)
{
    // TODO:
    
    
    // 1. return -1 if no children
    // acquire_spinlock(&global_process_lock);
    // if (_empty_list(&parent->children)){
    //     release_spinlock(&global_process_lock);
    //     return -1;
    // }
    
    while (1){
        
        // if (_empty_list(&parent->children)){
        //     release_spinlock(&global_process_lock);
        //     return -1;
        // }
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
        Proc *parent=thisproc();
        acquire_spinlock(&global_process_lock);
        // printk("191:proc.c: wait(), parent pid is %d\n",parent->pid);
        bool has_children = !_empty_list(&parent->children);
        if (!has_children){
            // printk("194: proc.c: no children, parent pid is %d\n",parent->pid);
            release_spinlock(&global_process_lock);
            printk("194: proc.c: no children, parent pid is %d\n",parent->pid);
            return -1;
        }
        // printk("197:proc.c: wait(), parent pid is %d\n",parent->pid);
        Proc *zombie_child=NULL;
        // if (has_children) {
        _for_in_list(node, &parent->children) {
            if (node==&parent->children){
                continue;
            }
            Proc *p = container_of(node, Proc, ptnode);
            // printk("Checking child pid: %d\n", p->pid);
            if (is_zombie(p)) {
                zombie_child = p;
                printk("Found zombie child pid: %d\n", p->pid);
                break;
            }
            // if (node->next==&parent->children){
            //     break;
            // }
        }
        // printk("210:proc.c: wait(), parent pid is %d\n",parent->pid);
        // }
        if (zombie_child){
            if (exitcode!=NULL){
                *exitcode=zombie_child->exitcode;
            }
            int child_pid=zombie_child->pid;
            if (zombie_child->kstack){
                kfree_page(zombie_child->kstack);
            }
            _detach_from_list(&zombie_child->ptnode);
            // zombie_child->state=UNUSED;
            // zombie_child->pid=0;
            kfree(zombie_child);
            release_spinlock(&global_process_lock);
            return child_pid;
        }

    // NOTE: be careful of concurrency
        release_spinlock(&global_process_lock);
        wait_sem(&parent->childexit);

        
    }
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    Proc *p=thisproc();
     
    acquire_spinlock(&global_process_lock);
    // acquire_sched_lock();
    // p->state=ZOMBIE; //会导致sched.c:163发生PANIC
#ifdef debug_sched //{ UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };
    printk("proc.c:224, p->state is %d\n",p->state);
#endif
    p->exitcode=code;
    // 2. clean up the resources
    
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // _for_in_list(node,&p->children){
    //     Proc *child=container_of(node,Proc,ptnode);
    //     child->parent=&root_proc;
    // }

// #ifdef debug_page_fault
//     printk("proc's ptnode is %llx, proc's children is %llx\n", (u64)&p->ptnode,(u64)&p->children);
// #endif
    // if (!_empty_list(&p->children)){
    //     ListNode *temp=_detach_from_list(&p->children);
    //     _merge_list(&root_proc.children, temp);
    // }
    // bool has_zombie_to_reparent = false;

    // // 1. 遍历所有子进程，更新它们的 parent 指针并检查僵尸状态
    // //    使用 _for_in_list_safe 是一个好习惯，尽管在这里我们是移动整个列表
    // _for_in_list(node, &p->children) {
    //     Proc *child = container_of(node, Proc, ptnode);
        
    //     // 关键修复 #1: 更新父进程指针
    //     child->parent = &root_proc;

    //     // 关键修复 #2: 检查是否存在已是僵尸的子进程
    //     if (child->state == ZOMBIE) {
    //         has_zombie_to_reparent = true;
    //     }
    // }

    // // 2. 将整个子进程链表过继给 root_proc
    // if (!_empty_list(&p->children)) {
    //     // 你的 _merge_list 应该能正确地将 p->children 的所有节点
    //     // 移动到 root_proc.children 中。
    //     // 一个标准的实现是：
    //     // a. 取出 p->children 的第一个和最后一个节点
    //     // b. 将它们链接到 root_proc.children 的末尾
    //     ListNode *temp = _detach_from_list(&p->children);
    //     _merge_list(&root_proc.children, temp); // 假设你有这样的函数
    //                                                    // 如果你的_merge_list(dst, src_head)能工作，也可以
    // }
    
    // // 清空自己的子进程链表
    // init_list_node(&p->children);

    // if (has_zombie_to_reparent) {
    //     post_sem(&root_proc.childexit);
    // }

    ListNode orphaned_children;
    init_list_node(&orphaned_children);
    while (!_empty_list(&p->children)) {
        ListNode *child_node = p->children.next;
        _detach_from_list(child_node);
        _insert_into_list(&orphaned_children, child_node);
    }
    while (!_empty_list(&orphaned_children)) {
        ListNode *child_node = orphaned_children.next;
        _detach_from_list(child_node);
        _insert_into_list(&root_proc.children, child_node);
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
        _insert_into_list(&root_proc.children, &p->ptnode);
    }

    
    if ((u64)p->parent<0x20){
        printk("BUG at proc.c :325\n");
    }
    
    
    // 4. sched(ZOMBIE)
    
    // *cpus[cpuid()].zombie_to_reap=*p->kcontext;
    acquire_sched_lock();
    acquire_spinlock(&p->lock);


    post_sem(&p->parent->childexit);
    release_spinlock(&global_process_lock);
    // sched(DYING);
    sched(ZOMBIE);
    // NOTE: be careful of concurrency
    
    // kfree(p);
    PANIC(); // prevent the warning of 'no_return function returns'
}
