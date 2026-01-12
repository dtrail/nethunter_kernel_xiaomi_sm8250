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
**Version 4.0 (Smart Edition)**

## Operational Guide & Troubleshooting

This kernel features the **Doom Sleep Module**, a hybrid solution for **SM8250** on **Android 14+**. It combines a sophisticated kernel-level wakelock filter with an intelligent user-space controller to silence aggressive GMS (Google Mobile Services) background activity while maintaining system stability via self-healing logic.

---

### 1. The Core Components

* **Smart Kernel Hook:** Intercepts specific wakelocks (e.g., `*gms_scheduler*`, `*mRoutingWakeLock*`) at the source.
    * **Grace Period:** Allows wakelocks for the first **2 seconds** after screen-off to allow apps to finish tasks cleanly.
    * **Burst Protection (Panic Mode):** If a blocked app "spams" wakelocks (Retry Storm), the kernel temporarily disables the blocker for **10 seconds** to prevent CPU spikes and battery drain.
* **Chimera Controller:** A background daemon that monitors screen state, handles maintenance intervals, and **live-reloads** your whitelist configuration.
* **User Config:** A simple text file to allow specific wakelocks without rebooting.

---

### 2. Configuration: The Whitelist
You no longer need to edit scripts to allow specific wakelocks.

1.  Navigate to: `/data/adb/chimera/whitelist.conf` (using a Root Explorer).
2.  Open the file. You will see a list of known wakelocks commented out with `#`.
3.  **To allow a wakelock:** Remove the `#` at the start of the line.
4.  **Save the file.**
5.  **Done.** The controller detects the change and updates the kernel automatically within 10–30 seconds. **No reboot required.**

---

### 3. CLI Tool: `chimera`
Interact with the system via the built-in terminal tool. Open any terminal emulator (as **root**) and use the following commands:

**Check Status**
Displays the Master Toggle state, Kernel Blocker status, and loaded Debug/Whitelist info.
```bash
chimera status
```

**Disable System**
(Master Kill-Switch) If you need to debug or ensure 100% sync (e.g., for banking or urgent notifications), use this to stop all restrictions immediately.
```bash
chimera off
```

**Enable System**
Reactivates the intelligent monitoring and the Doom Sleep logic.
```bash
chimera on
```

**Debug Mode**
Enables kernel logging to `dmesg`.
```bash
chimera debug on
chimera debug off
```

---

### 4. Manual Verification (Deep Dive)
If you want to verify that the logic is actually working, you can check the nodes directly:

**Verify Kernel Blocker**
Check if the kernel is currently armed (1 = Blocking, 0 = Allowed):
```bash
cat /sys/kernel/chimera_doom/active
```

**Verify Loaded Whitelist**
See which exceptions the kernel has currently loaded from your config file:
```bash
cat /sys/kernel/chimera_doom/whitelist
```

**Verify GMS Standby State**
Check which "Bucket" Android has assigned to Google Play Services:
```bash
dumpsys usagestats | grep -A 1 "com.google.android.gms"
```
* `RESTRICTED`: Doom Sleep is active. GMS is heavily throttled.
* `ACTIVE`: Screen is on, Grace Period is active, or Maintenance Window is open.

---

### 5. Logic Table

| Screen State | Battery Saver | Kernel State | GMS Bucket | Note |
| :--- | :--- | :--- | :--- | :--- |
| **ON** | Any | **Allowed** | ACTIVE | System behaves normally. |
| **OFF** (< 2s) | Any | **Allowed** | RESTRICTED | **Grace Period:** Apps can finish "Good Night" tasks. |
| **OFF** (> 2s) | OFF | **BLOCKED** | RESTRICTED | **Doom Mode:** Sync allowed every 60m. |
| **OFF** (> 2s) | ON | **BLOCKED** | RESTRICTED | **Doom Mode:** Sync allowed every 120m. |
| **OFF** (Panic) | Any | **Allowed** | RESTRICTED | **Burst Protection:** Temp. unblock (10s) if app spams. |

---

### 6. Troubleshooting

**I don't see any blocks in the logs:**
1. Enable debug: `chimera debug on`
2. Turn screen off and wait at least **5 seconds** (to pass the Grace Period).
3. Check logs:
   ```bash
   dmesg -w | grep "Chimera"
   ```
   * Look for: `Chimera Doom: BLOCKED [...]`
   * Look for: `Chimera Doom: ⚠️ BURST DETECTED!` (If Panic Mode triggers).

**Notifications are delayed:**
This is expected behavior during Deep Sleep. If it is too aggressive for your usage:
1.  Find the wakelock responsible for the app (via BetterBatteryStats or similar).
2.  Add it to `/data/adb/chimera/whitelist.conf`.
3.  Or disable the module: `chimera off`.

**'Command not found':**
Ensure you are running as **Root** (`su`). The CLI tool is installed in `/system/bin`, which should be in your `$PATH` automatically by Magisk.


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



