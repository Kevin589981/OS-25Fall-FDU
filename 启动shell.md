# OS-25Fall-FDU 实验最终要求
## 实验分值分布
| 内容 | 分值 | 负责助教 | 备注 |
| ---- | ---- | ---- | ---- |
| Console | 10 | 徐厚泽 | |
| Pipe | 5 | 徐厚泽 | O |
| File Descriptor | 15 | 唐傑伟 | |
| Shell | 15 | 唐傑伟 | |
| Exec | 20 | 孔令宇 | |
| Fork | 10 | 孔令宇 | |
| Memory Management | 15 | 孔令宇 | |
| Mmap | 10 | 孔令宇 | O |
| 自选 Bonus | 20 | TBC | O |

### 说明
- `O`：不实现此部分也可以启动 shell。如果时间来不及，请优先保证能启动 shell（启动 shell 所占分值较大）。
- 建议优先完成：File Descriptor + Exec + Fork + Shell + Console + Pipe。
- 满分 100 分，超出的部分算 bonus。

## 1. File Descriptor
已有完整的硬盘文件/目录抽象（inode），但 UNIX 设计哲学是“一切皆文件”——除狭义硬盘文件外，还有设备文件、管道文件等。这些文件是内核中的数据结构，却需和硬盘文件一样支持读写操作，因此需要设计一套统一抽象机制，即文件（file）。

### 面向对象视角理解“一切皆文件”
“文件”可视为抽象类或接口，核心是通过统一接口操作多种资源，既能最大化复用代码，又具备良好可扩展性，符合开闭原则（OCP）。

### Linux 系统中的接口实现
```c
struct file_operations {
  struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
	int (*readdir) (struct file *, void *, filldir_t);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *, int datasync);
	int (*fasync) (int, struct file *, int);
	int (*lock) (struct file *, int, struct file_lock *);
	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);
	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);
};
```

支持该接口的资源以数据为中心，例如：
- 键盘（字符设备）：管理用户输入的字符数据。
- 管道文件：管理进程间传递的数据。
- `/dev/random` 设备：管理随机数据。

### 文件的额外字段
- 偏移量：硬盘文件指从文件开头的字节数，设备文件和管道文件指迄今为止读/写的字节数，标识当前读写位置。
- 持有者数量：支持多个进程同时持有文件。
- 读写模式：如设备文件可能仅支持只读，管道文件可能一头只读、一头只写等。

### 文件表与文件描述符
- 全局文件表：组织所有文件对象（数组形式），避免不必要的开支。
- 进程文件表：每个进程独立拥有（数组形式），其中每个文件对象通过文件描述符（非负整数，即数组下标）标识，通过该下标可找到对应的文件对象。

### 简化的文件对象结构
```c
enum file_type {
    NONE,    // 未使用
    DEVICE,  // 设备文件
    INODE,   // 硬盘文件
    PIPE,    // 管道文件
};

struct file {
    enum file_type type;    // 文件类型
    bool readable, writable;// 读写模式
    int refcnt;             // 当前持有者数量
    usize offset;           // 当前读写偏移量
    union {
        struct inode *inode;    // 硬盘文件的下层接口（Inode）
        struct device *device;  // 设备文件的下层接口（Device）
        struct pipe *pipe;      // 管道文件的下层接口（Pipe）
    };
};
```

### 实验简化说明
- 支持的外设少（仅一个终端字符设备），且未实现虚拟文件系统（VFS）接口，因此直接复用硬盘的 inode 抽象作为设备文件的下层接口。
- 创建设备文件时，会在硬盘上创建类型为 `INODE_DEVICE` 的实在 inode，`inode_read` 等函数可能需要调用设备文件的下层接口（如 `console_read`）。

### 任务
1. 实现 `src/fs/file.c` 的下列函数：
```c
// 从全局文件表中分配一个空闲的文件
struct file* file_alloc();

// 获取文件的元信息（类型、偏移量等）
int file_stat(struct file* f, struct stat* st);

// 关闭文件
void file_close(struct file* f);

// 将长度为 n 的 addr 写入 f 的当前偏移处
isize file_read(struct file* f, char* addr, isize n);

// 将长度为 n 的 addr 从 f 的当前偏移处读入
isize file_write(struct file* f, char* addr, isize n);

// 文件的引用数+1
struct file* file_dup(struct file* f);

// 初始化全局文件表
void init_ftable();

// 初始化/释放进程文件表
void init_oftable(struct oftable*);
void free_oftable(struct oftable*);
```

2. 完成 `src/fs/inode.c` 中的 `namex` 函数（路径字符串解析逻辑）：
```c
static Inode* namex(const char* path, bool nameiparent, char* name, OpContext* ctx)
```

