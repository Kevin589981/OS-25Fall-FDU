#include <aarch64/intrinsic.h>
#include <common/spinlock.h>

#include <kernel/printk.h>
#include <kernel/cpu.h>
#include <kernel/proc.h>
#include <kernel/debug.h>
// 外部函数声明，假设这些函数在您的内核中已经实现
extern u64 get_timestamp(); // 获取当前时间戳（例如，微秒）
extern Proc *thisproc();   // 获取当前进程的指针
extern bool success;
// 锁超时阈值，例如10秒 (单位：微秒)
#define LOCK_TIMEOUT_US (u64)(100000llu*1000llu * 1000llu * 1000llu)

void init_spinlock(SpinLock *lock)
{
    lock->locked = 0;
    lock->owner_cpu = -1;
    lock->owner_pid = -1;
    lock->owner_file = NULL;
    lock->owner_line = 0;
    lock->acquire_timestamp = 0;
}

bool try_acquire_spinlock(SpinLock *lock)
{
    if (!lock->locked &&
        !__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        return true;
    } else {
        return false;
    }
}
extern SpinLock printk_lock;
void acquire_spinlock_internal(SpinLock *lock, const char *file, int line) {
    // 记录开始尝试获取锁的时间戳，用于检测自身等待是否超时
    // u64 start_time = get_timestamp();
    if ((u64)lock<=0x40){
        printk("BUG: NULL SPINLOCK ACQUIRE at %s:%d\n", file, line);
    }
    // 循环尝试，直到成功获取锁
    while (try_acquire_spinlock(lock) == false) {
        
#ifdef DEBUG_SPINLOCK
        u64 current_time = get_timestamp();
        // 检查锁是否已经被持有太久
        if (lock->acquire_timestamp > (u64)0 && (current_time - lock->acquire_timestamp) > LOCK_TIMEOUT_US && lock->owner_cpu != -1&&lock!=&printk_lock&&!success) {
            printk("\n--- KERNEL LOCK TIMEOUT ---\n");
            printk("Possible deadlock! Lock 0x%p held for too long.\n", lock);
            printk("Lock held by CPU: %d | PID: %d\n", lock->owner_cpu, lock->owner_pid);
            printk("Lock acquired at: %s:%d\n", lock->owner_file, lock->owner_line);
            printk("--- Current waiter ---\n");
            Proc *p = thisproc();
            printk("Current waiter CPU: %lld | PID: %d\n", cpuid(), (p ? p->pid : -1));
            printk("Attempting to acquire at: %s:%d\n", file, line);
            printk("---------------------------\n");

            // 重置锁的时间戳，避免在控制台疯狂刷屏
            lock->acquire_timestamp = current_time;
        }
#endif
        // 使用 'yield' 或 'wfe' 指令提示CPU我们处于自旋等待状态，可以节省功耗
        arch_yield();
    }

    // 成功获取锁后，记录当前持有者的信息
    Proc *p = thisproc();
    lock->owner_cpu = cpuid();
    lock->owner_pid = (p ? p->pid : -1);
    lock->owner_file = file;
    lock->owner_line = line;
    lock->acquire_timestamp = get_timestamp(); // 记录获取锁的精确时间

    // 添加一个内存屏障，确保后续的读写操作不会被重排到锁获取之前
    asm volatile("dmb ish" : : : "memory");
}

void release_spinlock(SpinLock *lock)
{
    // 在释放锁之前，先清除持有者信息
    lock->owner_cpu = -1;
    lock->owner_pid = -1;
    lock->owner_file = NULL;
    lock->owner_line = 0;
    lock->acquire_timestamp = 0;

    // 添加内存屏障，确保之前的写操作对其他CPU可见
    asm volatile("dmb ish" : : : "memory");

    // 原子地清除 locked 标志位
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}