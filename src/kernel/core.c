#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <common/buf.h>
#include <kernel/core.h>
#include <driver/virtio.h>
volatile bool panic_flag;
extern int virtio_blk_rw(Buf *b);
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
    init_filesystem();

    printk("Hello world! (Core %lld)\n", cpuid());
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