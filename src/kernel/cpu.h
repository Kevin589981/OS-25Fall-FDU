#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>
#include <common/list.h>

#define NCPU 4
#define SCHED_TIMESLICE_MS 8
struct sched {
    struct rb_root_ run_queue;  // 红黑树存储RUNNABLE进程
    u64 task_count;
    u64 min_vruntime;          // 跟踪最小vruntime
    struct Proc* current_proc; // 当前RUNNING进程
    struct Proc* idle;
};

struct cpu {
    bool online;
    struct rb_root_ timer;
    struct sched sched;
    KernelContext *zombie_to_reap; // 需要回收的僵尸进程
};

extern SpinLock global_sched_lock;
extern struct cpu cpus[NCPU];

struct timer {
    bool triggered;
    int elapse;
    u64 _key;
    struct rb_node_ _node; //用于存放需要定时的timer红黑树节点
    void (*handler)(struct timer *);
    u64 data;
};

void init_clock_handler();

void set_cpu_on();
void set_cpu_off();

void set_cpu_timer(struct timer *timer);
void cancel_cpu_timer(struct timer *timer);