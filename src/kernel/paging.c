// #include <aarch64/mmu.h>
// #include <common/defines.h>
// #include <common/list.h>
// #include <common/sem.h>
// #include <common/string.h>
// #include <fs/block_device.h>
// #include <fs/cache.h>
// #include <fs/file.h>
// #include <kernel/mem.h>
// #include <kernel/paging.h>
// #include <kernel/printk.h>
// #include <kernel/proc.h>
// #include <kernel/pt.h>
// #include <kernel/sched.h>


// void init_sections(ListNode *section_head) {
//     /* (Final) TODO BEGIN */
//     init_list_node(section_head);
//     /* (Final) TODO END */
// }

// void free_sections(struct pgdir *pd) {
//     /* (Final) TODO BEGIN */
//     if (pd == NULL || pd->section_head.next == NULL) {
//         return;
//     }
    
//     ListNode *node = pd->section_head.next;
//     while (node != &pd->section_head) {
//         printk("cpu: %lld, Freeing sections...\n",cpuid());
//         struct section *sec = container_of(node, struct section, stnode);
//         ListNode *next = node->next;
//         printk("cpu: %lld, Freeing section begin=0x%llx, end=0x%llx\n",cpuid(),
//                sec->begin, sec->end);
//         // 释放section占用的物理页
//         for (u64 va = sec->begin; va < sec->end; va += PAGE_SIZE) {
//             PTEntriesPtr pte = get_pte(pd, va, false);
//             if (pte != NULL) {
//                 printk("cpu: %lld, Freeing page at va=0x%llx\n",cpuid(),va);
//             }
//             if (pte && (*pte & PTE_VALID)) {
//                 void *pa = (void *)P2K(PTE_ADDRESS(*pte));
//                 kfree_page(pa);
//                 *pte = 0;
//             }
//             if (pte!=NULL)printk("cpu: %lld, Page at va=0x%llx freed\n",cpuid(),va);
//             if (va==0x1ff000ll){
//                 printk("BUG: trying to free va 0x1ff000\n");
//             }
//         }
//         printk("line 67: cpu: %lld, Freeing section: begin=0x%llx, end=0x%llx\n",cpuid(),
//                sec->begin, sec->end);
//         // 如果是file-backed section，释放inode引用
//         if (sec->fp) {
//             file_close(sec->fp); // 使用 file_close 而不是 inodes.put
//         }
//         printk("cpu: %lld, Section freed: begin=0x%llx, end=0x%llx\n",cpuid(),
//                sec->begin, sec->end);
//         // 释放section结构体
//         kfree(sec);
//         node = next;
//     }
    
//     // 重新初始化链表头
//     init_list_node(&pd->section_head);
//     /* (Final) TODO END */
// }

// u64 sbrk(i64 size) {
//     /**
//      * (Final) TODO BEGIN 
//      * 
//      * Increase the heap size of current process by `size`.
//      * If `size` is negative, decrease heap size. `size` must
//      * be a multiple of PAGE_SIZE.
//      * 
//      * Return the previous heap_end.
//      */

    
//     /* (Final) TODO END */
//     return (u64)-1; // TODO: 实现 sbrk
// }

// int pgfault_handler(u64 iss) {
//     Proc *p = thisproc();
//     struct pgdir *pd = &p->pgdir;
//     u64 addr =
//             arch_get_far(); // Attempting to access this address caused the page fault
//     (void)pd;  // 避免未使用变量警告
//     (void)addr; // 避免未使用变量警告
//     (void)iss;  // 避免未使用变量警告

//     /** 
//      * (Final) TODO BEGIN
//      * 
//      * 1. Find the section struct which contains the faulting address `addr`.
//      * 2. Check section flags to determine page fault type.
//      * 3. Handle the page fault accordingly.
//      * 4. Return to user code or kill the process.
//      */

//     /* (Final) TODO END */
//     // TODO: 实现 page fault 处理，暂时杀死进程
//     return -1;
// }

// void copy_sections(ListNode *from_head, ListNode *to_head)
// {
//     /* (Final) TODO BEGIN */

//     /* (Final) TODO END */
// }

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

void init_sections(ListNode *section_head)
{
    init_list_node(section_head);
}

