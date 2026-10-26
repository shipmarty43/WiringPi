# Orange Pi Zero 2W / H618 support

## Implemented scope

This fork adds a separate Linux GPIO character-device v2 backend in
`wiringPi/wiringPiH618.c`. It preserves the original library, CLI and directory
layout and does not substitute Allwinner addresses into BCM register code.

- Board detection requires **both** `xunlong,orangepi-zero2w` and
  `allwinner,sun50i-h618` in the NUL-separated device-tree compatible property.
  Other H618/H616 boards are not assumed to have this header layout.
- `wiringPiSetup`, `wiringPiSetupPhys`, `wiringPiSetupGpio`,
  `wiringPiSetupGpioDevice`, `wiringPiSetupSys`: GPIO character device, no `/dev/mem`.
- `pinMode(INPUT/OUTPUT/PM_OFF)`, `pullUpDnControl`, `digitalRead`, `digitalWrite`.
  `PM_OFF` releases the process's line request. `digitalWrite` requires an output
  request in the **same process**. Unclaimed `digitalRead` requests an input.
- `wpiPinToGpio`, `physPinToGpio`, `piBoardId`; `getAlt` reports INPUT/OUTPUT only
  for a line this process owns, otherwise -1 (mux cannot be inferred).
- `gpio readall`: all 40 physical pins, wiringOP wPi numbers, sunxi numbers,
  GPIO request status and consumer. It never requests a line or changes pinmux.
  “GPIO-unclaimed” does not prove a peripheral is unused; the kernel arbitrates
  actual requests. Voltage and peripheral mux are deliberately not fabricated.
- `gpio [-1|-g] read`, `write`, `mode`, and `hold pin 0/1 [seconds]`.
  `hold` without seconds retains ownership until Ctrl+C. A one-shot `write` or
  `mode` releases the request on exit: it does **not** configure a persistent
  output for a later process. Use library calls or `hold` for that workflow.
- Extension nodes use IDs >= 512 to avoid collision with native sunxi numbers.
  Existing SPI explicit bus/device API and I2C explicit-adapter API remain usable.
  H618 implicit `wiringPiI2CSetup` fails; select `wiringPiI2CSetupInterface` with
  the actual Linux adapter path after checking the running image.

Unsupported operations return -1 where available (void APIs report an error and
set `errno=ENOTSUP`; unsigned byte/pulse APIs return 0 with ENOTSUP): alternate
function writes, pad drive, hardware PWM/clock, software PWM/tone, interrupt
callbacks/waits, pulse measurement, Pi-specific byte operations and onboard ADC.
These are **not** advertised as working H618 implementations. Peripheral mux,
SPI, I2C, UART and PWM routing remains the responsibility of kernel drivers and
DT overlays. Full API coverage and physical validation are separate work.

## Header map and kernel discovery

