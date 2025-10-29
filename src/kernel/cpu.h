#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>
#include <common/list.h>

#define NCPU 4

// --- 新增/修改的宏 ---
#define SCHED_TIMESLICE_MS 8         // 保留作为idle进程或只有一个任务时的默认时间片
#define SCHED_LATENCY_MS 20          // 调度周期，单位：毫秒
#define SCHED_MIN_GRANULARITY_MS 2   // 最小时间片，单位：毫秒

struct sched {
    struct rb_root_ run_queue;  // 红黑树存储RUNNABLE进程
    u64 task_count;
    u64 min_vruntime;
    u64 queue_weight;           // 新增：记录在run_queue中的所有进程的权重之和
    struct Proc* current_proc; // 当前RUNNING进程
    struct Proc* idle;
};

struct cpu {
    bool online;
    struct rb_root_ timer;
    struct sched sched;
    KernelContext *zombie_to_reap;
};

extern SpinLock global_sched_lock;
extern struct cpu cpus[NCPU];

struct timer {
    bool triggered;
    int elapse;
    u64 _key;
    struct rb_node_ _node;
    void (*handler)(struct timer *);
    u64 data;
};

void init_clock_handler();

void set_cpu_on();
void set_cpu_off();

void set_cpu_timer(struct timer *timer);
void cancel_cpu_timer(struct timer *timer);