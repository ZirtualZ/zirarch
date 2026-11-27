#!/usr/bin/env bash
set -euo pipefail

# 1. Compress and push images
echo "Compressing system image..."
gzip -c builds/zirarch-debian-system.img > builds/zirarch-debian-system.img.gz

echo "Pushing boot and system images to device..."
adb push builds/zirarch-boot.img /data/zirarch-boot.img
adb push builds/zirarch-debian-system.img.gz /data/zirarch-debian-system.img.gz

# 2. Flash partitions on the device
echo "Flashing system (mmcblk0p19) and boot (mmcblk0p9) partitions..."
adb shell 'set -e
gzip -dc /data/zirarch-debian-system.img.gz | dd of=/dev/block/mmcblk0p19 bs=4M conv=fsync
dd if=/data/zirarch-boot.img of=/dev/block/mmcblk0p9 bs=1M conv=fsync
sync'

# 3. Reboot and wait for RNDIS
echo "Rebooting device..."
adb reboot

echo "Waiting 75 seconds for device to boot and RNDIS to come up..."
sleep 75

# 4. Test connection and SSH
echo "Pinging 192.168.7.2..."
ping -c 3 192.168.7.2

echo "Connecting via SSH..."
ssh \
  -o StrictHostKeyChecking=no \
  root@192.168.7.2