3. 在 `src/kernel/sysfile.c` 中实现系统调用：
   - close(3)
   - chdir(3)

4. 实现辅助函数：
```c
// 从描述符获得文件
static struct file *fd2file(int fd);

// 从进程文件表中分配一个空闲的位置给 f
int fdalloc(struct file *f);

// 根据路径创建一个 Inode
Inode *create(const char *path, short type, short major, short minor, OpContext *ctx);
```

5. （可后续完成）修改 `inode.c` 中 `read` 和 `write` 函数，支持设备文件。

## 2. Fork & Exec
`fork` 和 `execve` 是 Unix/Linux 系统编程的核心系统调用，组合使用是实现多进程和加载新程序的核心机制：
- `fork`：从当前进程（父进程）创建新进程（子进程）。
- `execve`：在当前进程中执行新程序。

### 2.1. ELF 可执行文件格式
ELF 文件是程序的“镜像”，包含程序入口地址、段大小、段属性等信息，加载到内存后跳转到入口地址即可运行程序。

#### 核心结构体定义（头文件：`musl/include/elf.h`）
```c
/**
 * ELF Header：ELF 文件的入口点，位于文件的开头，描述了文件的整体布局和重要属性。
 */
typedef struct {
  	// 标识 ELF 文件的魔数、架构类型（32/64 位）、字节序（小端/大端）等。
		unsigned char e_ident[EI_NIDENT];
  
  	// 文件类型（可执行文件、共享库、目标文件等）。
    Elf64_Half    e_type;
  
  	// 指定目标机器架构（如 x86、ARM）。
    Elf64_Half    e_machine;
  
  	// ELF 版本信息。
    Elf64_Word    e_version;
  
  	// 程序入口地址（可执行文件运行时的起始地址）。
    Elf64_Addr    e_entry;
  
  	// 程序头表（Program Header Table）的偏移量。
    Elf64_Off     e_phoff;
  
  	// 段头表（Section Header Table）的偏移量。
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

/**
 * Program Header Table (PHT)：描述程序运行时需要加载的段信息，主要用于可执行文件和共享库。
 */
typedef struct {
  	// 段类型（如加载段、动态段、解释器段等）。
    Elf64_Word    p_type;
  
  	// 段的权限（可读、可写、可执行）。
    Elf64_Word    p_flags;
  
  	// 段在文件中的偏移量。
    Elf64_Off     p_offset;
  
  	// 段的虚拟地址（内存中的地址）。
    Elf64_Addr    p_vaddr;
  
  	// 段的物理地址（针对嵌入式设备）。
    Elf64_Addr    p_paddr;
  
  	// 段在文件中的大小。
    Elf64_Xword   p_filesz;
  
  	// 段在内存中的大小（可能大于文件中大小）。
    Elf64_Xword   p_memsz;
  
  	// 段的对齐要求。
    Elf64_Xword   p_align;
} Elf64_Phdr;
```

#### 注意事项
- ELF 文件中的 section header 与实验中的 `struct section` 不同，本次实验无需考虑 ELF 的 section header，但需明确 `Elf64_Phdr` 与实验中 `struct section` 的对应关系。
- 提示：可参考 xv6 加载 ELF 的实现。

### 2.2. `fork()` 系统调用
- 计算机系统基础（ICS）复习：`fork()` 创建当前进程的完整复制，通过返回值区分父进程（返回子进程 PID）和子进程（返回 0）。
- 实现核心：从进程结构体（`Proc`）入手，判断成员是否需要复制/修改，例如深拷贝父进程页表、“复制”父进程文件描述符等。

#### 思考
- 文件描述符的“复制”是什么意思？

#### 注意事项
- 为配合 `fork()`，需在 `UserContext` 中加入所有寄存器的值，还需保存 `tpidr0` 和 `q0`（musl libc 会使用）。
- 提示：可参考 xv6 fork 的实现。

### 2.3. `execve()` 系统调用
```c
/**
 * @path: 可执行文件的地址。
 * @argv: 运行文件的参数（即 main 函数中的 argv ）。
 * @envp: 环境变量
 */
int execve(const char* path, char* const argv[], char* const envp[])
```
`execve()` 替换当前进程为 `path` 指向的 ELF 格式文件，需读取 `Elf64_Ehdr` 和 `Elf64_Phdr` 结构，根据 `p_flags` 判断段类型，将相关信息填入实验框架的 `struct section`，加载代码和数据到内存，设置用户栈、堆，最后跳转到 ELF 入口地址执行。

#### 思考
- 替换 ELF 文件时，当前进程的哪些部分需要释放，哪些不需要？
- 是否需要把 `argv` 和 `envp` 中的实际文本信息复制到新进程的地址空间中？

