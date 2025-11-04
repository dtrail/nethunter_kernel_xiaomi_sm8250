/* Experimental module to toggle advanced powersaving parameters during runtime
 *
 * (c) dtrail/Godis
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#define PROC_DIR "powersafe"
#define MAX_BUF_LEN 32

static struct pm_qos_request latency_req;
static struct proc_dir_entry *psafe_dir;

/* Track states */
static int latency_state = 0;     // 0 = default, 1 = 100µs
static int prime_state   = 0;     // 0 = default, 1 = 2.84GHz
static unsigned int io_weight_state = 0; // 0 = default, else custom
static int master_state  = 0;     // 0 = default, 1 = optimized

/* ---------- Latency toggle ---------- */
static ssize_t latency_toggle_write(struct file *file, const char __user *buf,
                                    size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    if (kbuf[0] == '1') {
        pm_qos_update_request(&latency_req, 100);
        latency_state = 1;
    } else {
        pm_qos_update_request(&latency_req, PM_QOS_DEFAULT_VALUE);
        latency_state = 0;
    }

    return count;
}

static ssize_t latency_toggle_read(struct file *file, char __user *buf,
                                   size_t count, loff_t *ppos)
{
    char kbuf[8];
    int len = snprintf(kbuf, sizeof(kbuf), "%d\n", latency_state);
    return simple_read_from_buffer(buf, count, ppos, kbuf, len);
}

static const struct file_operations latency_toggle_ops = {
    .owner = THIS_MODULE,
    .write = latency_toggle_write,
    .read  = latency_toggle_read,
};

/* ---------- PRIME frequency toggle ---------- */
static ssize_t prime_freq_boost_write(struct file *file, const char __user *buf,
                                      size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    struct cpufreq_policy *policy;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    policy = cpufreq_cpu_get(7);
    if (!policy)
        return count;

    if (kbuf[0] == '1') {
        policy->max = 2841600;
        prime_state = 1;
    } else {
        policy->max = policy->cpuinfo.max_freq; // restore default
        prime_state = 0;
    }
    cpufreq_update_policy(policy->cpu);
    cpufreq_cpu_put(policy);

    return count;
}

static ssize_t prime_freq_boost_read(struct file *file, char __user *buf,
                                     size_t count, loff_t *ppos)
{
    char kbuf[16];
    int len = snprintf(kbuf, sizeof(kbuf), "%d\n", prime_state);
    return simple_read_from_buffer(buf, count, ppos, kbuf, len);
}

static const struct file_operations prime_freq_boost_ops = {
    .owner = THIS_MODULE,
    .write = prime_freq_boost_write,
    .read  = prime_freq_boost_read,
};

/* ---------- I/O weight toggle ---------- */
static ssize_t io_weight_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    unsigned int weight;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    if (kstrtouint(kbuf, 10, &weight))
        return -EINVAL;

    io_weight_state = weight;
    pr_info("powersafe-toggle: io.weight set to %u\n", weight);

    return count;
}

static ssize_t io_weight_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    char kbuf[16];
    int len = snprintf(kbuf, sizeof(kbuf), "%u\n", io_weight_state);
    return simple_read_from_buffer(buf, count, ppos, kbuf, len);
}

static const struct file_operations io_weight_ops = {
    .owner = THIS_MODULE,
    .write = io_weight_write,
    .read  = io_weight_read,
};

/* ---------- Master toggle ---------- */
static ssize_t master_toggle_write(struct file *file, const char __user *buf,
                                   size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    struct cpufreq_policy *policy;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    if (kbuf[0] == '1') {
        /* Optimized profile */
        pm_qos_update_request(&latency_req, 100);
        latency_state = 1;

        policy = cpufreq_cpu_get(7);
        if (policy) {
            policy->max = 2841600;
            cpufreq_update_policy(policy->cpu);
            cpufreq_cpu_put(policy);
        }
        prime_state = 1;

        io_weight_state = 500;
        pr_info("powersafe-toggle: io.weight set to 500\n");

        master_state = 1;
    } else {
        /* Default profile: restore system defaults */
        pm_qos_update_request(&latency_req, PM_QOS_DEFAULT_VALUE);
        latency_state = 0;

        policy = cpufreq_cpu_get(7);
        if (policy) {
            policy->max = policy->cpuinfo.max_freq;
            cpufreq_update_policy(policy->cpu);
            cpufreq_cpu_put(policy);
        }
        prime_state = 0;

        io_weight_state = 0;
        pr_info("powersafe-toggle: io.weight reset to default\n");

        master_state = 0;
    }

    return count;
}

static ssize_t master_toggle_read(struct file *file, char __user *buf,
                                  size_t count, loff_t *ppos)
{
    char kbuf[8];
    int len = snprintf(kbuf, sizeof(kbuf), "%d\n", master_state);
    return simple_read_from_buffer(buf, count, ppos, kbuf, len);
}

static const struct file_operations master_toggle_ops = {
    .owner = THIS_MODULE,
    .write = master_toggle_write,
    .read  = master_toggle_read,
};

/* ---------- Module init/exit ---------- */
static int __init powersafe_init(void)
{
    psafe_dir = proc_mkdir(PROC_DIR, NULL);
    if (!psafe_dir)
        return -ENOMEM;

    proc_create("latency_toggle", 0666, psafe_dir, &latency_toggle_ops);
    proc_create("prime_freq_boost", 0666, psafe_dir, &prime_freq_boost_ops);
    proc_create("io_weight", 0666, psafe_dir, &io_weight_ops);
    proc_create("master_toggle", 0666, psafe_dir, &master_toggle_ops);

    pm_qos_add_request(&latency_req, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

    pr_info("powersafe-toggle loaded\n");
    return 0;
}

static void __exit powersafe_exit(void)
{
    pm_qos_remove_request(&latency_req);
    remove_proc_entry("latency_toggle", psafe_dir);
    remove_proc_entry("prime_freq_boost", psafe_dir);
    remove_proc_entry("io_weight", psafe_dir);
    remove_proc_entry("master_toggle", psafe_dir);
    remove_proc_entry(PROC_DIR, NULL);

    pr_info("powersafe-toggle unloaded\n");
}

module_init(powersafe_init);
module_exit(powersafe_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manuel & Copilot");
MODULE_DESCRIPTION("Eff-CPU Runtime Toggle Module with Master Switch and Read Handlers");
