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
extern Proc idle_procs[];

#define PAGE_SIZE 4096

// 保留全局锁用于is_zombie等操作
SpinLock global_sched_lock;

void create_idle_proc(){
    for (int i=0;i<NCPU;i++){
        cpus[i].sched.idle=&idle_procs[i];
        Proc *p=&idle_procs[i];
        p->state=RUNNING;
        p->idle=TRUE;
        p->pid=-1-i;
        p->kstack=kalloc_page();
        p->parent=NULL;
        
        void *sp=(void *)p->kstack+PAGE_SIZE;
        p->kcontext=(KernelContext *)(sp-sizeof(KernelContext));
        p->kcontext->lr=(u64)proc_entry;
        p->kcontext->x0=(u64)idle_entry;
        p->kcontext->x1=(u64)0;
        cpus[i].sched.current_proc = p;
        cpus[i].sched.idle=p;
    }
}

void init_sched()
{
    create_idle_proc();
    
    // 初始化全局锁
    init_spinlock(&global_sched_lock);
    
    // 初始化每个CPU的调度器
    for (int i=0;i<NCPU;i++){
        struct sched *s = &cpus[i].sched;
        init_spinlock(&s->lock);
        init_list_node(&s->run_queue);
        s->task_count = 0;
    }
}

Proc *thisproc()
{
    int id=cpuid();
    return cpus[id].sched.current_proc;
}

void init_schinfo(struct schinfo *p)
{
    p->vruntime=0;
    p->nice=0;
    p->start_exec_time=0;
}

void acquire_sched_lock()
{
    // 获取当前CPU的调度锁
    acquire_spinlock(&cpus[cpuid()].sched.lock);
}

void release_sched_lock()
{
    // 释放当前CPU的调度锁
    release_spinlock(&cpus[cpuid()].sched.lock);
}

bool is_zombie(Proc *p)
{
    // 使用全局锁保护状态读取
    bool r;
    acquire_spinlock(&global_sched_lock);
    r = p->state == ZOMBIE;
    release_spinlock(&global_sched_lock);
    return r;
}

bool activate_proc(Proc *p)
{
    if (p->state==RUNNING||p->state==RUNNABLE){
        return true;
    }
    
    if (p->state==SLEEPING||p->state==UNUSED){
        // 选择任务数最少的CPU队列
        int target_cpu = 0;
        u64 min_count = cpus[0].sched.task_count;
        
        for (int i = 1; i < NCPU; i++) {
            if (cpus[i].sched.task_count < min_count) {
                min_count = cpus[i].sched.task_count;
                target_cpu = i;
            }
        }
        
        // 将进程加入目标CPU的队列
        acquire_spinlock(&cpus[target_cpu].sched.lock);
        p->state = RUNNABLE;
        _insert_into_list(cpus[target_cpu].sched.run_queue.prev, &p->schinfo.node);
        cpus[target_cpu].sched.task_count++;
        release_spinlock(&cpus[target_cpu].sched.lock);
        
        return true;
    }
    
    PANIC();
    return false;
}

static void update_this_state(enum procstate new_state)
{
    Proc *this = thisproc();
    if (this->idle){
        return;
    }
    
    int my_cpu = cpuid();
    this->state = new_state;
    
    if (new_state == RUNNABLE || new_state == RUNNING){
        // 将当前进程放回当前CPU的队列
        _merge_list(cpus[my_cpu].sched.run_queue.prev, &this->schinfo.node);
    } else if (new_state == SLEEPING || new_state == ZOMBIE){
        // 从队列中移除
        _detach_from_list(&this->schinfo.node);
        if (new_state == ZOMBIE) {
            cpus[my_cpu].sched.task_count--;
        }
    }
}

static Proc *pick_next()
{
    if (panic_flag) return cpus[cpuid()].sched.idle;
    
    int my_cpu = cpuid();
    ListNode *my_queue = &cpus[my_cpu].sched.run_queue;
    Proc *next_proc = NULL;
    
    // 首先从当前CPU的队列中查找
    _for_in_list(node, my_queue){
        if (node == my_queue) continue;
        
        Proc *p = container_of(node, Proc, schinfo.node);
        if (p->state == RUNNABLE){
            next_proc = p;
            _detach_from_list(node);
            cpus[my_cpu].sched.task_count--;
            return next_proc;
        }
    }
    
    // 当前队列为空，尝试工作窃取
    // 先释放当前CPU的锁，避免死锁
    release_spinlock(&cpus[my_cpu].sched.lock);
    
    for (int i = 0; i < NCPU; i++) {
        if (i == my_cpu) continue;
        
        // 尝试从其他CPU窃取任务
        acquire_spinlock(&cpus[i].sched.lock);
        
        ListNode *other_queue = &cpus[i].sched.run_queue;
        Proc *stolen = NULL;
        
        _for_in_list(node, other_queue){
            if (node == other_queue) continue;
            
            Proc *p = container_of(node, Proc, schinfo.node);
            if (p->state == RUNNABLE){
                stolen = p;
                _detach_from_list(node);
                cpus[i].sched.task_count--;
                break;
            }
        }
        
        release_spinlock(&cpus[i].sched.lock);
        
        if (stolen) {
            // 窃取成功，重新获取当前CPU的锁后返回
            acquire_spinlock(&cpus[my_cpu].sched.lock);
            return stolen;
        }
    }
    
    // 没有找到任何可运行的进程，重新获取当前CPU的锁，返回idle进程
    acquire_spinlock(&cpus[my_cpu].sched.lock);
    return cpus[my_cpu].sched.idle;
}

static void update_this_proc(Proc *p)
{
    cpus[cpuid()].sched.current_proc = p;
}

void sched(enum procstate new_state)
{
    auto this = thisproc();
#ifdef debug_sched
    printk("this cpu is %lld, process's pid is %d, state is %d\n",cpuid(), this->pid, this->state);
#endif
    ASSERT(this->state == RUNNING);
    
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    
    if (next->state != RUNNABLE && !next->idle){
        printk("This proc is: %d, it is %d\n",this->pid,this->state);
        printk("Next proc is: %d, it is %d\n",next->pid,next->state);
    }
    ASSERT(next->state == RUNNABLE || next->idle);
    next->state = RUNNING;
    
    if (next != this) {
        auto old_ctx = &this->kcontext;
        swtch(next->kcontext, old_ctx);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}