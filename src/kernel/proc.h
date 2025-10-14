#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/rbtree.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

typedef struct UserContext {
    u64 spsr, elr;
    u64 x[18];
} UserContext;

typedef struct KernelContext {
    u64 lr, x0, x1;
    u64 x[11];
} KernelContext;

// embeded data for procs
struct schinfo {
    u64 vruntime;
    struct rb_node_ node;  // 改为红黑树节点
    int nice;              // -20 到 19
    u64 start_exec_time;
};

extern int prio_to_weight[];
#define WEIGHT(priority) prio_to_weight[priority+20]
#define NICE_0_LOAD 1024

typedef struct Proc {
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode ptnode;
    struct Proc *parent;
    struct schinfo schinfo;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;
} Proc;

void init_kproc();
void init_proc(Proc *);
Proc *create_proc();
int start_proc(Proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
int wait(int *exitcode);