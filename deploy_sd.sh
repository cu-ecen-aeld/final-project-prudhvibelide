#!/bin/bash
# Raspberry Pi 4 SD Card Deployment Script
# This script prepares an SD card with your custom Buildroot image

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Raspberry Pi 4 SD Card Setup ===${NC}"

# Configuration
BUILDROOT_OUTPUT="/home/prbe/Documents/AESD/Final-Project/final-project-prudhvibelide/buildroot/output/images"
TEMP_DIR="/tmp/rpi4_setup"

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Please run as root (sudo $0)${NC}"
    exit 1
fi

# List available block devices
echo -e "\n${YELLOW}Available block devices:${NC}"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT | grep -E "disk|part"

# Ask for SD card device
echo -e "\n${YELLOW}Enter SD card device (e.g., sdb - WITHOUT /dev/ prefix):${NC}"
read -p "Device: " DEVICE

SD_DEVICE="/dev/${DEVICE}"

# Safety check
if [ ! -b "$SD_DEVICE" ]; then
    echo -e "${RED}Error: $SD_DEVICE is not a valid block device${NC}"
    exit 1
fi

# Confirm with user
echo -e "\n${RED}WARNING: This will ERASE ALL DATA on $SD_DEVICE${NC}"
echo -e "Device info:"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT "$SD_DEVICE"
read -p "Are you sure you want to continue? (yes/no): " CONFIRM

if [ "$CONFIRM" != "yes" ]; then
    echo "Aborted."
    exit 0
fi

# Unmount any mounted partitions
echo -e "\n${GREEN}Unmounting any mounted partitions...${NC}"
umount ${SD_DEVICE}* 2>/dev/null || true

# Partition the SD card
echo -e "\n${GREEN}Creating partitions...${NC}"
parted -s "$SD_DEVICE" mklabel msdos
parted -s "$SD_DEVICE" mkpart primary fat32 1MiB 257MiB
parted -s "$SD_DEVICE" set 1 boot on
parted -s "$SD_DEVICE" mkpart primary ext4 257MiB 100%

# Wait for kernel to recognize partitions
sleep 2
partprobe "$SD_DEVICE"
sleep 2

# Format partitions
BOOT_PART="${SD_DEVICE}1"
ROOT_PART="${SD_DEVICE}2"

# Handle devices like /dev/mmcblk0 which have p1, p2 suffixes
if [[ "$SD_DEVICE" == *"mmcblk"* ]] || [[ "$SD_DEVICE" == *"nvme"* ]]; then
    BOOT_PART="${SD_DEVICE}p1"
    ROOT_PART="${SD_DEVICE}p2"
fi

echo -e "\n${GREEN}Formatting boot partition (FAT32)...${NC}"
mkfs.vfat -F 32 -n BOOT "$BOOT_PART"

echo -e "${GREEN}Formatting root partition (ext4)...${NC}"
mkfs.ext4 -L rootfs "$ROOT_PART"

# Create mount points
mkdir -p /mnt/rpi_boot /mnt/rpi_root

# Mount partitions
echo -e "\n${GREEN}Mounting partitions...${NC}"
mount "$BOOT_PART" /mnt/rpi_boot
mount "$ROOT_PART" /mnt/rpi_root

# Download Raspberry Pi firmware
echo -e "\n${GREEN}Downloading Raspberry Pi firmware files...${NC}"
mkdir -p "$TEMP_DIR"
cd "$TEMP_DIR"

if [ ! -d "firmware" ]; then
    git clone --depth=1 https://github.com/raspberrypi/firmware.git
fi

# Copy boot files
echo -e "\n${GREEN}Copying boot files...${NC}"
cp firmware/boot/bootcode.bin /mnt/rpi_boot/
cp firmware/boot/start*.elf /mnt/rpi_boot/
cp firmware/boot/fixup*.dat /mnt/rpi_boot/

# Copy kernel and device tree
echo -e "${GREEN}Copying kernel image...${NC}"
cp "$BUILDROOT_OUTPUT/Image" /mnt/rpi_boot/kernel8.img

echo -e "${GREEN}Copying device tree blob...${NC}"
cp "$BUILDROOT_OUTPUT/bcm2711-rpi-4-b.dtb" /mnt/rpi_boot/

# Extract root filesystem
echo -e "\n${GREEN}Extracting root filesystem (this may take a while)...${NC}"
tar -xf "$BUILDROOT_OUTPUT/rootfs.tar" -C /mnt/rpi_root/

# Create config.txt
echo -e "\n${GREEN}Creating boot configuration...${NC}"
cat > /mnt/rpi_boot/config.txt << 'EOF'
# Enable 64-bit mode
arm_64bit=1

# Kernel and device tree
kernel=kernel8.img
device_tree=bcm2711-rpi-4-b.dtb

# GPU memory (minimum for headless)
gpu_mem=64

# Enable UART for console
enable_uart=1

# Optional: Disable Bluetooth to free up serial port
dtoverlay=disable-bt
EOF

# Create cmdline.txt
cat > /mnt/rpi_boot/cmdline.txt << 'EOF'
console=serial0,115200 console=tty1 root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw
EOF

# Sync and unmount
echo -e "\n${GREEN}Syncing data to SD card...${NC}"
sync

echo -e "${GREEN}Unmounting partitions...${NC}"
umount /mnt/rpi_boot
umount /mnt/rpi_root

# Cleanup
rmdir /mnt/rpi_boot /mnt/rpi_root

echo -e "\n${GREEN}=== SD Card Setup Complete! ===${NC}"
echo -e "${YELLOW}You can now:"
echo "  1. Remove the SD card safely"
echo "  2. Insert it into your Raspberry Pi 4"
echo "  3. Connect power and monitor (or serial console)"
echo "  4. The system should boot"
echo ""
echo "Default login (if configured): root (no password)"
echo -e "${NC}"
