#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""MANUAL hardware test: physical 32 (PWM1) -> 1 kOhm -> 29 (GPIO input).

Requires the stock pwm1-pi11 overlay and no other devices on these contacts.
Uses the kernel PWM sysfs interface; does not implement WiringPi pwmWrite.
Only channel 1 of the exact six-channel sunxi_pwm controller is touched.
"""
import argparse
import signal
import subprocess
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--confirm-32-to-29', action='store_true', required=True)
    parser.add_argument('input_probe', type=Path)
    args = parser.parse_args()
    probe = args.input_probe.resolve(strict=True)
    dt = Path('/sys/firmware/devicetree/base')
    compatible = (dt / 'compatible').read_bytes().split(b'\0')
    if not {b'xunlong,orangepi-zero2w', b'allwinner,sun50i-h618'} <= set(compatible):
        raise RuntimeError('This test is only for Orange Pi Zero 2W / H618')
    node = dt / 'soc/pwm1@0300a000'
    if (node / 'status').read_bytes().rstrip(b'\0') != b'okay' or not (node / 'pinctrl-0').is_file():
        raise RuntimeError('Enable the verified stock pwm1-pi11 overlay first')
    pins = dt / 'soc/pinctrl@300b000/pwm1-pi-pin'
    if ((node / 'pinctrl-0').read_bytes() != (pins / 'phandle').read_bytes()
            or (pins / 'pins').read_bytes() != b'PI11\0'
            or (pins / 'function').read_bytes() != b'pwm1\0'):
        raise RuntimeError('PWM1 routing does not match physical contact 32 / PI11')
    chips = [p for p in Path('/sys/class/pwm').glob('pwmchip*')
             if (p / 'device').resolve().name == '300a000.pwm'
             and (p / 'device/driver').resolve().name == 'sunxi_pwm'
             and (p / 'npwm').read_text().strip() == '6']
    if len(chips) != 1:
        raise RuntimeError('Expected exactly one verified six-channel sunxi_pwm controller')
    chip = chips[0]
    pwm = chip / 'pwm1'
    if pwm.exists():
        raise RuntimeError('PWM1 is already exported; refusing to take it over')

    def write(name, value):
        (pwm / name).write_text(str(value))

    def read_input(mode):
        output = subprocess.check_output([str(probe), mode], text=True, timeout=5)
        return {k: int(v) for k, v in (word.split('=') for word in output.split())}

    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'Signal {signum}')

    signal.signal(signal.SIGTERM, interrupted)
    # A successful export is the only point where this process acquires ownership.
    (chip / 'export').write_text('1')
    try:
        for _ in range(100):
            if (pwm / 'enable').exists():
                break
            time.sleep(0.01)
        write('enable', 0)
        write('duty_cycle', 0)
        write('polarity', 'normal')
        for hz in (10, 100, 1000):
            period = 1_000_000_000 // hz
            write('enable', 0)
            write('duty_cycle', 0)
            write('period', period)
            write('enable', 1)
            for percent in (25, 50, 75):
                duty = period * percent // 100
                write('duty_cycle', duty)
                time.sleep(0.05)
                measured = read_input('wave')
                print(f'{hz} Hz, {percent}%: {measured}', flush=True)
                if abs(measured['frequency'] - hz) > max(1, hz * 0.02):
                    raise RuntimeError('Frequency outside 2% / 1 Hz tolerance')
                for name, expected in [('high_ns', duty), ('low_ns', period - duty)]:
                    # GPIO IRQ timestamps include interrupt latency; not a scope.
                    if abs(measured[name] - expected) > max(50_000, expected * 0.05):
                        raise RuntimeError(f'{name} outside 5% / 50 us tolerance')
        for percent, expected in [(0, 0), (100, 1)]:
            write('duty_cycle', period * percent // 100)
            time.sleep(0.05)
            if read_input('level')['level'] != expected:
                raise RuntimeError(f'Incorrect level at {percent}% duty')
            print(f'{percent}% duty: input level {expected}', flush=True)
    finally:
        # Attempt every cleanup operation even if an earlier one fails.
        errors = []
        for name, value in [('duty_cycle', 0), ('enable', 0)]:
            try:
                write(name, value)
            except OSError as error:
                errors.append(str(error))
        try:
            (chip / 'unexport').write_text('1')
        except OSError as error:
            errors.append(str(error))
        if errors:
            raise RuntimeError('PWM cleanup failed: ' + '; '.join(errors))
    print('Kernel PWM1 loopback passed; PWM disabled/unexported, input released')


if __name__ == '__main__':
    main()
