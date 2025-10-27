#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>

#define NCPU 4

struct sched {
    // TODO: customize your sched info
    // struct rb_root_ run_queue;//为每个CPU维护一个调度的红黑树
    // SpinLock lock;
    u64 task_count; //队列中的任务数量
    struct Proc* current_proc;
    struct Proc* idle;
};

struct cpu {
    bool online;
    struct rb_root_ timer;
    struct sched sched;
};

extern ListNode global_run_queue;
// extern struct rb_root_ global_run_queue;
extern SpinLock global_sched_lock;
extern struct cpu cpus[NCPU];

void set_cpu_on();
void set_cpu_off();
