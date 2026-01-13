#include <fs/block_device.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <common/defines.h>
#include <kernel/printk.h>

void init_filesystem() {
    printk("init_filesystem: calling init_block_device\n");
    init_block_device();
    printk("init_filesystem: init_block_device done\n");

    printk("init_filesystem: calling get_super_block\n");
    const SuperBlock* sblock = get_super_block();
    printk("init_filesystem: get_super_block done\n");
    
    printk("init_filesystem: calling init_bcache\n");
    init_bcache(sblock, &block_device);
    printk("init_filesystem: init_bcache done\n");
    
    printk("init_filesystem: calling init_inodes\n");
    init_inodes(sblock, &bcache);
    printk("init_filesystem: init_inodes done\n");
    
    printk("init_filesystem: calling init_ftable\n");
    init_ftable();
    printk("init_filesystem: init_ftable done\n");
}
