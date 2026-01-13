#define PRINT_BLOCK_DEVICE_LOG 1

#include <driver/virtio.h>
#include <fs/block_device.h>
#include <common/string.h>
#ifdef PRINT_BLOCK_DEVICE_LOG
    #include <kernel/printk.h>
#else
    #define printk(...) do { } while(0)
#endif

// 测试示例（可选）
void test_print_log() {
    // 当PRINT_FILE_SYSTEM_LOG定义时，会正常打印；未定义时，此行无任何效果
    printk("File system log: %s, %d\n", "test", 123);
}
/**
    @brief a simple implementation of reading a block from SD card.

    @param[in] block_no the block number to read
    @param[out] buffer the buffer to store the data
 */
static void sd_read(usize block_no, u8 *buffer) {
    printk("sd_read: reading block %lld\n", (u64)block_no);
    Buf b;
    b.block_no = (u32)block_no;
    b.flags = 0;
    printk("sd_read: calling virtio_blk_rw\n");
    virtio_blk_rw(&b);
    printk("sd_read: virtio_blk_rw returned\n");
    memcpy(buffer, b.data, BLOCK_SIZE);
    printk("sd_read: completed\n");
}

/**
    @brief a simple implementation of writing a block to SD card.

    @param[in] block_no the block number to write
    @param[in] buffer the buffer to store the data
 */
static void sd_write(usize block_no, u8 *buffer) {
    Buf b;
    b.block_no = (u32)block_no;
    b.flags = B_DIRTY | B_VALID;
    memcpy(b.data, buffer, BLOCK_SIZE);
    virtio_blk_rw(&b);
}

/**
    @brief the in-memory copy of the super block.

    We may need to read the super block multiple times, so keep a copy of it in
    memory.

    @note the super block, in our lab, is always read-only, so we don't need to
    write it back.
 */
static u8 sblock_data[BLOCK_SIZE];

BlockDevice block_device;

void init_block_device() {
    block_device.read = sd_read;
    block_device.write = sd_write;
    
    // 读取 SuperBlock
    // 文件系统在分区 2，起始扇区是 133120（见 MBR）
    // SuperBlock 在分区内的块 1，所以绝对位置是 133120 + 1 = 133121
    usize superblock_sector = 133120 + 1;
    printk("init_block_device: reading superblock from sector %llu\n", (u64)superblock_sector);
    sd_read(superblock_sector, sblock_data);
    printk("init_block_device: superblock loaded\n");
    
    #ifdef PRINT_BLOCK_DEVICE_LOG
    // 调试：打印 SuperBlock 信息
    const SuperBlock *sb = (const SuperBlock *)sblock_data;
    printk("SuperBlock info: num_blocks=%u, num_inodes=%u, inode_start=%u\n",
           sb->num_blocks, sb->num_inodes, sb->inode_start);
    #endif
}

const SuperBlock *get_super_block() { return (const SuperBlock *)sblock_data; }