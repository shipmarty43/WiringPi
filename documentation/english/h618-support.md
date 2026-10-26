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
- `wiringPiISR`, `wiringPiISR2`, `wiringPiISRStop`, `waitForInterrupt2` and
  `waitForInterruptClose`, using kernel GPIO v2 edge events and optional debounce.
- `softPwmCreate/Write/Stop`, `softToneCreate/Write/Stop`, and
  `pinMode(SOFT_PWM_OUTPUT/SOFT_TONE_OUTPUT)`, using cancellable worker threads.
- `pulseInNS`, `pulseIn`, `frequencyIn`, using kernel edge timestamps.

Unsupported operations return -1 where available (void APIs report an error and
set `errno=ENOTSUP`; unsigned byte APIs return 0 with ENOTSUP): alternate
function writes, pad drive, hardware PWM/clock, the deprecated
`waitForInterrupt` (use `waitForInterrupt2`), Pi-specific byte operations and
onboard ADC.
These are **not** advertised as working H618 implementations. Peripheral mux,
SPI, I2C, UART and PWM routing remains the responsibility of kernel drivers and
DT overlays. Full API coverage and physical validation are separate work.

### Worker ownership and timing

Each IRQ, measurement, software PWM or tone worker owns an exclusive kernel line
request. It never steals an existing digital-I/O request: use `pinMode(pin,
PM_OFF)` before changing a previously requested input/output into a worker.
Stop a worker before using normal digital I/O on that pin. Duplicate requests
fail with EBUSY. `PM_OFF` stops the worker and releases the line; `h618Close()`
joins all workers before closing the GPIO backend. Software outputs are driven
LOW before release. No output level is guaranteed after ownership is released.

Use one thread to coordinate setup, numbering changes, close and replacement of
workers. Do not start a replacement worker until a concurrent wait/measurement
on that line has returned. Stopping an active wait from another thread wakes it
with ECANCELED. Keep callbacks short: they run on the line's worker thread,
outside internal locks. Stop, close and blocking measurement/wait calls from a
callback fail with EDEADLK; arrange cleanup from the controlling thread instead.
Keep callback data alive until stop has returned. Kernel event sequence gaps
are reported as EOVERFLOW on stop instead of silently accepting lost edges.

`WPIWfiStatus.pinBCM` contains the **native sunxi number** on H618 despite its
legacy field name; timestamps use the kernel's monotonic clock. Rising, falling
and both-edge modes are supported; INT_EDGE_SETUP is not. Debounce is requested
from the kernel and unsupported settings propagate errors. `getAlt` currently
reports only ordinary digital-I/O requests, not worker-owned lines.

Software PWM uses 100 microseconds per range unit (range 100 nominally 100 Hz).
Tone clamps frequency to 0..5000 Hz; zero holds LOW. These are scheduler-dependent
signals, not precision hardware PWM. `pulseIn*` measures the next complete pulse;
`frequencyIn` estimates frequency between first and last rising edges in the
requested window. Timeouts or insufficient edges return zero with errno zero;
errors return zero with errno set. Kernel timestamps improve measurement over
userspace polling but do not establish electrical timing accuracy.

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
These level tests establish digital GPIO behavior for this pair on this kernel,
not every header contact, voltage accuracy, timing limits or SPI/I2C.
Voltage levels and signal timing have not been measured with external instruments.

### IRQ, software waveform and measurement validation

On the same image and 37-to-29 resistor connection, the manual
`wiringPi/test/wiringpi_test_h618_events.c` test passed on 2026-10-26:

- Ten rising and ten falling IRQ callbacks with correct pin/edge/timestamp data;
  duplicate ownership rejected; timed wait expiry and delayed rising event.
- Software PWM and tone at nominal 100 Hz, 0/100% PWM duty, zero tone and cleanup.
- Frequency and pulse measurement: approximately 98 Hz and 5.1 ms half-periods
  for the scheduler-driven nominal 100 Hz PWM.
- Kernel acceptance of a 1000 microsecond debounce request, close with active
  workers, reopen and reuse in native numbering. This checks debounce setup,
  not suppression of an independently measured burst of contact bounce.
