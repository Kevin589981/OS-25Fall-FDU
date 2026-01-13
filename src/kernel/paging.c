#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <fs/file.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>


void init_sections(ListNode *section_head) {
    /* (Final) TODO BEGIN */
    init_list_node(section_head);
    /* (Final) TODO END */
}

void free_sections(struct pgdir *pd) {
    /* (Final) TODO BEGIN */
    if (pd == NULL || pd->section_head.next == NULL) {
        return;
    }
    
    ListNode *node = pd->section_head.next;
    while (node != &pd->section_head) {
        struct section *sec = container_of(node, struct section, stnode);
        ListNode *next = node->next;
        
        // 释放section占用的物理页
        for (u64 va = sec->begin; va < sec->end; va += PAGE_SIZE) {
            PTEntriesPtr pte = get_pte(pd, va, false);
            if (pte && (*pte & PTE_VALID)) {
                void *pa = (void *)P2K(PTE_ADDRESS(*pte));
                kfree_page(pa);
                *pte = 0;
            }
        }
        
        // 如果是file-backed section，释放inode引用
        if (sec->fp) {
            file_close(sec->fp); // 使用 file_close 而不是 inodes.put
        }
        
        // 释放section结构体
        kfree(sec);
        node = next;
    }
    
    // 重新初始化链表头
    init_list_node(&pd->section_head);
    /* (Final) TODO END */
}

u64 sbrk(i64 size) {
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */

    
    /* (Final) TODO END */
    return (u64)-1; // TODO: 实现 sbrk
}

int pgfault_handler(u64 iss) {
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 addr =
            arch_get_far(); // Attempting to access this address caused the page fault
    (void)pd;  // 避免未使用变量警告
    (void)addr; // 避免未使用变量警告
    (void)iss;  // 避免未使用变量警告

    /** 
     * (Final) TODO BEGIN
     * 
     * 1. Find the section struct which contains the faulting address `addr`.
     * 2. Check section flags to determine page fault type.
     * 3. Handle the page fault accordingly.
     * 4. Return to user code or kill the process.
     */

    /* (Final) TODO END */
    // TODO: 实现 page fault 处理，暂时杀死进程
    return -1;
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */

    /* (Final) TODO END */
}
