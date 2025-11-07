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
#include <linux/fs.h>
#include <linux/slab.h>

#define PROC_DIR "powersafe"
#define MAX_BUF_LEN 32

static struct pm_qos_request latency_req;
static struct proc_dir_entry *psafe_dir;

/* Track states */
static int latency_state = 0;          // 0 = default, 1 = 100µs
static int prime_state   = 1;          // 0 = default, 1 = 3.18GHz
static unsigned int io_weight_state = 0; // 0 = default, else custom
static int master_state  = 0;          // 0 = default, 1 = optimized
static int input_boost_state = 0;      // 0 = off, 1 = on

/* ---------- Input boost helpers ---------- */

static void write_sysfs(const char *path, const char *val)
{
    struct file *f;
    loff_t pos = 0;
    mm_segment_t oldfs;

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    f = filp_open(path, O_WRONLY, 0);
    if (!IS_ERR(f)) {
        kernel_write(f, val, strlen(val), &pos);
        filp_close(f, NULL);
    }

    set_fs(oldfs);
}

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

static void set_input_boost_param_str(const char *name, const char *val)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/module/cpu_input_boost/parameters/%s", name);
    write_sysfs(path, val);
}

static void set_input_boost_param_int(const char *name, int val)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    set_input_boost_param_str(name, buf);
}

/* Convenience setters */
static void configure_input_boost(int input_ms, int wake_ms,
                                  int little_boost, int big_boost, int prime_boost,
                                  int little_cap, int big_cap, int prime_cap,
                                  int little_min, int big_min, int prime_min)
{
    set_input_boost_param_int("input_boost_ms", input_ms);
    set_input_boost_param_int("wake_boost_ms",  wake_ms);

    set_input_boost_param_int("l_cluster_boost_freq",      little_boost);
    set_input_boost_param_int("b_cluster_boost_freq",      big_boost);
    set_input_boost_param_int("p_cluster_boost_freq",      prime_boost);

    set_input_boost_param_int("l_cluster_max_boost_freq",  little_cap);
    set_input_boost_param_int("b_cluster_max_boost_freq",  big_cap);
    set_input_boost_param_int("p_cluster_max_boost_freq",  prime_cap);

    set_input_boost_param_int("little_default_min_freq",   little_min);
    set_input_boost_param_int("big_default_min_freq",      big_min);
    set_input_boost_param_int("prime_default_min_freq",    prime_min);
}

/* Helper to swwitch govenror */
static int set_governor(const char *gov)
{
    struct file *f;
    loff_t pos = 0;
    char buf[32];
    int ret;

    snprintf(buf, sizeof(buf), "%s\n", gov);  // add newline

    f = filp_open("/sys/devices/system/cpu/cpufreq/policy7/scaling_governor",
                  O_WRONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    ret = kernel_write(f, buf, strlen(buf), &pos);
    filp_close(f, NULL);

    return (ret < 0) ? ret : 0;
}

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
        //policy->max = 3187200;
        policy->max = policy->cpuinfo.max_freq;
        prime_state = 1;
    } else {
        //policy->max = policy->cpuinfo.max_freq; // restore default
        policy->max = 2841600;  // capped
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

/* ---------- Input boost proc entries ---------- */

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

/* BORE parameters tweaking */

/* ---------- BORE parameters tweaking ---------- */
static void configure_cfs(long latency, long min_gran, long wakeup_gran,
                          int child_runs_first, int scaling)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%ld", latency);
    write_sysfs("/proc/sys/kernel/sched_latency_ns", buf);

    snprintf(buf, sizeof(buf), "%ld", min_gran);
    write_sysfs("/proc/sys/kernel/sched_min_granularity_ns", buf);

    snprintf(buf, sizeof(buf), "%ld", wakeup_gran);
    write_sysfs("/proc/sys/kernel/sched_wakeup_granularity_ns", buf);

    snprintf(buf, sizeof(buf), "%d", child_runs_first);
    write_sysfs("/proc/sys/kernel/sched_child_runs_first", buf);

    snprintf(buf, sizeof(buf), "%d", scaling);
    write_sysfs("/proc/sys/kernel/sched_tunable_scaling", buf);
}


static const struct file_operations input_boost_state_fops = {
    .owner  = THIS_MODULE,
    .read   = input_boost_state_read,
    .llseek = noop_llseek,
};

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

static const struct file_operations input_boost_fops = {
    .owner = THIS_MODULE,
    .write = input_boost_write,
    .llseek = noop_llseek,
};

