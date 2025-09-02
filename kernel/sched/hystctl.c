#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched/sysctl.h>

static int hyst_mode __initdata = 0; // 0 = Balanced by default

static int __init hystctl_setup(char *str)
{
    int val;
    if (kstrtoint(str, 0, &val) == 0)
        hyst_mode = val;
    return 1;
}
__setup("hystmode=", hystctl_setup);

static int __init hystctl_init(void)
{
    pr_info("sched_busy_hyst built-in init, mode=%d\n", hyst_mode);

    switch (hyst_mode) {
    case 0: // Balanced (default)
        sched_busy_hysteresis_enable_cpus = 0xFF; // all CPUs
        sched_busy_hyst = 256; // ~25% margin
        break;
    case 1: // Performance
        sched_busy_hysteresis_enable_cpus = 0xF0; // big cores
        sched_busy_hyst = 64;  // ~6% margin
        break;
    case 2: // Battery
        sched_busy_hysteresis_enable_cpus = 0x0F; // little cores
        sched_busy_hyst = 512; // ~50% margin
        break;
    case 3: // Disabled
        sched_busy_hysteresis_enable_cpus = 0x00; // none
        sched_busy_hyst = 0;
        break;
    default:
        pr_warn("Unknown hyst_mode=%d, falling back to Balanced\n", hyst_mode);
        sched_busy_hysteresis_enable_cpus = 0xFF;
        sched_busy_hyst = 256;
        break;
    }

    pr_info("Applied sched_busy_hyst=%u, enable_cpus=0x%x\n",
            sched_busy_hyst, sched_busy_hysteresis_enable_cpus);
    return 0;
}
early_initcall(hystctl_init);