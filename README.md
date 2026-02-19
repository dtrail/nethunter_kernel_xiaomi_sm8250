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



# Upcoming features

# Chimera Familia: Doom Sleep Module
## Operational Guide & Troubleshooting (v5.0 - Stats Edition)

This kernel module and user-space controller implement the **Doom Sleep** logic for **SM8250** devices on **Android 14+**. It is a hybrid battery saver that combines a hard kernel-level wakelock filter with an intelligent daemon to silence aggressive background activity (primarily Google Mobile Services) while preserving essential hardware functions.

---

### 1. The Core Components

* **Kernel Hook:** Intercepts specific wakelocks at the source (`/sys/kernel/chimera_doom`).
* **In-Kernel Stats Engine:** Efficiently tracks blocked and allowed wakelocks at the kernel level without spamming `dmesg` or wasting CPU cycles.
* **Chimera Controller:** A background daemon that monitors screen state, applies "Doom Mode" or "Maintenance Windows" dynamically, and rotates logs.
* **User Whitelist:** A live-reloaded configuration file to protect specific hardware drivers (like Audio/Sensors) from being blocked.

---

### 2. CLI Tool: `chimera`

The module includes a helper tool for the terminal. You do **not** need to reboot to change settings.
Open a terminal (e.g., Termux), grant root access (`su`), and use the following commands:

#### Check System Status
Displays the Master Switch, Kernel Block status, Debug state, and Whitelist status.
> `chimera status`

#### Enable / Disable (Master Switch)
* **Disable:** Instantly stops all blocking and resets Google Services to default (Active). Use this for critical sync tasks, debugging, or banking apps.
* **Enable:** Reactivates the intelligent background monitoring.
> `chimera off`
> `chimera on`

#### Debug Mode
Enables verbose kernel logging to `dmesg`. Use this to see exactly which wakelocks are being blocked or allowed in real-time. *(Note: Regular stats are now handled via Markdown logs, so debug mode is only needed for deep troubleshooting).*
> `chimera debug on`
> `chimera debug off`

---

### 3. Statistics & Logging

Gone are the days of reading messy `dmesg` outputs. The Chimera Controller now automatically generates a beautiful Markdown table of your wakelock statistics.

* **Log Location:** `/sdcard/Chimera/logs/chimera_stats.md`
* **Log Rotation:** The controller automatically archives logs if they exceed 500KB and deletes archives older than **7 days**.

**Example Output:**

| Wakelock Name | Blocked (Total) | Allowed (Total) |
| :--- | :---: | :---: |
| *gms_scheduler* | **142** | 3 |
| sensor_ind | **0** | 85 |

---

### 4. Configuration (The Whitelist)

If an app (e.g., Audio Recorder, Sensors) stops working when the screen is off, you likely need to whitelist a driver wakelock.

* **Config File:** `/data/adb/chimera/whitelist.conf`
* **How to Edit:**
    1. Open the file with a root explorer or terminal editor (`nano` / `vi`).
    2. Remove the `#` from a line to **allow** that wakelock.
    3. Save the file.
    4. **No Reboot Needed:** The controller detects changes and updates the kernel within 10-30 seconds.

**Default Critical Whitelist (Audio Fix):**
Ensure these are enabled (no `#`) to prevent microphone deadlocks:
> `sensor_ind`
> `*mRoutingWakeLock*`

---

### 5. Logic & Behavior

| Screen State | Battery Saver | Kernel Blocker | GMS Bucket | Sync Interval |
| :--- | :--- | :--- | :--- | :--- |
| **ON** | Any | **OFF** (Allowed) | ACTIVE | N/A |
| **OFF** | OFF | **ON** (Blocked) | RESTRICTED | Every 60 min |
| **OFF** | ON | **ON** (Blocked) | RESTRICTED | Every 120 min |

* **Maintenance Window:** Every 60/120 minutes, the system wakes up for 60 seconds to allow notifications and syncs, then returns to Doom Sleep.
* **Burst Protection (Panic Mode):** If a blocked wakelock spams the kernel too aggressively (e.g., 50 times in 2 seconds), the kernel temporarily allows it for 5 seconds to prevent kernel panics and extreme CPU load.

---

### 6. Troubleshooting & Verification

#### Manual Verification
To manually verify that the kernel is receiving commands without using the CLI tool:

*Check Block State (1 = Blocking, 0 = Idle):*
> `cat /sys/kernel/chimera_doom/active`

*Check Kernel Stats Engine directly:*
> `cat /sys/kernel/chimera_doom/stats`

*Check GMS Standby Bucket (Expected: RESTRICTED when screen is off):*
> `dumpsys usagestats | grep -A 1 "com.google.android.gms"`

#### Common Errors
* **"Command not found":** Ensure you are running as Root (`su`). The binary is located at `/system/bin/chimera`.
* **Microphone/Audio stops working:** Ensure `*mRoutingWakeLock*` is uncommented in `/data/adb/chimera/whitelist.conf`.
* **"Text file busy" during manual update:** If updating the module manually via terminal, ensure you kill the running service first (`pkill -f chimera`). *(Note: The Magisk installer handles this automatically).*






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



