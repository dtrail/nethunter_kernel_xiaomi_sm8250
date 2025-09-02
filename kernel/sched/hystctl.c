#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched/sysctl.h>
#include <linux/debugfs.h>

static struct dentry *hystctl_dir;
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
    case 0: // Balanced
        sysctl_sched_busy_hyst_enable_cpus = 0xFF;
        sysctl_sched_busy_hyst = 256;
        break;
    case 1: // Performance
        sysctl_sched_busy_hyst_enable_cpus = 0xF0;
        sysctl_sched_busy_hyst = 64;
        break;
    case 2: // Battery
        sysctl_sched_busy_hyst_enable_cpus = 0x0F;
        sysctl_sched_busy_hyst = 512;
        break;
    case 3: // Disabled
        sysctl_sched_busy_hyst_enable_cpus = 0x00;
        sysctl_sched_busy_hyst = 0;
        break;
    default:
        pr_warn("Unknown hyst_mode=%d, falling back to Balanced\n", hyst_mode);
        sysctl_sched_busy_hyst_enable_cpus = 0xFF;
        sysctl_sched_busy_hyst = 256;
        break;
    }

    pr_info("Applied sched_busy_hyst=%u, enable_cpus=0x%x\n",
            sysctl_sched_busy_hyst, sysctl_sched_busy_hyst_enable_cpus);

    // ✅ Add debugfs entries
    hystctl_dir = debugfs_create_dir("hystctl", NULL);
    debugfs_create_u32("enable_cpus", 0644, hystctl_dir, &sysctl_sched_busy_hyst_enable_cpus);
    debugfs_create_u32("margin", 0644, hystctl_dir, &sysctl_sched_busy_hyst);

    return 0;
}
late_initcall(hystctl_init);