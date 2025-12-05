__asm(".globl printk\n"
      "printk:\n"
      "b printf");
// #include <cstdio>
// #include <cstdarg>

// extern "C" void printk(const char *fmt, ...) {
//     va_list args;
//     va_start(args, fmt);
//     vprintf(fmt, args);
//     va_end(args);
// }