/* ---------- Master toggle ---------- */
static ssize_t master_toggle_write(struct file *file, const char __user *ubuf,
                                   size_t count, loff_t *ppos)
{
    char kbuf[MAX_BUF_LEN];
    int new_state;
    struct cpufreq_policy *policy;

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
        configure_cfs(12000000, 3000000, 2000000, 0, 1);
        set_governor("schedutil");

        /* Restore prime max to default */
        policy = cpufreq_cpu_get(7);
        if (policy) {
            policy->max = policy->cpuinfo.max_freq;
            cpufreq_update_policy(policy->cpu);
            cpufreq_cpu_put(policy);
        }

        /* Input boost off, baseline mins */
        input_boost_state = 0;
        set_input_boost_enabled(false);
        configure_input_boost(
            0, 0,
            0, 0, 0,
            0, 0, 0,
            600000, 825600, 844800
        );

        pr_info("powersafe: state=0 (default)\n");
        break;

    case 1: /* Balanced */
        pm_qos_update_request(&latency_req, 200);
        latency_state = 1;
        io_weight_state = 250;
        prime_state = 0;
        configure_cfs(16000000, 4000000, 3000000, 0, 1);
	set_governor("schedutil");
	
        /* Prime left at default */
        policy = cpufreq_cpu_get(7);
        if (policy) {
            policy->max = policy->cpuinfo.max_freq;
            cpufreq_update_policy(policy->cpu);
            cpufreq_cpu_put(policy);
        }

        /* Input boost moderate */
        input_boost_state = 1;
        set_input_boost_enabled(true);
        configure_input_boost(
            58, 0,
            1708800, 1056000, 1401600,
            1804800, 2246400, 2553600,
            1171200, 825600, 844800
        );

        pr_info("powersafe: state=1 (balanced)\n");
        break;

    case 2: /* Performance + prime boost */
        pm_qos_update_request(&latency_req, 100);
        latency_state = 1;
        io_weight_state = 500;
        prime_state = 1;
        configure_cfs(12000000, 2000000, 1000000, 0, 1);
	set_governor("performance");

        /* Prime boosted to 3.12 GHz */
        policy = cpufreq_cpu_get(7);
        if (policy) {
            policy->max = 3120000;
            cpufreq_update_policy(policy->cpu);
            cpufreq_cpu_put(policy);
        }

        /* Input boost aggressive */
        input_boost_state = 1;
        set_input_boost_enabled(true);
        configure_input_boost(
            100, 1000,
            1708800, 2016000, 2841600,
            1804800, 2246400, 3120000,
            1171200, 1324800, 1804800
        );

        pr_info("powersafe: state=2 (performance, prime boost)\n");
        break;

    case 3: /* Powersafe */
        pm_qos_update_request(&latency_req, 400);
        latency_state = 1;
        io_weight_state = 100;
        prime_state = 0;
        configure_cfs(24000000, 6000000, 8000000, 0, 1);
        set_governor("powersave");

        /* Prime back to default */
        policy = cpufreq_cpu_get(7);
        if (policy) {
            policy->max = policy->cpuinfo.max_freq;
            cpufreq_update_policy(policy->cpu);
            cpufreq_cpu_put(policy);
        }

        /* Input boost disabled, conservative mins */
        input_boost_state = 0;
        set_input_boost_enabled(false);
        configure_input_boost(
            0, 0,
            0, 0, 0,
            0, 0, 0,
            600000, 825600, 844800
        );

        pr_info("powersafe: state=3 (powersafe)\n");
        break;

    default:
        return -EINVAL;
    }

    master_state = new_state;
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

    /* Existing entries */
    proc_create("latency_toggle", 0666, psafe_dir, &latency_toggle_ops);
    proc_create("prime_freq_boost", 0666, psafe_dir, &prime_freq_boost_ops);
    proc_create("io_weight", 0666, psafe_dir, &io_weight_ops);
    proc_create("master_toggle", 0666, psafe_dir, &master_toggle_ops);

    /* New input boost entries */
    proc_create("input_boost_state", 0444, psafe_dir, &input_boost_state_fops);
    proc_create("input_boost", 0222, psafe_dir, &input_boost_fops);

    pm_qos_add_request(&latency_req, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

    pr_info("powersafe-toggle loaded (with input boost)\n");
    return 0;
}

static void __exit powersafe_exit(void)
{
    pm_qos_remove_request(&latency_req);

    remove_proc_entry("latency_toggle", psafe_dir);
    remove_proc_entry("prime_freq_boost", psafe_dir);
    remove_proc_entry("io_weight", psafe_dir);
    remove_proc_entry("master_toggle", psafe_dir);
    remove_proc_entry("input_boost_state", psafe_dir);
    remove_proc_entry("input_boost", psafe_dir);
    remove_proc_entry(PROC_DIR, NULL);

    pr_info("powersafe-toggle unloaded\n");
}

module_init(powersafe_init);
module_exit(powersafe_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("dtrail & Copilot");
MODULE_DESCRIPTION("Powersafe Toggle Module with Master Switch, Input Boost, and Read Handlers");

