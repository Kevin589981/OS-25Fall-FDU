#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>
#include <kernel/paging.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

void init_syscall()
{
    for (u64 *p = (u64 *)&early_init; p < (u64 *)&rest_init; p++)
        ((void (*)()) * p)();
}

void *syscall_table[NR_SYSCALL] = {
    [0 ... NR_SYSCALL - 1] = NULL,
    [SYS_myreport] = (void *)syscall_myreport,
};

void syscall_entry(UserContext *context)
{
#ifdef lab3_debug1
    printk("syscall.c:18 \n");
#endif
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    // be sure to check the range of id. if id >= NR_SYSCALL, panic.
    u64 syscall_id=context->x[8];
    if (syscall_id>=NR_SYSCALL){
        PANIC();
    }
    void *func=syscall_table[syscall_id];
    if (func==NULL){
        PANIC();
    }
    typedef u64 (*func_call)(u64,u64,u64,u64,u64,u64);
    func_call call=(func_call)func;
    u64 x0=call(context->x[0],
                context->x[1],
                context->x[2],
                context->x[3],
                context->x[4],
                context->x[5]);
    context->x[0]=x0;
}

/** 
 * Check if the virtual address [start,start+size) is READABLE by the current
 * user process.
 */
bool user_readable(const void *start, usize size) {
    /* (Final) TODO BEGIN */
    struct Proc *p=thisproc();
    if (p==NULL||size==0)return false;
    struct pgdir *pgdir = &p->pgdir;
    u64 va = (u64)start;
    u64 end_va = va + size;
    u64 current_va=PAGE_BASE(va);


    for (;current_va<end_va;current_va+=PAGE_SIZE){
        
        PTEntriesPtr pte = get_pte(pgdir, current_va, false);
        if (pte == NULL) {
            return false;
        }
        
        if ((*pte & PTE_VALID) == 0 || (*pte & PTE_USER) == 0) {
            return false; 
        }
    }
    return true;
    /* (Final) TODO END */
}


/**
 * Check if the virtual address [start,start+size) is READABLE & WRITEABLE by
 * the current user process.
 */
bool user_writeable(const void *start, usize size) {
    /* (Final) TODO Begin */
    Proc *p = thisproc();
    if (p == NULL)
        return false;
    if (size == 0)
        return true;
    struct pgdir *pgdir = &p->pgdir;
    u64 va = (u64)start;
    u64 end_va = va + size;
    u64 current_va = PAGE_BASE(va);
    for (; current_va < end_va; current_va += PAGE_SIZE) {
        PTEntriesPtr pte = get_pte(pgdir, current_va, false);
        if (pte == NULL) {
            return false;
        }
        if ((*pte & PTE_VALID) == 0 || (*pte & PTE_USER) == 0) {
            return false; 
        }
        if ((*pte & PTE_RO) != 0) {
            return false;
        }
    }
    return TRUE;
    /* (Final) TODO End */
}

/** 
 * Get the length of a string including tailing '\0' in the memory space of
 * current user process return 0 if the length exceeds maxlen or the string is
 * not readable by the current user process.
 */
usize user_strlen(const char *str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}