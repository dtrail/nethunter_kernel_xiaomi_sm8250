#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sysctl.h>
#include <linux/debugfs.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dtrail");
MODULE_DESCRIPTION("Hysteresis control module for sched_busy");
MODULE_VERSION("1.0");

// Runtime tunables
static unsigned int sysctl_sched_busy_hyst = 256;
static unsigned int sysctl_sched_busy_hyst_enable_cpus = 0xFF;
static int hyst_mode = 0; // 0 = Balanced

// DebugFS root
static struct dentry *hystctl_dir;

// Sysctl table entries
static struct ctl_table hystctl_sysctl_table[] = {
    {
        .procname   = "hyst_mode",
        .data       = &hyst_mode,
        .maxlen     = sizeof(int),
        .mode       = 0644,
        .proc_handler = proc_dointvec,
    },
    {
        .procname   = "sched_busy_hyst",
        .data       = &sysctl_sched_busy_hyst,
        .maxlen     = sizeof(unsigned int),
        .mode       = 0644,
        .proc_handler = proc_dointvec,
    },
    {
        .procname   = "sched_busy_hyst_enable_cpus",
        .data       = &sysctl_sched_busy_hyst_enable_cpus,
        .maxlen     = sizeof(unsigned int),
        .mode       = 0644,
        .proc_handler = proc_dointvec,
    },
    {}
};

static struct ctl_table hystctl_root_table[] = {
    {
        .procname   = "kernel",
        .mode       = 0555,
        .child      = hystctl_sysctl_table,
    },
    {}
};

static struct ctl_table_header *hystctl_sysctl_header;

static int __init hystctl_init(void)
{
    pr_info("sched_busy_hyst init: hyst_mode=%d\n", hyst_mode);

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

    // DebugFS interface
    hystctl_dir = debugfs_create_dir("hystctl", NULL);
    if (hystctl_dir) {
        debugfs_create_u32("enable_cpus", 0644, hystctl_dir, &sysctl_sched_busy_hyst_enable_cpus);
        debugfs_create_u32("margin", 0644, hystctl_dir, &sysctl_sched_busy_hyst);
    } else {
        pr_warn("Failed to create hystctl debugfs directory\n");
    }

    // Sysctl interface
    hystctl_sysctl_header = register_sysctl_table(hystctl_root_table);
    if (!hystctl_sysctl_header)
        pr_warn("Failed to register hystctl sysctl table\n");

    return 0;
}
late_initcall(hystctl_init);