#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>
#include <common/string.h>
#include <driver/clock.h>

extern bool panic_flag;
extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);
u64 proc_entry(void (*entry)(u64), u64 arg);
extern Proc idle_procs[];

SpinLock global_sched_lock;

// 每个CPU的调度定时器
static struct timer sched_timers[NCPU];

// 用于在激活新进程时，给vruntime增加一个小的偏移量，避免多个进程vruntime完全相同
static u64 vruntime_spread_counter = 0;

static u64 calculate_timeslice(Proc *p)
{
    int cpu_id = cpuid();
    struct sched *s = &cpus[cpu_id].sched;

    // 如果是idle进程或者当前CPU只有一个可运行任务，则给予一个固定的默认时间片
    if (p->idle || s->task_count <= 1) {
        return SCHED_TIMESLICE_MS;
    }

    // 总权重 = 运行队列中的进程权重 + 即将运行的当前进程的权重
    u64 total_weight = s->queue_weight + WEIGHT(p->schinfo.nice);
    u64 timeslice;

    // 根据权重比例计算理想时间片
    // 注意：为保证计算精度，先乘后除
    if (total_weight == 0) return SCHED_MIN_GRANULARITY_MS; // 避免除以0
    timeslice = (u64)SCHED_LATENCY_MS * WEIGHT(p->schinfo.nice) / total_weight;

    // 确保时间片不小于最小粒度
    return timeslice < SCHED_MIN_GRANULARITY_MS ? SCHED_MIN_GRANULARITY_MS : timeslice;
}

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
    acquire_sched_lock();
    
    Proc *current = thisproc();
    // 如果当前进程不是RUNNING状态（可能在定时器触发前就阻塞了），则直接重设计时器并返回
    if (current->state != RUNNING) {
        release_sched_lock();
        timer->elapse = SCHED_TIMESLICE_MS;
        set_cpu_timer(timer);
        return;
    }

    // 更新当前进程的vruntime
    u64 current_time = get_timestamp();
    if (current->schinfo.start_exec_time > 0 && !current->idle) {
        u64 delta_exec = current_time - current->schinfo.start_exec_time;
        if (delta_exec > 0) {
            int weight = WEIGHT(current->schinfo.nice);
            u64 delta_vruntime = (delta_exec * NICE_0_LOAD) / weight;
            current->schinfo.vruntime += delta_vruntime;
        }
    }
    
    // 如果需要，进行抢占
    sched(RUNNABLE);
}

