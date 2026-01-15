#include <common/spinlock.h>
#include <driver/virtio.h>
#include <driver/interrupt.h>
#include <common/buf.h>
#include <common/sem.h>
#include <common/string.h>
#include <kernel/mem.h>
#define PRINT_VIRTIO_BLK_LOG 1
#ifdef PRINT_VIRTIO_BLK_LOG
    // 定义了该宏，正常包含printk头文件，使用原生printk
    #include <kernel/printk.h>
#else
    // 未定义该宏，将printk定义为空操作，不打印任何内容
    // do{...}while(0) 是为了保证宏在任何语法场景下都能正常工作（比如if/else后不加{}的情况）
    // __VA_ARGS__ 用于接收printk的可变参数（如格式化字符串+参数）
    #define printk(...) do { } while(0)
#endif


#define VIRTIO_MAGIC 0x74726976

struct disk {
    SpinLock lk;
    struct virtq virtq;
} disk;

static void desc_init(struct virtq *virtq)
{
    for (int i = 0; i < NQUEUE; i++) {
        if (i != NQUEUE - 1) {
            virtq->desc[i].flags = VIRTQ_DESC_F_NEXT;
            virtq->desc[i].next = i + 1;
        }
    }
}

static int alloc_desc(struct virtq *virtq)
{
    if (virtq->nfree == 0) {
        PANIC();
    }

    u16 d = virtq->free_head;
    if (virtq->desc[d].flags & VIRTQ_DESC_F_NEXT)
        virtq->free_head = virtq->desc[d].next;

    virtq->nfree--;

    return d;
}

