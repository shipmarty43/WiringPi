# Orange Pi Zero 2W / Allwinner H618 bring-up

Status: preparation and portable SPI regressions only. **H618 GPIO support is
not implemented or hardware validated yet.** Base: upstream `31f49ec`, WiringPi
3.20. This C library is distinct from the Python package `OPi.GPIO`.

## Register documentation

The reference located is Allwinner **H616 User Manual, revision 1.0** (2019),
831 PDF pages. It is not an H618-labelled manual and cannot establish that every
H618 peripheral is identical. The Linux 6.12 Zero 2W device tree includes
`sun50i-h616.dtsi`, providing a concrete starting point for checking shared IP.

- [Allwinner manual, cleaned mirror](https://pcminipro.ru/wp-content/uploads/2022/01/H616_User_Manual_V1.0_cleaned.pdf)
- [Linux 6.12 Zero 2W device tree](https://github.com/torvalds/linux/blob/v6.12/arch/arm64/boot/dts/allwinner/sun50i-h618-orangepi-zero2w.dts)
- [Linux H616 pin controller](https://github.com/torvalds/linux/blob/v6.12/drivers/pinctrl/sunxi/pinctrl-sun50i-h616.c)
- [H616 controller definitions](https://github.com/torvalds/linux/blob/v6.12/arch/arm64/boot/dts/allwinner/sun50i-h616.dtsi)
- [SPI controller bindings](https://github.com/torvalds/linux/blob/v6.12/Documentation/devicetree/bindings/spi/spi-controller.yaml)

The downloaded PDF SHA-256 is
`4ca95572826a1d98b687ae74c1bb2f62597b70f5aafa20d698c4ed6792b20e05`.
PDF viewer page numbers match the printed page numbers in this copy.

| Topic | Pages / location |
| --- | --- |
| Memory map | 30–31 |
| TWI bus gating/reset, SPI clocks and reset | 101–103 |
| TWI (I2C) | 573; programming 578; register list 582 |
| UART | 594; programming 600; register list 604 |
| SPI | 623; programming 631; register list 634 |
| GPIO / Port Controller | 689; register list 695 |
| PH and PI configuration registers | 713 and 718 |

Useful bases: CCU `0x03001000`, GPIO `0x0300b000`, TWI0 `0x05002000`,
SPI0 `0x05010000`, SPI1 `0x05011000`. Cross-check offsets, pin functions and
clock/reset sequencing against the actual kernel and board revision before use.

## Keep the upstream layout

Keep the public C API and existing `wiringPi/`, `gpio/`, `devLib/`, `examples/`,
`wiringPi/test/` and `documentation/` layout. Add SoC implementation files inside
`wiringPi/` and select them through explicit board detection; retain Raspberry Pi
paths and regression tests. Do not substitute BCM register constants with sunxi
addresses globally.

1. Detect `xunlong,orangepi-zero2w` / `allwinner,sun50i-h618` through device tree.
   Add a verified physical-pin / wiringPi-pin / sunxi bank-and-line table.
   Preserve the existing meaning of numbering modes for Raspberry Pi users.
2. Separate pin validation and translation from backend operations. Existing
   64-pin assumptions such as `pin & 63` cannot represent arbitrary sunxi GPIO
   IDs. GPIO chip-relative line offsets, legacy global GPIO numbers and header
   pin numbers must remain distinct.
3. Prefer the Linux GPIO character-device API for line ownership and events.
   Identify the correct chip by device identity; do not assume `gpiochip0`.
   Any optional MMIO backend must handle H618 mux/pull/drive registers explicitly
   and must not overwrite pins controlled by active kernel peripherals.
4. Use `/dev/spidevB.C` and explicit I2C adapter paths for Linux-managed buses.
   `wiringPiI2CSetup()` currently selects bus 0/1 using Raspberry Pi layout;
   `wiringPiI2CSetupInterface()` already accepts an explicit adapter path.
5. Extend `gpio readall` with verified board naming and modes. Gate PWM, clock,
   ADC and Raspberry Pi-specific APIs by demonstrated capability. Report
   unsupported operations explicitly; a matching header is not feature support.

The SPI storage correction in this branch supports the already advertised
seven bus numbers and five CS indices. It does **not** create SPI devices or
enable a device-tree overlay. The default SPI wrapper still selects bus zero;
use the explicit-bus API when the hardware is on SPI1.

## Test coverage and hardware acceptance

| Family | Portable checks | Required target checks | Current state |
| --- | --- | --- | --- |
| Version / Raspberry Pi identity | Upstream `wiringpi_test0_version` | Existing supported Pi regression | Host passed; RAM-file check skipped |
| All 10 public SPI functions | Fake open/ioctl/close, 35 bus/CS pairs, modes 0–3, invalid bounds, closed FD, FD zero, open failure | Loopback, per-CS isolation, real devices, measured clock/mode | Host passed; hardware pending |
| Setup, numbering, readall | DT fixtures, every header pin, invalid/unsupported pins | Actual DT/chip identity and header continuity | Pending |
| GPIO read/write, byte operations, pulls, drive, mux | Register or ioctl mocks, ownership/errors | Resistive loopback and measurements per exposed pin | Pending |
| ISR, ISR2, wait, stop, debounce | Event fixtures, cancellation, resource cleanup | Rising/falling/both edges, timeout and repeated restart | Pending |
| I2C byte, register, block, setup | Ioctl fixtures, failures and buffer boundaries | Known peripheral, repeated start and clock measurement | Pending |
| UART, shift, soft PWM/tone | PTY or mock tests, timing tolerances | UART loopback and logic-analyser capture | Pending |
| Hardware PWM/clock, pulse/frequency | Capability and unit conversion tests | Available channels, duty/range/frequency measurements | Pending |
| Timing, threads, locks, priority | Host timing/concurrency and error tests | Target load/jitter tests | Pending |
| devLib, expanders, ADC/DAC, PiFace | Per-driver protocol mocks | Each named device on a recorded wiring fixture | Pending / hardware-specific |

Before declaring complete coverage, inventory every exported function in the
public headers and record a test or a justified unsupported/not-applicable
result for each one. The table above is a bring-up plan, not a claim of complete
API coverage. Existing GPIO tests assume Raspberry Pi BCM wiring; do not run
them unchanged against an Orange Pi header.

Run the portable SPI test from the repository root:

```sh
make -C wiringPi/test host-test
```

It uses AddressSanitizer and UndefinedBehaviorSanitizer by default and no board
or installed WiringPi library. In a sandbox where LeakSanitizer cannot inspect
threads under ptrace, use `ASAN_OPTIONS=detect_leaks=0`; ASan/UBSan remain active.
Compiler environments without sanitizers can use `HOST_TEST_SANITIZERS=`.

Initial verification on 2026-10-26:

- Original code reproduced an out-of-bounds read at CS3 (`int[3]`).
- After resizing storage, the test exposed a descriptor-zero close failure.
- Both fixes pass the portable test with ASan/UBSan (LSan disabled in sandbox).
- `make -C wiringPi -j4` builds the shared library without installing it.
- Upstream version/revision decoding test passes on the host; hardware-only
  RAM detection is skipped by the test. No Orange Pi electrical tests were run.

Remaining SPI lifecycle audit: setup stores the descriptor before all three
configuration ioctls succeed, and repeated setup can replace an open descriptor.
Add failure-injection and resource-lifetime tests before treating those paths
as validated. The present regression focuses on channel storage and FD zero.
