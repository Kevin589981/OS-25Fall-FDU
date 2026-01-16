#include <aarch64/intrinsic.h>
#include <common/buf.h>
#include <common/string.h>
#include <driver/virtio.h>
#include <kernel/cpu.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <kernel/core.h>

#define INIT_ELR 0x400000
#define INIT_SP 0x80000000
#define INIT_SPSR 0x0
#define INIT_SIZE ((u64)eicode - (u64)icode)


extern char icode[], eicode[];
volatile bool panic_flag;
extern int virtio_blk_rw(Buf *b);
extern void set_parent_to_this(Proc *proc);
extern void trap_return();
NO_RETURN void idle_entry()
{
    set_cpu_on();
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

usize FS_PART_LBA_BASE=0;
NO_RETURN void kernel_entry()
{
    printk("Hello world! (Core %lld)\n", cpuid());
    // proc_test();
    // vm_test();
    // user_proc_test();
    // io_test();
    // pgfault_first_test();
    // pgfault_second_test();

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
    
    FS_PART_LBA_BASE = part2_start_lba;
    init_filesystem();
    /**
    * (Final) TODO BEGIN 
    * 
    * Map init.S to user space and trap_return to run icode.
    */

    // 1. 创建进程
    Proc *p = create_proc();
    if (p == NULL) PANIC();

    // 2. 初始化进程元数据和工作目录
    p->cwd = inodes.share(inodes.root);

    // 3. 创建并注册代码段 Section
    Section *sec = (Section *)kalloc(sizeof(Section));
    init_section(sec);
    if (!sec) PANIC();
    sec->flags = ST_TEXT;
    sec->begin = INIT_ELR;
    sec->end = INIT_ELR + INIT_SIZE;
    _insert_into_list(&p->pgdir.section_head, &sec->stnode);

    // 4. 映射代码段 (Text Section) - 修复：使用正确的地址 0x400000
    u64 icode_size = (u64)eicode - (u64)icode;
    u64 icode_pages = (icode_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (u64 i = 0; i < icode_pages; i++) {
        void *page = kalloc_page();
        if (page == NULL) PANIC();
        memset(page, 0, PAGE_SIZE);
        
        u64 copy_size = (icode_size - i * PAGE_SIZE) < PAGE_SIZE ? 
                        (icode_size - i * PAGE_SIZE) : PAGE_SIZE;
        memmove(page, icode + i * PAGE_SIZE, copy_size);
        
        // 映射到正确地址，并设置为只读
        vmmap(&p->pgdir, INIT_ELR + i * PAGE_SIZE, page, PTE_USER_DATA | PTE_RO);
    }

    // 5. 映射用户栈 (Stack Section)
    #define STACK_PAGES 4 
    
    for (u64 i = 0; i < STACK_PAGES; i++) {
        void *stack_page = kalloc_page();
        if (stack_page == NULL) PANIC();
        memset(stack_page, 0, PAGE_SIZE);
        
        // 栈从高地址向低地址增长，所以映射栈顶下方的页面
        u64 stack_va = INIT_SP - (i + 1) * PAGE_SIZE;
        vmmap(&p->pgdir, stack_va, stack_page, PTE_USER_DATA);
    }

    // 6. 设置用户上下文
    p->ucontext->x[0] = 0;              // 参数寄存器
    p->ucontext->elr = INIT_ELR;        // 指向代码起始地址
    p->ucontext->sp = INIT_SP;          // 指向栈顶
    p->ucontext->spsr = INIT_SPSR;      // 切换到 EL0 (用户态)

    // 7. 启动进程（start_proc 会设置 kcontext->lr）
    start_proc(p, trap_return, 0);
    printk("Create process %d\n", p->pid);

    // 8. 进入内核主循环
    while (1) {
        int pid = wait(NULL);
        (void)pid;  // 防止 unused 警告
    }

    /* (Final) TODO END */
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}