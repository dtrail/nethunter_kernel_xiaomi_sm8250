#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/jiffies.h>

/* --- KONFIGURATION --- */
// Auf 128 erhöht für besseres Profiling von System-Wakelocks!
#define MAX_TRACKED_LOCKS 128 

/* --- GLOBALE VARIABLEN --- */
static int doom_active = 0;
static int doom_debug = 0;
// Puffer auf 4096 Bytes erhöht, damit auch riesige User-Blocklisten passen
static char conf_blocklist[4096] = ""; 

/* Kernel Parameter Variablen */
static unsigned int conf_grace_ms = 2000;
static unsigned int conf_panic_duration_ms = 5000;
static unsigned int conf_burst_threshold = 50;
static unsigned int conf_burst_window_ms = 2000;

/* Interne Status Variablen */
static unsigned long doom_start_time = 0;
static bool panic_mode = false;
static unsigned long panic_end_time = 0;
static int spam_counter = 0;
static unsigned long last_block_time = 0;

/* --- STATISTIK STRUKTUR --- */
struct chimera_stat {
    char name[64];
    unsigned int count_blocked;
    unsigned int count_allowed;
    unsigned long last_hit_jiffies;
};

static struct chimera_stat stats_db[MAX_TRACKED_LOCKS];
static int stats_count = 0;

/* --- HELPER: STATISTIK UPDATE --- */
void update_stats(const char *name, bool blocked) {
    int i;
    unsigned long now = jiffies;

    // 1. Suche in existierender Liste
    for (i = 0; i < stats_count; i++) {
        if (strcmp(stats_db[i].name, name) == 0) {
            if (blocked) stats_db[i].count_blocked++;
            else stats_db[i].count_allowed++;
            stats_db[i].last_hit_jiffies = now;
            return;
        }
    }

    // 2. Neu anlegen, wenn Platz ist
    if (stats_count < MAX_TRACKED_LOCKS) {
        strlcpy(stats_db[stats_count].name, name, 64);
        if (blocked) stats_db[stats_count].count_blocked = 1;
        else stats_db[stats_count].count_allowed = 1;
        stats_db[stats_count].last_hit_jiffies = now;
        stats_count++;
    }
}

/* --- KERNLOGIK (INVERTIERT: Standard ist ERLAUBT) --- */
bool chimera_should_block(const char *name)
{
    unsigned long now = jiffies;

    // ==========================================================
    // BUG FIX: Verhindert leere [] Logs und CPU-Spam!
    // Ignoriere NULL, leere Strings oder Strings mit nur 1 Zeichen
    // ==========================================================
    if (!name || strlen(name) < 2) {
        return false; 
    }

    // Wenn Master-Switch aus, alles durchlassen
    if (!doom_active) return false;

    // --- 1. GRACE PERIOD ---
    // In den ersten Sekunden nach Screen Off sind wir gnädig
    if (jiffies_to_msecs(now - doom_start_time) < conf_grace_ms) {
        update_stats(name, false); 
        return false;
    }

    // --- 2. PANIC MODE CHECK ---
    if (panic_mode) {
        if (time_after(now, panic_end_time)) {
            panic_mode = false;
            spam_counter = 0;
            if (doom_debug) pr_err("CHIMERA-DEBUG: Panic Mode ENDED.\n");
        } else {
            update_stats(name, false);
            return false;
        }
    }

// --- 3. USER BLOCKLIST MATCHING ---
    if (conf_blocklist[0] != '\0') {
        bool match_found = false;
        int i = 0, t_len = 0;
        char token[128]; // Max Länge für einen einzelnen Wakelock-Namen
        
        // Kommagetrennte Liste sicher parsen (ohne den Kernel-Stack zu sprengen)
        while (conf_blocklist[i] != '\0') {
            if (conf_blocklist[i] == ',') {
                token[t_len] = '\0';
                if (t_len > 1 && strstr(name, token) != NULL) {
                    match_found = true; break;
                }
                t_len = 0;
            } else if (t_len < sizeof(token) - 1) {
                token[t_len++] = conf_blocklist[i];
            }
            i++;
        }
        // Letztes Token nach der Schleife prüfen
        if (!match_found && t_len > 1) {
            token[t_len] = '\0';
            if (strstr(name, token) != NULL) {
                match_found = true;
            }
        }

        // Wenn ein Match gefunden wurde -> BLOCKEN
        if (match_found) {
            
            // Burst Detection Logic
            unsigned long diff = jiffies_to_msecs(now - last_block_time);
            if (diff < conf_burst_window_ms) {
                spam_counter++;
            } else {
                spam_counter = 0;
            }
            last_block_time = now;

            if (spam_counter >= conf_burst_threshold) {
                panic_mode = true;
                panic_end_time = now + msecs_to_jiffies(conf_panic_duration_ms);
                spam_counter = 0;
                pr_crit("CHIMERA-EMERGENCY: %s\n", name); 
                
                if (doom_debug) pr_err("CHIMERA-DEBUG: ⚠️ BURST DETECTED! Triggering Panic. [%s]\n", name);
                update_stats(name, false);
                return false;
            }
            
            if (doom_debug) pr_err("CHIMERA-DEBUG: BLOCKED via User-List [%s]\n", name);
            update_stats(name, true);
            return true; 
        }
    }
EXPORT_SYMBOL(chimera_should_block);

/* --- SYSFS HANDLER --- */

// ACTIVE (Read/Write)
static ssize_t active_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", doom_active);
}
static ssize_t active_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int val;
    if (kstrtoint(buf, 10, &val) == 0) {
        if (val == 1 && doom_active == 0) doom_start_time = jiffies; // Timer Reset bei Screen Off
        doom_active = val;
    }
    return count;
}

