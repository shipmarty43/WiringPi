#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# The guest /init mounts /dev, /proc and /sys, runs test_gpio.py, and emits
# PASS-GPIO on success before powering down. No disk or network is attached.
set -eu
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 ARM64-Image test-initramfs.gz" >&2
    exit 2
fi
fixture=$(dirname "$0")/gpio-sim.dts
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
qemu=${QEMU_SYSTEM_AARCH64:-qemu-system-aarch64}
kernel=$1
initrd=$2
set -- -accel tcg -cpu cortex-a53 -smp 1 -m 256M -display none -monitor none -nic none
"$qemu" -machine "virt,dumpdtb=$tmp/virt.dtb" "$@"
dtc -@ -I dts -O dtb -o "$tmp/sim.dtbo" "$fixture"
fdtoverlay -i "$tmp/virt.dtb" -o "$tmp/guest.dtb" "$tmp/sim.dtbo"
status=0
timeout 90s "$qemu" -machine virt "$@" -serial stdio -no-reboot -kernel "$kernel" \
    -initrd "$initrd" -dtb "$tmp/guest.dtb" -append 'console=ttyAMA0 rdinit=/init' \
    > "$tmp/console.log" 2>&1 || status=$?
cat "$tmp/console.log"
[ "$status" -eq 0 ] && grep -aq '^PASS-GPIO' "$tmp/console.log" &&
    grep -aq '^test_events_and_timeout .* ok' "$tmp/console.log"
