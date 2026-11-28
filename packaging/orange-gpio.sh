#!/bin/bash
# Cross-build the board library and Python bindings without replacing distro gpio utilities.
set -euo pipefail
[[ $# == 2 ]] || { echo "Usage: $0 COMMIT OUTPUT_DIRECTORY" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/.." && pwd); rev=$(git -C "$repo" rev-parse --verify "$1^{commit}")
version="3.20+opi.$(git -C "$repo" rev-list --count "$rev").${rev:0:8}"
mkdir -p "$2"; out=$(realpath "$2")
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
git -C "$repo" archive "$rev" | tar -x -C "$tmp"
compiler="${CROSS_COMPILE:-aarch64-linux-gnu-}gcc"
args=(CC="$compiler")
if [[ -n ${SYSROOT:-} ]]; then
    sysroot=$(realpath "$SYSROOT")
    inc="$sysroot/usr/include"; lib="$sysroot/usr/lib/aarch64-linux-gnu"
    [[ -f $inc/stdio.h && -f $lib/libc.so && -e $sysroot/lib/aarch64-linux-gnu/libc.so.6 ]]
    args+=(EXTRA_CFLAGS="--sysroot=$sysroot -nostdinc -isystem $($compiler -print-file-name=include) -isystem $inc/aarch64-linux-gnu -isystem $inc"
           LDFLAGS="--sysroot=$sysroot -B$lib/ -L$lib -Wl,-rpath-link,$lib")
fi
make -j2 -C "$tmp/wiringPi" "${args[@]}"
root="$tmp/pkg"; priv="$root/usr/lib/wiringpi-orange"
mkdir -p "$priv" "$root/DEBIAN" "$root/usr/include/wiringpi-orange" "$root/usr/lib/python3/dist-packages"
install -m644 "$tmp"/wiringPi/libwiringPi.so.* "$priv/libwiringPi.so"
"${CROSS_COMPILE:-aarch64-linux-gnu-}strip" --strip-unneeded "$priv/libwiringPi.so"
soname=$(readelf -d "$priv/libwiringPi.so" | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
if [[ -n $soname && $soname != libwiringPi.so ]]; then ln -s libwiringPi.so "$priv/$soname"; fi
install -m644 "$tmp"/wiringPi/*.h "$root/usr/include/wiringpi-orange/"
install -m644 "$tmp"/python/wiringpi_zero{2,3}w.py "$root/usr/lib/python3/dist-packages/"
install -Dm644 "$tmp/COPYING.LESSER" "$root/usr/share/doc/wiringpi-orange-gpio/copyright"
minimum=$(readelf --version-info "$priv/libwiringPi.so" | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
[[ -n $minimum ]]
cat > "$root/DEBIAN/control" <<CONTROL
Package: wiringpi-orange-gpio
Version: $version
Architecture: arm64
Depends: libc6 (>= ${minimum#GLIBC_}), libcrypt1, libatomic1, python3
Maintainer: Orange Pi configurator maintainers <shipmarty43@users.noreply.github.com>
Description: Orange Pi Zero 2W and Zero 3W header GPIO library and Python bindings
CONTROL
dpkg-deb --root-owner-group --threads-max=1 --build "$root" "$out/wiringpi-orange-gpio_${version}_arm64.deb"
