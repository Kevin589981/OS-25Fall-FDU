#include <aarch64/trap.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>

void trap_global_handler(UserContext *context)
{
    thisproc()->ucontext = context;

    u64 esr = arch_get_esr();
    u64 ec = esr >> ESR_EC_SHIFT;
    u64 iss = esr & ESR_ISS_MASK;
    u64 ir = esr & ESR_IR_MASK;

    (void)iss;

    arch_reset_esr();

    switch (ec) {
    case ESR_EC_UNKNOWN: {
        if (ir)
            PANIC();
        else
            interrupt_global_handler();
    } break;
    case ESR_EC_SVC64: {
        PANIC();
    } break;
    case ESR_EC_IABORT_EL0:
    case ESR_EC_IABORT_EL1:
    case ESR_EC_DABORT_EL0:
    case ESR_EC_DABORT_EL1: {
        u64 far = arch_get_far();
        u64 elr = arch_get_elr(); // 发生错误的指令地址

        // 打印详细的调试报告
        printk("===== KERNEL PANIC: Memory Access Abort =====\n");
        printk("  - Exception Class (EC): 0x%llx (%s)\n", ec, 
               (ec == ESR_EC_IABORT_EL0 || ec == ESR_EC_IABORT_EL1) ? "Instruction Abort" : "Data Abort");
        printk("  - Faulting Virtual Address (FAR): 0x%llx\n", far);
        printk("  - Faulting Instruction Addr (ELR): 0x%llx\n", elr);
        printk("  - Exception Syndrome (ESR): 0x%llx\n", esr);
        printk("  - Current Process PID: %d\n", thisproc()->pid);
        printk("================================================\n");

        printk("Page fault\n");
        PANIC();
    } break;
    default: {
        printk("Unknwon exception %llu\n", ec);
        PANIC();
    }
    }
}

NO_RETURN void trap_error_handler(u64 type)
{
    printk("Unknown trap type %llu\n", type);
    PANIC();
}