- All 120 digital loopback checks repeated after the worker changes; final
  metadata confirmed physical 37 and 29 had no remaining owner.

Build and run from the repository root on the wired board:

```sh
make -C wiringPi -j4
cc -Wall -Wextra -Werror -IwiringPi wiringPi/test/wiringpi_test_h618_events.c \
  wiringPi/libwiringPi.so.3.20 -lpthread -o /tmp/h618-events
LD_LIBRARY_PATH="$PWD/wiringPi" timeout 20 /tmp/h618-events
```

The host-test target also checks real pthread/eventfd/poll lifecycle with mocked
GPIO: FD 0, failed thread/request cleanup, callback self-stop rejection, queue
overflow, waveform stop, timeout and external cancellation of a wait/measurement.
ASan/UBSan regressions passed on both x86-64 and the board's AArch64 CPU.

Hardware PWM library dispatch (`pinMode(PWM_OUTPUT)`, `pwmWrite`, `pwmSetClock`,
etc.) remains unimplemented. The kernel PWM controller was subsequently tested
separately as described below; software PWM results alone do not validate it.

### Kernel hardware PWM1 loopback validation

Later on 2026-10-26 the owner confirmed moving the 1 kOhm resistor to physical
**32 (PI11/PWM1) and 29 (PI0/input)**, with other devices disconnected, and
authorized the stock `pwm1-pi11` overlay and reboot. The original boot environment
was backed up; `overlays=pwm1-pi11` was added with
`overlay_prefix=sun50i-h616`. Offline overlay application checked PI11/pwm1 and
the live device tree after reboot confirmed the route. No raw MMIO was used.

Controller: device `300a000.pwm`, driver `sunxi_pwm`, six channels. The test
discovers `pwmchipN` from this device and driver rather than assuming chip 0.
PWM5 belongs to `ac300-clk` and was not changed. Only channel 1 was exported.

| Requested frequency | Measured frequency | Duty cycles | Result |
|---:|---:|---|---|
| 10 Hz | 10 Hz | 25%, 50%, 75% | Passed |
| 100 Hz | 100 Hz | 25%, 50%, 75% | Passed |
| 1000 Hz | 999–1000 Hz | 25%, 50%, 75% | Passed |

HIGH/LOW pulse widths passed a tolerance of max(5%, 50 microseconds); frequency
passed max(2%, 1 Hz). At 1000 Hz, HIGH widths were approximately 243, 488 and
731 microseconds for the three duty settings. GPIO kernel IRQ timestamps include
interrupt latency, so these measurements are not oscilloscope specifications.
0% and 100% duty were also read back as stable LOW and HIGH on physical 29.

Repeat only with the confirmed wiring and overlay:

```sh
cc -Wall -Wextra -Werror -IwiringPi \
  wiringPi/test/wiringpi_test_h618_pwm_input.c wiringPi/libwiringPi.so.3.20 \
  -lpthread -o /tmp/h618-pwm-input
sudo env LD_LIBRARY_PATH="$PWD/wiringPi" python3 \
  wiringPi/test/wiringpi_test_h618_pwm.py --confirm-32-to-29 /tmp/h618-pwm-input
```

The input probe never drives a GPIO. The Python harness verifies board, DT
routing and controller identity, refuses an already exported channel, and
disables/unexports its PWM in cleanup. SIGKILL/power loss cannot execute userspace
cleanup; do not use this test as an unattended PWM service. Final sysfs/debugfs
inspection confirmed PWM1 disabled and unexported, GPIO input 29 unclaimed,
and PWM5 unchanged. PI11 remains assigned to the PWM1 pinctrl consumer while the
overlay is enabled; it is not available as an ordinary GPIO even after unexport.

The overlay remains enabled on the test board. To return PI11 to normal GPIO,
remove only `pwm1-pi11` from the `overlays=` list and reboot. If no intervening
boot configuration edits have occurred, restoring the saved original environment
is also an option. Other PWM channels, polarity inversion, driver suspend/resume
and WiringPi's legacy hardware-PWM API are not validated by this test.