void free_sections(struct pgdir *pd)
{
    if (pd == NULL)
        return;
    
    // 检查链表是否已初始化且非空
    if (pd->section_head.next == NULL || 
        pd->section_head.next == &pd->section_head)
        return;

    ListNode *node = pd->section_head.next;
    while (node != &pd->section_head) {
        struct section *sec = container_of(node, struct section, stnode);
        ListNode *next = node->next;

        // 释放 section 占用的所有物理页
        for (u64 va = sec->begin; va < sec->end; va += PAGE_SIZE) {
            PTEntriesPtr pte = get_pte(pd, va, false);
            if (pte && (*pte & PTE_VALID)) {
                void *pa = (void *)P2K(PTE_ADDRESS(*pte));
                kfree_page(pa);
                *pte = 0;
            }
        }

        // 如果是 file-backed section，关闭文件
        if (sec->fp) {
            file_close(sec->fp);
        }

        // 释放 section 结构体
        kfree(sec);
        node = next;
    }

    // 重新初始化链表头
    init_list_node(&pd->section_head);
}

u64 sbrk(i64 size)
{
    // TODO: 实现堆管理
    return (u64)-1;
}

int pgfault_handler(u64 iss)
{
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 addr = arch_get_far();

    // 查找包含故障地址的 section
    ListNode *node = pd->section_head.next;
    while (node != &pd->section_head) {
        struct section *sec = container_of(node, struct section, stnode);
        if (addr >= sec->begin && addr < sec->end) {
            // 找到对应的 section，处理缺页
            u64 va = PAGE_BASE(addr);
            void *pa = kalloc_page();
            if (pa == NULL)
                return -1;
            memset(pa, 0, PAGE_SIZE);

            // 如果是 file-backed section，从文件读取数据
            if (sec->fp && sec->length > 0) {
                // 计算需要读取的文件偏移和长度
                u64 sec_file_start = sec->begin;
                u64 sec_file_end = sec->begin + sec->length;
                
                if (va < sec_file_end) {
                    u64 read_start = (va > sec_file_start) ? va : sec_file_start;
                    u64 read_end = (va + PAGE_SIZE < sec_file_end) ? 
                                   (va + PAGE_SIZE) : sec_file_end;
                    if (read_start < read_end) {
                        u64 file_off = sec->offset + (read_start - sec->begin);
                        u64 pa_off = read_start - va;
                        // TODO: 从文件读取数据到 pa + pa_off
                        printk("%lld, %lld\n", file_off, pa_off);
                    }
                }
            }

            vmmap(pd, va, pa, PTE_USER_DATA);
            return 0;
        }
        node = node->next;
    }

    // 未找到对应 section，非法访问
    return -1;
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    init_list_node(to_head);
    
    if (from_head->next == from_head)
        return;

    ListNode *node = from_head->next;
    int id=0;
    while (node != from_head) {
        printk("Copying sections, id= %d\n",id++);
        struct section *src = container_of(node, struct section, stnode);

        // section 链表一旦被破坏，container_of 会把任意 ListNode 当成 section。
        // 这里做最基本的健全性检查，避免把垃圾 fp 传给 file_dup()。
        // 经验上，坏链表常见特征：begin/end 不成对、end 落在内核高地址。
        if (src->begin >= src->end || (src->end >> 63) != 0) {
            printk("copy_sections: corrupted section list detected\n");
            printk("  from_head=%llx node=%llx src=%llx\n", (u64)from_head,
                   (u64)node, (u64)src);
            printk("  src->flags=%llx begin=%llx end=%llx fp=%llx\n",
                   src->flags, src->begin, src->end, (u64)src->fp);
            PANIC();
        }

        struct section *dst = kalloc(sizeof(struct section));
        if (dst == NULL)
            return;

        dst->begin = src->begin;
        dst->end = src->end;
        dst->flags = src->flags;
        dst->fp = src->fp;
        dst->offset = src->offset;
        dst->length = src->length;
        if (id-1==3){
            printk("!!!\n");
        }
        id +=0;
        if (dst->fp) {
            printk("Copying duplicating file reference, id=%d\n",id-1);
            file_dup(dst->fp);  // 增加文件引用计数
        }

        _insert_into_list(to_head, &dst->stnode);
        node = node->next;
    }
    printk("Copying sections done.\n");
}