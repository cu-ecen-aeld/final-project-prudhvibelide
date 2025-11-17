# ECEN5713: Advanced Embedded Software Development (Final Project)

This repository contains the implementation for the **Virtualized Embedded Media Controller for Network-Based Playback**.

For full project details and documentation, please see the [Project Overview Wiki Page](https://github.com/cu-ecen-aeld/final-project-prudhvibelide/wiki/Project-Overview).


## Update unitl Sprint 2 

Hardware: Raspberry Pi 4 Model B
OS: Custom Buildroot Linux (ARM64)
Kernel: Linux 6.6.78-v8
Language: C (Kernel module)


# Current Status

#Completed Features

A. Buildroot System
The entire Buildroot environment is fully functional:
Custom kernel compiled and boots on Raspberry Pi
Out-of-tree driver automatically compiled and installed
Device tree overlays installed and loaded during boot

B. Platform Driver Implementation
1. Used platform driver architecture
2. Proper device tree binding with compatible string matching
3. Automatic driver loading when device tree node is present
4. Clean probe/remove functions following kernel best practices

C. Device Tree Integration
1. Custom device tree overlay (music-input.dtbo)
2. GPIO configuration through device tree (GPIO 17)
3. Pin configuration with pull-up resistor
4. Overlay successfully loads at boot time

D. GPIO Management
Uses managed GPIO resources (devm_gpiod_get)
Automatic cleanup on driver removal
GPIO configured as input with internal pull-up
Tested and verified GPIO descriptor creation

E. Interrupt Handling
IRQ registered for falling-edge detection
Interrupt service routine (ISR) implemented
Triggers on button press (GPIO LOW)
Managed IRQ allocation (devm_request_irq)

F. Character Device Interface
/dev/music_input device node created automatically
Character device properly registered with kernel
File operations structure implemented
Open, release, and read operations functional

G. Build System Integration
Buildroot package created in br-external
Automatic compilation as kernel module
Module installed to correct kernel modules directory
Build tested and working

H. Deployment
SD card deployment script created
Bootable image with custom kernel and driver
Device tree overlay applied at boot
Driver auto-loads successfully

# Current Functionality

What Works Now:
Driver loads automatically on boot
Creates /dev/music_input device node
Reading from device returns: "button-read\n"
Button interrupt triggers successfully (visible in dmesg)
Platform driver binding confirmed in sysfs
GPIO resources properly managed

Verification Commands:
bash# Check driver loaded
lsmod | grep music_input
dmesg | grep music_input

# Test device
ls -l /dev/music_input
cat /dev/music_input

# Check binding
ls /sys/bus/platform/drivers/music_input/
