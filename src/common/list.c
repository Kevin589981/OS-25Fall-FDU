#include <common/list.h>
#include <kernel/printk.h>

void init_list_node(ListNode *node)
{
    node->prev = node;
    node->next = node;
}
#include <kernel/proc.h> 
extern Proc root_proc;
// extern void dump_proc_details(Proc *p, const char *label);
extern Proc *thisproc();
extern int flag_;
ListNode *_merge_list(ListNode *node1, ListNode *node2)
{   
    // u64 return_addr;
    // asm volatile("mov %0, lr" : "=r"(return_addr));
    // if (flag_==1) {
    //     printk("\n[DEBUG] --- INSIDE _merge_list (TARGETED call detected!) ---\n");
        
    //     // 1. 打印函数实际接收到的参数值 (诊断的核心)
    //     printk("[DEBUG] Received arg1 (node1): 0x%llx <-- COMPARE THIS!\n", (u64)node1);
        
    //     // 2. 再次完整转储 root_proc 的详细字段信息
    //     dump_proc_details(&root_proc, "root_proc state at _merge_list entry");
    // }
    // printk("This proc is %d\n",thisproc()->pid);
    // if (node1&&node1->next==NULL){
    //     printk("This proc is %d\n",thisproc()->pid);
    //     printk("list.c:27 node1 is %llx\n", (u64)node1);
    //     printk("list.c:28 node2 is %llx\n", (u64)node2);
        // if (((u64)node1 >= (u64)&root_proc && (u64)node1 < (u64)&root_proc + sizeof(Proc)) || 
        // (u64)node1 == 0xffff000040c972d8) { // 硬编码日志中出现的错误地址

        // ==================== 读取并打印返回地址 ====================
        
        // ==========================================================

    //     printk("\n[DEBUG] --- INSIDE _merge_list (SUSPICIOUS call detected!) ---\n");
        
    //     // *** 打印我们最关心的信息：调用者的返回地址 ***
    //     printk("[DEBUG] *** Caller's Return Address (LR): 0x%llx ***\n", return_addr);
        
    //     printk("[DEBUG] Received arg1 (node1): 0x%llx\n", (u64)node1);
        
    //     // dump_proc_details(&root_proc, "root_proc state at _merge_list entry");
    // }
    
    if (!node1)
        return node2;
    // if (node1->next==NULL){
    //     printk("list.c:%d\n", 17);
    // }
    if (!node2)
        return node1;

    // before: (arrow is the next pointer)
    //   ... --> node1 --> node3 --> ...
    //   ... <-- node2 <-- node4 <-- ...
    //
    // after:
    //   ... --> node1 --+  +-> node3 --> ...
    //                   |  |
    //   ... <-- node2 <-+  +-- node4 <-- ...
    ListNode *node3 = node1->next;
    // if (node1->next==NULL)printk("list.c:26\n");
    ListNode *node4 = node2->prev;

    node1->next = node2;
    node2->prev = node1;
    // if (node3==NULL)printk("list.c:30\n");
    node4->next = node3;
    // if (node4==NULL)printk("list.c:31\n");
    node3->prev = node4;
    // printk("list.c:32");
    return node1;
}

ListNode *_detach_from_list(ListNode *node)
{
    ListNode *prev = node->prev;

    node->prev->next = node->next;
    node->next->prev = node->prev;
    init_list_node(node);

    if (prev == node)
        return NULL;
    return prev;
}

QueueNode *add_to_queue(QueueNode **head, QueueNode *node)
{
    do
        node->next = *head;
    while (!__atomic_compare_exchange_n(head, &node->next, node, true,
                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    return node;
}

QueueNode *fetch_from_queue(QueueNode **head)
{
    QueueNode *node;
    do
        node = *head;
    while (node &&
           !__atomic_compare_exchange_n(head, &node, node->next, true,
                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    return node;
}

QueueNode *fetch_all_from_queue(QueueNode **head)
{
    return __atomic_exchange_n(head, NULL, __ATOMIC_ACQ_REL);
}

void queue_init(Queue *x)
{
    x->begin = x->end = 0;
    x->sz = 0;
    init_spinlock(&x->lk);
}
void queue_lock(Queue *x)
{
    acquire_spinlock(&x->lk);
}
void queue_unlock(Queue *x)
{
    release_spinlock(&x->lk);
}
void queue_push(Queue *x, ListNode *item)
{
    init_list_node(item);
    if (x->sz == 0) {
        x->begin = x->end = item;
        x->sz = 1;
    } else {
        _merge_list(x->end, item);
        x->end = item;
    }
}
void queue_pop(Queue *x)
{
    if (x->sz == 0)
        PANIC();
    if (x->sz == 1) {
        x->begin = x->end = 0;
    } else {
        auto t = x->begin;
        x->begin = x->begin->next;
        _detach_from_list(t);
    }
    x->sz--;
}
ListNode *queue_front(Queue *x)
{
    if (!x || !x->begin)
        PANIC();
    return x->begin;
}
bool queue_empty(Queue *x)
{
    return x->sz == 0;
}