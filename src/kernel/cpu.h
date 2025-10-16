#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>
#include <common/list.h>

#define NCPU 4

struct sched {
    struct rb_root_ run_queue;  // 红黑树存储RUNNABLE进程
    SpinLock lock;
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

void set_cpu_on();
void set_cpu_off();