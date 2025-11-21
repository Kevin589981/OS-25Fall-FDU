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
#define IDLE_WAKEUP_MS 1  // idle被唤醒的延迟时间

SpinLock global_sched_lock;

// 每个CPU的调度定时器
static struct timer sched_timers[NCPU];

// 用于初始vruntime的展开，避免所有进程vruntime相同
static u64 vruntime_spread_counter = 0;

static u64 calculate_timeslice(Proc *p)
{
    int cpu_id = cpuid();
    struct sched *s = &cpus[cpu_id].sched;

    // idle进程不需要时间片计算
    if (p->idle) {
        return 0;
    }

    // 如果当前CPU只有一个可运行任务，给予默认时间片
    if (s->task_count <= 1) {
        return SCHED_TIMESLICE_MS;
    }

    // 总权重 = 运行队列中的进程权重 + 当前进程的权重
    u64 total_weight = s->queue_weight + WEIGHT(p->schinfo.nice);
    u64 timeslice;

    // 根据权重比例计算理想时间片
    timeslice = (u64)SCHED_LATENCY_MS * WEIGHT(p->schinfo.nice) / total_weight;

    // 确保时间片不小于最小粒度
    if (timeslice < SCHED_MIN_GRANULARITY_MS) {
        return SCHED_MIN_GRANULARITY_MS;
    }

    return timeslice;
}

// 红黑树比较函数：按vruntime排序
static bool rb_proc_less(struct rb_node_ *lnode, struct rb_node_ *rnode)
{
    Proc *lproc = container_of(lnode, Proc, schinfo.node);
    Proc *rproc = container_of(rnode, Proc, schinfo.node);
    
    if (lproc->schinfo.vruntime < rproc->schinfo.vruntime)
        return true;
    if (lproc->schinfo.vruntime > rproc->schinfo.vruntime)
        return false;
    return lproc->pid < rproc->pid;
}

static void update_this_proc(Proc *p);
static Proc *pick_next();

// 精确更新进程的vruntime（基于实际CPU运行时间）
static void update_vruntime_precise(Proc *p)
{
    if (p->idle) return;
    
    u64 current_time = get_timestamp();
    
    // 只有在有有效开始时间的情况下才更新
    if (p->schinfo.start_exec_time > 0 && current_time >= p->schinfo.start_exec_time) {
        u64 delta_exec = current_time - p->schinfo.start_exec_time;
        if (delta_exec > 0) {
            int weight = WEIGHT(p->schinfo.nice);
            u64 delta_vruntime = (delta_exec * NICE_0_LOAD) / weight;
            p->schinfo.vruntime += delta_vruntime;
        }
    }
    
    // 重置开始时间，避免重复计算
    p->schinfo.start_exec_time = 0;
}

// 开始运行一个进程（设置状态和定时器）
static void start_running_proc(Proc *next)
{
    int cpu_id = cpuid();
    
    next->state = RUNNING;
    next->schinfo.start_exec_time = get_timestamp();
    
    // 只有非idle进程才设置定时器
    if (!next->idle) {
        sched_timers[cpu_id].elapse = calculate_timeslice(next);
        set_cpu_timer(&sched_timers[cpu_id]);
    }
}

