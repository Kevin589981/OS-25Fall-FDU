#include <aarch64/trap.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <driver/interrupt.h>
#include <kernel/proc.h>
#include <kernel/syscall.h>

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
        if (ir){
        u64 far = arch_get_far();
        u64 elr = arch_get_elr(); 
                    printk("===== KERNEL PANIC: Memory Access Abort =====\n");
                    printk("  - Exception Class (EC): 0x%llx (%s)\n", ec, 
                        (ec == ESR_EC_IABORT_EL0 || ec == ESR_EC_IABORT_EL1) ? "Instruction Abort" : "Data Abort");
                    printk("  - Faulting Virtual Address (FAR): 0x%llx\n", far);
                    printk("  - Faulting Instruction Addr (ELR): 0x%llx\n", elr);
                    printk("  - Exception Syndrome (ESR): 0x%llx\n", esr);
                    printk("  - Current Process PID: %d\n", thisproc()->pid);
                    printk("================================================\n");
                    PANIC();
        }
            
            
        else
            interrupt_global_handler();
    } break;
    case ESR_EC_SVC64: {
        syscall_entry(context);
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

    // TODO: stop killed process while returning to user space
}

NO_RETURN void trap_error_handler(u64 type)
{
    printk("Unknown trap type %llu\n", type);
    PANIC();
}


// // ====================================================================
// // file: kernel/trap.c (修改后)
// // ====================================================================
// #include <aarch64/trap.h>
// #include <aarch64/intrinsic.h>
// #include <kernel/sched.h>
// #include <kernel/printk.h>
// #include <driver/interrupt.h>
// #include <kernel/proc.h>
// #include <kernel/debug.h> // 确保包含了 PANIC 宏的头文件

// // 辅助函数，用于打印一块内存的内容
// static void dump_memory(u64 address, int words) {
//     printk("Dumping memory from 0x%llx:\n", address);
//     u64 *ptr = (u64 *)address;
//     for (int i = 0; i < words; i += 2) {
//         printk("  [0x%llx]: 0x%llx 0x%llx\n",
//                (u64)&ptr[i], ptr[i], ptr[i+1]);
//     }
// }

// // 修改函数签名以接收完整的 TrapFrame
// void trap_global_handler(TrapFrame *frame)
// {
//     // 注意：现在 'frame' 包含了所有寄存器，而不仅仅是 UserContext
//     // 如果需要，并且异常来自用户态，可以设置 thisproc()->ucontext
//     // thisproc()->ucontext = (UserContext*)frame; // 这样做类型不匹配，需要小心

//     u64 esr = arch_get_esr();
//     u64 ec = esr >> ESR_EC_SHIFT;
//     u64 iss = esr & ESR_ISS_MASK;
//     u64 ir = esr & ESR_IR_MASK;

//     (void)iss;

//     arch_reset_esr();

//     // 计算异常发生时的原始 SP (在我们的栈帧创建之前)
//     u64 original_sp = (u64)frame + 272; // 272 是我们在 trap.S 中分配的大小

//     switch (ec) {
//         case ESR_EC_UNKNOWN:
//         if (ir){
//         u64 far = arch_get_far();
//         // ELR 现在从 frame 中获取
//         u64 elr = frame->elr;
        
//         // 打印详细的调试报告 - 这是我们添加的核心内容
//         printk("===== KERNEL PANIC: IR =====\n");
//         printk("  - Exception Class (EC): 0x%llx (%s)\n", ec, 
//                (ec == ESR_EC_IABORT_EL0 || ec == ESR_EC_IABORT_EL1) ? "Instruction Abort" : "Data Abort");
//         printk("  - Faulting Virtual Address (FAR): 0x%llx\n", far);
        
//         printk("\n--- CPU CONTEXT AT CRASH ---\n");
//         printk("  - PC (ELR): 0x%llx   <-- CRASH LOCATION\n", elr);
//         printk("  - LR (x30): 0x%llx   <-- RETURN ADDRESS (CALLER)\n", frame->regs[30]);
//         printk("  - SP:       0x%llx\n", original_sp);
//         printk("  - ESR:      0x%llx\n", esr);
//         if(thisproc()) {
//             printk("  - Current Process PID: %d\n", thisproc()->pid);
//         }

//         printk("\n--- GENERAL PURPOSE REGISTERS ---\n");
//         for (int i = 0; i < 30; i += 2) {
//             printk("  x%d: %llx   x%d: %llx\n", i, frame->regs[i], i + 1, frame->regs[i + 1]);
//         }
//         printk("  x30: %llx (LR)\n", frame->regs[30]);

//         printk("\n--- STACK DUMP (around SP) ---\n");
//         dump_memory(original_sp - 64, 16); // 打印 SP 附近 128 字节

//         printk("================================================\n");
//         PANIC();
//         }
//         else {
//             interrupt_global_handler();

//         }break;
    
//     case ESR_EC_IABORT_EL0:
//     case ESR_EC_IABORT_EL1:
//     case ESR_EC_DABORT_EL0:
//     case ESR_EC_DABORT_EL1: {
//         u64 far = arch_get_far();
//         // ELR 现在从 frame 中获取
//         u64 elr = frame->elr;
        
//         // 打印详细的调试报告 - 这是我们添加的核心内容
//         printk("===== KERNEL PANIC: Memory Access Abort =====\n");
//         printk("  - Exception Class (EC): 0x%llx (%s)\n", ec, 
//                (ec == ESR_EC_IABORT_EL0 || ec == ESR_EC_IABORT_EL1) ? "Instruction Abort" : "Data Abort");
//         printk("  - Faulting Virtual Address (FAR): 0x%llx\n", far);
        
//         printk("\n--- CPU CONTEXT AT CRASH ---\n");
//         printk("  - PC (ELR): 0x%llx   <-- CRASH LOCATION\n", elr);
//         printk("  - LR (x30): 0x%llx   <-- RETURN ADDRESS (CALLER)\n", frame->regs[30]);
//         printk("  - SP:       0x%llx\n", original_sp);
//         printk("  - ESR:      0x%llx\n", esr);
//         if(thisproc()) {
//             printk("  - Current Process PID: %d\n", thisproc()->pid);
//         }

//         printk("\n--- GENERAL PURPOSE REGISTERS ---\n");
//         for (int i = 0; i < 30; i += 2) {
//             printk("  x%d: %llx   x%d: %llx\n", i, frame->regs[i], i + 1, frame->regs[i + 1]);
//         }
//         printk("  x30: %llx (LR)\n", frame->regs[30]);

//         printk("\n--- STACK DUMP (around SP) ---\n");
//         dump_memory(original_sp - 64, 16); // 打印 SP 附近 128 字节

//         printk("================================================\n");
//         PANIC(); // 停止系统
//     } break;

//     case ESR_EC_SVC64: {
//         PANIC(); // 或者在这里处理系统调用
//     } break;

//     default: {
//         printk("Unknown exception %llu\n", ec);
//         PANIC();
//     }
//     }
// }

// NO_RETURN void trap_error_handler(u64 type)
// {
//     printk("Unknown trap type %llu\n", type);
//     PANIC();
// }
