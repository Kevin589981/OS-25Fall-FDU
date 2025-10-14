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
void proc_entry();

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
void create_idle_proc(){
    for (int i=0;i<NCPU;i++){
        cpus[i].sched.idle=&idle_procs[i];
        Proc *p=&idle_procs[i];
        p->state=RUNNING;
        p->idle=TRUE;
        p->pid=-1-i;
        // p->kstack=(void *)idle_stacks[i];
        p->kstack=kalloc_page();
        p->parent=NULL;
        // init_list_node(&p->children);
        // init_list_node(&p->ptnode);
        // schinfo不会用到，不必初始化
        //当前位于栈底
        void *sp=(void *)p->kstack+PAGE_SIZE;
        p->ucontext=NULL;
        p->kcontext=(KernelContext *)(sp-sizeof(KernelContext));
        // memset(p->kcontext,0,sizeof(KernelContext));
        p->kcontext->lr=(u64)idle_entry;
        cpus[i].sched.current_proc = p;
        cpus[i].sched.idle=p;
    }
}

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
    create_idle_proc();
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
    acquire_spinlock(&global_process_lock);
    // memset(p,0,sizeof(Proc));

    p->pid=pid_allocator();
    p->state=UNUSED;
    p->kstack=kalloc_page();
    if (p->kstack==NULL){
        release_spinlock(&global_process_lock);
        PANIC();
    }
    memset(p->kstack,0,PAGE_SIZE);

    // 这里会覆盖掉的，没必要写
    // p->ucontext=(UserContext *)((u64)p->kstack+PAGE_SIZE-sizeof(UserContext));
    // p->kcontext=(KernelContext *)((u64)p->ucontext-sizeof(KernelContext));
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
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
    Proc *parent=thisproc();
    acquire_spinlock(&global_process_lock);
    proc->parent=parent;
// #ifdef debug_page_fault
//     printk("proc.c:144: proc's ptnode is %llx\n", (u64)&proc->ptnode);
// #endif
    _insert_into_list(&parent->children,&proc->ptnode);
    release_spinlock(&global_process_lock);
}

// void test_printk_(ListNode *p){
//     printk("Here, %d\n",154);
//     if (p==NULL)printk("Problem occurred %llx\n",(u64)p);
//     if (p!=NULL)printk("Problem occurred 2:%llx\n",(u64)p);
//     if (p->next!=NULL){
//         printk("Problem occurred %llx\n",(u64)p->next);
//     }
// }
// void dump_proc_details(Proc *p, const char *label)
// {
//     if (!p) {
//         printk("===== dump_proc_details: %s - Proc is NULL =====\n", label);
//         return;
//     }

//     u64 base_addr = (u64)p;

//     printk("\n===== Dumping Proc Details: %s =====\n", label);
//     printk("Base Address of Proc struct: 0x%llx\n", base_addr);
//     printk("----------------------------------------------------------------------------------\n");
//     printk("  Offset | Member            | Address              | Value\n");
//     printk("----------------------------------------------------------------------------------\n");

//     // 手动计算偏移量: (u64)&(p->member) - base_addr
//     printk("  +%-5llu | killed            | 0x%llx | %d\n", (u64)&p->killed - base_addr, (u64)&p->killed, p->killed);
//     printk("  +%-5llu | idle              | 0x%llx | %d\n", (u64)&p->idle - base_addr, (u64)&p->idle, p->idle);
//     printk("  +%-5llu | pid               | 0x%llx | %d\n", (u64)&p->pid - base_addr, (u64)&p->pid, p->pid);
//     printk("  +%-5llu | exitcode          | 0x%llx | %d\n", (u64)&p->exitcode - base_addr, (u64)&p->exitcode, p->exitcode);
//     printk("  +%-5llu | state             | 0x%llx | %d\n", (u64)&p->state - base_addr, (u64)&p->state, p->state);
    
//     printk("----------------------------------------------------------------------------------\n");
//     printk("  +%-5llu | childexit (struct)| 0x%llx | --> (details omitted)\n", (u64)&p->childexit - base_addr, (u64)&p->childexit);

//     printk("----------------------------------------------------------------------------------\n");
//     printk("  +%-5llu | children (struct) | 0x%llx | --> (see below) *** KEY ***\n", (u64)&p->children - base_addr, (u64)&p->children);
//     printk("         |   .next           | 0x%llx | 0x%llx\n", (u64)&p->children.next, (u64)p->children.next);
//     printk("         |   .prev           | 0x%llx | 0x%llx\n", (u64)&p->children.prev, (u64)p->children.prev);

//     printk("----------------------------------------------------------------------------------\n");
//     printk("  +%-5llu | ptnode (struct)   | 0x%llx | --> (see below)\n", (u64)&p->ptnode - base_addr, (u64)&p->ptnode);
//     printk("         |   .next           | 0x%llx | 0x%llx\n", (u64)&p->ptnode.next, (u64)p->ptnode.next);
//     printk("         |   .prev           | 0x%llx | 0x%llx\n", (u64)&p->ptnode.prev, (u64)p->ptnode.prev);

