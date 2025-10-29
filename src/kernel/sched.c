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
#define NICE_0_LOAD 1024

SpinLock global_sched_lock;

// 红黑树比较函数：按vruntime排序
static bool rb_proc_less(struct rb_node_ *lnode, struct rb_node_ *rnode)
{
    Proc *lproc = container_of(lnode, Proc, schinfo.node);
    Proc *rproc = container_of(rnode, Proc, schinfo.node);
    
    // vruntime小的在左边
    if (lproc->schinfo.vruntime < rproc->schinfo.vruntime)
        return true;
    if (lproc->schinfo.vruntime > rproc->schinfo.vruntime)
        return false;
    // vruntime相同时按pid排序，保证确定性
    return lproc->pid < rproc->pid;
}

void create_idle_proc()
{
    for (int i = 0; i < NCPU; i++) {
        cpus[i].sched.idle = &idle_procs[i];
        Proc *p = &idle_procs[i];
        p->state = RUNNING;
        p->idle = TRUE;
        p->pid = -1 - i;
        // p->kstack = kalloc_page();
        p->kstack = NULL;
        p->parent = NULL;
        
        // void *sp = (void *)p->kstack + PAGE_SIZE;
        // p->kcontext = (KernelContext *)(sp - sizeof(KernelContext));
        // p->kcontext->lr = (u64)&proc_entry;
        // p->kcontext->x0 = (u64)idle_entry;
        // p->kcontext->x1 = (u64)0;
        p->ucontext = NULL;
        p->kcontext = NULL;

        cpus[i].sched.current_proc = p;
        cpus[i].sched.idle = p;
    }
}

void init_sched()
{
    create_idle_proc();
    
    init_spinlock(&global_sched_lock);
    
    for (int i = 0; i < NCPU; i++) {
        struct sched *s = &cpus[i].sched;
        // init_spinlock(&s->lock); // Per-CPU锁已被移除
        // 初始化红黑树
        s->run_queue.rb_node = NULL;
        s->task_count = 0;
        s->min_vruntime = 0;
        cpus[i].zombie_to_reap=kalloc(sizeof(KernelContext));
    }
}

Proc *thisproc()
{
    int id = cpuid();
    return cpus[id].sched.current_proc;
}

void init_schinfo(struct schinfo *p)
{
    p->vruntime = 0;
    p->nice = 0;
    p->start_exec_time = 0;
    p->node.rb_left = NULL;
    p->node.rb_right = NULL;
    p->node.__rb_parent_color = 0;
}

// void acquire_sched_lock()
// {
//     acquire_spinlock(&global_sched_lock);
// }

void release_sched_lock()
{
    release_spinlock(&global_sched_lock);
}

bool is_zombie(Proc *p)
{
    bool r;
    // acquire_spinlock(&p->lock);
    acquire_sched_lock();
    r = p->state == ZOMBIE;

    release_sched_lock();
    return r;
}

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
    release_sched_lock();
    return r;
}

// bool is_unused(Proc *p)
// {
//     bool r;
//     acquire_sched_lock();
//     r = p->state == UNUSED;
//     release_sched_lock();
//     return r;
// }

bool activate_proc(Proc *p)
{   

    acquire_sched_lock(); 
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock(); 
        return false;
    }
    
    else if (p->state == SLEEPING || p->state == UNUSED) {
        // 使用全局锁保护所有CPU的调度队列

        // 选择任务数最少的CPU
        int target_cpu = 0;
        u64 min_count = cpus[0].sched.task_count;
        
        for (int i = 1; i < NCPU; i++) {
            if (cpus[i].sched.task_count < min_count) {
                min_count = cpus[i].sched.task_count;
                target_cpu = i;
            }
        }
    
        // 新进程的vruntime设置为目标CPU的最小vruntime，避免饥饿
        p->schinfo.vruntime = cpus[target_cpu].sched.min_vruntime;
        p->state = RUNNABLE;
    
        // 插入红黑树
        _rb_insert(&p->schinfo.node, &cpus[target_cpu].sched.run_queue, rb_proc_less);
        cpus[target_cpu].sched.task_count++;
    
        release_sched_lock(); // 释放全局锁
        return true;
    }else if (p->state==ZOMBIE){
        release_sched_lock();
        printk("activate zombie proc %d\n",p->pid);
        return false;
    }

    release_sched_lock();
    printk("activate_proc: invalid process state\n");
    PANIC();
    return false;
}

