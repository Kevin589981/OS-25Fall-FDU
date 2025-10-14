// #define debug_sched 1
// #define debug_page_fault 1

// #define DEBUG 1

#ifdef DEBUG
    // If DEBUG is defined, DEBUG_PRINTK expands to a full printk call.
    #define DEBUG_PRINTK(fmt, ...) \
        printk("[File:%s Line:%d] CPU:%lld PID:%d: " fmt "\n", \
               __FILE__, __LINE__, cpuid(), thisproc()->pid, ##__VA_ARGS__)
#else
    // If DEBUG is not defined, DEBUG_PRINTK expands to nothing.
    #define DEBUG_PRINTK(fmt, ...) do {} while (0)
#endif


