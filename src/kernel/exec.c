// #include <elf.h>
// #include <common/string.h>
// #include <common/defines.h>
// #include <kernel/console.h>
// #include <kernel/proc.h>
// #include <kernel/sched.h>
// #include <kernel/syscall.h>
// #include <kernel/pt.h>
// #include <kernel/mem.h>
// #include <kernel/paging.h>
// #include <kernel/printk.h>
// #include <aarch64/trap.h>
// #include <fs/file.h>
// #include <fs/inode.h>

// extern int fdalloc(struct file *f);
// #ifndef MAXARGS
// #define MAXARGS 10
// #endif
// int execve(const char *path, char *const argv[], char *const envp[])
// {
//     /* (Final) TODO BEGIN */
//     printk("execve: path=%s\n", path);
//     Elf64_Ehdr elf_hdr;
//     Elf64_Phdr prog_hdr;
//     Inode *ip=NULL;
//     Proc *p=thisproc();
//     struct pgdir new_pgdir;
//     u64 sp, ustack[MAXARGS+1];
//     int argc,i;
//     OpContext ctx;
//     bcache.begin_op(&ctx);
//     if ((ip=namei(path,&ctx))==NULL){
//         bcache.end_op(&ctx);
//         return -1; //文件不存在
//     }
//     inodes.lock(ip);
//     if (inodes.read(ip,(u8 *)&elf_hdr,0,sizeof(elf_hdr))!=sizeof(elf_hdr)){
//         goto fail;
//     }
//     if (elf_hdr.e_ident[EI_MAG0]!=ELFMAG0 ||
//         elf_hdr.e_ident[EI_MAG1]!=ELFMAG1 ||
//         elf_hdr.e_ident[EI_MAG2]!=ELFMAG2 ||
//         elf_hdr.e_ident[EI_MAG3]!=ELFMAG3 ||
//         elf_hdr.e_type!=ET_EXEC ||
//         elf_hdr.e_machine!=EM_AARCH64){
//         goto fail;
//     }

//     // 初始化新页目录和section链表
//     init_pgdir(&new_pgdir);
//     init_sections(&new_pgdir.section_head);
    
//     // Step2: 加载所有PT_LOAD段
//     for (int i=0;i<elf_hdr.e_phnum;i++){
//         if (inodes.read(ip,(u8 *)&prog_hdr,elf_hdr.e_phoff+i*sizeof(prog_hdr),sizeof(prog_hdr))!=sizeof(prog_hdr)){
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
//         if (prog_hdr.p_type!=PT_LOAD){
//             printk("CPU: %lld, Skipping non-PT_LOAD segment %d.\n",cpuid(),i);
//             continue;
//         }
//         if (prog_hdr.p_memsz<prog_hdr.p_filesz||
//             prog_hdr.p_vaddr+prog_hdr.p_memsz<prog_hdr.p_vaddr){
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
//         printk("Loading segment %d: vaddr=0x%lx, memsz=0x%lx, filesz=0x%lx, offset=0x%lx\n",
//                i, prog_hdr.p_vaddr, prog_hdr.p_memsz, prog_hdr.p_filesz, prog_hdr.p_offset);
//         // 创建section
//         struct section *sec = kalloc(sizeof(struct section));
//         if (sec == NULL){
//             free_sections(&new_pgdir);
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
        
//         // 设置section的范围（页对齐）
//         sec->begin = PAGE_BASE(prog_hdr.p_vaddr);
//         sec->end = PAGE_BASE((prog_hdr.p_vaddr + prog_hdr.p_memsz + (PAGE_SIZE - 1)));
        
//         // 设置section标志
//         if (prog_hdr.p_flags & PF_X) {
//             // 可执行段（代码段）
//             sec->flags = ST_TEXT;  // ST_FILE | ST_RO
//         } else {
//             // 数据段
//             sec->flags = ST_DATA;  // ST_FILE
//         }
        
//         // 设置文件相关信息
//         // TODO: 实际应该创建File结构，但先置为NULL
//         sec->fp = NULL; // 暂时置为NULL，待实现file系统
//         sec->offset = prog_hdr.p_offset;
//         sec->length = prog_hdr.p_filesz;
        
