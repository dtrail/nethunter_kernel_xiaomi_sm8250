Apollo / Alioth N0tHunter Kernel [4.19][AOSP]
=============================================

- For Kali NetHunter 2025.X
- Tested on Android 13 - 16 AOSP-Roms (specifically tested on LineageOS 21 - 22.2)
- Based on N0Kernel by EmanuelCN
- Supports Magisk ONLY (*NetHunter doesn't work properly with KSU, APatch or any other root solution, no mtter the version.)
- Supports all officially supported external devices (USB WI-FI adapters, HID devices, BT devices, CAN, etc.)
- Additional support for non-standard Realtek USB adapters (88xx, 8188eus, etc.)
- Full support for CAN-Arsenal, USB-Arsenal, HID-Attacks
- MIUI is possible, but UNTESTED: You need Emanuel's modified N0Kernel DTBO Image for MIUI, which you can get on his Telegram channel (N0Kernel releases). 
----> However, it might induce instability, because I did not build that DTBO together with the kernel. Since I'm not using MIUI, I probably won't support it later. 
        *Not to mention that MIUI is not a recommended rom for use with NetHunter.


NOTE: as of switching to another source, I've stopped porting it to LOS sources, because the most fitting one being the LOS 21 kernel. That source is outdated and I cannot guarantee compatibility to other android versions and Roms. Most other Apollo kernels are based on N0Kernel anyway. So, I'll stick to the current one for now, but I'll continue developing additional modules and a new power-efficiency system, which is loosely based on the logic of my old JBX-Kernel for the Moto RAZR (2012),to get better battery backup and performance gains, depending on users preference. 


Feedback is always appreciated. You can get support on the XDA-Thread here: https://xdaforums.com/t/kernel-4-19-a13-apollo-kali-nethunter-n0thunter-kernel-12-13.4703051/

Happy hunting ;-)


# CONFIGURATIONS SECTION

*Here you will find certain extra functions and features, that I've added - or will add in the future - and how to make use of them.


**Scheduler Hysteresis**

*Read more: https://pastebin.com/LPTzTDAT

Use Termux, Sysctl GUI or FKM (or whatever you want) to set:

```bash
"hyst_mode"
```
0 - Balanced (Default)

1 - Performance

2 - Battery

3 - Off (Fully disable scheduler hysteresis)


shell:
/* replace X with number from above */

```bash
echo X > /proc/sys/kernel/hyst_mode
```

Btw, if using FKM: don't touch this parameter: /proc/sys/kernel/sched_busy_hysteresis_enable_cpus !
FKM misinterpreted it as a binary toggle, so "0" and "1" don't work here and the status given by the app is wrong.

-------------------------------------


**Powersafe Toggle**

*Change values:*

*the module manages several parameters: IO-Weight, CPU Latency, Input Boost, Max Freq (superseded by other tweaks, like FDE.AI, scripts, etc.)

Profile | echo  | target path | Main result
--------           |------            |-------------                                  |--------------
Default           | 0                  | /proc/powersafe/master_toggle | Kernel default values (default)
Balanced        | 1                  | /proc/powersafe/master_toggle | Balanced performance / battery-life
Performance  | 2                  | /proc/powersafe/master_toggle | Boost Prime Core
Battery           | 3                  | /proc/powersafe/master_toggle | All parameters tweaked for best power savings, input boost disabled
-----------


*Finetuning*

You can cat/echo to the following parameters, if needed. But **beware**, these are controlled by the master_toggle!
Don't change them blindly!

- input_boost
- input_boost_state
-  io_weight
-  latency_toggle
-  prime_boost_freq


NOTE: By default, the kernel provides great performance and balanced energy consumption. 
You can add the master to FKM tiles and switch between states by using Android's quick tiles.



**Chimera Familia: Doom Sleep Module**
## Operational Guide & Troubleshooting (v6.0 - Profiles & Stats Edition)

This kernel module and user-space controller implement the **Doom Sleep** logic for **SM8250** devices on **Android 14+**. It is a hybrid battery saver that combines a hard kernel-level wakelock filter with an intelligent daemon to silence aggressive background activity. 

**What's new in v6.0:** The kernel is now a pure execution engine. All blocking logic has been moved to a user-configurable **Blocklist**. By commenting or uncommenting lines, users can create custom battery-saving profiles without recompiling the kernel. Furthermore, Wakelock statistics are now safely stored in the Magisk directory.

---

### 1. The Core Components

* **Kernel Hook:** Intercepts specific wakelocks at the source (`/sys/kernel/chimera_doom`). By default, the kernel blocks *nothing* until told otherwise by the controller.
* **In-Kernel Stats Engine:** Efficiently tracks up to 128 unique blocked and allowed wakelocks at the kernel level without spamming `dmesg` or wasting CPU cycles.
* **Chimera Controller:** A background daemon that monitors screen state, applies "Doom Mode", triggers "Maintenance Windows", and parses the user blocklist.
* **User Blocklist (Profiles):** A live-reloaded configuration file. Users can easily swap profiles (e.g., Extreme Battery vs. Light Gaming) by replacing this text file.

---

### 2. CLI Tool: `chimera`

The module includes a helper tool for the terminal. You do **not** need to reboot to change settings.
Open a terminal (e.g., Termux), grant root access (`su`), and use the following commands:

