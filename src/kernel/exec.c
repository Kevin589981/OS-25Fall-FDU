#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

#define USTACK_TOP 0x800000000000
#define USTACK_SIZE 0x800000
#define RESERVE_SIZE 0x40

extern int fdalloc(struct file *f);

static int load_elf_header(Inode *ip, Elf64_Ehdr *elf)
{
    if (inodes.read(ip, (u8 *)elf, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) return -1;
    return 0;
}

static int check_elf_header(Elf64_Ehdr *elf)
{
    u8 *e_ident = elf->e_ident;
    if (strncmp((const char *)e_ident, ELFMAG, SELFMAG) != 0 || e_ident[EI_CLASS] != ELFCLASS64) return -1;
    return 0;
}

static Section *create_section(u64 begin, u64 end, int flags)
{
    Section *sec = (Section *)kalloc(sizeof(Section));
    memset(sec, 0, sizeof(Section));
    init_section(sec);
    sec->begin = begin;
    sec->end = end;
    sec->flags = flags;
    return sec;
}

static int load_and_map_segments(Inode *ip, Elf64_Ehdr *elf, struct pgdir *pgdir)
{
    Elf64_Phdr phdr;
    u64 section_stack_top = 0;

    for (u64 i = 0, off = elf->e_phoff; i < elf->e_phnum; i++, off += sizeof(Elf64_Phdr))
    {
        if (inodes.read(ip, (u8 *)&phdr, off, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)) return -1;
        if (phdr.p_type != PT_LOAD) continue;

        section_stack_top = MAX(section_stack_top, phdr.p_vaddr + phdr.p_memsz);
        Section *sec = create_section(phdr.p_vaddr, phdr.p_vaddr + phdr.p_filesz, ST_TEXT);

        if (phdr.p_flags == (PF_R | PF_X))
        {
            // text segment (RX)
            sec->fp = file_alloc();
            sec->fp->ip = inodes.share(ip);
            sec->fp->readable = true;
            sec->fp->writable = false;
            sec->fp->ref = 1;
            sec->fp->off = 0;
            sec->fp->type = FD_INODE;
            sec->length = phdr.p_filesz;
            sec->offset = phdr.p_offset;
        }
        else if (phdr.p_flags == (PF_R | PF_W))
        {
            // data segment (RW)
            sec->flags = ST_DATA;
            sec->end = sec->begin + phdr.p_memsz;
            u64 filesz = phdr.p_filesz, offset = phdr.p_offset, va = phdr.p_vaddr;
            while (filesz)
            {
                u64 cursize = MIN(filesz, (u64)PAGE_SIZE - VA_OFFSET(va));
                void *pg = kalloc_page();
                memset(pg, 0, PAGE_SIZE);
                vmmap(pgdir, PAGE_BASE(va), pg, PTE_USER_DATA | PTE_RW);
                if (inodes.read(ip, (u8 *)(pg + VA_OFFSET(va)), offset, cursize) != cursize) return -1;
                filesz -= cursize;
                offset += cursize;
                va += cursize;
            }

            // BSS (If located in the current page, it's already zeroed)
            if (PAGE_BASE(va) + PAGE_SIZE < phdr.p_vaddr + phdr.p_memsz)
            {
                va = PAGE_BASE(va) + PAGE_SIZE;
                filesz = phdr.p_vaddr + phdr.p_memsz - va;
                while (filesz > 0)
                {
                    u64 cursize = MIN((u64)PAGE_SIZE, filesz);
                    vmmap(pgdir, PAGE_BASE(va), get_zero_page(), PTE_USER_DATA | PTE_RO);
                    filesz -= cursize;
                    va += cursize;
                }
            }
        }
        else
        {
            printk("Invalid program header flags\n");
            return -1;
        }
        _insert_into_list(&pgdir->section_head, &sec->stnode);
    }

    return 0;
}

static int setup_ustack(struct pgdir *pgdir, char *const argv[], char *const envp[])
{
    u64 stack_top = USTACK_TOP - RESERVE_SIZE;
    Section *st_ustack = create_section(stack_top - USTACK_SIZE, stack_top, ST_USTACK);
    _insert_into_list(&pgdir->section_head, &st_ustack->stnode);

    u64 argc = 0, arg_len = 0, envc = 0, env_len = 0, zero = 0;
    if (envp)
    {
        while (envp[envc])
        {
            env_len += strlen(envp[envc]) + 1;
            envc++;
        }
    }
    if (argv)
    {
        while (argv[argc])
        {
            arg_len += strlen(argv[argc]) + 1;
            argc++;
        }
    }

    u64 str_total_len = env_len + arg_len;
    u64 str_start = stack_top - str_total_len;
    u64 ptr_tot = (2 + argc + envc + 1) * 8;
    u64 argc_start = (str_start - ptr_tot) & (~0xf);
    if (argc_start < stack_top - USTACK_SIZE) PANIC();

    u64 argv_start = argc_start + 8;
    u64 sp = argc_start;
    copyout(pgdir, (void *)sp, &argc, 8);

    for (u64 i = 0; i < argc; i++)
    {
        usize len = strlen(argv[i]) + 1;
        copyout(pgdir, (void *)str_start, argv[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8);

    argv_start += 8;
    for (u64 i = 0; i < envc; i++)
    {
        usize len = strlen(envp[i]) + 1;
        copyout(pgdir, (void *)str_start, envp[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8);
    thisproc()->ucontext->sp = sp;
    return 0;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */
    Elf64_Ehdr elf;
    Inode *ip;
    struct pgdir *pgdir, *oldpgdir;
    Proc *p = thisproc();
    OpContext ctx;
    bcache.begin_op(&ctx);

    /* Step 1: Load data from the file stored in `path`. */
    if ((ip = namei(path, &ctx)) == 0)
    {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    pgdir = (struct pgdir *)kalloc(sizeof(struct pgdir));
    init_pgdir(pgdir);

    /* Step 2: Load program headers and the program itself. */
    if (load_elf_header(ip, &elf) < 0 || check_elf_header(&elf) < 0 || load_and_map_segments(ip, &elf, pgdir) < 0)
    {
        free_pgdir(pgdir);
        inodes.unlock(ip);
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    /* Step 3: Allocate and initialize user stack. */
    Section *heap = create_section(PAGE_BASE(elf.e_entry) + PAGE_SIZE, PAGE_BASE(elf.e_entry) + PAGE_SIZE, ST_HEAP);
    _insert_into_list(&pgdir->section_head, &heap->stnode);
    if (setup_ustack(pgdir, argv, envp) < 0)
    {
        free_pgdir(pgdir);
        return -1;
    }

    /* Step 4: Switch to the new page table. */
    oldpgdir = &p->pgdir;
    free_pgdir(oldpgdir);
    p->ucontext->elr = elf.e_entry;
    memcpy(&p->pgdir, pgdir, sizeof(struct pgdir));
    init_list_node(&p->pgdir.section_head);
    _insert_into_list(&pgdir->section_head, &p->pgdir.section_head);
    _detach_from_list(&pgdir->section_head);
    kfree(pgdir);
    attach_pgdir(&p->pgdir);
    return 0;
    /* (Final) TODO END */
}