//     printk("----------------------------------------------------------------------------------\n");
//     printk("  +%-5llu | parent            | 0x%llx | 0x%llx\n", (u64)&p->parent - base_addr, (u64)&p->parent, (u64)p->parent);
//     printk("  +%-5llu | schinfo (struct)  | 0x%llx | (details omitted)\n", (u64)&p->schinfo - base_addr, (u64)&p->schinfo);
//     printk("  +%-5llu | kstack            | 0x%llx | 0x%llx\n", (u64)&p->kstack - base_addr, (u64)&p->kstack, (u64)p->kstack);
//     printk("  +%-5llu | ucontext          | 0x%llx | 0x%llx\n", (u64)&p->ucontext - base_addr, (u64)&p->ucontext, (u64)p->ucontext);
//     printk("  +%-5llu | kcontext          | 0x%llx | 0x%llx\n", (u64)&p->kcontext - base_addr, (u64)&p->kcontext, (u64)p->kcontext);
//     printk("==================================================================================\n\n");
// }
// ========================================================================
// int flag_=0;
int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    if (p->parent==NULL){
        acquire_spinlock(&global_process_lock);
        p->parent=&root_proc;
// #ifdef debug_page_fault
//     printk("proc.c:158: proc's ptnode is %llx, ptnode.next is %llx, ptnode.prev is %llx\n", (u64)&p->ptnode, (u64)p->ptnode.next, (u64)p->ptnode.prev);
//     printk("proc.c:159: proc's pid id %d\n",p->pid);
//     printk("[START_PROC] Before insert, root_proc.children is %llx, root_proc.children.next is %llx, root_proc.children.prev is %llx\n", (u64)&root_proc.children, (u64)root_proc.children.next, (u64)root_proc.children.prev);
// #endif
        // if (root_proc.children.next==NULL)printk("Error %d\n",165);
        // _insert_into_list(&root_proc.children, &p->ptnode);
        // init_list_node(&p->ptnode);
        // _merge_list(&root_proc.children, &p->ptnode);
                // flag_=1;
                // ==================== 调试代码：调用前 (超详细版) ====================
                // printk("\n\n[DEBUG] === CONTEXT BEFORE calling _merge_list(cpu is %lld) ===\n",cpuid());
        
                // // 1. 打印期望传递的参数地址
                // printk("[DEBUG] Intended arg1 (&root_proc.children): 0x%llx\n", (u64)&root_proc.children);
                // printk("[DEBUG] Intended arg2 (&p->ptnode):           0x%llx\n", (u64)&p->ptnode);
        
                // // 2. 完整转储 root_proc 的详细字段信息
                // dump_proc_details(&root_proc, "root_proc state BEFORE call");
                
                // printk("[DEBUG] Now calling _merge_list...\n");
                // =================================================================
        // 恢复原始的函数调用
        // printk("This proc is %d\n",thisproc()->pid);
        _merge_list(&root_proc.children, &p->ptnode);
        // printk("[DEBUG] === AFTER returning from _merge_list(cpu is %lld) ===\n\n",cpuid());


        // root_proc.children.next->prev=&p->ptnode;
        // root_proc.children.next=&p->ptnode;
        release_spinlock(&global_process_lock);
    }
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    void *sp=(void *)p->kstack+PAGE_SIZE;
    sp-=sizeof(KernelContext);
    p->kcontext=(KernelContext *)sp;
    memset(p->kcontext,0,sizeof(KernelContext));
    p->kcontext->lr=(u64)proc_entry;
    p->kcontext->x0=(u64)entry;
    p->kcontext->x1=(u64)arg;
    // 3. activate the proc and return its pid
    p->state=UNUSED;
    activate_proc(p);//activate函数内部有锁
    // NOTE: be careful of concurrency
    printk("cpuid: %lld, start pid: %d\n",cpuid(),p->pid);
    return p->pid;

}

int wait(int *exitcode)
{
    // TODO:
    Proc *parent=thisproc();
    // 1. return -1 if no children
    // acquire_spinlock(&global_process_lock);
    // if (_empty_list(&parent->children)){
    //     release_spinlock(&global_process_lock);
    //     return -1;
    // }
    while (1){
        acquire_spinlock(&global_process_lock);
        if (_empty_list(&parent->children)){
            release_spinlock(&global_process_lock);
            return -1;
        }
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
        Proc *zombie_child=NULL;
        _for_in_list(node,&parent->children){
            Proc *p=container_of(node,Proc,ptnode);
            if (p->state==ZOMBIE){
                zombie_child=p;
                break;
            }
        }
        if (zombie_child){
            if (exitcode!=NULL){
                *exitcode=zombie_child->exitcode;

            }
            int child_pid=zombie_child->pid;
            if (zombie_child->kstack){
                kfree_page(zombie_child->kstack);
            }
            _detach_from_list(&zombie_child->ptnode);
            zombie_child->state=UNUSED;
            zombie_child->pid=0;
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
    // p->state=ZOMBIE; //会导致sched.c:163发生PANIC
#ifdef debug_sched //{ UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };
    printk("proc.c:224, p->state is %d\n",p->state);
#endif
    p->exitcode=code;
    // 2. clean up the resources
    
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    _for_in_list(node,&p->children){
        Proc *child=container_of(node,Proc,ptnode);
        child->parent=&root_proc;
    }
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

    PANIC(); // prevent the warning of 'no_return function returns'
}
