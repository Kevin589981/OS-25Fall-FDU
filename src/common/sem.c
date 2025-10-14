#include <common/sem.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/list.h>
#include <kernel/debug.h>
void init_sem(Semaphore *sem, int val)
{
    sem->val = val;
    init_spinlock(&sem->lock);
    init_list_node(&sem->sleeplist);
}

bool get_sem(Semaphore *sem)
{
    bool ret = false;
    acquire_spinlock(&sem->lock);
    if (sem->val > 0) {
        sem->val--;
        ret = true;
    }
    release_spinlock(&sem->lock);
    return ret;
}

int get_all_sem(Semaphore *sem)
{
    int ret = 0;
    acquire_spinlock(&sem->lock);
    if (sem->val > 0) {
        ret = sem->val;
        sem->val = 0;
    }
    release_spinlock(&sem->lock);
    return ret;
}

bool wait_sem(Semaphore *sem)
{
    acquire_spinlock(&sem->lock);
    if (--sem->val >= 0) {
        release_spinlock(&sem->lock);
        return true;
    }
    WaitData *wait = kalloc(sizeof(WaitData));
    wait->proc = thisproc();
    wait->up = false;

// #ifdef debug_sem
//     printk("sem->sleeplist: %llx",(u64)&sem->sleeplist);
// #endif
// #ifdef debug_page_fault
//     // printk("sem.c:54: proc's ptnode is %llx, ptnode.next is %llx, ptnode.prev is %llx\n", (u64)&p->ptnode, (u64)p->ptnode.next, (u64)p->ptnode.prev);
//     // printk("sem.c:55: proc's pid id %d\n",p->pid);
//     printk("sem->sleeplist: %llx\n",(u64)&sem->sleeplist);
//     // printk("[START_PROC] Before insert, root_proc.children is %llx, root_proc.children.next is %llx, root_proc.children.prev is %llx\n", (u64)&root_proc.children, (u64)root_proc.children.next, (u64)root_proc.children.prev);
// #endif
    _insert_into_list(&sem->sleeplist, &wait->slnode);
    acquire_sched_lock();
    release_spinlock(&sem->lock);
#ifdef debug_sched //{ UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };
    printk("proc.c:224, cpuid is %lld\n",cpuid());
#endif
    sched(SLEEPING);
    acquire_spinlock(&sem->lock); // also the lock for waitdata
    if (!wait->up) // wakeup by other sources
    {
        ASSERT(++sem->val <= 0);
        _detach_from_list(&wait->slnode);
    }
    release_spinlock(&sem->lock);
    bool ret = wait->up;
    kfree(wait);
    return ret;
}

void post_sem(Semaphore *sem)
{
    acquire_spinlock(&sem->lock);
    if (++sem->val <= 0) {
        ASSERT(!_empty_list(&sem->sleeplist));
        auto wait = container_of(sem->sleeplist.prev, WaitData, slnode);
        wait->up = true;
        _detach_from_list(&wait->slnode);
        activate_proc(wait->proc);
    }
    release_spinlock(&sem->lock);
}