#### Check System Status
Displays the Master Switch, Kernel Block status, Debug state, and current Blocklist configuration.
> `chimera status`

#### Enable / Disable (Master Switch)
* **Disable:** Instantly stops all blocking. The kernel lets everything pass. Use this for critical sync tasks, debugging, or banking apps.
* **Enable:** Reactivates the intelligent background monitoring and applies the Blocklist.
> `chimera off`
> `chimera on`

#### Debug Mode
Enables verbose kernel logging to `dmesg`. Use this only for deep troubleshooting. Regular statistics are handled via Markdown logs.
> `chimera debug on`
> `chimera debug off`

---

### 3. Statistics & Logging

The Chimera Controller automatically generates a clean Markdown table of your wakelock statistics. It acts as a built-in, zero-battery-drain wakelock detector!

* **Log Location:** `/data/adb/chimera/logs/chimera_stats.md`
* **Log Rotation:** The controller archives logs if they exceed 500KB and deletes archives older than **7 days**.

**Example Output:**

| Wakelock Name | Blocked (Total) | Allowed (Total) |
| :--- | :---: | :---: |
| DIAG_WS | **142** | 3 |
| eventpoll | **0** | 2197 |

---

### 4. Configuration (The Blocklist)

The entire blocking logic is controlled by a simple text file. 

* **Config File:** `/data/adb/chimera/blocklist.conf`
* **How to Edit / Create Profiles:**
    1. Open the file with a root explorer or terminal editor (`nano` / `vi`).
    2. **Uncommented** (No `#`): The wakelock will be **BLOCKED** during Deep Sleep.
    3. **Commented** (With `#`): The wakelock will be **ALLOWED** (Ignored by the blocker).
    4. Save the file. The controller detects changes and updates the kernel within 10-30 seconds. No reboot needed!

**Default Blocklist Layout:**
```text
# --- CRITICAL SYSTEM (Commented = ALLOWED) ---
# NEVER block these! Blocking will cause kernel panics/freezes.
# eventpoll
# alarmtimer
# [timerfd]

# --- HARDWARE & AUDIO (Commented = ALLOWED) ---
# Do NOT uncomment these!  tests revealed, that It will cause the microphone to not work after 2-3 days (only in messenger apps. Phone calls continue working. Technically those apps receive the mic in locked state due to blocking.)
# sensor_ind
# *mRoutingWakeLock*

# --- TELEMETRY & DIAGNOSTICS (Uncommented = BLOCKED) ---
# Safe to block. Stops Qualcomm/System data collection.
DIAG_WS
*telemetry*
*mdm_stats*
*logd*
pdp_watchdog

# --- GOOGLE SERVICES (Uncommented = BLOCKED) ---
*gms_scheduler*
GcmSchedulerWakeupService
QosUploaderService
PayGcmTaskService
Google_C2DM
ChromeSync
*SendReportAction*

# --- EXPERIMENTAL NETWORK (Commented = ALLOWED) ---
# Uncomment for extreme battery, but might delay Push-Notifications!
# qcom_rx_wakelock
# wlan_pno_wl
# wlan_rx_wake
# IPA_WS
```

*Tip: You can create different files like `extreme.conf` and `gaming.conf` and copy them to `blocklist.conf` to switch profiles on the fly!*

---

### 5. Logic & Behavior

| Screen State | Battery Saver | Kernel Blocker | GMS Bucket | Sync Interval |
| :--- | :--- | :--- | :--- | :--- |
| **ON** | Any | **OFF** (Allowed) | ACTIVE | N/A |
| **OFF** | OFF | **ON** (Blocked) | RESTRICTED | Every 60 min |
| **OFF** | ON | **ON** (Blocked) | RESTRICTED | Every 120 min |

* **Maintenance Window:** Every 60/120 minutes, the system wakes up for 60 seconds to allow notifications and syncs, then returns to Doom Sleep.
* **Burst Protection (Panic Mode):** If a blocked wakelock spams the kernel too aggressively (e.g., 50 times in 2 seconds), the kernel temporarily allows it for 5 seconds to prevent kernel panics and extreme CPU load. The system heals itself automatically.

---

### 6. Troubleshooting & Verification

#### Manual Verification
To manually verify the kernel engine without using the CLI tool:

*Check Block State (1 = Blocking, 0 = Idle):*
> `cat /sys/kernel/chimera_doom/active`

*Check Kernel Stats Engine directly:*
> `cat /sys/kernel/chimera_doom/stats`

*Check active Blocklist directly in Kernel:*
> `cat /sys/kernel/chimera_doom/blocklist`

#### Common Errors
* **"Command not found":** Ensure you are running as Root (`su`). The binary is located at `/system/bin/chimera`.
* **Microphone/Audio stops working:** You accidentally uncommented a hardware wakelock like `*mRoutingWakeLock*` in your `blocklist.conf`. Add the `#` back!

* **Where are my logs?** The markdown table is located in the Magisk directory (`/data/adb/chimera/logs/`) to prevent Android storage permission issues. Use a root explorer to view them.






# DOWNLOAD

STABLE: https://github.com/dtrail/nethunter_kernel_xiaomi_sm8250/releases



# CREDITS


**Credits to:

        EmanuelCN (N0Kernel)
        
        kvsnr113 (E404)
        
        XDA-Forums







Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.
See Documentation/00-INDEX for a list of what is contained in each file.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.



