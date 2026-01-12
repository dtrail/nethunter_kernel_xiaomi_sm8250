/*
 * drivers/misc/chimera_doom.c
 * Chimera Familia - Doom Sleep Implementation v3.1
 * * Features: 
 * - Hard Wakelock Blocking for GMS & System
 * - Smart Grace Period (Allow wakelocks briefly after screen off)
 * - Burst Protection (Auto-allow if an app spams wakelocks -> Panic Mode)
 * - User Whitelist via Sysfs (Runtime configurable exceptions)
 *
 * Target: SM8250 / Android 14
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/pm_wakeup.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h> 

// --- STATE VARIABLES ---
static bool doom_active = false;
static bool doom_debug = false;
static struct kobject *doom_kobj;
static unsigned long doom_start_time = 0;

// --- USER WHITELIST BUFFER ---
// Speichert die vom Userspace (Skript) übergebene Whitelist
static char conf_whitelist[1024] = ""; 

// --- BURST PROTECTION VARIABLES ---
static unsigned long last_block_time = 0; 
static int spam_counter = 0;              
static bool panic_mode = false;           
static unsigned long panic_end_time = 0;  

// --- KONFIGURATION (Defaults) ---
static unsigned int conf_grace_ms = 2000;       // 2s Gnadenfrist
static unsigned int conf_cycle_total_ms = 0;    // 0 = Aus (Dauerblock)
static unsigned int conf_cycle_allow_ms = 5000; 

// Burst Settings: Wenn 20 Blocks innerhalb von jeweils 100ms passieren -> 10s Panik
static unsigned int conf_burst_threshold = 20; 
static unsigned int conf_burst_window_ms = 100; 
static unsigned int conf_panic_duration_ms = 10000; 

// --------------------------------------------------------------------------
// CHIMERA HITLIST (Die "Bösen")
// --------------------------------------------------------------------------
static char *blocked_list[] = {
    // --- Google Play Services (GMS) ---
    "*gms_scheduler*", "GcmSchedulerWakeupService", "QosUploaderService",
    "PayGcmTaskService", "Google_C2DM", "ChromeSync", "*SendReportAction*",
    
    // --- System / Core ---
    "*SyncLoopWakeLock*", "*job_scheduler*", "*NetworkStats*", 
    "*LocationManagerService*", 
    
    // --- Hardware / Drivers ---
    "wlan_pno_wl", "sensor_ind", 
    "*mRoutingWakeLock*",      // NFC
    "*hal_bluetooth_lock*",    // Bluetooth
    
    // --- Qualcomm / Kernel ---
    "qcom_rx_wakelock", "*Rcu*",
    
    NULL
};

/* * KERNLOGIK: Soll blockiert werden?
 */
bool chimera_should_block(const char *name)
{
    int i = 0;
    unsigned long now = jiffies;
    unsigned long elapsed_ms;

    // Safety First
    if (!doom_active || !name) return false;

    // --- 1. USER WHITELIST CHECK (PRIORITÄT 1) ---
    // Wenn der Name in der User-Whitelist steht -> IMMER ERLAUBEN
    if (conf_whitelist[0] != '\0') {
        if (strstr(conf_whitelist, name)) {
            // Optional: Debugging für Whitelist
            // if (doom_debug) pr_info_ratelimited("Chimera: Allowed via Whitelist [%s]\n", name);
            return false; 
        }
    }

    // --- 2. PANIC MODE CHECK (Selbstheilung) ---
    if (panic_mode) {
        if (time_after(now, panic_end_time)) {
            panic_mode = false;
            spam_counter = 0;
            if (doom_debug) pr_info("Chimera Doom: Panic Mode ENDED. Resuming block.\n");
        } else {
            return false; // Alles durchlassen während Panik
        }
    }

    // --- 3. GRACE PERIOD CHECK ---
    if (jiffies_to_msecs(now - doom_start_time) < conf_grace_ms) return false; 

    // --- 4. DUTY CYCLE CHECK ---
    if (conf_cycle_total_ms > 0) {
        elapsed_ms = jiffies_to_msecs(now - doom_start_time);
        if ((elapsed_ms % conf_cycle_total_ms) >= (conf_cycle_total_ms - conf_cycle_allow_ms)) {
            spam_counter = 0; 
            return false; 
        }
    }

    // --- 5. BLACKLIST MATCHING ---
    while (blocked_list[i]) {
        if (strstr(name, blocked_list[i])) {
            
            // --- BURST DETECTION ---
            unsigned long diff = jiffies_to_msecs(now - last_block_time);
            
            if (diff < conf_burst_window_ms) {
                spam_counter++;
            } else {
                spam_counter = 0; // Reset bei Pause
            }
            last_block_time = now;

            // Panic Trigger?
            if (spam_counter >= conf_burst_threshold) {
                panic_mode = true;
                panic_end_time = now + msecs_to_jiffies(conf_panic_duration_ms);
                spam_counter = 0;
                
                if (doom_debug) {
                    pr_info("Chimera Doom: ⚠️ BURST DETECTED! Panic Mode for %d ms. Allowing: %s\n", 
                            conf_panic_duration_ms, name);
                }
                return false; // Diesmal erlauben!
            }
            // -----------------------

            if (doom_debug) {
                 pr_info_ratelimited("Chimera Doom: BLOCKED [%s] (Rule: %s)\n", name, blocked_list[i]);
            }
            return true; // BLOCKIEREN
        }
        i++;
    }
    return false;
}

