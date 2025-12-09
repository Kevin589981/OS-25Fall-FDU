#if defined(__aarch64___)||defined(__arm__)
__asm(".globl printk\n"
      "printk:\n"
      "b printf");
#elif defined(__x86_64__)||defined(__i386__)
__asm__(".globl printk\n"
        "printk:\n"
        "jmp printf");
#else
#error "Unsupported architecture"
#endif