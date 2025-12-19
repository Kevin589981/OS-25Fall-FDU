#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h> // For uint64_t for clarity

// 在MSVC下，需要包含 intrin.h 并使用 _BitScanForward64
#if defined(_MSC_VER)
#include <intrin.h>
#endif

// 定义测试迭代次数
#define NUM_ITERATIONS 100000000

// =============================================================================
//                   方法一: 软件循环法 (Software Loop)
// =============================================================================
// 模拟在C代码中逐位检查，这是最慢的方法。
int find_first_bit_sw(uint64_t mask) {
    if (mask == 0) return -1;
    for (int i = 0; i < 64; ++i) {
        if (mask & (1ULL << i)) {
            return i;
        }
    }
    return -1; // 理论上不会到达这里
}

// =============================================================================
//                方法二: 编译器内置函数法 (Compiler Built-in)
// =============================================================================
// 这是推荐的可移植方法。编译器会将其转换为最高效的指令。
// 对于GCC/Clang, __builtin_ffsl 返回的是1-based index, 0 if no bit is set.
// 对于MSVC, _BitScanForward64 返回0/1表示是否找到, index通过指针返回.
int find_first_bit_builtin(uint64_t mask) {
    if (mask == 0) return -1;
#if defined(__GNUC__) || defined(__clang__)
    // __builtin_ffsl 返回的是从1开始的索引。我们需要的是从0开始。
    // ffs = find first set. l = long.
    return __builtin_ffsl(mask) - 1;
#elif defined(_MSC_VER)
    unsigned long index;
    if (_BitScanForward64(&index, mask)) {
        return index;
    }
    return -1;
#else
    // 作为其他编译器的回退方案
    return find_first_bit_sw(mask);
#endif
}

// =============================================================================
//                  方法三: 内联汇编法 (Inline Assembly)
// =============================================================================
// 直接使用 x86-64 的 'bsf' 指令。这是性能的极限。
// 注意: 此代码仅适用于 GCC/Clang on x86-64 架构。
int find_first_bit_asm(uint64_t mask) {
    if (mask == 0) return -1;
    uint64_t index;
    // asm ("指令" : 输出操作数 : 输入操作数 : 破坏描述);
    // "bsfq %1, %0" :
    //   - 'q' 后缀表示操作数是64位的 (quad word)。
    //   - %1 是第二个操作数 (输入, mask)。
    //   - %0 是第一个操作数 (输出, index)。
    // "=r" (index) :
    //   - '=' 表示这是输出操作数。
    //   - 'r' 表示将变量(index)放入任意通用寄存器。
    // "r" (mask) :
    //   - 'r' 表示将变量(mask)放入任意通用寄存器。
    asm ("bsfq %1, %0" : "=r" (index) : "r" (mask));
    return (int)index;
}


int main() {
    printf("开始位扫描性能基准测试 (%d 次迭代)\n", NUM_ITERATIONS);
    printf("====================================================\n");

    clock_t start, end;
    volatile long long result = 0; // volatile 防止编译器优化掉整个循环

    // --- 测试软件循环法 ---
    start = clock();
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        // 创建一个非零的随机数
        uint64_t test_val = rand() | (1ULL << (rand() % 64)) | 1;
        result += find_first_bit_sw(test_val);
    }
    end = clock();
    printf("方法一 (软件循环)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);
    (void)result; // 确保result被使用

    // --- 测试编译器内置函数法 ---
    result = 0;
    start = clock();
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        uint64_t test_val = rand() | (1ULL << (rand() % 64)) | 1;
        result += find_first_bit_builtin(test_val);
    }
    end = clock();
    printf("方法二 (编译器内置函数) 耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);
    (void)result;

    // --- 测试内联汇编法 ---
    result = 0;
    start = clock();
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        uint64_t test_val = rand() | (1ULL << (rand() % 64)) | 1;
        result += find_first_bit_asm(test_val);
    }
    end = clock();
    printf("方法三 (内联汇编 bsf)  耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);
    (void)result;


    printf("\n====================================================\n");
    printf("测试完成。\n");

    return 0;
}