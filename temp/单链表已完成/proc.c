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

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    
    memset(p,0,sizeof(Proc));

    acquire_spinlock(&global_process_lock);

    init_sem(&p->childexit, 1);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->idle = FALSE;
    p->exitcode=0;
    p->killed=FALSE;
    p->pid=pid_allocator();
    p->state=UNUSED;
    p->kstack=kalloc_page();
    init_schinfo(&p->schinfo);
    if (p->kstack==NULL){
        release_spinlock(&global_process_lock);
        PANIC();
    }
    memset(p->kstack,0,PAGE_SIZE);

    // 这里会覆盖掉的，没必要写
    p->kcontext=(KernelContext *)((u64)p->kstack+PAGE_SIZE-16-sizeof(KernelContext));
    p->ucontext=(UserContext *)((u64)p->kstack+PAGE_SIZE-16-sizeof(KernelContext)-sizeof(UserContext));

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
    printk("cpuid: %lld, start pid: %d\n",cpuid(),p->pid);
    return id;

}

int wait(int *exitcode)
{
    // TODO:
    acquire_spinlock(&global_process_lock);
    Proc *parent=thisproc();
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
        
        bool has_children = !_empty_list(&parent->children);
        if (!has_children){
            release_spinlock(&global_process_lock);
            return -1;
        }

        Proc *zombie_child=NULL;
        // if (has_children) {
        _for_in_list(node, &parent->children) {
            Proc *p = container_of(node, Proc, ptnode);
            if (p->state == ZOMBIE) {
                zombie_child = p;
                break;
            }
            // if (node->next==&parent->children){
            //     break;
            // }
        }
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

        acquire_spinlock(&global_process_lock);
    }
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    
     
    acquire_spinlock(&global_process_lock);
    Proc *p=thisproc();
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
    if (!_empty_list(&p->children)){
        ListNode *temp=_detach_from_list(&p->children);
        _merge_list(&root_proc.children, temp);
    }
    init_list_node(&p->children);
    if (p->parent){
        post_sem(&p->parent->childexit);
    }
    release_spinlock(&global_process_lock);
    // 4. sched(ZOMBIE)
    acquire_sched_lock();
    sched(ZOMBIE);
    // NOTE: be careful of concurrency
    
    // kfree(p);
    PANIC(); // prevent the warning of 'no_return function returns'
}
