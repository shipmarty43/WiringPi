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

The same image subsequently booted on the user's board. Native library/devLib/CLI
builds, ASan/UBSan mock regressions and the Raspberry Pi revision-decoding tests
passed on 2026-10-26. Actual `gpio readall` discovered `gpiochip1` with label
`300b000.pinctrl` and 288 unnamed lines; it printed all 40 header positions
without requesting GPIO lines. Physical 8/10 were reported busy. The other chip
is `7022000.pinctrl` with 32 lines and was not selected. No spidev device nodes
were present; I2C adapter 0 was R-I2C at `7081400`, and adapter 1 was HDMI.

Board testing exposed and fixed an identity-display bug: the upstream
`PI_MAKER_UNKNOWN` value actually indexes “Sony Japan”. H618 now has a separate
Xunlong maker value outside the Raspberry Pi revision field; existing Raspberry
Pi maker indices remain unchanged. `gpio -v` prints unknown revision/RAM honestly
and describes the character-device backend without invoking the unsupported
Raspberry Pi descriptor API. The existing `WIRINGPI_FORCE_REVISION` test hook
still exercises Raspberry Pi identity decoding on H618; it does not change GPIO
backend detection or pin mapping.

### Physical loopback validation

After a reboot on 2026-10-26, the user connected physical header contacts **37
(PI16, sunxi 272, wPi 25)** and **29 (PI0, sunxi 256, wPi 19)** through a 1 kOhm
resistor. The initial 20-level output-37/input-29 test passed. The expanded
`wiringPi/test/wiringpi_test_h618_loopback.c` then passed:

- 120 alternating level checks: both directions in physical, wPi and sunxi
  numbering (20 checks per direction per mode).
- Internal pull-up/down readback with both contacts configured as inputs, one
  pull enabled and the other disabled, on each contact in all three modes.
- Preservation of output HIGH while changing its bias; INPUT/OUTPUT reporting.
- `PM_OFF` release of both lines, close/reopen between modes, and final GPIO
  metadata showing no owner on either contact.

An independent CLI process test confirmed that a second `hold` on physical 37
failed with EBUSY while physical 29 still read the original LOW. SIGKILL of the
owner released the request in the kernel; reacquisition and normal timed exit
also released the line. Physical UART contacts 8/10 were not requested or changed.

The expanded test exposed a stale setup flag: returning to wPi numbering after
physical numbering, or reopening after `h618Close()`, incorrectly skipped setup.
H618 setup now revalidates/reopens the backend and selects the requested numbering
without dropping existing requests. `wiringPiSetupSys()` follows the same path.
Host and native ASan/UBSan regressions cover mode switching, preserved ownership,
failed reopen and retry; native Raspberry Pi revision-decoding tests also passed.

To repeat, first disconnect other devices from those two contacts and connect
them through 1 kOhm. Compile and run the manual test on Zero 2W; never run it on
unknown wiring. It only owns those two contacts and does not transmit RF.
These results establish digital GPIO behavior for this pair on this kernel, not
every header contact, voltage accuracy, timing limits, SPI/I2C, PWM or interrupts.
Voltage levels and signal timing have not been measured with external instruments.