#### 实现流程提示
```c
/*
 * Step1: 从 path 对应的文件中读取数据。
 * 前 sizeof(struct Elf64_Ehdr) 字节是 ELF 头，需检查 ELF 魔数，获取 e_phoff（程序头表起始偏移）和 e_phnum（程序头表项数量）。
 *
 * Step2: 加载程序头表和程序本身
 * 程序头表存储形式：struct Elf64_Phdr phdr[e_phnum];
 * e_phoff 是文件中程序头表的偏移量（即 phdr[0] 的地址）。
 * 对于每个程序头，若类型（p_type）为 LOAD，则需加载：
 * 简单方式：
 * (1) 分配内存，虚拟地址区间 [vaddr, vaddr+filesz)
 * (2) 将文件中 [offset, offset + filesz) 的内容复制到内存 [vaddr, vaddr+filesz)
 * 由于已实现动态虚拟内存管理，可尝试仅设置文件和偏移量（延迟分配）
 * 提示：本实验中大多数可执行文件仅有两个可加载程序头，第一个是文本段（flag=RX），第二个是数据+BSS段（flag=RW），可通过头标志验证。
 * 第二个头中，[p_vaddr, p_vaddr+p_filesz) 是数据段，[p_vaddr+p_filesz, p_vaddr+p_memsz) 是 BSS 段（需初始化为 0），可将数据段和 BSS 段放入同一个 struct section，建议使用零页实现写时复制（COW）。
 *
 * Step3: 分配并初始化用户栈
 * 用户栈的虚拟地址无需固定，可随机化（提示：可一次性分配，或延迟分配）。
 * 压入参数字符串，初始栈结构如下：
 *   +-------------+
 *   | envp[m] = 0 |
 *   +-------------+
 *   |    ....     |
 *   +-------------+
 *   |   envp[0]   |  若不实现环境变量可忽略
 *   +-------------+
 *   | argv[n] = 0 |  n == argc
 *   +-------------+
 *   |    ....     |
 *   +-------------+
 *   |   argv[0]   |
 *   +-------------+
 *   |    argc     |
 *   +-------------+  <== sp
 *
 * ## 示例
 * sp -= 8; *(size_t *)sp = argc; （提示：若当前页表是新页表，可直接修改 sp）
 * thisproc()->tf->sp = sp; （提示：栈指针必须 16 字节对齐！）
 * 入口地址存储在 elf_header.entry 中
*/
```

#### 补充说明
- 可执行文件中通常仅加载两段：RX（代码段，text 段，属性 ST_FILE + RO）和 RW（数据段，data + bss 段），其他段可跳过。

### 2.4. `copyout`
复制内容到指定页表，用于 `execve` 中跨用户地址空间复制内容（若使用其他实现则无需实现）。
```c
/*                                        
 * 从 p 复制 len 字节到页表 pgdir 中的用户地址 va。
 * 为给定文件分配文件描述符。
 * 必要时分配物理页。
 * 成功时接管调用者的文件引用。
 * 适用于 pgdir 不是当前页表的场景。
 * 成功返回 0，失败返回 -1
 */                                                          
int copyout(struct pgdir* pd, void* va, void *p, usize len)
```

### 2.5. 任务
1. 实现下列内容：
   - `kernel/syscall.c` 中的 `user_readable` 和 `user_writeable`：检查系统调用中用户程序传入的指针指向的内存空间是否有效且可读写。
   - `kernel/proc.c` 中的 `fork`。
   - `kernel/exec.c` 中的 `execve`。
   - `kernel/pt.c` 中的 `copyout`（可选）。
   - `kernel/paging.c` 中的 `copy_sections`。
   - `kernel/paging.c` 中的 `init_sections`：无需单独初始化堆段。
   - `kernel/paging.c` 中的 `pgfault`：增加文件相关处理逻辑。

## 3. Shell
### 任务
1. 补全两个用户态程序的实现：
   - cat(1)：`src/user/cat/main.c`，支持 `cat + 单一文件名` 命令形式（输出单一文件），其他功能可自行补充。
   - mkdir(1)：`src/user/mkdir/main.c`。

2. 内核启动后执行第一个用户态程序 `src/user/init.S`：在 `src/kernel/core.c` 的 `kernel_entry()` 中手动创建并启动第一个用户态进程（将 `init.S` 的代码映射到进程的 section 中）。

#### 提示
- 可在 `core.c` 中使用 `extern char icode[]; extern char eicode[];` 获取 `init.S` 代码段的内核地址（Lab 0 中清空 BSS 段时曾使用类似操作）。
- 实现成功后，内核将输出 shell 的 prompt（`$`），但此时暂无法交互，需后续实现 console 支持用户输入。
