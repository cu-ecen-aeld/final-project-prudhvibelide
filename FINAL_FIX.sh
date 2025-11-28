#!/bin/bash
set -e

echo "=== DIAGNOSING AND FIXING EVERYTHING ==="

# 1. CHECK SOURCE FILES
echo "Checking source files..."
if ! grep -q "volume_up" br-external/package/music-daemon/src/music_daemon.c; then
    echo "ERROR: music_daemon.c missing volume_up function"
    exit 1
fi

if ! grep -q "NUM_SONGS      6" br-external/package/music-daemon/src/music_daemon.c; then
    echo "ERROR: NUM_SONGS not set to 6"
    exit 1
fi

if ! grep -q "current_song = current_song - 1" br-external/package/music-daemon/src/music_daemon.c; then
    echo "ERROR: prev function not fixed"
    exit 1
fi

echo "✓ Source files correct"

# 2. FIX .MK FILE (install path)
echo "Fixing .mk file..."
sed -i 's|/usr/sbin/music_daemon|/usr/bin/music_daemon|g' br-external/package/music-daemon/music-daemon.mk
echo "✓ .mk file fixed"

# 3. ENABLE ALSA-UTILS
echo "Enabling alsa-utils..."
cd buildroot
if ! grep -q "BR2_PACKAGE_ALSA_UTILS=y" .config; then
    echo "BR2_PACKAGE_ALSA_UTILS=y" >> .config
    echo "BR2_PACKAGE_ALSA_UTILS_AMIXER=y" >> .config
fi

# 4. FULL CLEAN BUILD
echo "Full clean build (takes 10 min)..."
make clean
make -j$(nproc) > /tmp/build.log 2>&1 || (tail -50 /tmp/build.log && exit 1)

# 5. VERIFY BINARIES
echo "Verifying binaries..."
strings output/target/usr/bin/music_daemon | grep -q "volume_up" || (echo "ERROR: volume_up missing from binary" && exit 1)
strings output/target/usr/bin/music_daemon | grep -q "NUM_SONGS" || (echo "ERROR: NUM_SONGS missing" && exit 1)
test -f output/target/usr/bin/amixer || (echo "ERROR: amixer not built" && exit 1)
echo "✓ All code present in binaries"

# 6. DEPLOY
echo "Deploying to SD card..."
cd ..
dtc -@ -I dts -O dtb -o br-external/package/music-gpio/src/music-input.dtbo br-external/package/music-gpio/src/music-input.dts

sudo umount /dev/sdb* 2>/dev/null || true
sudo mount /dev/sdb1 /mnt/bootfat
sudo mount /dev/sdb2 /mnt/rootfs

sudo cp buildroot/output/images/Image /mnt/bootfat/kernel8.img
sudo cp buildroot/output/images/bcm2711-rpi-4-b.dtb /mnt/bootfat/
sudo mkdir -p /mnt/bootfat/overlays
sudo cp br-external/package/music-gpio/src/music-input.dtbo /mnt/bootfat/overlays/

sudo rm -rf /mnt/rootfs/*
sudo tar -xpf buildroot/output/images/rootfs.tar -C /mnt/rootfs/
sudo mkdir -p /mnt/rootfs/usr/share/music
sudo cp ~/Downloads/Song*.mp3 /mnt/rootfs/usr/share/music/

# 7. FINAL VERIFICATION ON SD CARD
echo "Final verification..."
test -f /mnt/rootfs/usr/bin/music_daemon || (echo "ERROR: daemon not on SD card" && exit 1)
test -f /mnt/rootfs/usr/bin/amixer || (echo "ERROR: amixer not on SD card" && exit 1)
strings /mnt/rootfs/usr/bin/music_daemon | grep -q "volume_up" || (echo "ERROR: SD card daemon missing volume code" && exit 1)
COUNT=$(ls /mnt/rootfs/usr/share/music/*.mp3 2>/dev/null | wc -l)
test "$COUNT" -ge 6 || (echo "WARNING: Only $COUNT MP3 files found" && exit 1)

echo "✓ SD card verified"

sync
sudo umount /mnt/bootfat /mnt/rootfs

echo ""
echo "=== SUCCESS ==="
echo "✓ 6 songs"
echo "✓ Volume control code"
echo "✓ Prev fix"
echo "✓ amixer installed"
echo "✓ Everything deployed"
echo ""
echo "Boot Pi now."
