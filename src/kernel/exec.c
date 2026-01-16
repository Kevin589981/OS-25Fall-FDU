/**
 * @file exec.c
 * @brief Implementation of the execve system call using a hybrid loading strategy.
 * 
 * This file implements execve with a demand-paging approach for executable (text)
 * segments to accelerate process startup and conserve memory. Data segments are
 * eagerly loaded for simplicity and immediate availability.
 */

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
#define MAXARGS 32

extern int fdalloc(struct file *f);

/**
 * @brief Loads ELF segments into a new address space using a hybrid strategy.
 * 
 * - RX (Text) Segments: Configured for demand paging. A section is created with
 *   file-backing information, but no physical pages are allocated upfront. Pages
 *   will be loaded by the page fault handler on first access.
 * - RW (Data) Segments: Eagerly loaded. Physical pages are allocated, data is
 *   read from the file, BSS is zeroed, and pages are mapped immediately.
 * 
 * @param ip The inode of the executable file.
 * @param ehdr A pointer to the ELF header.
 * @param new_pgdir The new page directory to be populated.
 * @return 0 on success, -1 on failure.
 */
static int load_elf_segments(Inode *ip, Elf64_Ehdr *ehdr, struct pgdir *new_pgdir)
{
    Elf64_Phdr phdr;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        u64 ph_off = ehdr->e_phoff + i * sizeof(phdr);
        if (inodes.read(ip, (u8 *)&phdr, ph_off, sizeof(phdr)) != sizeof(phdr))
            return -1;

        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0)
            continue;

        if (phdr.p_memsz < phdr.p_filesz || phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr)
            return -1;

        struct section *sec = kalloc(sizeof(struct section));
        if (sec == NULL) return -1;
        memset(sec, 0, sizeof(struct section));
        init_section(sec);


        sec->begin = phdr.p_vaddr;
        sec->end = phdr.p_vaddr + phdr.p_filesz;
        sec->flags = ST_TEXT;

        // 精确匹配段标志
        if (phdr.p_flags == (PF_R | PF_X)) { 
            // Text Segment (RX) -> Demand Paging
            sec->fp = file_alloc();
            if (sec->fp == NULL) {
                kfree(sec);
                return -1;
            }
            sec->fp->ip = inodes.share(ip);
            sec->fp->readable = true;
            sec->fp->writable = false;
            sec->fp->ref = 1;
            sec->fp->off = 0;
            sec->fp->type = FD_INODE;
            sec->offset = phdr.p_offset;
            sec->length = phdr.p_filesz;
        
        } else if (phdr.p_flags == (PF_R | PF_W)) { 
            // Data Segment (RW) -> Eager Loading
            sec->flags = ST_DATA;
            sec->end = sec->begin + phdr.p_memsz;  // 使用 memsz（包含 BSS）
            
            u64 filesz = phdr.p_filesz, offset = phdr.p_offset, va = phdr.p_vaddr;
            
            // 读取文件数据部分
            while (filesz) {
                u64 cursize = MIN(filesz, (u64)PAGE_SIZE - VA_OFFSET(va));
                void *pg = kalloc_page();
                if (pg == NULL) {
                    kfree(sec);
                    return -1;
                }
                memset(pg, 0, PAGE_SIZE);
                vmmap(new_pgdir, PAGE_BASE(va), pg, PTE_USER_DATA | PTE_RW);
                if (inodes.read(ip, (u8 *)(pg + VA_OFFSET(va)), offset, cursize) != cursize) {
                    kfree(sec);
                    return -1;
                }
                filesz -= cursize;
                offset += cursize;
                va += cursize;
            }

            // BSS 部分：如果当前页之后还有更多内存需求
            if (PAGE_BASE(va) + PAGE_SIZE < phdr.p_vaddr + phdr.p_memsz) {
                va = PAGE_BASE(va) + PAGE_SIZE;
                filesz = phdr.p_vaddr + phdr.p_memsz - va;
                while (filesz > 0) {
                    u64 cursize = MIN((u64)PAGE_SIZE, filesz);
                    vmmap(new_pgdir, PAGE_BASE(va), get_zero_page(), PTE_USER_DATA | PTE_RO);
                    filesz -= cursize;
                    va += cursize;
                }
            }
        } else {
            printk("Invalid program header flags\n");
            kfree(sec);
            return -1;
        }

        _insert_into_list(&new_pgdir->section_head, &sec->stnode);
    }
    return 0;
}