// DEBUG (Read/Write)
static ssize_t debug_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", doom_debug);
}
static ssize_t debug_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int val;
    if (kstrtoint(buf, 10, &val) == 0) doom_debug = val;
    return count;
}

// BLOCKLIST (Read/Write - ehemals whitelist)
static ssize_t blocklist_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%s\n", conf_blocklist);
}
static ssize_t blocklist_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    if (count < sizeof(conf_blocklist)) {
        strlcpy(conf_blocklist, buf, sizeof(conf_blocklist));
        if (conf_blocklist[count-1] == '\n') conf_blocklist[count-1] = '\0';
    }
    return count;
}

// GRACE_MS (Read/Write)
static ssize_t grace_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%u\n", conf_grace_ms);
}
static ssize_t grace_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    kstrtouint(buf, 10, &conf_grace_ms);
    return count;
}

// PANIC_MS (Read/Write)
static ssize_t panic_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%u\n", conf_panic_duration_ms);
}
static ssize_t panic_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    kstrtouint(buf, 10, &conf_panic_duration_ms);
    return count;
}

// STATS (Read Only - Gibt Daten für die Markdown-Tabelle aus)
static ssize_t stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int i;
    int len = 0;
    
    // Header für den Controller zum Parsen
    len += sprintf(buf + len, "Name|Blocked|Allowed\n");
    
    for (i = 0; i < stats_count; i++) {
        // SICHERHEIT: Verhindere Buffer Overflow! 
        // Der Sysfs Buffer ist max PAGE_SIZE (ca. 4096 bytes). 
        // Wir brechen ab, bevor wir das Limit erreichen.
        if (len > 3800) break; 
        
        len += sprintf(buf + len, "%s|%u|%u\n", 
                       stats_db[i].name, 
                       stats_db[i].count_blocked, 
                       stats_db[i].count_allowed);
    }
    return len;
}

/* Attribute Definitionen */
static struct kobj_attribute active_attr = __ATTR(active, 0664, active_show, active_store);
static struct kobj_attribute debug_attr = __ATTR(debug, 0664, debug_show, debug_store);
static struct kobj_attribute blocklist_attr = __ATTR(blocklist, 0664, blocklist_show, blocklist_store);
static struct kobj_attribute grace_attr = __ATTR(grace_ms, 0664, grace_show, grace_store);
static struct kobj_attribute panic_attr = __ATTR(panic_ms, 0664, panic_show, panic_store);
static struct kobj_attribute stats_attr = __ATTR(stats, 0444, stats_show, NULL);

static struct attribute *chimera_attrs[] = {
    &active_attr.attr,
    &debug_attr.attr,
    &blocklist_attr.attr,
    &grace_attr.attr,
    &panic_attr.attr,
    &stats_attr.attr,
    NULL,
};

static struct attribute_group chimera_attr_group = {
    .attrs = chimera_attrs,
};

static struct kobject *chimera_kobj;

static int __init chimera_init(void)
{
    int retval;
    chimera_kobj = kobject_create_and_add("chimera_doom", kernel_kobj);
    if (!chimera_kobj) return -ENOMEM;

    retval = sysfs_create_group(chimera_kobj, &chimera_attr_group);
    if (retval) kobject_put(chimera_kobj);

    pr_info("Chimera Doom Module Loaded (v6.0 Profiles Edition)\n");
    return retval;
}

static void __exit chimera_exit(void)
{
    kobject_put(chimera_kobj);
    pr_info("Chimera Doom Module Unloaded\n");
}

module_init(chimera_init);
module_exit(chimera_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chimera Familia");
