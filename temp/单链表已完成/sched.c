// sched.c

#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>
#include <kernel/debug.h>
#include <common/string.h>
#include <common/list.h>
extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);
u64 proc_entry(void (*entry)(u64), u64 arg);
extern int idle_entry();
#define PAGE_SIZE 4096
// --- 定义全局调度资源 ---
// struct rb_root_ global_run_queue;
ListNode global_run_queue;
SpinLock global_sched_lock;
// u64 global_min_vruntime = 0; 
// --------------------
extern Proc idle_procs[];
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
        // p->ucontext=NULL;
        p->kcontext=(KernelContext *)(sp-sizeof(KernelContext));
        // memset(p->kcontext,0,sizeof(KernelContext));
        p->kcontext->lr=(u64)proc_entry;
        p->kcontext->x0=(u64)idle_entry;
        p->kcontext->x1=(u64)0;
        cpus[i].sched.current_proc = p;
        cpus[i].sched.idle=p;
    }
}
// void update_vruntime(Proc *p){
//     if (p==NULL||p->idle==TRUE){
//         return;
//     }
//     u64 now=get_timestamp();
//     u64 delta_exec=now-p->schinfo.start_exec_time;
//     // 应该不会为负数
//     int weight=WEIGHT(p->schinfo.nice);
//     u64 delta_vruntime=(delta_exec*WEIGHT(0))/weight;
//     p->schinfo.vruntime+=delta_vruntime;
// }

// bool compare_runtime(rb_node lnode, rb_node rnode){
//     // 先找到这个红黑树节点所在的进程
//     Proc *p1=container_of(lnode,Proc,schinfo.node);
//     Proc *p2=container_of(rnode,Proc,schinfo.node);
//     return p1->schinfo.vruntime<p2->schinfo.vruntime;
// }

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // --- 初始化全局资源 ---
    create_idle_proc();
    init_spinlock(&global_sched_lock);
    // global_run_queue.rb_node = NULL;
    // -------------------
    init_list_node(&global_run_queue);

    // 2. initialize the scheduler info of each CPU
    for (int i=0;i<NCPU;i++){
        struct sched *temp=&cpus[i].sched;
        // init_spinlock(&temp->lock); // 已移至全局
        // temp->run_queue.rb_node=NULL; // 已移至全局
        temp->task_count=0; // 注意：task_count的含义可能需要重新审视
        // temp->current_proc=NULL;
        // temp->idle=NULL; a
    }
}

Proc *thisproc()
{
    // TODO: return the current process
    // 可以根据当前的CPU是哪个来找到这个进程是哪个
    int id=cpuid();
    return cpus[id].sched.current_proc;
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process
    p->vruntime=0;
    p->nice=0;
    // 后续还会更新并覆盖的，这里可以直接设置为0
    p->start_exec_time=0;
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    acquire_spinlock(&global_sched_lock);
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need
    release_spinlock(&global_sched_lock);
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool activate_proc(Proc *p)
{
    // TODO:
    acquire_sched_lock();
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    if (p->state==RUNNING||p->state==RUNNABLE){
        release_sched_lock();
        return true;
    }
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    else if (p->state==SLEEPING||p->state==UNUSED){
        // printk("activate_proc: adding pid=%d to runqueue\n", p->pid);
        p->state=RUNNABLE;
        // if (p->schinfo.vruntime < global_min_vruntime) {
        //     p->schinfo.vruntime = global_min_vruntime;
        // }
        // _rb_insert(&p->schinfo.node, &global_run_queue, compare_runtime);
        // cpus[cpuid()].sched.task_count++; // 这个计数器现在是全局的了
        // -------------------
        DEBUG_PRINTK();
        _insert_into_list(global_run_queue.prev,&p->schinfo.node);
        DEBUG_PRINTK();
    }
    // else: panic
    else {
        PANIC();
    }
    DEBUG_PRINTK();
    release_sched_lock();
    return true;
}

// 用来把当前运行的函数切换出去，可能切换为RUNNABLE\SLEEPING\ZOMBIE
static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and modify the sched queue if necessary
    Proc *this=thisproc();
    if (this->idle){
        return;
    }
    // update_vruntime(this);
    // if (this->state == RUNNABLE) {
    //     _rb_erase(&this->schinfo.node, &global_run_queue);
    // }
    this->state=new_state;
    if (new_state==RUNNABLE||new_state==RUNNING){
        // if (this->schinfo.vruntime < global_min_vruntime) {
        //     this->schinfo.vruntime = global_min_vruntime;
        // }
        // _rb_insert(&this->schinfo.node, &global_run_queue, compare_runtime);
        // cpus[cpuid()].sched.task_count++;
        // -------------------
        //! 判断不在链表中
        _merge_list(global_run_queue.prev,&this->schinfo.node);
    }else if(new_state==SLEEPING||new_state==ZOMBIE){
        // //不需要减少任务计数
        _detach_from_list(&this->schinfo.node);
        
    }
}

