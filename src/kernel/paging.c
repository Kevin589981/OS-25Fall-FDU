#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>

#define FAULT_STATUS_CODE_MASK 0x3f

#define ADDRESS_SIZE_FAULT_0 0b000000
#define ADDRESS_SIZE_FAULT_1 0b000001
#define ADDRESS_SIZE_FAULT_2 0b000010
#define ADDRESS_SIZE_FAULT_3 0b000011

#define TRANSLATION_FAULT_0 0b000100
#define TRANSLATION_FAULT_1 0b000101
#define TRANSLATION_FAULT_2 0b000110
#define TRANSLATION_FAULT_3 0b000111

#define ACCESS_FLAG_FAULT_0 0b001000
#define ACCESS_FLAG_FAULT_1 0b001001
#define ACCESS_FLAG_FAULT_2 0b001010
#define ACCESS_FLAG_FAULT_3 0b001011

#define PERMISSION_FAULT_0 0b001100
#define PERMISSION_FAULT_1 0b001101
#define PERMISSION_FAULT_2 0b001110
#define PERMISSION_FAULT_3 0b001111

void init_section(Section *sec)
{
    memset(sec, 0, sizeof(Section));
    init_list_node(&sec->stnode);
}

void init_sections(ListNode *section_head)
{
    /* (Final) TODO BEGIN */
    init_list_node(section_head);
    /* (Final) TODO END */
}


void free_sections(struct pgdir *pd)
{
    if (pd == NULL)
        return;
    
    acquire_spinlock(&pd->lock);
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
    release_spinlock(&pd->lock);
}

u64 sbrk(i64 size)
{
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */

    ASSERT(size % PAGE_SIZE == 0);
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    Section *heap_sec;

    acquire_spinlock(&pd->lock);
    ASSERT((heap_sec = lookup_section(pd, ST_HEAP)));

    if (size == 0)
    {
        release_spinlock(&pd->lock);
        return heap_sec->end;
    }

    u64 prev_heap_end = heap_sec->end;
    heap_sec->end += size;

    if (size > 0) ASSERT(heap_sec->end > prev_heap_end);
    else
    {
        ASSERT(heap_sec->end < prev_heap_end);

        for (u64 i = heap_sec->end; i < prev_heap_end; i += PAGE_SIZE)
        {
            PTEntriesPtr pte = get_pte(pd, i, false);
            if (pte && (*pte & PTE_VALID))
            {
                kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
                *pte = 0;
            }
        }
    }

    release_spinlock(&pd->lock);
    return prev_heap_end;
    /* (Final) TODO END */
}

Section *lookup_section(struct pgdir *pd, u64 va)
{
    _for_in_list(node, &pd->section_head)
    {
        if (node == &pd->section_head) continue;
        Section *sec = container_of(node, Section, stnode);
        if (va >= sec->begin && va < sec->end) return sec;
    }
    return NULL;
}

int pgfault_handler(u64 iss)
{
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    u64 fault_addr = arch_get_far();
    Section *fault_sec = lookup_section(pd, fault_addr);
    ASSERT(fault_sec);

    u64 fsc = iss & FAULT_STATUS_CODE_MASK;
    switch (fsc)
    {
    case ADDRESS_SIZE_FAULT_0:
    case ADDRESS_SIZE_FAULT_1:
    case ADDRESS_SIZE_FAULT_2:
    case ADDRESS_SIZE_FAULT_3:
        PANIC();
        break;
    case TRANSLATION_FAULT_0:
    case TRANSLATION_FAULT_1:
    case TRANSLATION_FAULT_2:
    case TRANSLATION_FAULT_3:
        // Handle missing PTE
        switch (fault_sec->flags)
        {
        case ST_HEAP:
        case ST_USTACK:
            {
                void *pg = kalloc_page();
                vmmap(pd, fault_addr, pg, PTE_USER_DATA | PTE_RW);
            }
            break;
        case ST_TEXT:
            if (fault_sec->length == 0) exit(-1);
            {
                usize total_bytes = fault_sec->length;
                u64 current_addr = fault_sec->begin;
                fault_sec->fp->off = fault_sec->offset;

                while (total_bytes)
                {
                    usize bytes_to_read = MIN(total_bytes, (u64)PAGE_SIZE - VA_OFFSET(current_addr));
                    PTEntriesPtr pte = get_pte(pd, current_addr, true);
                    if (!(*pte & PTE_VALID))
                    {
                        void *pg = kalloc_page();
                        vmmap(pd, current_addr, pg, PTE_USER_DATA | PTE_RO);
                    }
                    if (file_read(fault_sec->fp, (char *)(P2K(PTE_ADDRESS(*pte)) + VA_OFFSET(current_addr)), bytes_to_read) != (isize)bytes_to_read) PANIC();
                    total_bytes -= bytes_to_read;
                    current_addr += bytes_to_read;
                }
                fault_sec->length = 0;
                file_close(fault_sec->fp);
                fault_sec->fp = NULL;
            }
            break;
        default:
            printk("The section type is unknown.\n");
            PANIC();
        }
        break;
    case ACCESS_FLAG_FAULT_0:
    case ACCESS_FLAG_FAULT_1:
    case ACCESS_FLAG_FAULT_2:
    case ACCESS_FLAG_FAULT_3:
        PANIC();
        break;
    case PERMISSION_FAULT_0:
    case PERMISSION_FAULT_1:
    case PERMISSION_FAULT_2:
    case PERMISSION_FAULT_3:
        // Handle permission fault (COW)
        {
            ASSERT(fault_sec->flags == ST_DATA || fault_sec->flags == ST_USTACK || fault_sec->flags == ST_HEAP);
            PTEntriesPtr pte = get_pte(pd, fault_addr, false);
            void *pg = kalloc_page();
            memcpy(pg, (void *)P2K(PTE_ADDRESS(*pte)), PAGE_SIZE);
            kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
            vmmap(pd, fault_addr, pg, PTE_USER_DATA | PTE_RW);
        }
        break;
    default:
        PANIC();
    }
    release_spinlock(&pd->lock);
    arch_tlbi_vmalle1is();
    return 1;
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */
    _for_in_list(node, from_head)
    {
        if (node == from_head) continue;
        Section *from_sec = container_of(node, Section, stnode);
        Section *to_sec = (Section*)kalloc(sizeof(Section));
        if (!to_sec) PANIC();
        memcpy(to_sec, from_sec, sizeof(Section));
        _insert_into_list(to_head, &to_sec->stnode);
    }
    /* (Final) TODO END */
}