static void free_desc(struct virtq *virtq, u16 n)
{
    u16 head = n;
    int empty = 0;

    if (virtq->nfree == 0)
        empty = 1;

    while (virtq->nfree++, (virtq->desc[n].flags & VIRTQ_DESC_F_NEXT)) {
        n = virtq->desc[n].next;
    }

    virtq->desc[n].flags = VIRTQ_DESC_F_NEXT;
    if (!empty)
        virtq->desc[n].next = virtq->free_head;
    virtq->free_head = head;
}
static inline u64 __get_daif(void) {
    u64 daif;
    asm volatile("mrs %0, daif" : "=r"(daif));
    return daif;
}
int virtio_blk_rw(Buf *b)
{
    enum diskop op = DREAD;
    // printk("63\n");
    // 脏数据，需要写入
    if (b->flags & B_DIRTY)
        op = DWRITE;
    // 初始化b的信号量
    init_sem(&b->sem, 0);
    // 获得要读写的块
    u64 sector = b->block_no;
    // hdr是操作的元数据
    struct virtio_blk_req_hdr hdr;

    if (op == DREAD)
        hdr.type = VIRTIO_BLK_T_IN;
    else if (op == DWRITE)
        hdr.type = VIRTIO_BLK_T_OUT;
    else
        return -1;
    hdr.reserved = 0;
    hdr.sector = sector;
    // printk("acquire spinlock disk.lk\n");
    acquire_spinlock(&disk.lk);
    // printk("84\n");
    // 3个描述符，依次表示指令是什么，指示数据的目标内存地址，返回结果成功还是失败
    int d0 = alloc_desc(&disk.virtq);
    if (d0 < 0)
        return -1;
    disk.virtq.desc[d0].addr = (u64)V2P(&hdr);
    disk.virtq.desc[d0].len = sizeof(hdr);
    disk.virtq.desc[d0].flags = VIRTQ_DESC_F_NEXT;

    int d1 = alloc_desc(&disk.virtq);
    if (d1 < 0)
        return -1;
    disk.virtq.desc[d0].next = d1;
    // 读出缓冲区的物理地址
    disk.virtq.desc[d1].addr = (u64)V2P(b->data);
    disk.virtq.desc[d1].len = 512;
    disk.virtq.desc[d1].flags = VIRTQ_DESC_F_NEXT;
    if (op == DREAD)
        disk.virtq.desc[d1].flags |= VIRTQ_DESC_F_WRITE;

    int d2 = alloc_desc(&disk.virtq);
    if (d2 < 0)
        return -1;
    disk.virtq.desc[d1].next = d2;
    disk.virtq.desc[d2].addr = (u64)V2P(&disk.virtq.info[d0].status);
    disk.virtq.desc[d2].len = sizeof(disk.virtq.info[d0].status);
    disk.virtq.desc[d2].flags = VIRTQ_DESC_F_WRITE;
    disk.virtq.desc[d2].next = 0;

    disk.virtq.avail->ring[disk.virtq.avail->idx % NQUEUE] = d0;
    disk.virtq.avail->idx++;

    disk.virtq.info[d0].buf = b->data;

    arch_fence();
    REG(VIRTIO_REG_QUEUE_NOTIFY) = 0;
    // if (sector==133185){
    //     printk("virtio_blk_rw: submitted request for sector 133185\n");
    // }
    arch_fence();
    // printk("virtio_blk_rw: request submitted, waiting for completion\n");

    // printk("buf is %llx(136)\n",(u64)b);
    /* LAB 4 TODO 1 BEGIN */
    release_spinlock(&disk.lk);
    // printk("virtio_bk.c:wait sem\n");
    unalertable_wait_sem(&b->sem);
    //     printk("virtio_blk_rw: waiting, polling INTERRUPT_STATUS...\n");
    // for (int i = 0; i < 100000; i++) {
    //     u32 int_status = REG(VIRTIO_REG_INTERRUPT_STATUS);
    //     if (int_status != 0) {
    //         printk("virtio_blk_rw: INTERRUPT_STATUS = 0x%x after %d loops\n", 
    //                int_status, i);
    //         printk("virtio_blk_rw: used->idx = %u, last_used = %u\n",
    //                disk.virtq.used->idx, disk.virtq.last_used_idx);
    //         break;
    //     }
    //     if (i % 10000 == 0) {
    //         printk("virtio_blk_rw: still waiting... loop %d\n", i);
    //     }
    // }
    // ========== 完整诊断 ==========
// printk("===== VIRTIO DIAG before wait =====\n");
// printk("  Current CPU: %lld\n", cpuid());


// // 检查 IRQ 是否被禁用 (I 位是 bit 7)
// #define IRQ_DISABLED() ((__get_daif() >> 7) & 1)
// printk("  DAIF: 0x%llx (IRQ %s)\n", 
//        __get_daif(), IRQ_DISABLED() ? "DISABLED" : "enabled");
// printk("  avail->idx: %u\n", disk.virtq.avail->idx);
// printk("  avail->flags: 0x%x\n", disk.virtq.avail->flags);
// printk("  used->idx: %u\n", disk.virtq.used->idx);
// printk("  used->flags: 0x%x\n", disk.virtq.used->flags);
// printk("  last_used_idx: %u\n", disk.virtq.last_used_idx);
// printk("  INTERRUPT_STATUS: 0x%x\n", REG(VIRTIO_REG_INTERRUPT_STATUS));
// printk("  VIRTIO_STATUS: 0x%x\n", REG(VIRTIO_REG_STATUS));
// printk("=====================================\n");

// // 等待一小段时间后再检查
// for (volatile int i = 0; i < 1000000; i++);

// printk("===== VIRTIO DIAG after delay =====\n");
// printk("  used->idx: %u\n", disk.virtq.used->idx);
// printk("  INTERRUPT_STATUS: 0x%x\n", REG(VIRTIO_REG_INTERRUPT_STATUS));
// printk("=====================================\n");
// // ================================
// asm volatile("msr daifclr, #2" ::: "memory");
    // asm volatile("msr daifclr, #2" ::: "memory");
    // printk("virtio_bk.c:wait sem\n");
    // if (wait_sem(&b->sem)){};
    printk("virtio_bk.c:sem get\n");
    acquire_spinlock(&disk.lk);
    // _lock_sem(&b->sem);
    // while (!disk.virtq.info[d0].done) {
    //     release_spinlock(&disk.lk);
    //     if(!_wait_sem(&b->sem, true)){
    //         return -1;
    //     }
    //     //_lock_sem(&b->sem);
    //     acquire_spinlock(&disk.lk);
    // }
    /* LAB 4 TODO 1 END */

    disk.virtq.info[d0].done = 0;
    free_desc(&disk.virtq, d0);
    release_spinlock(&disk.lk);
    return 0;
}
// 中断，触发唤醒对应的进程
static void virtio_blk_intr()
{
    // printk("virtio_blk_intr: interrupt received\n");
    acquire_spinlock(&disk.lk);

    u32 intr_status = REG(VIRTIO_REG_INTERRUPT_STATUS);
    REG(VIRTIO_REG_INTERRUPT_ACK) = intr_status & 0x3;
    // printk("virtio_blk_intr: intr_status=%x\n", intr_status);

    int d0;
    while (disk.virtq.last_used_idx != disk.virtq.used->idx) {
        // printk("virtio_blk_intr: processing used ring entry\n");
        d0 = disk.virtq.used->ring[disk.virtq.last_used_idx % NQUEUE].id;
        if (disk.virtq.info[d0].status != 0) {
            // printk("virtio_blk_intr: ERROR status=%d\n", disk.virtq.info[d0].status);
            PANIC();
        }

        /* LAB 4 TODO 2 BEGIN */
        u8 *data_ptr = disk.virtq.info[d0].buf;
        if (data_ptr) {
            Buf *b = container_of(data_ptr, Buf, data[0]);
            // printk("virtio_blk_intr: posting semaphore\n");
            // printk("buf is %llx(184)\n",(u64)b);
            post_all_sem(&b->sem);
        }

        
        /* LAB 4 TODO 2 END */

        disk.virtq.info[d0].buf = NULL;
        disk.virtq.last_used_idx++;
    }

    // printk("virtio_blk_intr: done\n");
    release_spinlock(&disk.lk);
}

