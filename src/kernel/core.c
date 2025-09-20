#include <aarch64/intrinsic.h>
#include <test/test.h>

#include <kernel/printk.h>
NO_RETURN void idle_entry() {
    kalloc_test();
    printk("idle_entry passed.(from cpu: %lld)\n",cpuid());
    arch_stop_cpu();
}
