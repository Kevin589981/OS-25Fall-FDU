#pragma once

#include <common/defines.h>
#include <aarch64/intrinsic.h>

// typedef struct {
//     volatile bool locked;
// } SpinLock;

// void init_spinlock(SpinLock *);
// bool try_acquire_spinlock(SpinLock *);
// void acquire_spinlock(SpinLock *);
// void release_spinlock(SpinLock *);

// common/spinlock.h

// 假设这是自旋锁的数据结构
typedef struct SpinLock {
    volatile int locked;

    // 用于调试：记录锁的持有者信息
    int owner_cpu;
    int owner_pid;
    const char *owner_file;
    int owner_line;
    u64 acquire_timestamp;
} SpinLock;

/**
 * @brief 初始化自旋锁
 * @param lock 指向 SpinLock 对象的指针
 */
void init_spinlock(SpinLock *lock);

/**
 * @brief 获取自旋锁 (内部包含超时检测)
 * @param lock 指向 SpinLock 对象的指针
 * @param file 调用此函数的文件名 (由宏自动传入)
 * @param line 调用此函数的代码行号 (由宏自动传入)
 */
void acquire_spinlock_internal(SpinLock *lock, const char *file, int line);
bool try_acquire_spinlock(SpinLock *lock);
/**
 * @brief 释放自旋锁
 * @param lock 指向 SpinLock 对象的指针
 */
void release_spinlock(SpinLock *lock);

/**
 * @brief 这是您在代码中实际调用的宏
 *        它会自动将文件名和行号传递给内部实现函数
 */
#define acquire_spinlock(lock) acquire_spinlock_internal(lock, __FILE__, __LINE__)