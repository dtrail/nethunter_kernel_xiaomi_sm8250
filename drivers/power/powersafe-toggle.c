// SPDX-License-Identifier: GPL-2.0
/*
 * powersafe.c - Topology-aware power management module with 4-state master toggle
 *
 * Profiles:
 *   0 - Default
 *       - Restore kernel defaults
 *       - Switch governors to "schedutil" for all clusters
 *       - Clear prime boost
 *       - PM QoS CPU DMA latency -> default
 *
 *   1 - Balanced
 *       - Keep current governors
 *       - Moderate PM QoS latency
 *       - Moderate I/O weight (tracked only here; wire to your IOSched as needed)
 *
 *   2 - Performance (+ prime boost)
 *       - Switch governors to "userspace" for all clusters
 *       - Set performance frequencies on little/big clusters
 *       - Apply prime boost frequency to prime cluster/core(s) (if present)
 *       - Lower PM QoS latency
 *
 *   3 - Powersafe
 *       - Switch governors to "userspace"
 *       - Set conservative frequencies on clusters (including prime, if present)
 *       - Higher PM QoS latency
 *
 * Debug state files (read-only):
 *   /proc/powersafe/master_toggle
 *   /proc/powersafe/latency_state
 *   /proc/powersafe/prime_state
 *   /proc/powersafe/io_weight_state
 *
 * Control:
 *   echo <0|1|2|3> > /proc/powersafe/master_toggle
 *
 * Requirements:
 * - Kernel patched to export:
 *     int cpufreq_force_governor_cpu(unsigned int cpu, const char *gov_name);
 * - CPU topology masks exported (Android/vendor kernels typically do this):
 *     extern const struct cpumask *const cpu_little_mask;
 *     extern const struct cpumask *const cpu_big_mask;
 *     extern const struct cpumask *const cpu_prime_mask;
 *
 * Notes:
 * - Frequencies are clamped to each policy's min/max.
 * - io_weight_state is tracked only; connect it to your I/O scheduler if desired.
 * - If cpu_prime_mask is empty (no prime core), prime-related loops are no-ops.
 *
 * Co-created by Manuel & Copilot
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>


/* === Topology masks (exported by kernel/cpu.c on many Android/vendor trees) === */
extern const struct cpumask *const cpu_little_mask;
extern const struct cpumask *const cpu_big_mask;
extern const struct cpumask *const cpu_prime_mask;

/* === Proc paths === */
#define PROC_DIR        "powersafe"
#define PROC_MASTER     "master_toggle"
#define PROC_LATENCY    "latency_state"
#define PROC_PRIME      "prime_state"
#define PROC_IOW        "io_weight_state"

#define MAX_BUF_LEN     32

/* === State tracking === */
static int latency_state            = 0;     /* 0 = default, 1 = custom latency applied */
static int prime_state              = 0;     /* 0 = inactive, 1 = prime boost active */
static unsigned int io_weight_state = 0;     /* 0 = default, else custom weight */
static int master_state             = 0;     /* 0 = default, 1 = balanced, 2 = perf, 3 = powersafe */
static int input_boost_state 	    = 0;     // 0 = off, 1 = on

/* === Proc entries === */
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_master;
static struct proc_dir_entry *proc_latency;
static struct proc_dir_entry *proc_prime;
static struct proc_dir_entry *proc_iow;

/* === PM QoS request === */
static struct pm_qos_request latency_req;

/* === Policy frequencies (kHz). Tune to your platform's OPP table. === */
static const unsigned int little_perf_khz   = 1324800;
static const unsigned int big_perf_khz      = 2016000;
static const unsigned int prime_perf_khz    = 2841600;  /* base perf on prime */

static const unsigned int prime_boost_khz   = 3120000;  /* higher prime boost (if supported) */

static const unsigned int little_save_khz   = 960000;
static const unsigned int big_save_khz      = 1324800;
static const unsigned int prime_save_khz    = 1804800;

/* === Helpers === */

static void set_input_boost_enabled(bool enable)
{
    struct file *f;
    mm_segment_t oldfs;
    char buf[2];

    snprintf(buf, sizeof(buf), "%d", enable ? 1 : 0);

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    f = filp_open("/sys/module/cpu_input_boost/parameters/input_boost_enabled", O_WRONLY, 0);
    if (!IS_ERR(f)) {
        kernel_write(f, buf, strlen(buf), &f->f_pos);
        filp_close(f, NULL);
    }

    set_fs(oldfs);
}

