#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/rbtree.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

typedef struct UserContext {
    // TODO: customize your trap frame
    u64 spsr, elr;
    u64 x[18];
} UserContext;

typedef struct KernelContext {
    // TODO: customize your context
    u64 lr, x0, x1; //lr为x30寄存器
    u64 x[11]; //x11-x29寄存器
} KernelContext;

// embeded data for procs
struct schinfo {
    // TODO: customize your sched info
    u64 vruntime;
    struct rb_node_ node;
    int nice;
    u64 start_exec_time;
};

// int prio_to_weight[40]={
// /* -20 */     88761,     71755,     56483,     46273,     36291,
// /* -15 */     29154,     23254,     18705,     14949,     11916,
// /* -10 */      9548,      7620,      6100,      4904,      3906,
// /*  -5 */      3121,      2501,      1991,      1586,      1277,
// /*   0 */      1024,       820,       655,       526,       423,
// /*   5 */       335,       272,       215,       172,       137,
// /*  10 */       110,        87,        70,        56,        45,
// /*  15 */        36,        29,        23,        18,        15,
// };
extern int prio_to_weight[];
#define WEIGHT(priority) prio_to_weight[priority+20]

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
