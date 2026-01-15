#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <common/buf.h>
#include <kernel/core.h>
#include <driver/virtio.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/pt.h>
#include <kernel/paging.h>
#include <common/string.h>
#include <common/list.h>
#include <fs/block_device.h>

volatile bool panic_flag;
extern int virtio_blk_rw(Buf *b);
extern void set_parent_to_this(Proc *proc);
extern void trap_return();
NO_RETURN void idle_entry()
{
    set_cpu_on();
    if (cpuid() == 0) {
        printk("CPU 0: entering idle loop\n");
    }
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap
        {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void kernel_entry()
{

        /* LAB 4 TODO 3 BEGIN */
    Buf mbr_buf;
    mbr_buf.block_no = 0;  // MBR在LBA 0
    mbr_buf.flags = 0;      // 读操作

    printk("Reading MBR...\n");

    // 调用同步读写函数读取MBR
    if (virtio_blk_rw(&mbr_buf) != 0) {
        printk("Failed to read MBR!");
        PANIC();
    }

    // 数据已经在 mbr_buf.data 中了
    MBR *mbr = (MBR *)mbr_buf.data;

    // 检查签名
    if (mbr->signature != 0xAA55) {
        printk("Invalid MBR signature!");
        PANIC();
    }

    // 获取第二分区信息
    PartitionEntry *part2 = &mbr->partition[1];
    u32 part2_start_lba = part2->start_lba;
    u32 part2_num_sectors = part2->num_sectors;

    printk("Partition 2: Start LBA = %u, Size = %u sectors\n", 
       part2_start_lba, part2_num_sectors);
    
    extern usize FS_PART_LBA_BASE;
    FS_PART_LBA_BASE = part2_start_lba;
    // printk("kernel_entry: started on CPU %lld\n", cpuid());
    // extern void init_filesystem();
    // printk("kernel_entry: calling init_filesystem\n");
    init_filesystem();
    // printk("kernel_entry: init_filesystem done\n");
    
    // printk("Hello world! (Core %lld)\n", cpuid());
    // proc_test();
    // vm_test();
    // user_proc_test();
    // printk("test proc_test() and user_proc_test() in lab5 passed.\n");
    // io_test();


    // block_device.read(part2_start_lba+1, (u8 *)get_super_block());

    /* LAB 4 TODO 3 END */

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */
    
//     extern char icode[], eicode[];
    
//     // 创建第一个用户进程
//     Proc *p = create_proc();
//     if (p == NULL) {
//         PANIC();
//     }
//     printk("Creating first user process (PID %d)...\n", p->pid);
//     Proc *parent = thisproc();
//     printk("Parent PID is %d\n", parent->pid);
//     // 设置父进程为root进程
//     set_parent_to_this(p);
//     init_list_node(&p->pgdir.section_head);

//     // 创建代码段section
//     u64 icode_size = (u64)eicode - (u64)icode;
//     printk("icode_size is %lld\n",icode_size);
//     u64 icode_pages = (icode_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
//     struct section *text_sec = kalloc(sizeof(struct section));
//     if (text_sec == NULL) {
//         PANIC();
//     }
//     printk("allocating section\n");
//     text_sec->begin = 0x0;
//     text_sec->end = icode_pages * PAGE_SIZE;
//     text_sec->flags = ST_HEAP;  // 匿名段
//     text_sec->fp = NULL;
//     text_sec->offset = 0;
//     text_sec->length = 0;
//     printk("insert into list\n");
//     _insert_into_list(&p->pgdir.section_head, &text_sec->stnode);
//     printk("entering loop.\n");
//     // 分配物理页并复制icode内容
//     for (u64 i = 0; i < icode_pages; i++) {
//         void *page = kalloc_page();
//         if (page == NULL) {
//             PANIC();
//         }
//         memset(page, 0, PAGE_SIZE);
        
//         u64 copy_size = MIN((u64)PAGE_SIZE, icode_size - i * PAGE_SIZE);
//         printk("memmove %lld\n",i);
//         memmove(page, icode + i * PAGE_SIZE, copy_size);
//         printk("vmmap %lld\n",i);
//         vmmap(&p->pgdir, i * PAGE_SIZE, page, PTE_USER_DATA);
//     }
//     printk("allocating stack\n");
//     // 创建用户栈
//     #define INIT_STACK_SIZE (8 * PAGE_SIZE)
//     #define INIT_STACK_TOP 0x0000004000000000UL
    
//     struct section *stack_sec = kalloc(sizeof(struct section));
//     if (stack_sec == NULL) {
//         PANIC();
//     }
    
//     stack_sec->begin = INIT_STACK_TOP - INIT_STACK_SIZE;
//     stack_sec->end = INIT_STACK_TOP;
//     stack_sec->flags = ST_HEAP;
//     stack_sec->fp = NULL;
//     stack_sec->offset = 0;
//     stack_sec->length = 0;
//     // init_list_node(&p->pgdir.section_head); 
//     _insert_into_list(&p->pgdir.section_head, &stack_sec->stnode);
    
//     // 设置进程上下文
//     p->ucontext->elr = 0;  // 从icode开始执行
//     p->ucontext->sp = INIT_STACK_TOP;  // 栈顶
//     p->ucontext->spsr = 0;  // 用户模式
//     // CHECK_IRQ();
//     // 设置返回上下文
//     p->kcontext->lr = (u64)&trap_return;
    
//     // 激活进程
//     p->state = UNUSED;
    
    
//     // printk("First user process created, entering scheduler...\n");
    
//     // auto p = create_proc();
//     // extern char icode[], eicode[];
//     // printk("core: icode:0x%llu, eicode:0x%llu, kend = 0x%llu\n", (u64)icode, (u64)eicode, 0x400000 + (u64)eicode - (u64)icode);
//     // if (!add_section(&p->pgdir.section_head, 0x400000, 0x400000 + (u64)eicode - (u64)icode, ST_TEXT, 0, 0, "text"))
//     //     PANIC();
//     // for (u64 q = (u64)icode; q < (u64)eicode; q += PAGE_SIZE) {
//     //     // void* page = kalloc_page();
//     //     // memcpy(page, (void*)q, PAGE_SIZE);
//     //     // vmmap(&p->pgdir, 0x400000 + q - (u64)icode, page, PTE_USER_DATA | PTE_RO);
//     //     *get_pte(&p->pgdir, 0x400000 + q - (u64)icode, true) = K2P(q) | PTE_USER_DATA;
//     // }
//     // p->ucontext->elr = 0x400000;
//     // p->ucontext->spsr = 0;
//     p->cwd = inodes.share(inodes.root);

//     start_proc(p, trap_return, 0);

//     /* (Final) TODO END */

//     // 启动调度器，永不返回
//     // while (1) {
//     //     yield();
//     // }
//     while (1) {
//         auto pid = wait(NULL);
//         (void)pid;
//     }
// }

extern char icode[], eicode[];
    
    // 1. 创建进程
    Proc *p = create_proc();
    if (p == NULL) PANIC();

    // 2. 初始化进程元数据
    // 注意：不要手动设为 UNUSED，create_proc 通常会处理状态
    // 如果 start_proc 不负责状态切换，请确保它是 RUNNABLE
    init_list_node(&p->pgdir.section_head);
    p->cwd = inodes.share(inodes.root);

    // 3. 映射代码段 (Text Section)
    // 建议使用 0x400000 防止与 NULL 指针冲突
    u64 icode_base = 0x40000000; 
    u64 icode_size = (u64)eicode - (u64)icode;
    u64 icode_pages = (icode_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (u64 i = 0; i < icode_pages; i++) {
        void *page = kalloc_page();
        if (page == NULL) PANIC();
        memset(page, 0, PAGE_SIZE);
        
        u64 copy_size = MIN((u64)PAGE_SIZE, icode_size - i * PAGE_SIZE);
        memmove(page, icode + i * PAGE_SIZE, copy_size);
        
        // 映射到指定的 icode_base
        vmmap(&p->pgdir, icode_base + i * PAGE_SIZE, page, PTE_USER_DATA);
    }

    // 4. 映射用户栈 (Stack Section) - 必须物理映射！
    #define INIT_STACK_TOP 0x80000000UL
    #define STACK_PAGES 4 
    
    for (u64 i = 0; i < STACK_PAGES; i++) {
        void *stack_page = kalloc_page();
        if (stack_page == NULL) PANIC();
        memset(stack_page, 0, PAGE_SIZE);
        
        // 栈从高地址向低地址增长，所以映射栈顶下方的页面
        u64 stack_va = INIT_STACK_TOP - (i + 1) * PAGE_SIZE;
        vmmap(&p->pgdir, stack_va, stack_page, PTE_USER_DATA);
    }

    // 5. 设置上下文
    p->ucontext->elr = icode_base;      // 指向代码起始地址
    p->ucontext->sp = INIT_STACK_TOP;   // 指向栈顶
    p->ucontext->spsr = 0;              // 切换到 EL0 (用户态)
    
    // 设置内核返回地址
    p->kcontext->lr = (u64)&trap_return;

    // 6. 启动进程
    // 确保 start_proc 会将 p 加入调度队列并将状态设为 RUNNABLE
    start_proc(p, trap_return, 0);

    // 7. 进入内核主循环
    while (1) {
        
        int pid=wait(NULL); // 等待回收子进程，防止内核退出
        (void)pid;//防止unused警告
    }
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    while (1) {
        bool all_offline = true;
        for (int i = 0; i < NCPU; i++) {
            if (cpus[i].online) {
                all_offline = false;
                break;
            }
        }
        if (all_offline)
            break;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}