#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/uart.h>
#include <kernel/core.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

static volatile bool boot_secondary_cpus = false;
static void check_bss_zero(void *start, void *end){
    unsigned char *p   = (unsigned char *)start;
    long long unsigned int len  = (unsigned char *)end - p;
    long long unsigned int bad  = 0;

    for (long long unsigned int i = 0; i < len; ++i) {
        if (p[i] != 0) {
            ++bad;
        }
    }
    if (bad == 0)
        printk("BSS check PASSED: %llu bytes are all zero.\n", len);
    else
        printk("BSS check FAILED: %llu non-zero bytes found.\n", bad);
}

void main() {
    if (cpuid() == 0) {
        /* @todo: Clear BSS section.*/
        extern char edata[], end[];
        memset(edata, 0, (usize)(end - edata));

        smp_init();
        uart_init();
        printk_init();

        /* initialize kernel memory allocator */
        kinit();

        arch_fence();

        // Set a flag indicating that the secondary CPUs can start executing.
        boot_secondary_cpus = true;
    } else {
        while (!boot_secondary_cpus);
        arch_fence();
    }

    set_return_addr(idle_entry);
}
