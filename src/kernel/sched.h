#pragma once

#include <kernel/proc.h>
#include <kernel/cpu.h>
void init_sched();
void init_schinfo(struct schinfo *);

bool activate_proc(Proc *);
bool is_zombie(Proc *);
bool is_unused(Proc *);
// void acquire_sched_lock();
void release_sched_lock();
void sched(enum procstate new_state);

#define acquire_sched_lock()  \
    do { \
        acquire_spinlock_internal(&global_sched_lock, __FILE__, __LINE__);\
    } while (0)

// MUST call lock_for_sched() before sched() !!!
// #define yield() (acquire_sched_lock(), sched(RUNNABLE))
#define yield() do{acquire_sched_lock(); sched(RUNNABLE);}while(0)

Proc *thisproc();
