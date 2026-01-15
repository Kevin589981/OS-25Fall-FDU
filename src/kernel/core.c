#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <common/buf.h>
#include <common/string.h>
#include <kernel/core.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <driver/virtio.h>

#define INIT_ELR 0x400000
#define INIT_SP 0x80000000
#define INIT_SPSR 0x0
#define INIT_SIZE ((u64)eicode - (u64)icode)

u32 LBA;
volatile bool panic_flag;
extern int virtio_blk_rw(Buf *b);
extern char icode[], eicode[];
void trap_return();
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

NO_RETURN void kernel_entry()
{
    printk("Hello world! (Core %lld)\n", cpuid());

    /* LAB 4 TODO 3 BEGIN */
    Buf b;
    b.flags = 0;
    b.block_no = (u32)0x0;
    virtio_blk_rw(&b);
    u8 *data = b.data;
    LBA = *(int *)(data + 0x1CE + 0x8);
    /* LAB 4 TODO 3 END */
    
    init_filesystem();
    // proc_test();
    // vm_test();
    // user_proc_test();
    // printk("test proc_test() and user_proc_test() in lab5 passed.\n");
    // io_test();

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
    /* LAB 4 TODO 3 END */

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */

    Proc *init_proc = create_proc();
    init_proc->ucontext->x[0] = 0;
    init_proc->ucontext->elr = INIT_ELR;
    init_proc->ucontext->spsr = INIT_SPSR;
    init_proc->ucontext->sp = INIT_SP;

    Section *sec = (Section *)kalloc(sizeof(Section));
    init_section(sec);
    if (!sec) PANIC();
    sec->flags = ST_TEXT;
    sec->begin = INIT_ELR;
    sec->end = INIT_ELR + INIT_SIZE;

    _insert_into_list(&init_proc->pgdir.section_head, &sec->stnode);

    void *p = kalloc_page();
    if (!p) PANIC();
    memset(p, 0, PAGE_SIZE);
    memcpy(p, (void*)icode, PAGE_SIZE);
    vmmap(&init_proc->pgdir, INIT_ELR, p, PTE_USER_DATA | PTE_RO);
    start_proc(init_proc, trap_return, 0);
    printk("Create process %d\n", init_proc->pid);

    while (1)
    {
        int a;
        a = wait(&a);
    }

    PANIC();
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