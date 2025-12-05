#pragma once
#include <common/defines.h>

NO_RETURN void idle_entry();

typedef struct{
    u8 boot_flag;
    u8 start_chsp[3];
    u8 type;
    u8 end_chs[3];
    u32 start_lba;
    u32 num_sectors;
}__attribute__((packed)) PartitionEntry;

typedef struct{
    u8 bootstrap[0x1BE];
    PartitionEntry partition[4];
    u16 signature;
}__attribute__((packed)) MBR;