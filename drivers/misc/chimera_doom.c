/*
 * drivers/misc/chimera_doom.c
 * Chimera Familia - Doom Sleep Implementation
 * Target: SM8250 / Android 14
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>

// --- POWER MANAGEMENT HEADERS (order is important here!) ---
// 1. Defines 'struct device', which pm_wakeup.h requires
#include <linux/device.h> 

// 2. Base PM definitions. Sometimes asks for pm_wakeup, so include it here first
#include <linux/pm.h>

// 3. Suspend Notifications
#include <linux/suspend.h>

// 4. Now load pm_wakelock.h here, 
// silencing "don't include directly" guards.
#include <linux/pm_wakeup.h>

// --- SYSFS HEADERS ---
#include <linux/kobject.h>
#include <linux/sysfs.h>


// --- STATE VARIABLES ---
static bool doom_active = false;
static bool doom_debug = false; // NEU: Debug Toggle
static struct kobject *doom_kobj;

// --------------------------------------------------------------------------
// CHIMERA HITLIST
// --------------------------------------------------------------------------
static char *blocked_list[] = {
    // --- Google Play Services (GMS) ---
    "*gms_scheduler*",           // Der Hauptfeind (Heartbeat)
    "GcmSchedulerWakeupService", // GCM Cloud Messaging
    "QosUploaderService",        // Telemetrie
    "PayGcmTaskService",         // Google Pay
    "Google_C2DM",               // Push-Heartbeat
    "ChromeSync",                // Sync Manager

    // --- System / Android Core ---
    "*SyncLoopWakeLock*",        // Android Sync Manager
    "wlan_pno_wl",               // WLAN Scans
    "sensor_ind",                // Sensoren

    // --- Google Apps Specific ---
    "*SendReportAction*",        // Google Messages Telemetrie
    
    // --- Qualcomm Specific ---
    "qcom_rx_wakelock",          // Datenpaket-Empfang
    
    NULL // Terminator
};
// --------------------------------------------------------------------------

/* Exportierte Funktion für wakeup.c */
bool chimera_should_block(const char *name)
{
    int i = 0;

    // Safety checks
    if (!doom_active || !name) 
        return false;

    while (blocked_list[i]) {
        // strstr prüft Substring Match
        if (strstr(name, blocked_list[i])) {
            
            // --- NEU: DEBUG LOGGING ---
            if (doom_debug) {
                // Ratelimited printk: Verhindert Log-Spamming und Lag
                pr_info_ratelimited("Chimera Doom: BLOCKED wakelock [%s] (Rule: %s)\n", 
                                    name, blocked_list[i]);
            }
            // --------------------------

            return true; // Treffer -> Blockieren
        }
        i++;
    }
    return false;
}

// --------------------------------------------------------------------------
// SYSFS INTERFACE
// --------------------------------------------------------------------------

// 1. ACTIVE NODE
static ssize_t active_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", doom_active);
}

static ssize_t active_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int ret, val;
    ret = kstrtoint(buf, 10, &val);
    if (ret < 0) return ret;
    doom_active = (val != 0);
    return count;
}
static struct kobj_attribute active_attr = __ATTR(active, 0644, active_show, active_store);

// 2. DEBUG NODE (NEU)
static ssize_t debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", doom_debug);
}

static ssize_t debug_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    int ret, val;
    ret = kstrtoint(buf, 10, &val);
    if (ret < 0) return ret;
    doom_debug = (val != 0);
    
    if (doom_debug)
        pr_info("Chimera Doom: Debug Logging ENABLED\n");
    else
        pr_info("Chimera Doom: Debug Logging DISABLED\n");
        
    return count;
}
static struct kobj_attribute debug_attr = __ATTR(debug, 0644, debug_show, debug_store);

// Attribute Group zusammenfassen (Sauberer Kernel-Stil)
static struct attribute *doom_attrs[] = {
    &active_attr.attr,
    &debug_attr.attr,
    NULL,
};

static struct attribute_group doom_attr_group = {
    .attrs = doom_attrs,
};

static int __init chimera_init(void)
{
    int error;
    
    // Erstelle Verzeichnis /sys/kernel/chimera_doom
    doom_kobj = kobject_create_and_add("chimera_doom", kernel_kobj);
    if (!doom_kobj) return -ENOMEM;

    // Erstelle die Gruppe (active + debug)
    error = sysfs_create_group(doom_kobj, &doom_attr_group);
    if (error) {
        kobject_put(doom_kobj);
    }
    
    return error;
}

static void __exit chimera_exit(void)
{
    kobject_put(doom_kobj);
}

module_init(chimera_init);
module_exit(chimera_exit);