/* Clamp and set target frequency for a specific CPU under userspace governor */
static void set_userspace_target_cpu(unsigned int cpu, unsigned int freq_khz)
{
    struct cpufreq_policy *policy;

    policy = cpufreq_cpu_get(cpu);
    if (!policy)
        return;

    if (freq_khz < policy->min)
        freq_khz = policy->min;
    if (freq_khz > policy->max)
        freq_khz = policy->max;

    cpufreq_driver_target(policy, freq_khz, CPUFREQ_RELATION_L);
    cpufreq_cpu_put(policy);
}

/* Switch governor for a specific CPU via exported helper */
static int switch_governor_cpu(unsigned int cpu, const char *gov)
{
    return cpufreq_force_governor_cpu(cpu, gov);
}

/* Apply a frequency to all CPUs in a mask */
static void set_cluster_freq(const struct cpumask *mask, unsigned int freq_khz)
{
    unsigned int cpu;
    for_each_cpu(cpu, mask) {
        set_userspace_target_cpu(cpu, freq_khz);
    }
}

/* Switch governor for all CPUs in a mask */
static void switch_cluster_governor(const struct cpumask *mask, const char *gov)
{
    unsigned int cpu;
    for_each_cpu(cpu, mask) {
        switch_governor_cpu(cpu, gov);
    }
}

/* === Procfs common read helpers (read-only state files) === */

#define PROC_ENTRY_RO(name, var) \
static ssize_t name##_read(struct file *file, char __user *buf, \
                           size_t count, loff_t *ppos) \
{ \
    char kbuf[MAX_BUF_LEN]; \
    int len; \
    if (*ppos > 0) \
        return 0; \
    len = snprintf(kbuf, sizeof(kbuf), "%d\n", (var)); \
    if (copy_to_user(buf, kbuf, len)) \
        return -EFAULT; \
    *ppos = len; \
    return len; \
} \
static const struct proc_ops name##_fops = { \
    .proc_read = name##_read, \
};

PROC_ENTRY_RO(latency_state, latency_state)
PROC_ENTRY_RO(prime_state,   prime_state)
PROC_ENTRY_RO(io_weight_state, io_weight_state)

/* === Master toggle read/write === */