static int virtq_init(struct virtq *vq)
{
    memset(vq, 0, sizeof(*vq));

    vq->desc = kalloc_page();
    vq->avail = kalloc_page();
    vq->used = kalloc_page();

    memset(vq->desc, 0, 4096);
    memset(vq->avail, 0, 4096);
    memset(vq->used, 0, 4096);

    if (!vq->desc || !vq->avail || !vq->used) {
        PANIC();
    }
    vq->nfree = NQUEUE;
    desc_init(vq);

    return 0;
}

void virtio_init()
{
    if (REG(VIRTIO_REG_MAGICVALUE) != VIRTIO_MAGIC ||
        REG(VIRTIO_REG_VERSION) != 2 || REG(VIRTIO_REG_DEVICE_ID) != 2) {
        printk("[Virtio]: Device not found.");
        PANIC();
    }

    /* Reset the device. */
    REG(VIRTIO_REG_STATUS) = 0;

    u32 status = 0;

    /* Set the ACKNOWLEDGE status bit: the guest OS has noticed the device. */
    status |= DEV_STATUS_ACKNOWLEDGE;
    REG(VIRTIO_REG_STATUS) = status;

    /* Set the DRIVER status bit: the guest OS knows how to drive the device. */
    status |= DEV_STATUS_DRIVER;
    REG(VIRTIO_REG_STATUS) = status;

    /* Read device feature bits, and write the subset of feature bits understood by the OS and driver to the device. */
    REG(VIRTIO_REG_DEVICE_FEATURES_SEL) = 0;
    REG(VIRTIO_REG_DRIVER_FEATURES_SEL) = 0;

    u32 features = REG(VIRTIO_REG_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_SEG_MAX);
    features &= ~(1 << VIRTIO_BLK_F_GEOMETRY);
    features &= ~(1 << VIRTIO_BLK_F_RO);
    features &= ~(1 << VIRTIO_BLK_F_BLK_SIZE);
    features &= ~(1 << VIRTIO_BLK_F_FLUSH);
    features &= ~(1 << VIRTIO_BLK_F_TOPOLOGY);
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    REG(VIRTIO_REG_DRIVER_FEATURES) = features;

    status |= DEV_STATUS_FEATURES_OK;
    REG(VIRTIO_REG_STATUS) = status;

    arch_fence();
    status = REG(VIRTIO_REG_STATUS);
    arch_fence();
    if (!(status & DEV_STATUS_FEATURES_OK)) {
        PANIC();
    }

    virtq_init(&disk.virtq);

    int qmax = REG(VIRTIO_REG_QUEUE_NUM_MAX);
    if (qmax < NQUEUE) {
        printk("[Virtio]: Too many queues.");
        PANIC();
    }

    REG(VIRTIO_REG_QUEUE_SEL) = 0;
    REG(VIRTIO_REG_QUEUE_NUM) = NQUEUE;

    u64 phy_desc = V2P(disk.virtq.desc);
    REG(VIRTIO_REG_QUEUE_DESC_LOW) = LO(phy_desc);
    REG(VIRTIO_REG_QUEUE_DESC_HIGH) = HI(phy_desc);

    u64 phy_avail = V2P(disk.virtq.avail);
    REG(VIRTIO_REG_QUEUE_DRIVER_LOW) = LO(phy_avail);
    REG(VIRTIO_REG_QUEUE_DRIVER_HIGH) = HI(phy_avail);
    u64 phy_used = V2P(disk.virtq.used);

    REG(VIRTIO_REG_QUEUE_DEVICE_LOW) = LO(phy_used);
    REG(VIRTIO_REG_QUEUE_DEVICE_HIGH) = HI(phy_used);

    arch_fence();

    REG(VIRTIO_REG_QUEUE_READY) = 1;
    status |= DEV_STATUS_DRIVER_OK;
    REG(VIRTIO_REG_STATUS) = status;

    arch_fence();

    set_interrupt_handler(VIRTIO_BLK_IRQ, virtio_blk_intr);
    printk("interrupt handler set.\n");
    init_spinlock(&disk.lk);
}