void create_idle_proc()
{
    for (int i = 0; i < NCPU; i++) {
        Proc *p = &idle_procs[i];
        memset(p, 0, sizeof(Proc));
        p->state = RUNNING;
        p->idle = true;
        p->pid = -1 - i;
        p->kstack = NULL; // idle 进程不使用独立内核栈，它在启动栈上运行
        p->parent = NULL;
        p->ucontext = NULL;
        p->kcontext = NULL;
        init_pgdir(&p->pgdir); // 即使是idle进程，也要有个空的页目录

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
    // 注意：检查进程状态通常需要更大的进程锁，但如果是从父进程的wait循环中调用，
    // 且已持有进程锁，可以短暂获取调度锁来安全检查
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

bool _activate_proc(Proc *p, bool onalert)
{
    acquire_sched_lock(); 
    
    if (p->state == RUNNING || p->state == RUNNABLE) {
        release_sched_lock(); 
        return false;
    }
    if (p->state == DEEPSLEEPING && onalert) {
        release_sched_lock();
        return false;
    }

    if (p->state == SLEEPING || p->state == DEEPSLEEPING || p->state == UNUSED) {
        // 负载均衡：选择任务最少的CPU
        int target_cpu = 0;
        u64 min_count = cpus[0].sched.task_count;
        for (int i = 1; i < NCPU; i++) {
            if (cpus[i].sched.task_count < min_count) {
                min_count = cpus[i].sched.task_count;
                target_cpu = i;
            }
        }
    
        u64 base_vruntime;
        // 如果目标CPU没有任务，其min_vruntime可能很旧，需要从全局找一个基准
        if (cpus[target_cpu].sched.task_count == 0) {
            u64 global_min_vruntime = 0;
            bool found_active_cpu = false;
            for (int i = 0; i < NCPU; i++) {
                if (cpus[i].sched.task_count > 0) {
                    if (!found_active_cpu || cpus[i].sched.min_vruntime < global_min_vruntime) {
                        global_min_vruntime = cpus[i].sched.min_vruntime;
                    }
                    found_active_cpu = true;
                }
            }
            base_vruntime = found_active_cpu ? global_min_vruntime : cpus[target_cpu].sched.min_vruntime;
            // 更新空闲CPU的min_vruntime，使其与系统保持同步
            if (found_active_cpu) cpus[target_cpu].sched.min_vruntime = global_min_vruntime;
        } else {
            base_vruntime = cpus[target_cpu].sched.min_vruntime;
        }

        u64 spread_offset = (vruntime_spread_counter++ % 100) * 10;
        p->schinfo.vruntime = base_vruntime + spread_offset;
        
        p->state = RUNNABLE;
    
        if (_rb_insert(&p->schinfo.node, &cpus[target_cpu].sched.run_queue, rb_proc_less) != 0) {
            PANIC();
        }
        cpus[target_cpu].sched.task_count++;
        cpus[target_cpu].sched.queue_weight += WEIGHT(p->schinfo.nice);
    
        release_sched_lock();
        return true;
    }
    
    // 不应激活ZOMBIE状态的进程
    release_sched_lock();
    if(p->state != ZOMBIE) PANIC();
    return false;
}

// 更新当前进程的vruntime
static void update_vruntime(Proc *p)
{
    if (p->idle || p->schinfo.start_exec_time == 0) return;
    
    u64 current_time = get_timestamp();
    u64 delta_exec = current_time - p->schinfo.start_exec_time;
    if (delta_exec > 0) {
        int weight = WEIGHT(p->schinfo.nice);
        u64 delta_vruntime = (delta_exec * NICE_0_LOAD) / weight;
        p->schinfo.vruntime += delta_vruntime;
    }
}

static void update_this_state(enum procstate new_state)
{
    Proc *this = thisproc();
    int my_cpu = cpuid();
    
    if (this->idle) {
        this->state = RUNNING; // idle进程永远是RUNNING
        return;
    }

    update_vruntime(this);
    
    // 更新min_vruntime，以队列中最左边的vruntime为基准，或者当前进程的vruntime
    struct rb_node_ *leftmost = _rb_first(&cpus[my_cpu].sched.run_queue);
    u64 queue_min_vruntime = -1; // -1 represents infinity
    if(leftmost) {
        queue_min_vruntime = container_of(leftmost, Proc, schinfo.node)->schinfo.vruntime;
    }

    if(this->schinfo.vruntime > queue_min_vruntime) {
        cpus[my_cpu].sched.min_vruntime = queue_min_vruntime;
    } else {
        cpus[my_cpu].sched.min_vruntime = this->schinfo.vruntime;
    }

    this->state = new_state;
    
    if (new_state == RUNNABLE) {
        // 从RUNNING变为RUNNABLE，重新插入红黑树
        if (_rb_insert(&this->schinfo.node, &cpus[my_cpu].sched.run_queue, rb_proc_less) != 0) {
            PANIC();
        }
        cpus[my_cpu].sched.queue_weight += WEIGHT(this->schinfo.nice);
    } else if (new_state == ZOMBIE || new_state == SLEEPING || new_state == DEEPSLEEPING) {
        // 进程离开运行队列，总任务数减少
        cpus[my_cpu].sched.task_count--;
    }
}

static Proc *pick_next()
{
    int my_cpu = cpuid();
    struct sched *s = &cpus[my_cpu].sched;

    if (panic_flag) return s->idle;
    
    struct rb_node_ *leftmost = _rb_first(&s->run_queue);
    
    if (leftmost) {
        Proc* next_proc = container_of(leftmost, Proc, schinfo.node);
        _rb_erase(leftmost, &s->run_queue);
        s->queue_weight -= WEIGHT(next_proc->schinfo.nice);
        return next_proc;
    }
    
    // 当前队列为空，尝试工作窃取
    for (int i = 1; i < NCPU; i++) {
        int target_cpu_id = (my_cpu + i) % NCPU;
        struct sched *target_s = &cpus[target_cpu_id].sched;

        // 只从任务数大于1的CPU窃取，避免把对方偷空
        if (target_s->task_count > 1) {
            struct rb_node_ *stolen_node = _rb_first(&target_s->run_queue);
            if (stolen_node) {
                Proc *stolen = container_of(stolen_node, Proc, schinfo.node);
                
                // 从目标CPU移除
                _rb_erase(stolen_node, &target_s->run_queue);
                target_s->task_count--;
                target_s->queue_weight -= WEIGHT(stolen->schinfo.nice);
                
                // 调整vruntime以适应本地CPU的min_vruntime
                if (stolen->schinfo.vruntime < s->min_vruntime) {
                    stolen->schinfo.vruntime = s->min_vruntime;
                }
                
                // 添加到本地CPU
                s->task_count++;
                return stolen;
            }
        }
    }
    
    return s->idle;
}

static void update_this_proc(Proc *p)
{
    cpus[cpuid()].sched.current_proc = p;
}

void sched(enum procstate new_state)
{
    ASSERT(global_sched_lock.locked == 1);
    
    Proc* this = thisproc();
    int my_cpu = cpuid();
    
    if (this->killed && new_state != ZOMBIE) {
        release_sched_lock();
        return;
    }
    
    ASSERT(this->state == RUNNING);
    
    // 取消当前CPU的调度定时器（静态条件？）
    if (!sched_timers[my_cpu].triggered) {
        cancel_cpu_timer(&sched_timers[my_cpu]);
        sched_timers[my_cpu].triggered = true;
    }

    update_this_state(new_state);
    
    Proc* next = pick_next();
    
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE || next->idle);
    
    next->state = RUNNING;
    next->schinfo.start_exec_time = get_timestamp();
    
    // 为新进程设置调度定时器（包括idle进程）
    sched_timers[my_cpu].elapse = calculate_timeslice(next);
    set_cpu_timer(&sched_timers[my_cpu]);
    
    if (next != this) {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }

    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    // 新启动的进程继承了调度器的锁，需要在这里释放
    release_sched_lock();
    
    set_return_addr(entry);
    return arg;
}