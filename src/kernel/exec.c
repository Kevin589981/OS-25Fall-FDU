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

extern int fdalloc(struct file *f);
#ifndef MAXARGS
#define MAXARGS 10
#endif
int execve(const char *path, char *const argv[], char *const envp[])
{
    /* (Final) TODO BEGIN */
    printk("execve: path=%s\n", path);
    Elf64_Ehdr elf_hdr;
    Elf64_Phdr prog_hdr;
    Inode *ip=NULL;
    Proc *p=thisproc();
    struct pgdir new_pgdir;
    u64 sp, ustack[MAXARGS+1];
    int argc,i;
    OpContext ctx;
    bcache.begin_op(&ctx);
    if ((ip=namei(path,&ctx))==NULL){
        bcache.end_op(&ctx);
        return -1; //文件不存在
    }
    inodes.lock(ip);
    if (inodes.read(ip,(u8 *)&elf_hdr,0,sizeof(elf_hdr))!=sizeof(elf_hdr)){
        goto fail;
    }
    if (elf_hdr.e_ident[EI_MAG0]!=ELFMAG0 ||
        elf_hdr.e_ident[EI_MAG1]!=ELFMAG1 ||
        elf_hdr.e_ident[EI_MAG2]!=ELFMAG2 ||
        elf_hdr.e_ident[EI_MAG3]!=ELFMAG3 ||
        elf_hdr.e_type!=ET_EXEC ||
        elf_hdr.e_machine!=EM_AARCH64){
        goto fail;
    }

    // 初始化新页目录和section链表
    init_pgdir(&new_pgdir);
    init_sections(&new_pgdir.section_head);
    
    // Step2: 加载所有PT_LOAD段
    for (int i=0;i<elf_hdr.e_phnum;i++){
        if (inodes.read(ip,(u8 *)&prog_hdr,elf_hdr.e_phoff+i*sizeof(prog_hdr),sizeof(prog_hdr))!=sizeof(prog_hdr)){
            free_pgdir(&new_pgdir);
            goto fail;
        }
        if (prog_hdr.p_type!=PT_LOAD){
            continue;
        }
        if (prog_hdr.p_memsz<prog_hdr.p_filesz||
            prog_hdr.p_vaddr+prog_hdr.p_memsz<prog_hdr.p_vaddr){
            free_pgdir(&new_pgdir);
            goto fail;
        }
        
        // 创建section
        struct section *sec = kalloc(sizeof(struct section));
        if (sec == NULL){
            free_sections(&new_pgdir);
            free_pgdir(&new_pgdir);
            goto fail;
        }
        
        // 设置section的范围（页对齐）
        sec->begin = PAGE_BASE(prog_hdr.p_vaddr);
        sec->end = PAGE_BASE((prog_hdr.p_vaddr + prog_hdr.p_memsz + (PAGE_SIZE - 1)));
        
        // 设置section标志
        if (prog_hdr.p_flags & PF_X) {
            // 可执行段（代码段）
            sec->flags = ST_TEXT;  // ST_FILE | ST_RO
        } else {
            // 数据段
            sec->flags = ST_DATA;  // ST_FILE
        }
        
        // 设置文件相关信息
        // TODO: 实际应该创建File结构，但先置为NULL
        sec->fp = NULL; // 暂时置为NULL，待实现file系统
        sec->offset = prog_hdr.p_offset;
        sec->length = prog_hdr.p_filesz;
        
        // 添加到section链表
        _insert_into_list(&new_pgdir.section_head, &sec->stnode);
    }
    
    // Step3: 创建用户栈
    #define USER_STACK_SIZE (8 * PAGE_SIZE)
    #define USER_STACK_TOP 0x0000004000000000UL
    
    struct section *stack_sec = kalloc(sizeof(struct section));
    if (stack_sec == NULL){
        free_sections(&new_pgdir);
        free_pgdir(&new_pgdir);
        goto fail;
    }
    
    stack_sec->begin = USER_STACK_TOP - USER_STACK_SIZE;
    stack_sec->end = USER_STACK_TOP;
    stack_sec->flags = ST_HEAP;  // 匿名段
    stack_sec->fp = NULL;
    stack_sec->offset = 0;
    stack_sec->length = 0;
    
    _insert_into_list(&new_pgdir.section_head, &stack_sec->stnode);
    
    // Step4: 准备参数并压入栈
    sp = USER_STACK_TOP;
    
    // 计算argc
    for (argc = 0; argv && argv[argc]; argc++){
        if (argc >= MAXARGS){
            free_sections(&new_pgdir);
            free_pgdir(&new_pgdir);
            goto fail;
        }
    }
    
    // 计算envc
    int envc;
    for (envc = 0; envp && envp[envc]; envc++){
        if (envc >= MAXARGS){
            free_sections(&new_pgdir);
            free_pgdir(&new_pgdir);
            goto fail;
        }
    }
    
    // 压入envp字符串内容
    u64 uenvp[MAXARGS+1];
    for (i = envc - 1; i >= 0; i--) {
        usize len = strlen(envp[i]) + 1;
        sp -= len;
        sp = round_down(sp, 8);  // 8字节对齐
        if (copyout(&new_pgdir, (void*)sp, envp[i], len) < 0){
            free_sections(&new_pgdir);
            free_pgdir(&new_pgdir);
            goto fail;
        }
        uenvp[i] = sp;
    }
    uenvp[envc] = 0;  // NULL终止符
    
    // 压入argv字符串内容
    for (i = argc - 1; i >= 0; i--) {
        usize len = strlen(argv[i]) + 1;
        sp -= len;
        sp = round_down(sp, 8);  // 8字节对齐
        if (copyout(&new_pgdir, (void*)sp, argv[i], len) < 0){
            free_sections(&new_pgdir);
            free_pgdir(&new_pgdir);
            goto fail;
        }
        ustack[i] = sp;
    }
    ustack[argc] = 0;  // NULL终止符
    
    // 压入envp指针数组
    sp -= (envc + 1) * sizeof(u64);
    sp = round_down(sp, 16);  // 16字节对齐
    if (copyout(&new_pgdir, (void*)sp, uenvp, (envc + 1) * sizeof(u64)) < 0){
        free_sections(&new_pgdir);
        free_pgdir(&new_pgdir);
        goto fail;
    }
    
    // 压入argv指针数组
    sp -= (argc + 1) * sizeof(u64);
    sp = round_down(sp, 16);  // 16字节对齐
    if (copyout(&new_pgdir, (void*)sp, ustack, (argc + 1) * sizeof(u64)) < 0){
        free_sections(&new_pgdir);
        free_pgdir(&new_pgdir);
        goto fail;
    }
    
    // 压入argc
    u64 argc_val = argc;
    sp -= sizeof(u64);
    sp = round_down(sp, 16);  // 栈指针必须16字节对齐
    if (copyout(&new_pgdir, (void*)sp, &argc_val, sizeof(u64)) < 0){
        free_sections(&new_pgdir);
        free_pgdir(&new_pgdir);
        goto fail;
    }
    
    // Step5: 保存旧的页目录并切换到新的
    struct pgdir old_pgdir = p->pgdir;
    p->pgdir = new_pgdir;
    
    // 设置进程上下文
    p->ucontext->elr = elf_hdr.e_entry;  // 入口地址
    p->ucontext->sp = sp;                // 栈指针
    
    // 释放旧的页目录和sections
    free_sections(&old_pgdir);
    free_pgdir(&old_pgdir);
    
    // 关闭所有CLOEXEC文件
    for (int fd = 0; fd < NOFILE; fd++) {
        struct file *f = p->oftable.files[fd];
        if (f) {
            // 注意：这里简化处理，实际可能需要检查O_CLOEXEC标志
            // 但当前实现中可能没有这个标志，所以暂不处理
        }
    }
    
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

fail:
    if (ip){
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
    }
    bcache.end_op(&ctx);
    return -1;
    /* (Final) TODO END */
}
