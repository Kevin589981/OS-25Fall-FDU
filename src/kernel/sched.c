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

// 每个CPU的调度定时器
static struct timer sched_timers[NCPU];

// 用于初始vruntime的展开，避免所有进程vruntime相同
static u64 vruntime_spread_counter = 0;

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
static void update_this_proc(Proc *p);
static Proc *pick_next();

// 调度定时器处理函数（抢占式调度核心）
static void sched_timer_handler(struct timer *timer)
{
    // 获取调度锁
    acquire_spinlock(&global_sched_lock);
    
    Proc *current = thisproc();
    int cpu_id = cpuid();
    struct rb_root_ *queue = &cpus[cpu_id].sched.run_queue;
    struct rb_node_ *leftmost = _rb_first(queue);
    
    // idle进程：检查是否有任务需要运行
    if (current->idle) {
        if (leftmost) {
            // 有任务等待，执行调度切换到该任务
            auto next = pick_next();
            update_this_proc(next);
            ASSERT(next->state == RUNNABLE || next->idle);
            
            next->state = RUNNING;
            next->schinfo.start_exec_time = get_timestamp();
            
            // 为新进程设置调度定时器
            sched_timers[cpu_id].elapse = SCHED_TIMESLICE_MS;
            set_cpu_timer(&sched_timers[cpu_id]);
            
            if (next != current) {
                attach_pgdir(&next->pgdir);
                swtch(next->kcontext, &current->kcontext);
            }
            
            release_spinlock(&global_sched_lock);
            return;
        } else {
            // 没有任务，尝试工作窃取
            Proc *stolen = NULL;
            for (int i = 1; i < NCPU; i++) {
                int target_cpu_id = (cpu_id + i) % NCPU;
                struct rb_root_ *other_queue = &cpus[target_cpu_id].sched.run_queue;
                
                // 如果其他CPU有超过1个任务，尝试窃取
                if (cpus[target_cpu_id].sched.task_count > 1) {
                    struct rb_node_ *leftmost_other = _rb_first(other_queue);
                    if (leftmost_other) {
                        stolen = container_of(leftmost_other, Proc, schinfo.node);
                        _rb_erase(leftmost_other, other_queue);
                        cpus[target_cpu_id].sched.task_count--;
                        
                        // 调整vruntime
                        if (stolen->schinfo.vruntime < cpus[cpu_id].sched.min_vruntime) {
                            stolen->schinfo.vruntime = cpus[cpu_id].sched.min_vruntime;
                        }
                        cpus[cpu_id].sched.task_count++;
                        
                        update_this_proc(stolen);
                        stolen->state = RUNNING;
                        stolen->schinfo.start_exec_time = get_timestamp();
                        
                        sched_timers[cpu_id].elapse = SCHED_TIMESLICE_MS;
                        set_cpu_timer(&sched_timers[cpu_id]);
                        
                        attach_pgdir(&stolen->pgdir);
                        swtch(stolen->kcontext, &current->kcontext);
                        
                        release_spinlock(&global_sched_lock);
                        return;
                    }
                }
            }
            
            // 没有可窃取的任务，继续运行idle
            release_spinlock(&global_sched_lock);
            timer->elapse = SCHED_TIMESLICE_MS;
            set_cpu_timer(timer);
            return;
        }
    }
    
    // 如果当前进程不是RUNNING状态，不进行抢占
    if (current->state != RUNNING) {
        release_spinlock(&global_sched_lock);
        timer->elapse = SCHED_TIMESLICE_MS;
        set_cpu_timer(timer);
        return;
    }
    
    // 更新当前进程的vruntime
    u64 current_time = get_timestamp();
    if (current->schinfo.start_exec_time > 0) {
        u64 delta_exec = current_time - current->schinfo.start_exec_time;
        if (delta_exec > 0) {
            int weight = WEIGHT(current->schinfo.nice);
            u64 delta_vruntime = (delta_exec * NICE_0_LOAD) / weight;
            current->schinfo.vruntime += delta_vruntime;
        }
    }
    
    // 检查是否需要抢占
    bool should_preempt = false;
    
    if (leftmost) {
        // 队列中有等待的进程，需要抢占以保证公平性
        should_preempt = true;
    }
    
    if (should_preempt) {
        // 执行抢占调度
        sched(RUNNABLE);
        // sched会释放锁并设置定时器，返回后不需要再处理
    } else {
        release_spinlock(&global_sched_lock);
        // 没有其他进程，继续运行当前进程，重新设置定时器
        timer->elapse = SCHED_TIMESLICE_MS;
        set_cpu_timer(timer);
    }
}