// --------------------------------------------------------------------------
// SYSFS HANDLING
// --------------------------------------------------------------------------

// --- Active Switch ---
static ssize_t active_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", doom_active);
}
static ssize_t active_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret, val;
    ret = kstrtoint(buf, 10, &val);
    if (ret < 0) return ret;
    bool new_state = (val != 0);
    if (new_state && !doom_active) doom_start_time = jiffies; // Timer Reset
    doom_active = new_state;
    return count;
}
static struct kobj_attribute active_attr = __ATTR(active, 0644, active_show, active_store);

// --- Whitelist (String) ---
static ssize_t whitelist_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%s\n", conf_whitelist);
}
static ssize_t whitelist_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    // Safe Copy
    snprintf(conf_whitelist, sizeof(conf_whitelist), "%s", buf);
    // Remove Trailing Newline (wichtig für strstr)
    size_t len = strlen(conf_whitelist);
    if (len > 0 && conf_whitelist[len-1] == '\n') conf_whitelist[len-1] = '\0';
    
    if (doom_debug) pr_info("Chimera: New Whitelist loaded: [%s]\n", conf_whitelist);
    return count;
}
static struct kobj_attribute whitelist_attr = __ATTR(whitelist, 0644, whitelist_show, whitelist_store);

// --- Config Values (UInts) ---
// Helper Makro um Tipparbeit zu sparen
#define CHIMERA_ATTR_UINT(_name, _var) \
static ssize_t _name##_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) { \
    return sprintf(buf, "%u\n", _var); \
} \
static ssize_t _name##_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) { \
    unsigned int val; \
    if (kstrtouint(buf, 10, &val) < 0) return -EINVAL; \
    _var = val; \
    return count; \
} \
static struct kobj_attribute _name##_attr = __ATTR(_name, 0644, _name##_show, _name##_store);

CHIMERA_ATTR_UINT(debug, doom_debug)
CHIMERA_ATTR_UINT(grace_ms, conf_grace_ms)
CHIMERA_ATTR_UINT(cycle_total_ms, conf_cycle_total_ms)
CHIMERA_ATTR_UINT(cycle_allow_ms, conf_cycle_allow_ms)
CHIMERA_ATTR_UINT(panic_ms, conf_panic_duration_ms)

// --- Attribute Group ---
static struct attribute *doom_attrs[] = {
    &active_attr.attr,
    &debug_attr.attr,
    &whitelist_attr.attr,
    &grace_ms_attr.attr,
    &cycle_total_ms_attr.attr,
    &cycle_allow_ms_attr.attr,
    &panic_ms_attr.attr,
    NULL,
};

static struct attribute_group doom_attr_group = { .attrs = doom_attrs };

static int __init chimera_init(void) {
    int error;
    doom_kobj = kobject_create_and_add("chimera_doom", kernel_kobj);
    if (!doom_kobj) return -ENOMEM;
    error = sysfs_create_group(doom_kobj, &doom_attr_group);
    if (error) kobject_put(doom_kobj);
    return error;
}

static void __exit chimera_exit(void) {
    kobject_put(doom_kobj);
}

module_init(chimera_init);
module_exit(chimera_exit);