//         // 添加到section链表
//         _insert_into_list(&new_pgdir.section_head, &sec->stnode);
//         printk("CPU: %lld, Inserted section: begin=0x%llx, end=0x%llx, flags=0x%llx\n",cpuid(),
//                sec->begin, sec->end, sec->flags);
//     }
//     printk("CPU: %lld, All loadable segments processed.\n",cpuid());
//     // Step3: 创建用户栈
//     #define USER_STACK_SIZE (8 * PAGE_SIZE)
//     #define USER_STACK_TOP 0x0000004000000000UL
    
//     struct section *stack_sec = kalloc(sizeof(struct section));
//     if (stack_sec == NULL){
//         free_sections(&new_pgdir);
//         free_pgdir(&new_pgdir);
//         goto fail;
//     }
//     printk("CPU: %lld, 113.\n",cpuid());
//     stack_sec->begin = USER_STACK_TOP - USER_STACK_SIZE;
//     stack_sec->end = USER_STACK_TOP;
//     stack_sec->flags = ST_HEAP;  // 匿名段
//     stack_sec->fp = NULL;
//     stack_sec->offset = 0;
//     stack_sec->length = 0;
    
//     _insert_into_list(&new_pgdir.section_head, &stack_sec->stnode);
    
//     // Step4: 准备参数并压入栈
//     sp = USER_STACK_TOP;
    
//     // 计算argc
//     for (argc = 0; argv && argv[argc]; argc++){
//         if (argc >= MAXARGS){
//             free_sections(&new_pgdir);
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
//     }
//     printk("CPU: %lld, 134.\n",cpuid());
//     // 计算envc
//     int envc;
//     for (envc = 0; envp && envp[envc]; envc++){
//         if (envc >= MAXARGS){
//             free_sections(&new_pgdir);
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
//     }
    
//     // 压入envp字符串内容
//     u64 uenvp[MAXARGS+1];
//     for (i = envc - 1; i >= 0; i--) {
//         usize len = strlen(envp[i]) + 1;
//         sp -= len;
//         sp = round_down(sp, 8);  // 8字节对齐
//         if (copyout(&new_pgdir, (void*)sp, envp[i], len) < 0){
//             free_sections(&new_pgdir);
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
//         uenvp[i] = sp;
//     }
//     uenvp[envc] = 0;  // NULL终止符
//     printk("CPU: %lld, 159.\n",cpuid());
//     // 压入argv字符串内容
//     for (i = argc - 1; i >= 0; i--) {
//         usize len = strlen(argv[i]) + 1;
//         sp -= len;
//         sp = round_down(sp, 8);  // 8字节对齐
//         if (copyout(&new_pgdir, (void*)sp, argv[i], len) < 0){
//             free_sections(&new_pgdir);
//             free_pgdir(&new_pgdir);
//             goto fail;
//         }
//         ustack[i] = sp;
//     }
//     ustack[argc] = 0;  // NULL终止符
    
//     // 压入envp指针数组
//     sp -= (envc + 1) * sizeof(u64);
//     sp = round_down(sp, 16);  // 16字节对齐
//     if (copyout(&new_pgdir, (void*)sp, uenvp, (envc + 1) * sizeof(u64)) < 0){
//         free_sections(&new_pgdir);
//         free_pgdir(&new_pgdir);
//         goto fail;
//     }
    
//     // 压入argv指针数组
//     sp -= (argc + 1) * sizeof(u64);
//     sp = round_down(sp, 16);  // 16字节对齐
//     if (copyout(&new_pgdir, (void*)sp, ustack, (argc + 1) * sizeof(u64)) < 0){
//         free_sections(&new_pgdir);
//         free_pgdir(&new_pgdir);
//         goto fail;
//     }
//     printk("CPU: %lld, 191.\n",cpuid());
//     // 压入argc
//     u64 argc_val = argc;
//     sp -= sizeof(u64);
//     sp = round_down(sp, 16);  // 栈指针必须16字节对齐
//     if (copyout(&new_pgdir, (void*)sp, &argc_val, sizeof(u64)) < 0){
//         free_sections(&new_pgdir);
//         free_pgdir(&new_pgdir);
//         goto fail;
//     }
    