// static Proc *pick_next()
// {
//     // TODO: if using template sched function, you should implement this routinue
//     // choose the next process to run, and return idle if no runnable process
//     if (panic_flag) return cpus[cpuid()].sched.idle;
    
//     // --- 使用全局红黑树 ---
//     rb_node next_node=_rb_first(&global_run_queue);
//     // -------------------

//     if (next_node){
//         _rb_erase(next_node, &global_run_queue);
//         // cpus[cpuid()].sched.task_count--;
//         Proc *next_proc=container_of(next_node,Proc,schinfo.node);
//         rb_node *new_first = _rb_first(&global_run_queue);
//         if (new_first) {
//             Proc *p = container_of(new_first, Proc, schinfo.node);
//             global_min_vruntime = p->schinfo.vruntime;
//         }

//         next_proc->schinfo.start_exec_time=get_timestamp();
        
//         return next_proc;
//     }else{
//         return cpus[cpuid()].sched.idle;
//     }
// }
static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if (panic_flag) return cpus[cpuid()].sched.idle;
    
    // --- 使用全局红黑树 ---
    // rb_node 已经被 typedef 为 struct rb_node_ *
    // rb_node next_node=_rb_first(&global_run_queue);
    // -------------------
    // ListNode *next_node=NULL;
    Proc *next_proc=NULL;
    _for_in_list(node, &global_run_queue){
        if (node==&global_run_queue)continue;
        
        Proc *p=container_of(node,Proc,schinfo.node);
        if (p->state==RUNNABLE){
            next_proc=p;
            _detach_from_list(node);
            break;
        }
        // if (node->next==&global_run_queue){
        //     break;
        // }
    }
    DEBUG_PRINTK();
    if (next_proc){
        // _rb_erase(next_node, &global_run_queue);
        
        // 在取出 vruntime 最小的进程后，更新全局的 min_vruntime
        // 使其指向新的 vruntime 最小的进程。
        // rb_node new_first = _rb_first(&global_run_queue);
        // if (new_first) {
        //     Proc *p = container_of(new_first, Proc, schinfo.node);
        //     global_min_vruntime = p->schinfo.vruntime;
        // }

        // cpus[cpuid()].sched.task_count--;
        // Proc *next_proc=container_of(next_node,Proc,schinfo.node);
     
        // next_proc->schinfo.start_exec_time=get_timestamp();
        
        return next_proc;
    }else{
        return cpus[cpuid()].sched.idle;
    }
}

// 设置当前CPU运行的进程
static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process
    // reset_clock(1000);
    cpus[cpuid()].sched.current_proc=p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
// call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
#ifdef debug_sched
    // if (this->state != RUNNING)
    printk("this cpu is %lld, process's pid is %d, state is %d\n",cpuid(), this->pid, this->state);
#endif
    ASSERT(this->state == RUNNING);
    //! 给进程传入新的目标状态
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    if (next->state!=RUNNABLE && !next->idle){
        printk("This proc is: %d, it is %d\n",this->pid,this->state);
        printk("Next proc is: %d, it is %d\n",next->pid,next->state);
    }
    ASSERT(next->state == RUNNABLE||next->idle);
    next->state = RUNNING;
    
    if (next != this) {
        auto old_ctx = &this->kcontext;
        // ← 在切换前释放！
        swtch(next->kcontext, old_ctx);
    }
    release_sched_lock(); //!
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}