// 更新当前进程的vruntime
static void update_vruntime(Proc *p, u64 current_time)
{
    if (p->idle) return;
    
    if (p->schinfo.start_exec_time == 0) {
        p->schinfo.start_exec_time = current_time;
        return;
    }
    
    u64 delta_exec = current_time - p->schinfo.start_exec_time;
    if (delta_exec > 0) {
        // vruntime增量 = 实际运行时间 * NICE_0_LOAD / weight
        int weight = WEIGHT(p->schinfo.nice);
        u64 delta_vruntime = (delta_exec * NICE_0_LOAD) / weight;
        p->schinfo.vruntime += delta_vruntime;
    }
    
    p->schinfo.start_exec_time = current_time;
}

static void update_this_state(enum procstate new_state)
{
    Proc *this = thisproc();
    if (this->idle) {
        this->state = new_state;
        return;
    }
    
    int my_cpu = cpuid();
    u64 current_time = get_timestamp(); // 需要实现获取时间戳的函数
    
    // 更新vruntime
    update_vruntime(this, current_time);
    
    // 更新min_vruntime
    if (this->schinfo.vruntime > cpus[my_cpu].sched.min_vruntime) {
        cpus[my_cpu].sched.min_vruntime = this->schinfo.vruntime;
    }
    
    this->state = new_state;
    
    if (new_state == RUNNABLE) {
        // RUNNING -> RUNNABLE: 插入红黑树
        _rb_insert(&this->schinfo.node, &cpus[my_cpu].sched.run_queue, rb_proc_less);
    } else if (new_state == SLEEPING || new_state == ZOMBIE) {
        // 从RUNNING变为SLEEPING/ZOMBIE，不需要操作红黑树（因为本来就不在树中）
        if (new_state == ZOMBIE) {
            cpus[my_cpu].sched.task_count--;
        }
    }
    // RUNNING状态的进程不在红黑树中
}

static Proc *pick_next()
{
    if (panic_flag) return cpus[cpuid()].sched.idle;
    
    int my_cpu = cpuid();
    struct rb_root_ *my_queue = &cpus[my_cpu].sched.run_queue;
    Proc *next_proc = NULL;
    
    // 从红黑树中选择最左节点（vruntime最小）
    struct rb_node_ *leftmost = _rb_first(my_queue);
    
    if (leftmost) {
        next_proc = container_of(leftmost, Proc, schinfo.node);
        _rb_erase(leftmost, my_queue);
        // task_count不减少，因为进程马上要被执行，它仍然是这个CPU的任务
        return next_proc;
    }
    
    // 当前队列为空，尝试工作窃取
    // 注意：此时已持有全局调度锁
    
    Proc *stolen = NULL;
    for (int i = 0; i < NCPU; i++) {
        int target_cpu_id = (my_cpu + i + 1) % NCPU; // 从下一个CPU开始遍历
        if (target_cpu_id == my_cpu) continue;
    
        // 已持有全局锁，可以直接安全地访问其他CPU的队列
        struct rb_root_ *other_queue = &cpus[target_cpu_id].sched.run_queue;
        if (cpus[target_cpu_id].sched.task_count > 0) {
            struct rb_node_ *leftmost_other = _rb_first(other_queue);
            if (leftmost_other) {
                stolen = container_of(leftmost_other, Proc, schinfo.node);
                _rb_erase(leftmost_other, other_queue);
                cpus[target_cpu_id].sched.task_count--;
                break; // 窃取成功，退出循环
            }
        }
    }
    
    // 如果窃取成功
    if (stolen) {
        // 调整vruntime以适应本地CPU的min_vruntime
        if (stolen->schinfo.vruntime < cpus[my_cpu].sched.min_vruntime) {
            stolen->schinfo.vruntime = cpus[my_cpu].sched.min_vruntime;
        }
        // 窃取来的任务现在属于我了
        cpus[my_cpu].sched.task_count++;
        return stolen;
    }
    
    // 如果循环结束都没有偷到，直接返回idle
    return cpus[my_cpu].sched.idle;
}


