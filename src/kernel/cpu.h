#pragma once

#include <kernel/proc.h>
#include <common/rbtree.h>
#include <common/list.h>

#define NCPU 4

struct sched {
    // 每个CPU的运行队列和锁
    ListNode run_queue;
    SpinLock lock;
    u64 task_count;
    struct Proc* current_proc;
    struct Proc* idle;
};

struct cpu {
    bool online;
    struct rb_root_ timer;
    struct sched sched;
};

extern SpinLock global_sched_lock;  // 保留用于is_zombie等全局操作
extern struct cpu cpus[NCPU];

void set_cpu_on();
void set_cpu_off();