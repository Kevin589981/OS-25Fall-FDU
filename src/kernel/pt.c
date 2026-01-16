#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>
#include <common/defines.h>
#include <kernel/printk.h>
#include <kernel/paging.h>

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    // TODO:
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // u64 page_base=PAGE_BASE(va);
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    PTEntriesPtr l0_table = pgdir->pt;
    if (!l0_table){
        if (!alloc){
            return NULL;
        }
        l0_table=kalloc_page();
        if (!l0_table){
            return NULL;
        }
        pgdir->pt=l0_table;
    }

    // 得到l1页表的地址
    u64 idx0=VA_PART0(va);
    PTEntry *pte0=&l0_table[idx0];
    PTEntriesPtr l1_table;
    if ((*pte0&PTE_TABLE)==PTE_TABLE){
        l1_table=(PTEntriesPtr)P2K(PTE_ADDRESS(*pte0));
    }else{
        if (!alloc){
            return NULL;
        }
        l1_table=kalloc_page();
        if (l1_table==NULL){
            return NULL;
        }
        *pte0=K2P(l1_table)|PTE_TABLE;
    }

    // 得到l2页表的位置
    u64 idx1=VA_PART1(va);
    PTEntry *pte1=&l1_table[idx1];
    PTEntriesPtr l2_table;
    if ((*pte1&PTE_TABLE)==PTE_TABLE){
        l2_table=(PTEntriesPtr)P2K(PTE_ADDRESS(*pte1));
    }else{
        if (!alloc){
            return NULL;
        }
        l2_table=kalloc_page();
        if (l2_table==NULL){
            return NULL;
        }
        *pte1=K2P(l2_table)|PTE_TABLE;
    }

    // 得到l3页表的位置
    u64 idx2=VA_PART2(va);
    PTEntry *pte2=&l2_table[idx2];
    PTEntriesPtr l3_table;
    if ((*pte2&PTE_TABLE)==PTE_TABLE){
        l3_table=(PTEntriesPtr)P2K(PTE_ADDRESS(*pte2));
    }else{
        if (!alloc){
            return NULL;
        }
        l3_table=kalloc_page();
        if (l3_table==NULL){
            return NULL;
        }
        *pte2=K2P(l3_table)|PTE_TABLE;
    }

    u64 idx3=VA_PART3(va);
    return &l3_table[idx3];
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = NULL;
    init_spinlock(&pgdir->lock);
    init_list_node(&pgdir->section_head);
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    if (pgdir->pt==NULL)return;
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    PTEntriesPtr l0_table = pgdir->pt;
    for (u64 i=0;i<N_PTE_PER_TABLE;i++){
        if ((l0_table[i]&PTE_TABLE)==PTE_TABLE){
            PTEntriesPtr l1_table=(PTEntriesPtr)P2K(PTE_ADDRESS(l0_table[i]));
            for (u64 j=0;j<N_PTE_PER_TABLE;j++){
                if ((l1_table[j]&PTE_TABLE)==PTE_TABLE){
                    PTEntriesPtr l2_table=(PTEntriesPtr)P2K(PTE_ADDRESS(l1_table[j]));
                    for (u64 k=0;k<N_PTE_PER_TABLE;k++){
                        if ((l2_table[k]&PTE_TABLE)==PTE_TABLE){
                            PTEntriesPtr l3_table=(PTEntriesPtr)P2K(PTE_ADDRESS(l2_table[k]));
                            kfree_page(l3_table);
                        }
                    }
                    kfree_page(l2_table);
                }
            }
            kfree_page(l1_table);
        }
    }
    kfree_page(l0_table);
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

/**
 * Map virtual address 'va' to the physical address represented by kernel
 * address 'ka' in page directory 'pd', 'flags' is the flags for the page
 * table entry.
 */
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags)
{
    /* (Final) TODO BEGIN */
    PTEntriesPtr pte = get_pte(pd, va, true);
    if (pte == NULL) {
        PANIC();
    }
    *pte = K2P(ka) | flags | PTE_VALID;
    arch_tlbi_vmalle1is();
    /* (Final) TODO END */
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir *pd, void *va, void *p, usize len)
{
    /* (Final) TODO BEGIN */
    u64 va_start = (u64)va;
    u8 *src = (u8 *)p;
    
    while (len > 0) {
        u64 va_page = PAGE_BASE(va_start);
        u64 offset = va_start - va_page;
        u64 n = MIN(PAGE_SIZE - offset, len);
        
        // 获取或分配页表项
        PTEntriesPtr pte = get_pte(pd, va_start, true);
        if (pte == NULL) {
            return -1;
        }
        
        // 如果页面不存在，分配新页
        void *pa;
        if ((*pte & PTE_VALID) == 0) {
            void *page = kalloc_page();
            if (page == NULL) {
                return -1;
            }
            memset(page, 0, PAGE_SIZE);
            *pte = K2P(page) | PTE_USER_DATA | PTE_VALID;
            pa = page;
        } else {
            pa = (void *)P2K(PTE_ADDRESS(*pte));
        }
        
        memmove((u8 *)pa + offset, src, n);
        
        len -= n;
        src += n;
        va_start += n;
    }
    
    return 0;
    /* (Final) TODO END */
}