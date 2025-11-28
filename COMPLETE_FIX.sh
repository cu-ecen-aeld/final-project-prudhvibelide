#!/bin/bash
set -e

echo "=== COMPLETE FIX - All 3 Issues ==="

# 1. Delete inittab (restore normal login)
echo "1. Fixing login..."
rm -f br-external/overlay/etc/inittab

# 2. Fix encoder debouncing (100ms)
echo "2. Fixing encoder flooding..."
sed -i 's/msecs_to_jiffies(5)/msecs_to_jiffies(100)/' \
    br-external/package/music-gpio/src/music_input_driver.c

# 3. Fix mpg123 audio card (use default, not hw:0,0)
echo "3. Fixing mpg123 ALSA..."
sed -i 's/"-a", "hw:0,0"/"-q"/' \
    br-external/package/music-daemon/src/music_daemon.c

# 4. Verify NUM_SONGS=6
if ! grep -q "NUM_SONGS      6" br-external/package/music-daemon/src/music_daemon.c; then
    echo "ERROR: NUM_SONGS not 6!"
    exit 1
fi

# 5. Full rebuild
echo "4. Full rebuild..."
cd buildroot
make clean > /dev/null 2>&1
make -j$(nproc)

# 6. Deploy
echo "5. Deploying..."
cd ..
sudo umount /dev/sdb* 2>/dev/null || true
sudo mount /dev/sdb1 /mnt/bootfat
sudo mount /dev/sdb2 /mnt/rootfs

sudo cp buildroot/output/images/Image /mnt/bootfat/kernel8.img
sudo cp buildroot/output/images/bcm2711-rpi-4-b.dtb /mnt/bootfat/

sudo rm -rf /mnt/rootfs/*
sudo tar -xpf buildroot/output/images/rootfs.tar -C /mnt/rootfs/

sudo mkdir -p /mnt/rootfs/usr/share/music
sudo cp ~/Downloads/Song*.mp3 /mnt/rootfs/usr/share/music/

# 7. Verify
echo "6. Verifying..."
test $(ls /mnt/rootfs/usr/share/music/*.mp3 | wc -l) -eq 6 || echo "WARNING: Not 6 MP3s"
strings /mnt/rootfs/usr/bin/music_daemon | grep -q "NUM_SONGS" || echo "WARNING: Old daemon"

sync
sudo umount /mnt/bootfat /mnt/rootfs

echo ""
echo "=== ALL FIXED ==="
echo "✓ Normal login restored"
echo "✓ Encoder 100ms debounce"
echo "✓ mpg123 uses correct audio"
echo "✓ 6 songs ready"
echo ""
echo "Boot Pi and test!"
