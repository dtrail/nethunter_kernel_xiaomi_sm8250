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


NOTE: I'm probably switching sources to LOS-Kernel due to some issues I couldn't fix with N0Kernel source, such as kernel modules support and compatiblity to certain other components.
        Whenever I find the time to do that...

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