static int setup_user_stack(struct pgdir *pgdir, char *const argv[], char *const envp[])
{
    u64 stack_top = USTACK_TOP - RESERVE_SIZE;
    
    // 创建栈 section
    struct section *st_ustack = kalloc(sizeof(struct section));
    if (st_ustack == NULL) return -1;
    memset(st_ustack, 0, sizeof(struct section));
    init_section(st_ustack);
    st_ustack->begin = stack_top - USTACK_SIZE;
    st_ustack->end = stack_top;
    st_ustack->flags = ST_USTACK;
    _insert_into_list(&pgdir->section_head, &st_ustack->stnode);

    // 不预先映射栈页面，由 copyout 触发 page fault 按需分配

    u64 argc = 0, arg_len = 0, envc = 0, env_len = 0, zero = 0;
    
    // 统计环境变量
    if (envp) {
        while (envp[envc]) {
            env_len += strlen(envp[envc]) + 1;
            envc++;
        }
    }
    
    // 统计参数
    if (argv) {
        while (argv[argc]) {
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
    
    // 写入 argc
    copyout(pgdir, (void *)sp, &argc, 8);

    // 写入 argv 字符串和指针
    for (u64 i = 0; i < argc; i++) {
        usize len = strlen(argv[i]) + 1;
        copyout(pgdir, (void *)str_start, argv[i], len);
        copyout(pgdir, (void *)argv_start, &str_start, 8);
        str_start += len;
        argv_start += 8;
    }
    copyout(pgdir, (void *)argv_start, &zero, 8);

    // 写入 envp 字符串和指针
    argv_start += 8;
    for (u64 i = 0; i < envc; i++) {
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

/**
 * @brief Replaces the current process image with a new one.
 */
int execve(const char *path, char *const argv[], char *const envp[])
{   
    Elf64_Ehdr ehdr;
    Inode *ip = NULL;
    Proc *p = thisproc();
    struct pgdir *new_pgdir;
    OpContext ctx;

    bcache.begin_op(&ctx);

    // Step 1: 打开可执行文件
    if ((ip = namei(path, &ctx)) == NULL) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);
    
    // Step 2: 分配新的页目录
    new_pgdir = (struct pgdir *)kalloc(sizeof(struct pgdir));
    if (new_pgdir == NULL) {
        inodes.unlock(ip);
        bcache.end_op(&ctx);
        return -1;
    }
    init_pgdir(new_pgdir);

    // Step 3: 读取并验证 ELF 头
    if (inodes.read(ip, (u8 *)&ehdr, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
        goto fail;
    }
    
    u8 *e_ident = ehdr.e_ident;
    if (strncmp((const char *)e_ident, ELFMAG, SELFMAG) != 0 || e_ident[EI_CLASS] != ELFCLASS64) {
        goto fail;
    }

    // Step 4: 加载 ELF 段
    if (load_elf_segments(ip, &ehdr, new_pgdir) < 0) {
        goto fail;
    }
    
    inodes.unlock(ip);
    bcache.end_op(&ctx);

    // Step 5: 创建 heap 段
    struct section *heap = kalloc(sizeof(struct section));
    if (heap == NULL) {
        free_pgdir(new_pgdir);
        kfree(new_pgdir);
        return -1;
    }
    memset(heap, 0, sizeof(struct section));
    init_section(heap);
    heap->begin = PAGE_BASE(ehdr.e_entry) + PAGE_SIZE;
    heap->end = PAGE_BASE(ehdr.e_entry) + PAGE_SIZE;  // 初始为空
    heap->flags = ST_HEAP;
    _insert_into_list(&new_pgdir->section_head, &heap->stnode);

    // Step 6: 设置用户栈
    if (setup_user_stack(new_pgdir, argv, envp) < 0) {
        free_pgdir(new_pgdir);
        kfree(new_pgdir);
        return -1;
    }

    // Step 7: 切换地址空间
    struct pgdir *oldpgdir = &p->pgdir;
    free_pgdir(oldpgdir);
    
    // printk("[EXEC] PID=%d execve '%s', preserving file descriptors\n", p->pid, path);
    // 打印当前打开的文件描述符
    // for (int i = 0; i < NOFILE; i++) {
    //     if (p->oftable.files[i]) {
    //         printk("[EXEC]   fd[%d]: type=%d, readable=%d, writable=%d\n",
    //                i, p->oftable.files[i]->type, 
    //                p->oftable.files[i]->readable,
    //                p->oftable.files[i]->writable);
    //     }
    // }
    
    p->ucontext->elr = ehdr.e_entry;
    
    memcpy(&p->pgdir, new_pgdir, sizeof(struct pgdir));
    init_list_node(&p->pgdir.section_head);
    _insert_into_list(&new_pgdir->section_head, &p->pgdir.section_head);
    _detach_from_list(&new_pgdir->section_head);
    kfree(new_pgdir);
    
    attach_pgdir(&p->pgdir);
    
    // printk("Exec successfully\n");
    return 0;

fail:
    free_pgdir(new_pgdir);
    kfree(new_pgdir);
    inodes.unlock(ip);
    bcache.end_op(&ctx);
    return -1;
}