static ssize_t master_toggle_read(struct file *file, char __user *buf,
                                  size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    int len;

    if (*ppos > 0)
        return 0;

    len = snprintf(kbuf, sizeof(kbuf), "%d\n", master_state);
    if (copy_to_user(buf, kbuf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static ssize_t master_toggle_write(struct file *file, const char __user *ubuf,
                                   size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    int new_state;

    if (count == 0 || count >= MAX_BUF_LEN)
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtoint(kbuf, 10, &new_state))
        return -EINVAL;

    switch (new_state) {
    case 0: /* Default */
        pm_qos_update_request(&latency_req, PM_QOS_DEFAULT_VALUE);
        latency_state = 0;
        io_weight_state = 0;
        prime_state = 0;

        /* Restore default governor (schedutil) for all clusters */
        switch_cluster_governor(cpu_little_mask, "schedutil");
        switch_cluster_governor(cpu_big_mask,    "schedutil");
        switch_cluster_governor(cpu_prime_mask,  "performance");
        set_input_boost_enabled(false);

        pr_info("powersafe: state=0 (default)\n");
        break;

    case 1: /* Balanced */
        pm_qos_update_request(&latency_req, 200); /* example balanced latency */
        latency_state = 1;
        io_weight_state = 250;                    /* example balanced weight */
        prime_state = 0;                          /* no extra boost */
        set_input_boost_enabled(false);

        /* Governor unchanged */
        pr_info("powersafe: state=1 (balanced)\n");
        break;

    case 2: /* Performance + prime boost */
        pm_qos_update_request(&latency_req, 100);
        latency_state = 1;
        io_weight_state = 500;
        prime_state = 1;
        set_input_boost_enabled(true);

        /* Switch to userspace governor for all clusters (handles empty masks gracefully) */
        switch_cluster_governor(cpu_little_mask, "userspace");
        switch_cluster_governor(cpu_big_mask,    "userspace");
        switch_cluster_governor(cpu_prime_mask,  "userspace");

        /* Apply performance policy frequencies */
        set_cluster_freq(cpu_little_mask, little_perf_khz);
        set_cluster_freq(cpu_big_mask,    big_perf_khz);
        set_cluster_freq(cpu_prime_mask,  prime_perf_khz);

        /* Prime boost (if policy allows) */
        set_cluster_freq(cpu_prime_mask,  prime_boost_khz);

        pr_info("powersafe: state=2 (performance, prime boost)\n");
        break;

    case 3: /* Powersafe */
        pm_qos_update_request(&latency_req, 400);
        latency_state = 1;
        io_weight_state = 100;
        prime_state = 0;

        /* Switch to userspace governor */
        switch_cluster_governor(cpu_little_mask, "userspace");
        switch_cluster_governor(cpu_big_mask,    "userspace");
        switch_cluster_governor(cpu_prime_mask,  "userspace");
        set_input_boost_enabled(false);

        /* Conservative frequencies */
        set_cluster_freq(cpu_little_mask, little_save_khz);
        set_cluster_freq(cpu_big_mask,    big_save_khz);
        set_cluster_freq(cpu_prime_mask,  prime_save_khz);

        pr_info("powersafe: state=3 (powersafe)\n");
        break;

    default:
        return -EINVAL;
    }

    master_state = new_state;
    return count;
}

static const struct proc_ops master_toggle_fops = {
    .proc_read  = master_toggle_read,
    .proc_write = master_toggle_write,
};

// Input boost read

static ssize_t input_boost_state_read(struct file *file, char __user *buf,
                                      size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    int len;

    if (*ppos > 0)
        return 0;

    len = snprintf(kbuf, sizeof(kbuf), "%d\n", input_boost_state);
    if (copy_to_user(buf, kbuf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static const struct proc_ops input_boost_state_fops = {
    .proc_read = input_boost_state_read,
};

// Input boost r/w

static ssize_t input_boost_write(struct file *file, const char __user *ubuf,
                                 size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    int val;

    if (count == 0 || count >= MAX_BUF_LEN)
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtoint(kbuf, 10, &val))
        return -EINVAL;

    if (val == 0 || val == 1) {
        input_boost_state = val;
        set_input_boost_enabled(val);
        pr_info("powersafe: input boost manually %s\n", val ? "enabled" : "disabled");
        return count;
    }

    return -EINVAL;
}

static const struct proc_ops input_boost_fops = {
    .proc_write = input_boost_write,
};

/* === Module init/exit === */

static int __init powersafe_init(void)
{
    /* Create proc directory */
    proc_dir = proc_mkdir(PROC_DIR, NULL);
    if (!proc_dir)
        return -ENOMEM;

    /* Master toggle (RW) */
    proc_master = proc_create(PROC_MASTER, 0666, proc_dir, &master_toggle_fops);
    if (!proc_master)
        goto err_cleanup;
      
    /* Input boot */
    proc_create("input_boost_state", 0444, proc_dir, &input_boost_state_fops);
    proc_create("input_boost", 0222, proc_dir, &input_boost_fops);

    /* Debug state files (RO) */
    proc_latency = proc_create(PROC_LATENCY, 0444, proc_dir, &latency_state_fops);
    if (!proc_latency)
        goto err_cleanup;

    proc_prime = proc_create(PROC_PRIME, 0444, proc_dir, &prime_state_fops);
    if (!proc_prime)
        goto err_cleanup;

    proc_iow = proc_create(PROC_IOW, 0444, proc_dir, &io_weight_state_fops);
    if (!proc_iow)
        goto err_cleanup;

    /* Initialize PM QoS to default */
    pm_qos_add_request(&latency_req, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

    pr_info("powersafe: module loaded (topology-aware)\n");
    return 0;

err_cleanup:
    if (proc_master)
        remove_proc_entry(PROC_MASTER, proc_dir);
    if (proc_latency)
        remove_proc_entry(PROC_LATENCY, proc_dir);
    if (proc_prime)
        remove_proc_entry(PROC_PRIME, proc_dir);
    if (proc_iow)
        remove_proc_entry(PROC_IOW, proc_dir);
    if (proc_dir)
        remove_proc_entry(PROC_DIR, NULL);
        
	remove_proc_entry("input_boost_state", proc_dir);
	remove_proc_entry("input_boost", proc_dir);
    return -ENOMEM;
}

static void __exit powersafe_exit(void)
{
    pm_qos_remove_request(&latency_req);

    if (proc_master)
        remove_proc_entry(PROC_MASTER, proc_dir);
    if (proc_latency)
        remove_proc_entry(PROC_LATENCY, proc_dir);
    if (proc_prime)
        remove_proc_entry(PROC_PRIME, proc_dir);
    if (proc_iow)
        remove_proc_entry(PROC_IOW, proc_dir);
    if (proc_dir)
        remove_proc_entry(PROC_DIR, NULL);

    pr_info("powersafe: module unloaded\n");
}

module_init(powersafe_init);
module_exit(powersafe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dtrail + Copilot");
MODULE_DESCRIPTION("Topology-aware power management with 4-state master toggle and prime boost");
