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
    
    // printk("\n=== PAGE FAULT ===\n");
    // printk("pid=%d fault_addr=%llx iss=%llx\n", 
    //        p->pid, (unsigned long long)fault_addr, (unsigned long long)iss);
    
    acquire_spinlock(&pd->lock);
    
    // 调试：打印所有 section
    // int sec_count = 0;
    // printk("Sections:\n");
    // _for_in_list(node, &pd->section_head)
    // {
    //     if (node == &pd->section_head) continue;
    //     Section *s = container_of(node, Section, stnode);
    //     printk("  [%d] [%llx, %llx) flags=%llx fp=%p\n", 
    //            sec_count++, (unsigned long long)s->begin, 
    //            (unsigned long long)s->end, s->flags, s->fp);
    // }
    // printk("Total sections: %d\n", sec_count);
    Section *fault_sec = lookup_section(pd, fault_addr);
    
    if (!fault_sec) {
        // 非法访问，没有对应的 section
        release_spinlock(&pd->lock);
        // printk("pgfault: no section for addr %llx\n", (unsigned long long)fault_addr);
        PANIC();
        return -1; // 返回 -1 会导致进程被杀死
    }

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
        // 使用 if-else 而不是 switch，以支持组合标志位
        if (fault_sec->flags & ST_HEAP || fault_sec->flags & ST_USTACK)
        {
            void *pg = kalloc_page();
            vmmap(pd, fault_addr, pg, PTE_USER_DATA | PTE_RW);
        }
        else if (fault_sec->flags == ST_TEXT)
        {
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
        }
         else if (fault_sec->flags & ST_FILE)
        {
            // 处理 mmap 的文件映射（按需加载）
            // printk("ST_FILE: fp=%p, length=%llu, offset=%llu\n", 
            //        fault_sec->fp, (unsigned long long)fault_sec->length, 
            //        (unsigned long long)fault_sec->offset);
            if (fault_sec->fp && fault_sec->length > 0) {
                void *pg = kalloc_page();
                memset(pg, 0, PAGE_SIZE); // 清零
                
                // 计算页内偏移
                u64 page_base = fault_addr & ~(PAGE_SIZE - 1);
                u64 offset_in_section = page_base - fault_sec->begin;
                u64 file_offset = fault_sec->offset + offset_in_section;
                
                // printk("ST_FILE: page_base=%llx, offset_in_section=%llu, file_offset=%llu\n",
                //        (unsigned long long)page_base, (unsigned long long)offset_in_section,
                //        (unsigned long long)file_offset);
                
                u64 remaining = fault_sec->length > offset_in_section ? 
                                fault_sec->length - offset_in_section : 0;
                usize bytes_to_read = (usize)MIN((u64)PAGE_SIZE, remaining);
                // printk("ST_FILE: remaining=%llu, bytes_to_read=%llu\n",
                //        (unsigned long long)remaining, (unsigned long long)bytes_to_read);
                if (bytes_to_read > 0) {
                    // 直接使用 inodes.read 指定偏移量，避免使用 file.off
                    Inode *ip = fault_sec->fp->ip;
                    release_spinlock(&pd->lock);
                    inodes.lock(ip);
                    isize read_bytes = inodes.read(ip, (u8 *)pg, file_offset, bytes_to_read);
                    inodes.unlock(ip);
                    acquire_spinlock(&pd->lock);
                    // printk("read_bytes=%lld, pg[0]=%c\n", 
                    //        (long long)read_bytes, ((char*)pg)[0]);
                    if (read_bytes < 0) {
                        kfree_page(pg);
                        exit(-1);
                    }
                }
                
                // 映射页表（按共享/权限设置）
                bool mmap_write = (fault_sec->flags & ST_MMAP_WRITE) != 0;
                bool mmap_shared = (fault_sec->flags & ST_SHARED) != 0;
                u64 perm = PTE_RO;
                if (!mmap_write) {
                    perm = PTE_RO;
                } else if (mmap_shared) {
                    perm = PTE_RW;
                } else {
                    // MAP_PRIVATE 可写：先映射为只读以触发 COW
                    perm = PTE_RO;
                }
                vmmap(pd, fault_addr, pg, PTE_USER_DATA | perm);
            } else {
                // printk("ST_FILE: anonymous path taken\n");
                // 匿名映射，直接分配零页
                void *pg = kalloc_page();
                memset(pg, 0, PAGE_SIZE);
                vmmap(pd, fault_addr, pg, PTE_USER_DATA | PTE_RW);
            }
        }
        else
        {
            printk("The section type is unknown: %llx\n", (unsigned long long)fault_sec->flags);
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
        // Handle permission fault (COW or shared write-enable)
        {
            ASSERT(fault_sec->flags == ST_DATA || fault_sec->flags == ST_USTACK || 
                   fault_sec->flags == ST_HEAP || fault_sec->flags == ST_FILE ||
                   (fault_sec->flags & ST_FILE));
            PTEntriesPtr pte = get_pte(pd, fault_addr, false);
            if ((fault_sec->flags & ST_MMAP) && !(fault_sec->flags & ST_MMAP_WRITE)) {
                // mmap 区域不可写
                release_spinlock(&pd->lock);
                exit(-1);
            }
            if ((fault_sec->flags & ST_MMAP) && (fault_sec->flags & ST_SHARED)) {
                // MAP_SHARED：不做复制，直接允许写
                vmmap(pd, fault_addr, (void *)P2K(PTE_ADDRESS(*pte)), PTE_USER_DATA | PTE_RW);
            } else {
                // COW
                void *pg = kalloc_page();
                memcpy(pg, (void *)P2K(PTE_ADDRESS(*pte)), PAGE_SIZE);
                kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
                vmmap(pd, fault_addr, pg, PTE_USER_DATA | PTE_RW);
            }
        }
        break;
    default:
        PANIC();
    }
    release_spinlock(&pd->lock);
    arch_tlbi_vmalle1is();
    // printk("pgfault handled.\n");
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
        
        // 初始化新 section
        init_section(to_sec);
        
        // 复制基本字段
        to_sec->begin = from_sec->begin;
        to_sec->end = from_sec->end;
        to_sec->flags = from_sec->flags;
        to_sec->offset = from_sec->offset;
        to_sec->length = from_sec->length;
        
        // 如果有文件指针，增加引用计数
        if (from_sec->fp) {
            to_sec->fp = file_dup(from_sec->fp);
        }
        
        _insert_into_list(to_head, &to_sec->stnode);
    }
    /* (Final) TODO END */
}