static void update_this_proc(Proc *p)
{
    cpus[cpuid()].sched.current_proc = p;
}

void sched(enum procstate new_state)
{
    // 进入调度器，不加全局锁
    // acquire_sched_lock();
    ASSERT(global_sched_lock.locked==1);
    
    auto this = thisproc();
    // if (this->pid>0) printk("sched.c:300, pid: %d,\n old state: %d,new state: %d\n",this->pid, this->state, new_state);
    // if (this->pid==1 && new_state==ZOMBIE){
    //     printk("root proc is dying\n");
    //     PANIC();
    // }
#ifdef debug_sched
    printk("this cpu is %lld, process's pid is %d, state is %d\n", 
           cpuid(), this->pid, this->state);
#endif
    if (this->killed && new_state!=ZOMBIE){
        release_sched_lock();
        return;
    }
    
    ASSERT(this->state == RUNNING);
    
    update_this_state(new_state);
    
    auto next = pick_next();
    
    update_this_proc(next);
    if (next->state != RUNNABLE && !next->idle) {
        printk("This proc is: %d, it is %d\n", this->pid, this->state);
        printk("Next proc is: %d, it is %d\n", next->pid, next->state);
    }
    ASSERT(next->state == RUNNABLE);
    

    next->state = RUNNING;
    next->schinfo.start_exec_time = get_timestamp();
    
    if (next != this) {
        attach_pgdir(&next->pgdir);
        if (next->pid>1){
            printk("sched.c:335\n");
        }
        swtch(next->kcontext, &this->kcontext);
    }

    // 从swtch返回后（即当前进程被重新调度），释放全局调度锁
    release_sched_lock();
}
void trap_return(u64);
u64 proc_entry(void (*entry)(u64), u64 arg)
{
    // 新启动的进程继承了调度器的锁，需要在这里释放
    release_sched_lock();
    // if (entry == trap_return) {
    //     Proc *p = thisproc();

    //     // 这是关键一步：
    //     // 手动将内核栈指针 SP 移动到我们之前存放 UserContext 的位置。
    //     // 这就模拟了 trap_entry 的行为，为 trap_return 准备好了它需要的数据。
    //     asm volatile("mov sp, %0" : : "r"((u64)p->ucontext));
        
    //     // 现在 SP 已经指向了正确的 UserContext，可以安全地调用 trap_return 了。
    //     trap_return(0); // 参数无所谓，不会被使用

    //     // trap_return 永远不应该返回到这里。如果返回了，说明出错了。
    //     printk("trap_return returned to proc_entry!");
    //     PANIC(); 
    // }
    //     if (entry == trap_return) {
    //     Proc *p = thisproc();
    //     u64 current_sp;

    //     // 获取执行 mov sp 指令前的 sp 值
    //     asm volatile("mov %0, sp" : "=r"(current_sp));

    //     // ============ 添加的测试代码 ============
    //     if (p->pid > 0) { // 只打印用户进程，避免干扰
    //         printk("\n--- SP POINTER TEST (PID: %d) ---\n", p->pid);
    //         printk("  [BEFORE mov sp] Current SP      = 0x%llx\n", current_sp);
    //         printk("  [TARGET]        p->ucontext     = 0x%llx\n", (usize)p->ucontext);
    //         printk("  [REFERENCE]     p->kcontext     = 0x%llx\n", (usize)p->kcontext);
    //         printk("--- END TEST ---\n\n");
    //     }
    //     // =======================================

    //     // 这是关键一步：
    //     // 手动将内核栈指针 SP 移动到我们之前存放 UserContext 的位置。
    //     asm volatile("mov sp, %0" : : "r"((u64)p->ucontext));
        
    //     // 现在 SP 已经指向了正确的 UserContext，可以安全地调用 trap_return 了。
    //     trap_return(0); // 参数无所谓，不会被使用

    //     // trap_return 永远不应该返回到这里。如果返回了，说明出错了。
    //     printk("trap_return returned to proc_entry!");
    //     PANIC(); 
    // }
    set_return_addr(entry);
    return arg;
}

