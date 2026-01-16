#pragma once

#include <aarch64/mmu.h>
#include <kernel/proc.h>

#define SECTION_TYPE u16
#define ST_FILE 1
#define ST_SWAP (1 << 1)
#define ST_RO (1 << 2)
#define ST_HEAP (1 << 3)
#define ST_TEXT (ST_FILE | ST_RO)
#define ST_DATA ST_FILE
#define ST_BSS ST_FILE
#define ST_USTACK (1 << 4)

typedef struct section {
    u64 flags;
    u64 begin;
    u64 end;
    ListNode stnode;

    /* The following fields are for the file-backed sections. */

    File *fp;
    u64 offset; // Offset in file
    u64 length; // Length of mapped content in file
} Section;

void init_section(Section *);
void init_sections(ListNode *section_head);

Section *lookup_section(struct pgdir *pd, u64 va);
int pgfault_handler(u64 iss);

void free_section_pages(struct pgdir *pd, Section *sec);
void free_section(struct pgdir *pd, Section *sec);
void free_sections(struct pgdir *pd);
void copy_sections(ListNode *from_head, ListNode *to_head);
u64 sbrk(i64 size);
