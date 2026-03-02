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

-------------------------------------


**Chimera Familia: Doom Sleep Module**
## Operational Guide & Troubleshooting (v6.1 - WebUI & Smart Engine Edition)

This kernel module and user-space controller implement the **Doom Sleep** logic for **SM8250** devices on **Android 14+**. It is a hybrid battery saver that combines a hard kernel-level wakelock filter with an intelligent daemon to silence aggressive background activity. 

**What's new in v6.1:** Introducing a fully integrated **WebUI** for KernelSU/Magisk, real-time wakelock tracking, and one-click blocking. The new **Smart Engine** intelligently pauses blocking during screen-off phone calls or media playback. The kernel engine now uses robust substring matching (no more `*` wildcards needed), and a new **Auto-Heal** mechanism automatically quarantines unstable wakelocks to prevent reboots.

---

### 1. The Core Components

* **Kernel Hook:** Intercepts specific wakelocks at the source (`/sys/kernel/chimera_doom`). By default, the kernel blocks *nothing* until told otherwise by the controller.
* **In-Kernel Stats Engine:** Efficiently tracks up to 128 unique system wakelocks (blocked and allowed) at the kernel level without spamming `dmesg` or wasting CPU cycles.
* **Chimera Controller:** A background daemon that monitors screen state, applies "Doom Mode", triggers "Maintenance Windows", and parses the user blocklist.
* **WebUI Dashboard:** A modern, built-in graphical interface. No extra APK required! Accessible directly through your root manager (KernelSU/Magisk) to monitor live stats and toggle wakelocks.
* **User Blocklist (Profiles):** A live-reloaded configuration file. Changes made via WebUI or text editor are applied automatically within seconds.

---

### 2. Control Interfaces: WebUI & CLI

You have two ways to interact with the module. You do **not** need to reboot to change settings.

#### A. The Graphical WebUI (Recommended)
Open your root manager (KernelSU or supported Magisk forks), navigate to your modules, and tap the **Action/Settings Icon** on the Chimera module. 
* View live wakelock statistics (Allowed vs. Blocked).
* Instantly Block/Allow wakelocks with a single tap.
* Add custom wakelocks manually.
* Toggle the Master Switch.

#### B. The CLI Tool: `chimera` (For Terminal & Tasker)
Open a terminal (e.g., Termux), grant root access (`su`), and use the following commands:
* `chimera status` - Displays the Master Switch, Kernel Block status, and current configuration.
* `chimera off` / `chimera on` - Instantly disables or enables the entire blocking engine.
* `chimera debug on` / `chimera debug off` - Enables verbose kernel logging to `dmesg` for deep troubleshooting.

---

### 3. Statistics & Logging

The module acts as a built-in, zero-battery-drain wakelock detector. The WebUI visualizes this data, but you can also access the raw logs:

* **Log Location:** `/data/adb/chimera/logs/chimera_stats.md`
* **Log Rotation:** The controller archives logs if they exceed 500KB and deletes archives older than **7 days**.

---

### 4. Configuration (The Blocklist)

The blocking logic is controlled by a simple text file at `/data/adb/chimera/blocklist.conf`.
*Note: The kernel uses smart substring matching. If you add `telemetry`, it will automatically block `qcom_telemetry_ws`. **Do not use wildcards (`*`).***

**Default Blocklist Layout (v6.1):**

# --- CRITICAL SYSTEM (Commented = ALLOWED) ---
# NEVER block these! Blocking will cause kernel panics/freezes.
# eventpoll
# alarmtimer
# [timerfd]

# --- HARDWARE & AUDIO (Commented = ALLOWED) ---
# Do NOT uncomment these unless you want broken audio in messenger apps!
# sensor_ind
# mRoutingWakeLock

# --- TELEMETRY & DIAGNOSTICS (Uncommented = BLOCKED) ---
# Safe to block. Stops Qualcomm/System data collection.
# DIAG_WS (Commented to ensure reliable phone calls)
telemetry
# mdm_stats
# logd
pdp_watchdog

# --- GOOGLE SERVICES (Uncommented = BLOCKED) ---
gms_scheduler
GcmSchedulerWakeupService
QosUploaderService
PayGcmTaskService
Google_C2DM
ChromeSync
SendReportAction

# --- EXPERIMENTAL NETWORK (Commented = ALLOWED) ---
# Uncomment for extreme battery, but might delay Push-Notifications!
# qcom_rx_wakelock
# wlan_pno_wl
# wlan_rx_wake
# IPA_WS


---

### 5. Logic & Behavior (Smart Engine)

| Screen State | Battery Saver | Kernel Blocker | Smart Detect | Sync Interval |
| :--- | :--- | :--- | :--- | :--- |
| **ON** | Any | **OFF** (Allowed) | N/A | N/A |
| **OFF** | OFF | **ON** (Blocked) | Active Calls / Media | Every 60 min |
| **OFF** | ON | **ON** (Blocked) | Active Calls / Media | Every 120 min |

* **Maintenance Window:** Every 60/120 minutes, the system wakes up for 60 seconds to allow notifications and syncs, then returns to Doom Sleep.
* **Smart Detection:** The controller detects active phone calls and media playback (e.g., Spotify) while the screen is off and temporarily disables the blocker to prevent audio stutter or dropped calls.
* **Auto-Heal (Burst Protection):** If a blocked wakelock spams the kernel too aggressively, the kernel triggers an emergency flag. The daemon detects this, instantly unblocks the wakelock, moves it to an `# --- EMERGENCY ENTRIES ---` section in your blocklist, and notes the intervention in your logs. Your device heals itself before a reboot can occur.

---

### 6. Troubleshooting & Verification

#### Manual Verification
To manually verify the kernel engine without using the CLI or WebUI:
* Check Block State (1 = Blocking, 0 = Idle): `cat /sys/kernel/chimera_doom/active`
* Check Kernel Stats Engine directly: `cat /sys/kernel/chimera_doom/stats`

#### Common Errors
* **"Command not found":** Ensure you are running as Root (`su`). The binary is located at `/system/bin/chimera`.
* **Microphone/Audio stops working:** You accidentally uncommented a hardware wakelock like `mRoutingWakeLock` in your config. Use the WebUI to allow it again!
* **Wakelocks show "0 Blocked" in the WebUI:** Ensure you did not use `*` wildcards in your custom names. The system uses simple partial matching (e.g., `gms_scheduler` is correct, `*gms_scheduler*` is wrong).






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




