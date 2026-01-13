#include <fs/block_device.h>
#include <fs/cache.h>
#include <fs/defines.h>
#include <fs/fs.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <common/defines.h>
// 条件编译：根据PRINT_FILE_SYSTEM_LOG是否定义来控制printk的行为
#ifdef PRINT_FILE_SYSTEM_LOG
    // 定义了该宏，正常包含printk头文件，使用原生printk
    #include <kernel/printk.h>
#else
    // 未定义该宏，将printk定义为空操作，不打印任何内容
    // do{...}while(0) 是为了保证宏在任何语法场景下都能正常工作（比如if/else后不加{}的情况）
    // __VA_ARGS__ 用于接收printk的可变参数（如格式化字符串+参数）
    #define printk(...) do { } while(0)
#endif

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