//     // Step5: 保存旧的页目录并切换到新的
//     struct pgdir old_pgdir = p->pgdir;
    
//     // 迁移旧的section链表到old_pgdir (其在栈上)
//     // 必须更新链表中节点的指针，使其指向old_pgdir.section_head，
//     // 否则free_sections中的循环无法正确通过比对 &old_pgdir.section_head 来终止。
//     init_list_node(&old_pgdir.section_head);
//     if (!_empty_list(&p->pgdir.section_head)) {
//         ListNode *first = p->pgdir.section_head.next;
//         ListNode *last = p->pgdir.section_head.prev;
        
//         old_pgdir.section_head.next = first;
//         old_pgdir.section_head.prev = last;
//         first->prev = &old_pgdir.section_head;
//         last->next = &old_pgdir.section_head;
        
//         // 重置原链表头，保持一致性，虽然稍后会被覆盖
//         init_list_node(&p->pgdir.section_head);
//     }

//     p->pgdir = new_pgdir;
    
//     // 设置进程上下文
//     p->ucontext->elr = elf_hdr.e_entry;  // 入口地址
//     p->ucontext->sp = sp;                // 栈指针
//     printk("CPU: %lld, 209.\n",cpuid());
//     // 释放旧的页目录和sections
//     free_sections(&old_pgdir);
//     printk("CPU: %lld, 212.\n",cpuid());
//     free_pgdir(&old_pgdir);
    
//     // 关闭所有CLOEXEC文件
//     for (int fd = 0; fd < NOFILE; fd++) {
//         struct file *f = p->oftable.files[fd];
//         if (f) {
//             // 注意：这里简化处理，实际可能需要检查O_CLOEXEC标志
//             // 但当前实现中可能没有这个标志，所以暂不处理
//         }
//     }
//     printk("CPU: %lld, 222.\n",cpuid());
//     inodes.unlock(ip);
//     inodes.put(&ctx, ip);
//     bcache.end_op(&ctx);
//     // printk("execve: loaded program '%s' (argc=%d, envc=%d)\n", path, argc, envc);
//     return 0;

// fail:
//     printk("execve: failed to load program '%s'\n", path);
//     if (ip){
//         inodes.unlock(ip);
//         inodes.put(&ctx, ip);
//     }
//     bcache.end_op(&ctx);
//     return -1;
//     /* (Final) TODO END */
// }

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
#define MAXARGS 32
#endif

#define USER_STACK_TOP   0x0000004000000000UL
#define USER_STACK_SIZE  (8 * PAGE_SIZE)

