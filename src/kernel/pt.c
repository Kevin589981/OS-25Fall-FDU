#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

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
    // Return Kernel Address.
}

void init_pgdir(struct pgdir *pgdir)
{
    pgdir->pt = NULL;
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