Sources: [Orange Pi Zero 2W documentation](http://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/details/Orange-Pi-Zero-2W.html),
user manual v1.1 section 3.17; the manufacturer's
[wiringOP next pin tables](https://github.com/orangepi-xunlong/wiringOP/blob/next/wiringPi/wiringPi.c)
(`pinToGpio_ZERO_2_W`, `physToGpio_ZERO_2_W`).

| wPi | Physical | SoC | sunxi |
|---:|---:|---|---:|
| 0 | 3 | PI8 | 264 |
| 1 | 5 | PI7 | 263 |
| 2 | 7 | PI13 | 269 |
| 3 | 8 | PH0 | 224 |
| 4 | 10 | PH1 | 225 |
| 5 | 11 | PH2 | 226 |
| 6 | 12 | PI1 | 257 |
| 7 | 13 | PH3 | 227 |
| 8 | 15 | PI5 | 261 |
| 9 | 16 | PI14 | 270 |
| 10 | 18 | PH4 | 228 |
| 11 | 19 | PH7 | 231 |
| 12 | 21 | PH8 | 232 |
| 13 | 22 | PI6 | 262 |
| 14 | 23 | PH6 | 230 |
| 15 | 24 | PH5 | 229 |
| 16 | 26 | PH9 | 233 |
| 17 | 27 | PI10 | 266 |
| 18 | 28 | PI9 | 265 |
| 19 | 29 | PI0 | 256 |
| 20 | 31 | PI15 | 271 |
| 21 | 32 | PI11 | 267 |
| 22 | 33 | PI12 | 268 |
| 23 | 35 | PI2 | 258 |
| 24 | 36 | PC12 | 76 |
| 25 | 37 | PI16 | 272 |
| 26 | 38 | PI4 | 260 |
| 27 | 40 | PI3 | 259 |

Native numbering is bank × 32 + line, **not** a gpiochip offset. Discovery scans
`/dev/gpiochip*`, checks the `300b000.pinctrl` / `0300b000.pinctrl` label and locates
all 28 named lines when supplied. Mainline sunxi does not populate GPIO line
names: for the exact controller label and 288-line H616 layout, unnamed header
lines use the verified hardware offsets (sunxi numbers). This follows
[`sunxi_pinctrl_init_with_flags`](https://github.com/torvalds/linux/blob/master/drivers/pinctrl/sunxi/pinctrl-sunxi.c)
and the [H616 pin descriptor](https://github.com/torvalds/linux/blob/master/drivers/pinctrl/sunxi/pinctrl-sun50i-h616.c):
zero pin base, last pin PI16, and GPIO ranges mapping each native pin to the same
hardware offset. The dynamically allocated global GPIO base is not used.
Unknown partially named layouts and duplicate names are rejected. Images
overriding names/layouts need an explicitly verified mapping extension.
Power/ground, invalid indices and wrong numbering modes are rejected.

The [kernel v2 GPIO ABI](https://docs.kernel.org/userspace-api/gpio/chardev.html)
requires Linux >= 5.10. Busy lines and permission errors propagate. Mode/pull
changes reconfigure an owned request without a release gap, preserve the output
value, and keep the old request on ioctl failure. Descriptors including FD 0 are
valid, closed on release/process exit, and state changes are mutex-protected.

## Validation and bring-up

Host regressions, no real GPIO access:

```sh
ASAN_OPTIONS=detect_leaks=0 make -C wiringPi/test host-test
```

These cover all header wPi entries, invalid/power pins, board compatibility,
chip identity, named/non-native offsets and the unnamed 288-line mainline layout,
busy lines, descriptor-zero ownership,
cleanup, failed reconfiguration, and public API dispatch in all three numbering
modes. SPI regressions run alongside them. `readall` is checked to make zero
requests. ASan/UBSan check the test runs; disabling LeakSanitizer is needed in
some restricted environments. Raspberry Pi revision-decoding regressions also
remain applicable. Build the library and CLI on the board using the upstream
build process; do not install x86 binaries on Armbian.

The clean SD inspected on 2026-10-26 contains Armbian community
26.11.0-trunk.108 / Ubuntu 26.04 / 6.18.57-current-sunxi64, matching Zero 2W
compatible strings, `CONFIG_GPIO_CDEV=y`, and `CONFIG_PINCTRL_SUN50I_H616=y`.
No GPIO line-name overrides appear on its main pinctrl node. This is an offline
compatibility check, **not a boot or electrical test**. Both x86-64 and AArch64
library/CLI builds were checked. The AArch64 mock integration test and CLI help
also ran under QEMU with no GPIO hardware access. These do not prove signal levels.

After first boot, start with `gpio -v` and `gpio readall`. For a controlled loopback,
disconnect devices from physical pins 37 and 29, connect them through 1 kOhm,
and compile/run `wiringPi/test/wiringpi_test_h618_loopback.c`. It owns only those
two lines, checks 20 alternating levels and releases them. It has not yet run
on the user's board. Confirm pin voltages, pull resistors, busy peripheral
rejection and behavior after process exit on the actual board before relying on
this backend. No RF transmission is involved in this test.