int execve(const char *path, char *const argv[], char *const envp[])
{   
    printk("execve: path=%s\n", path);
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;
    Inode *ip = NULL;
    Proc *p = thisproc();
    struct pgdir new_pgdir, old_pgdir;
    OpContext ctx;
    u64 sp;
    u64 ustack[MAXARGS + 1];    // argv 指针数组
    u64 uenvp_arr[MAXARGS + 1]; // envp 指针数组
    int argc = 0, envc = 0;

    bcache.begin_op(&ctx);

    // ========== Step 1: 打开并验证 ELF 文件 ==========
    ip = namei(path, &ctx);
    if (ip == NULL) {
        bcache.end_op(&ctx);
        return -1;
    }
    inodes.lock(ip);

    // 读取 ELF 头
    if (inodes.read(ip, (u8 *)&ehdr, 0, sizeof(ehdr)) != sizeof(ehdr))
        goto fail;

    // 验证 ELF 魔数和格式
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3 ||
        ehdr.e_type != ET_EXEC || ehdr.e_machine != EM_AARCH64)
        goto fail;

    // ========== Step 2: 初始化新页目录并加载程序段 ==========
    init_pgdir(&new_pgdir);
    init_sections(&new_pgdir.section_head);

    // 遍历所有程序头
    for (int i = 0; i < ehdr.e_phnum; i++) {
        u64 ph_off = ehdr.e_phoff + i * sizeof(phdr);
        if (inodes.read(ip, (u8 *)&phdr, ph_off, sizeof(phdr)) != sizeof(phdr))
            goto fail_pgdir;

        // 只处理 PT_LOAD 类型的段
        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0)
            continue;

        // 验证段参数合法性
        if (phdr.p_memsz < phdr.p_filesz ||
            phdr.p_vaddr + phdr.p_memsz < phdr.p_vaddr)
            goto fail_pgdir;

        // 创建 section 结构
        struct section *sec = kalloc(sizeof(struct section));
        if (sec == NULL)
            goto fail_pgdir;

        // 页对齐的虚拟地址范围
        sec->begin = PAGE_BASE(phdr.p_vaddr);
        sec->end = PAGE_BASE(((u64)phdr.p_vaddr + (u64)phdr.p_memsz + (PAGE_SIZE - 1)));
        sec->flags = (phdr.p_flags & PF_X) ? ST_TEXT : ST_DATA;
        sec->fp = NULL;
        sec->offset = phdr.p_offset;
        sec->length = phdr.p_filesz;

        _insert_into_list(&new_pgdir.section_head, &sec->stnode);

        // 为每一页分配物理内存并加载文件内容
        for (u64 va = sec->begin; va < sec->end; va += PAGE_SIZE) {
            void *pa = kalloc_page();
            if (pa == NULL)
                goto fail_pgdir;
            memset(pa, 0, PAGE_SIZE);

            // 计算当前页需要从文件读取的数据范围
            u64 seg_file_start = phdr.p_vaddr;
            u64 seg_file_end = phdr.p_vaddr + phdr.p_filesz;

            if (va < seg_file_end) {
                u64 read_start = (va > seg_file_start) ? va : seg_file_start;
                u64 read_end = (va + PAGE_SIZE < seg_file_end) ? 
                               (va + PAGE_SIZE) : seg_file_end;
                
                if (read_start < read_end) {
                    u64 file_off = phdr.p_offset + (read_start - seg_file_start);
                    u64 pa_off = read_start - va;
                    u64 len = read_end - read_start;
                    inodes.read(ip, (u8 *)pa + pa_off, file_off, len);
                }
            }
            // BSS 段部分已经由 memset 清零

            vmmap(&new_pgdir, va, pa, PTE_USER_DATA);
        }
    }

    // ========== Step 3: 创建用户栈 ==========
    {
        struct section *stack_sec = kalloc(sizeof(struct section));
        if (stack_sec == NULL)
            goto fail_pgdir;

        stack_sec->begin = USER_STACK_TOP - USER_STACK_SIZE;
        stack_sec->end = USER_STACK_TOP;
        stack_sec->flags = ST_HEAP;  // 匿名段
        stack_sec->fp = NULL;
        stack_sec->offset = 0;
        stack_sec->length = 0;

        _insert_into_list(&new_pgdir.section_head, &stack_sec->stnode);

        // 分配栈的物理页
        for (u64 va = stack_sec->begin; va < stack_sec->end; va += PAGE_SIZE) {
            void *pa = kalloc_page();
            if (pa == NULL)
                goto fail_pgdir;
            memset(pa, 0, PAGE_SIZE);
            vmmap(&new_pgdir, va, pa, PTE_USER_DATA);
        }
    }

    // ========== Step 4: 保存旧页目录并切换到新页目录 ==========
    old_pgdir = p->pgdir;

    // 关键：修复 old_pgdir.section_head 的双向引用
    // 直接做 struct 赋值会把“链表头指针值”也拷贝过去：
    // - 非空链表：首/尾节点仍指向 &p->pgdir.section_head
    // - 空链表：old_pgdir.section_head.next/prev 甚至会等于 &p->pgdir.section_head
    // 如果不重绑，free_sections(&old_pgdir) 会把 &p->pgdir.section_head 当作节点来遍历，造成内存破坏。
    if (_empty_list(&p->pgdir.section_head)) {
        init_list_node(&old_pgdir.section_head);
    } else {
        old_pgdir.section_head.next = p->pgdir.section_head.next;
        old_pgdir.section_head.prev = p->pgdir.section_head.prev;
        old_pgdir.section_head.next->prev = &old_pgdir.section_head;
        old_pgdir.section_head.prev->next = &old_pgdir.section_head;
    }

    p->pgdir = new_pgdir;
    // 同 old_pgdir 的道理：new_pgdir 是栈变量，构建 section 链表时
    // 首/尾节点都指向 &new_pgdir.section_head。
    // 把 struct 拷进 p->pgdir 后，必须把首/尾节点重绑到 &p->pgdir.section_head。
    if (_empty_list(&p->pgdir.section_head)) {
        init_list_node(&p->pgdir.section_head);
    } else {
        p->pgdir.section_head.next->prev = &p->pgdir.section_head;
        p->pgdir.section_head.prev->next = &p->pgdir.section_head;
    }
    attach_pgdir(&p->pgdir);

    // ========== Step 5: 设置栈参数 ==========
    sp = USER_STACK_TOP;

    // 计算 argc 和 envc
    for (argc = 0; argv && argv[argc] && argc < MAXARGS; argc++);
    for (envc = 0; envp && envp[envc] && envc < MAXARGS; envc++);

    // 压入环境变量字符串（从后往前）
    for (int i = envc - 1; i >= 0; i--) {
        usize len = strlen(envp[i]) + 1;
        sp = (sp - len) & ~7UL;  // 8 字节对齐
        if (copyout(&p->pgdir, (void *)sp, envp[i], len) < 0)
            goto fail_restore;
        uenvp_arr[i] = sp;
    }
    uenvp_arr[envc] = 0;  // NULL 终止

    // 压入参数字符串（从后往前）
    for (int i = argc - 1; i >= 0; i--) {
        usize len = strlen(argv[i]) + 1;
        sp = (sp - len) & ~7UL;  // 8 字节对齐
        if (copyout(&p->pgdir, (void *)sp, argv[i], len) < 0)
            goto fail_restore;
        ustack[i] = sp;
    }
    ustack[argc] = 0;  // NULL 终止

    // 16 字节对齐
    sp &= ~15UL;

    // 压入 envp 指针数组
    sp -= (envc + 1) * sizeof(u64);
    if (copyout(&p->pgdir, (void *)sp, uenvp_arr, (envc + 1) * sizeof(u64)) < 0)
        goto fail_restore;

    // 压入 argv 指针数组
    sp -= (argc + 1) * sizeof(u64);
    if (copyout(&p->pgdir, (void *)sp, ustack, (argc + 1) * sizeof(u64)) < 0)
        goto fail_restore;

    // 压入 argc
    sp -= sizeof(u64);
    u64 argc_val = (u64)argc;
    if (copyout(&p->pgdir, (void *)sp, &argc_val, sizeof(u64)) < 0)
        goto fail_restore;

    // 确保最终栈指针 16 字节对齐
    sp &= ~15UL;

    // ========== Step 6: 设置进程上下文 ==========
    p->ucontext->elr = ehdr.e_entry;  // 程序入口地址
    p->ucontext->sp = sp;              // 栈指针

    // 释放旧的地址空间
    free_sections(&old_pgdir);
    free_pgdir(&old_pgdir);

    // 成功返回
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return 0;

fail_restore:
    // 恢复旧页目录
    p->pgdir = old_pgdir;
    // 注意：old_pgdir 是栈上的临时变量，早先我们把首/尾节点重绑到了
    // &old_pgdir.section_head。这里把 struct 拷回 p->pgdir 后，必须再次把
    // 首/尾节点重绑到 &p->pgdir.section_head，否则链表会指向栈地址，后续 fork/copy_sections
    // 遍历时就会把任意 ListNode 当成 section，出现你看到的 begin/end/fp 垃圾值。
    if (_empty_list(&p->pgdir.section_head)) {
        init_list_node(&p->pgdir.section_head);
    } else {
        p->pgdir.section_head.next->prev = &p->pgdir.section_head;
        p->pgdir.section_head.prev->next = &p->pgdir.section_head;
    }
    attach_pgdir(&p->pgdir);
    free_sections(&new_pgdir);
    free_pgdir(&new_pgdir);
    goto fail;

fail_pgdir:
    free_sections(&new_pgdir);
    free_pgdir(&new_pgdir);

fail:
    inodes.unlock(ip);
    inodes.put(&ctx, ip);
    bcache.end_op(&ctx);
    return -1;
}