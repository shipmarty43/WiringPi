#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
set -eu
pkg=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
src=$pkg/../wiringPi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -shared -fPIC -pthread -Wall -Wextra -Werror -I"$src" -DWPI_PYTHON_FIXTURE \
  "-DH618_PWM_DT=\"$tmp/dt\"" "-DH618_PWM_SYSFS=\"$tmp/pwm\"" \
  "$src/wiringPiH618.c" "$src/wiringPiH618Line.c" "$src/wiringPiH618Threads.c" \
  "$src/test/wiringpi_test_h618_pwm_host.c" -Wl,--wrap=h618Detected,--wrap=h618Setup \
  -o "$tmp/libwiringPi.so"
PYTHONPATH="$pkg" LD_LIBRARY_PATH="$tmp" python3 - "$tmp" <<'PY'
import errno
import os
from pathlib import Path
import sys
import wiringpi_zero2w as g

lib = g._library()
lib.wpiTestPwmFixture()
chip = Path(sys.argv[1]) / "pwm/pwmchip7/pwm1"
def fails(expected, op):
    try:
        op()
    except OSError as exc:
        assert exc.errno == expected, exc
    else:
        raise AssertionError("Operation unexpectedly succeeded")

fails(errno.EBUSY, lambda: g.request_output(32, initial=1))
with g.PWM1(range_counts=24000, divisor=1, duty=6000) as pwm:
    assert (chip / "period").read_text() == "1000000"
    assert (chip / "duty_cycle").read_text() == "250000"
    fails(errno.EBUSY, lambda: g.PWM1(range_counts=24000, divisor=1))
    child = os.fork()
    if not child:
        fails(errno.ECHILD, lambda: pwm.set_duty(0))
        fails(errno.ECHILD, pwm.close)
        os._exit(0)
    assert os.waitpid(child, 0)[1] == 0
    pwm.set_duty(12000)
    assert (chip / "duty_cycle").read_text() == "500000"
    lib.wpiTestFailUnexport()
    fails(errno.EIO, pwm.close)
assert not chip.exists()
pwm.close()
with g.PWM1(range_counts=12000, divisor=2):
    assert chip.exists()
print("PASS Python PWM1 period/duty, contention, pinmux, fork and cleanup retry")
PY