// 调度定时器处理函数
static void sched_timer_handler(struct timer *timer)
{
    acquire_sched_lock();
    
    Proc *current = thisproc();
    int cpu_id = cpuid();
    struct rb_root_ *queue = &cpus[cpu_id].sched.run_queue;
    struct rb_node_ *leftmost = _rb_first(queue);
    
    // idle进程的处理
    if (current->idle) {
        if (leftmost) {
            auto next = pick_next();
            update_this_proc(next);
            ASSERT(next->state == RUNNABLE || next->idle);
            
            start_running_proc(next);
            
            if (next != current) {
                attach_pgdir(&next->pgdir);
                swtch(next->kcontext, &current->kcontext);
            }
            
            release_sched_lock();
            return;
        } else {
            // 工作窃取
            Proc *stolen = NULL;
            for (int i = 1; i < NCPU; i++) {
                int target_cpu_id = (cpu_id + i) % NCPU;
                struct rb_root_ *other_queue = &cpus[target_cpu_id].sched.run_queue;
                
                if (cpus[target_cpu_id].sched.task_count > 1) {
                    struct rb_node_ *leftmost_other = _rb_first(other_queue);
                    if (leftmost_other) {
                        stolen = container_of(leftmost_other, Proc, schinfo.node);
                        _rb_erase(leftmost_other, other_queue);
                        cpus[target_cpu_id].sched.task_count--;
                        cpus[target_cpu_id].sched.queue_weight -= WEIGHT(stolen->schinfo.nice);
                        
                        if (stolen->schinfo.vruntime < cpus[cpu_id].sched.min_vruntime) {
                            stolen->schinfo.vruntime = cpus[cpu_id].sched.min_vruntime;
                        }
                        cpus[cpu_id].sched.task_count++;
                        
                        update_this_proc(stolen);
                        start_running_proc(stolen);
                        
                        attach_pgdir(&stolen->pgdir);
                        swtch(stolen->kcontext, &current->kcontext);
                        
                        release_sched_lock();
                        return;
                    }
                }
            }
            
            // 没有可运行的进程，idle继续运行，不设置定时器
            release_sched_lock();
            return;
        }
    }
    
    if (current->state != RUNNING) {
        release_sched_lock();
        return;
    }
    
    // 更新当前进程的vruntime
    update_vruntime_precise(current);
    
    // 检查是否需要抢占
    if (leftmost) {
        sched(RUNNABLE);
    } else {
        // 没有其他进程，重新设置定时器继续运行
        release_sched_lock();
        current->schinfo.start_exec_time = get_timestamp();
        timer->elapse = calculate_timeslice(current);
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
        s->run_queue.rb_node = NULL;
        s->task_count = 0;
        s->min_vruntime = 0;
        s->queue_weight = 0;
        
        memset(&sched_timers[i], 0, sizeof(struct timer));
        sched_timers[i].elapse = SCHED_TIMESLICE_MS;
        sched_timers[i].handler = sched_timer_handler;
        sched_timers[i].data = i; 
        sched_timers[i].triggered = true; 
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

// 唤醒idle CPU（通过设置一个短暂的定时器）
static void wakeup_idle_cpu(int cpu_id)
{
    // 注意：这里我们需要在目标CPU上设置定时器
    // 由于set_cpu_timer只能在当前CPU上操作，我们采用一个变通方案：
    // 设置一个特殊的唤醒标志，让idle在下次检查时立即调度
    // 或者直接在activate_proc中给idle CPU设置定时器（需要修改timer机制支持跨CPU）
    
    // 简化方案：依赖idle的yield()周期性检查
    // 这里不做额外操作，因为activate_proc已经把进程加入队列
    // idle的下一次yield()会自然调度到新进程
}

bool activate_proc(Proc *p)
{   
    acquire_sched_lock(); 
    
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock(); 
        return false;
    }
    
    if (p->state == SLEEPING || p->state == UNUSED) {
        usize target_cpu = 0;
        u64 min_count = cpus[0].sched.task_count;
        
        for (int i = 1; i < NCPU; i++) {
            if (cpus[i].sched.task_count < min_count) {
                min_count = cpus[i].sched.task_count;
                target_cpu = i;
            }
        }
    
        u64 base_vruntime;
        if (cpus[target_cpu].sched.task_count == 0) {
            u64 global_min_vruntime = (u64)-1;
            bool found_active_cpu = false;
            for (int i = 0; i < NCPU; i++) {
                if (cpus[i].sched.task_count > 0) {
                    if (!found_active_cpu || cpus[i].sched.min_vruntime < global_min_vruntime) {
                        global_min_vruntime = cpus[i].sched.min_vruntime;
                    }
                    found_active_cpu = true;
                }
            }

            if (found_active_cpu) {
                base_vruntime = global_min_vruntime;
                cpus[target_cpu].sched.min_vruntime = global_min_vruntime;
            } else {
                base_vruntime = cpus[target_cpu].sched.min_vruntime;
            }
        } else {
            base_vruntime = cpus[target_cpu].sched.min_vruntime;
        }

        u64 spread_offset = vruntime_spread_counter * 10;
        vruntime_spread_counter++;
        if (vruntime_spread_counter >= 100) {
            vruntime_spread_counter = 0;
        }
        
        p->schinfo.vruntime = base_vruntime + spread_offset;
        p->schinfo.start_exec_time = 0;  // 重置开始时间
        
        p->state = RUNNABLE;
    
        _rb_insert(&p->schinfo.node, &cpus[target_cpu].sched.run_queue, rb_proc_less);
        cpus[target_cpu].sched.task_count++;
        cpus[target_cpu].sched.queue_weight += WEIGHT(p->schinfo.nice);
    
        // 如果目标CPU正在运行idle，唤醒它
        bool target_is_idle = cpus[target_cpu].sched.current_proc->idle;
        
        release_sched_lock();
        
        // 如果目标CPU是idle且是当前CPU，立即yield
        if (target_is_idle && target_cpu == cpuid()) {
            // 当前CPU就是目标CPU，可以直接触发调度
            // 但这需要在没有锁的情况下进行
        } else if (target_is_idle) {
            // 目标CPU是其他CPU且在运行idle
            // 通过设置一个短暂的唤醒定时器（需要跨CPU支持）
            // 简化方案：依赖idle的定期yield检查
            wakeup_idle_cpu(target_cpu);
        }
        
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

static void update_this_state(enum procstate new_state)
{
    Proc *this = thisproc();
    if (this->idle) {
        this->state = RUNNING;
        return;
    }
    
    int my_cpu = cpuid();
    
    // 精确更新vruntime
    update_vruntime_precise(this);
    
    // 更新min_vruntime
    if (this->schinfo.vruntime > cpus[my_cpu].sched.min_vruntime) {
        cpus[my_cpu].sched.min_vruntime = this->schinfo.vruntime;
    }
    
    this->state = new_state;
    
    if (new_state == RUNNABLE) {
        _rb_insert(&this->schinfo.node, &cpus[my_cpu].sched.run_queue, rb_proc_less);
        cpus[my_cpu].sched.queue_weight += WEIGHT(this->schinfo.nice);
    } else if (new_state == ZOMBIE || new_state == SLEEPING) {
        cpus[my_cpu].sched.task_count--;
    }
}

static Proc *pick_next()
{
    if (panic_flag) return cpus[cpuid()].sched.idle;
    
    int my_cpu = cpuid();
    struct rb_root_ *my_queue = &cpus[my_cpu].sched.run_queue;
    Proc *next_proc = NULL;
    
    struct rb_node_ *leftmost = _rb_first(my_queue);
    
    if (leftmost) {
        next_proc = container_of(leftmost, Proc, schinfo.node);
        _rb_erase(leftmost, my_queue);
        cpus[my_cpu].sched.queue_weight -= WEIGHT(next_proc->schinfo.nice);
        return next_proc;
    }
    
    // 工作窃取
    Proc *stolen = NULL;
    for (int i = 0; i < NCPU; i++) {
        int target_cpu_id = (my_cpu + i + 1) % NCPU;
        if (target_cpu_id == my_cpu) continue;
    
        struct rb_root_ *other_queue = &cpus[target_cpu_id].sched.run_queue;
        if (cpus[target_cpu_id].sched.task_count > 1) {
            struct rb_node_ *leftmost_other = _rb_first(other_queue);
            if (leftmost_other) {
                stolen = container_of(leftmost_other, Proc, schinfo.node);
                _rb_erase(leftmost_other, other_queue);
                cpus[target_cpu_id].sched.task_count--;
                cpus[target_cpu_id].sched.queue_weight -= WEIGHT(stolen->schinfo.nice);
                break;
            }
        }
    }
    
    if (stolen) {
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
    
    start_running_proc(next);
    
    if (next != this) {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }

    release_sched_lock();
}

void trap_return(u64);

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    
    set_return_addr(entry);
    return arg;
}