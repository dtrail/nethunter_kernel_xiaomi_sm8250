/* This is an experimental module to toggle advanced powersaving parameters during runtime
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

/* F6: PM QoS CPU-Latenzgrenze */
static ssize_t latency_toggle_write(struct file *file, const char __user *buf,
                                    size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    if (copy_from_user(kbuf, buf, count)) return -EFAULT;

    if (kbuf[0] == '1')
        pm_qos_update_request(&latency_req, 100);  // Effizienz
    else
        pm_qos_update_request(&latency_req, 70);   // Interaktiv

    return count;
}
static const struct proc_ops latency_toggle_ops = {
    .proc_write = latency_toggle_write,
};

/* F9: PRIME-Kern Frequenzlimit (2.84 ↔ 3.18 GHz) */
static ssize_t prime_freq_boost_write(struct file *file, const char __user *buf,
                                      size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    unsigned int freq = 2841600;

    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    if (kbuf[0] == '0') freq = 3187200;

    struct cpufreq_policy *policy = cpufreq_cpu_get(7);
    if (policy) {
        cpufreq_verify_within_limits(policy, policy->min, freq);
        cpufreq_cpu_put(policy);
    }

    return count;
}
static const struct proc_ops prime_freq_boost_ops = {
    .proc_write = prime_freq_boost_write,
};

/* F11: I/O-Gewichtung (CGroup oder BFQ) */
static ssize_t io_weight_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    unsigned int weight;

    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    if (kstrtouint(kbuf, 10, &weight)) return -EINVAL;

    // Beispiel: Logging – tatsächliche Anwendung hängt vom System ab
    pr_info("powersafe-toggle: io.weight set to %u\n", weight);

    return count;
}
static const struct proc_ops io_weight_ops = {
    .proc_write = io_weight_write,
};

/* Modul-Init */
static int __init powersafe_init(void)
{
    psafe_dir = proc_mkdir(PROC_DIR, NULL);
    if (!psafe_dir) return -ENOMEM;

    proc_create("latency_toggle", 0666, psafe_dir, &latency_toggle_ops);
    proc_create("prime_freq_boost", 0666, psafe_dir, &prime_freq_boost_ops);
    proc_create("io_weight", 0666, psafe_dir, &io_weight_ops);

    pm_qos_add_request(&latency_req, PM_QOS_CPU_LATENCY, 70);  // Default

    pr_info("powersafe-toggle loaded\n");
    return 0;
}

/* Modul-Exit */
static void __exit powersafe_exit(void)
{
    pm_qos_remove_request(&latency_req);
    remove_proc_entry("latency_toggle", psafe_dir);
    remove_proc_entry("prime_freq_boost", psafe_dir);
    remove_proc_entry("io_weight", psafe_dir);
    remove_proc_entry(PROC_DIR, NULL);

    pr_info("powersafe-toggle unloaded\n");
}

module_init(powersafe_init);
module_exit(powersafe_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manuel & Copilot");
MODULE_DESCRIPTION("Eff-CPU Runtime Toggle Module");