void create_idle_proc()
{
    for (int i = 0; i < NCPU; i++) {
        cpus[i].sched.idle = &idle_procs[i];
        Proc *p = &idle_procs[i];
        p->state = RUNNING;
        p->idle = TRUE;
        p->pid = -1 - i;
        p->kstack = NULL;
        p->parent = NULL;
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
        // 初始化红黑树
        s->run_queue.rb_node = NULL;
        s->task_count = 0;
        s->min_vruntime = 0;
        cpus[i].zombie_to_reap = kalloc(sizeof(KernelContext));
        
        // 初始化调度定时器
        memset(&sched_timers[i], 0, sizeof(struct timer));
        sched_timers[i].elapse = SCHED_TIMESLICE_MS;
        sched_timers[i].handler = sched_timer_handler;
        sched_timers[i].data = i; // 存储CPU ID
        sched_timers[i].triggered = true; // 初始状态为已触发（不在树中）
        // 初始化rb_node
        sched_timers[i]._node.rb_left = NULL;
        sched_timers[i]._node.rb_right = NULL;
        sched_timers[i]._node.__rb_parent_color = 0;
    }
    
    vruntime_spread_counter = 0;
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

void acquire_sched_lock()
{
    acquire_spinlock(&global_sched_lock);
}

void release_sched_lock()
{
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

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
    release_sched_lock();
    return r;
}

bool activate_proc(Proc *p)
{   
    acquire_sched_lock(); 
    
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock(); 
        return false;
    }
    
    else if (p->state == SLEEPING || p->state == UNUSED) {
        // 选择任务数最少的CPU（使用轮询作为tie-breaker）
        int target_cpu = 0;
        u64 min_count = cpus[0].sched.task_count;
        
        for (int i = 1; i < NCPU; i++) {
            if (cpus[i].sched.task_count < min_count) {
                min_count = cpus[i].sched.task_count;
                target_cpu = i;
            }
        }
    
        // 新进程的vruntime设置为目标CPU的最小vruntime + 小偏移
        // 偏移量避免所有新进程vruntime完全相同，导致某些进程连续运行
        u64 spread_offset = vruntime_spread_counter * 100000; // 约0.1ms的vruntime
        vruntime_spread_counter++;
        if (vruntime_spread_counter >= 100) {
            vruntime_spread_counter = 0; // 循环使用，避免溢出
        }
        
        p->schinfo.vruntime = cpus[target_cpu].sched.min_vruntime + spread_offset;
        p->state = RUNNABLE;
    
        // 插入红黑树
        _rb_insert(&p->schinfo.node, &cpus[target_cpu].sched.run_queue, rb_proc_less);
        cpus[target_cpu].sched.task_count++;
    
        release_sched_lock();
        return true;
    }
    else if (p->state == ZOMBIE){
        release_sched_lock();
        return false;
    }

    release_sched_lock();
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
        // idle进程保持RUNNING状态，不参与调度队列
        this->state = RUNNING;
        return;
    }
    
    int my_cpu = cpuid();
    u64 current_time = get_timestamp();
    
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
    } else if (new_state == ZOMBIE) {
        cpus[my_cpu].sched.task_count--;
    }
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
        return next_proc;
    }
    
    // 当前队列为空，尝试工作窃取
    Proc *stolen = NULL;
    for (int i = 0; i < NCPU; i++) {
        int target_cpu_id = (my_cpu + i + 1) % NCPU;
        if (target_cpu_id == my_cpu) continue;
    
        struct rb_root_ *other_queue = &cpus[target_cpu_id].sched.run_queue;
        if (cpus[target_cpu_id].sched.task_count > 1) { // 只从有多个任务的CPU窃取
            struct rb_node_ *leftmost_other = _rb_first(other_queue);
            if (leftmost_other) {
                stolen = container_of(leftmost_other, Proc, schinfo.node);
                _rb_erase(leftmost_other, other_queue);
                cpus[target_cpu_id].sched.task_count--;
                break;
            }
        }
    }
    
    if (stolen) {
        // 调整vruntime以适应本地CPU的min_vruntime
        if (stolen->schinfo.vruntime < cpus[my_cpu].sched.min_vruntime) {
            stolen->schinfo.vruntime = cpus[my_cpu].sched.min_vruntime;
        }
        cpus[my_cpu].sched.task_count++;
        return stolen;
    }
    
    return cpus[my_cpu].sched.idle;
}

static void update_this_proc(Proc *p)
{
    cpus[cpuid()].sched.current_proc = p;
}

void sched(enum procstate new_state)
{
    ASSERT(global_sched_lock.locked == 1);
    
    auto this = thisproc();
    int my_cpu = cpuid();
    
    if (this->killed && new_state != ZOMBIE) {
        release_sched_lock();
        return;
    }
    
    ASSERT(this->state == RUNNING);
    
    // 取消当前CPU的调度定时器
    if (!sched_timers[my_cpu].triggered) {
        cancel_cpu_timer(&sched_timers[my_cpu]);
        sched_timers[my_cpu].triggered = true;
    }
    
    update_this_state(new_state);
    
    auto next = pick_next();
    
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE || next->idle);
    
    next->state = RUNNING;
    next->schinfo.start_exec_time = get_timestamp();
    
    // 为新进程设置调度定时器（包括idle进程）
    sched_timers[my_cpu].elapse = SCHED_TIMESLICE_MS;
    set_cpu_timer(&sched_timers[my_cpu]);
    
    if (next != this) {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }

    release_sched_lock();
}

void trap_return(u64);

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    // 新启动的进程继承了调度器的锁，需要在这里释放
    release_sched_lock();
    
    set_return_addr(entry);
    return arg;
}