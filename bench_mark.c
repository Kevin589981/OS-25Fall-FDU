#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h> // For container_of macro

// =============================================================================
//                            通用定义和辅助函数
// =============================================================================

#define MAX_PID 32000
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITMAP_SIZE (MAX_PID / BITS_PER_LONG + 1)

// container_of 宏: 从一个结构体成员指针获取其父结构体的指针
#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type,member) );})

// ------------------- 双向链表实现 (为方案二提供) -------------------
typedef struct ListNode {
    struct ListNode *next, *prev;
} ListNode;

static inline void init_list_node(ListNode *node) {
    node->next = node;
    node->prev = node;
}

static inline void _insert_into_list(ListNode *head, ListNode *new_node) {
    new_node->next = head->next;
    new_node->prev = head;
    head->next->prev = new_node;
    head->next = new_node;
}

static inline void _detach_from_list(ListNode *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static inline int _empty_list(const ListNode *head) {
    return head->next == head;
}


// =============================================================================
//                            方案一：位图法 (Bitmap)
// =============================================================================

unsigned long pid_bitmap[BITMAP_SIZE];
static int next_pid_to_check = 0;

void init_pid_allocator_v1() {
    memset(pid_bitmap, 0, sizeof(pid_bitmap));
    next_pid_to_check = 1; // Start from 1, as 0 is often reserved
}

void pid_recycler_v1(int pid) {
    if (pid <= 0 || pid >= MAX_PID) {
        return;
    }
    int index = pid / BITS_PER_LONG;
    int offset = pid % BITS_PER_LONG;
    pid_bitmap[index] &= ~(1UL << offset);
}

int pid_allocator_v1() {
    for (int i = 0; i < MAX_PID; ++i) {
        int pid = (next_pid_to_check + i) % MAX_PID;
        if (pid == 0) continue;

        int index = pid / BITS_PER_LONG;
        int offset = pid % BITS_PER_LONG;

        if (!(pid_bitmap[index] & (1UL << offset))) {
            pid_bitmap[index] |= (1UL << offset);
            next_pid_to_check = (pid + 1) % MAX_PID;
            if (next_pid_to_check == 0) next_pid_to_check = 1;
            return pid;
        }
    }
    return -1;
}

// =============================================================================
//                          方案二：链表法 (Linked List)
// =============================================================================

static int allocated_pid;

typedef struct FreePidNode {
    ListNode node;
    int pid;
} FreePidNode;

static ListNode free_pid_list;

void init_pid_allocator_v2() {
    allocated_pid = 0;
    init_list_node(&free_pid_list);
}

void pid_recycler_v2(int pid) {
    FreePidNode *node = (FreePidNode*)malloc(sizeof(FreePidNode));
    if (node == NULL) {
        fprintf(stderr, "WARNING: Failed to allocate memory for PID recycling.\n");
        return;
    }
    node->pid = pid;
    _insert_into_list(&free_pid_list, &node->node);
}

int pid_allocator_v2() {
    if (!_empty_list(&free_pid_list)) {
        ListNode *node = free_pid_list.next;
        _detach_from_list(node);
        FreePidNode *fpn = container_of(node, FreePidNode, node);
        int pid = fpn->pid;
        free(fpn);
        return pid;
    }

    if (allocated_pid < MAX_PID -1) {
         ++allocated_pid;
         return allocated_pid;
    }

    return -1; // PID exhausted
}


// =============================================================================
//                             基准测试代码
// =============================================================================

// 用于存储测试中分配的PID
int pids[MAX_PID];

// 测试场景一: 连续分配所有PID
void benchmark_sequential_alloc() {
    printf("\n--- 场景一: 连续分配 %d 个 PID ---\n", MAX_PID - 1);

    // 测试方案一
    init_pid_allocator_v1();
    clock_t start = clock();
    for (int i = 1; i < MAX_PID; ++i) {
        pid_allocator_v1();
    }
    clock_t end = clock();
    printf("方案一 (位图法)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);

    // 测试方案二
    init_pid_allocator_v2();
    start = clock();
    for (int i = 1; i < MAX_PID; ++i) {
        pid_allocator_v2();
    }
    end = clock();
    printf("方案二 (链表法)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);
}

// 测试场景二: 随机分配和回收
void benchmark_random_alloc_recycle() {
    const int ops_count = 200000; // 操作次数
    printf("\n--- 场景二: %d 次随机分配与回收 ---\n", ops_count);
    int allocated_count = 0;

    // 测试方案一
    init_pid_allocator_v1();
    memset(pids, 0, sizeof(pids));
    clock_t start = clock();
    for (int i = 0; i < ops_count; ++i) {
        if (allocated_count > 0 && (rand() % 3 == 0 || allocated_count >= MAX_PID -1) ) { // 1/3 概率回收
            int idx_to_recycle = rand() % allocated_count;
            pid_recycler_v1(pids[idx_to_recycle]);
            pids[idx_to_recycle] = pids[allocated_count - 1]; // 将最后一个元素填补空位
            allocated_count--;
        } else { // 2/3 概率分配
             if(allocated_count < MAX_PID -1){
                pids[allocated_count++] = pid_allocator_v1();
             }
        }
    }
    clock_t end = clock();
    printf("方案一 (位图法)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);

    // 测试方案二
    init_pid_allocator_v2();
    memset(pids, 0, sizeof(pids));
    allocated_count = 0;
    start = clock();
    for (int i = 0; i < ops_count; ++i) {
        if (allocated_count > 0 && (rand() % 3 == 0 || allocated_count >= MAX_PID - 1)) {
            int idx_to_recycle = rand() % allocated_count;
            pid_recycler_v2(pids[idx_to_recycle]);
            pids[idx_to_recycle] = pids[allocated_count - 1];
            allocated_count--;
        } else {
             if(allocated_count < MAX_PID - 1){
                pids[allocated_count++] = pid_allocator_v2();
             }
        }
    }
    end = clock();
    printf("方案二 (链表法)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);
}

// 测试场景三: 回收半数后再分配
void benchmark_recycle_half_then_realloc() {
    const int half_pid = (MAX_PID - 1) / 2;
    printf("\n--- 场景三: 回收 %d 个 PID 后再重新分配 ---\n", half_pid);

    // --- 方案一 ---
    init_pid_allocator_v1();
    for (int i = 1; i < MAX_PID; ++i) pids[i] = pid_allocator_v1();
    // 随机回收一半
    for (int i = 0; i < half_pid; ++i) {
        int idx = rand() % (MAX_PID-1) + 1;
        if (pids[idx] != -1) {
            pid_recycler_v1(pids[idx]);
            pids[idx] = -1;
        } else {
            i--; // Retry if already recycled
        }
    }
    clock_t start = clock();
    for (int i = 0; i < half_pid; ++i) pid_allocator_v1();
    clock_t end = clock();
    printf("方案一 (位图法)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);


    // --- 方案二 ---
    init_pid_allocator_v2();
    for (int i = 1; i < MAX_PID; ++i) pids[i] = pid_allocator_v2();
    for (int i = 0; i < half_pid; ++i) {
        int idx = rand() % (MAX_PID-1) + 1;
        if (pids[idx] != -1) {
            pid_recycler_v2(pids[idx]);
            pids[idx] = -1;
        } else {
            i--;
        }
    }
    start = clock();
    for (int i = 0; i < half_pid; ++i) pid_allocator_v2();
    end = clock();
    printf("方案二 (链表法)      耗时: %f 秒\n", (double)(end - start) / CLOCKS_PER_SEC);
}


int main() {
    srand(time(NULL)); // 初始化随机数种子

    printf("开始PID分配器性能基准测试 (MAX_PID = %d)\n", MAX_PID);
    printf("====================================================\n");

    benchmark_sequential_alloc();
    benchmark_random_alloc_recycle();
    benchmark_recycle_half_then_realloc();

    printf("\n====================================================\n");
    printf("测试完成。\n");

    return 0;
}