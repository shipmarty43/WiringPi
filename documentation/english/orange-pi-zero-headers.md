# Orange Pi Zero 2W and Zero 3W header reference

Use the **physical pin** to follow a wire from one board to the other. For example, header pin 32 is PI11 on the Zero 2W (wPi 21, sunxi 267) and PD1 on the Zero 3W (PIO line 97). Both pads can generate PWM, through different controllers.

**Library support.** This fork implements the Zero 2W (H618) GPIO backend and the `wiringpi_zero2w` Python binding. The Zero 3W (A733) columns are a porting reference. Its tested next-boot pin configurations live in the companion Armbian configurator. [H618 API and hardware tests](h618-support.md) · [A733 configurator][a733]

## The 40-pin header

Both boards have 28 GPIO contacts. The fixed supply contacts are **1 and 17** (3.3 V) and **2 and 4** (5 V). Ground is on **6, 9, 14, 20, 25, 30, 34 and 39**. Pin 1 is marked on the board.

The H618 wPi column is for `wiringPiSetup()`. `wiringPiSetupPhys()` and the Python binding take physical pin numbers. `wiringPiSetupGpio()` on this backend takes the sunxi number. A733 offsets are local to the indicated GPIO controller. Resolve the Linux `/dev/gpiochipN` by its device-tree node before using an offset. For A733, PIO is `/soc@3000000/pinctrl@2000000` and R_PIO is `/soc@3000000/pinctrl@7025000`. [H618 numbering and discovery](h618-support.md#header-map-and-kernel-discovery) · [Linux GPIO character-device API](https://docs.kernel.org/userspace-api/gpio/chardev.html)

Each row can be used for digital GPIO when free. The signal columns list selected peripheral routes. TWI is Allwinner's name for I²C. An em dash means that the table lists only GPIO for that pad. The active function depends on the board's device tree and its owners.

| Physical pin | H618 pad | H618 wPi | H618 sunxi | Zero 2W signals | A733 pad | A733 controller / offset | Zero 3W signals |
|---:|---|---:|---:|---|---|---|---|
| 3 | PI8 | 0 | 264 | TWI1 SDA, UART2 CTS | PB3 | PIO / 35 | SPI2 MISO, TWI0 SDA, UART2 CTS |
| 5 | PI7 | 1 | 263 | TWI1 SCL, UART2 RTS | PB2 | PIO / 34 | SPI2 MOSI, TWI0 SCL, UART2 RTS |
| 7 | PI13 | 2 | 269 | UART4 TX, PWM3 | PB4 | PIO / 36 | SPI2 CS1, PWM0-0 |
| 8 | PH0 | 3 | 224 | UART0 TX, PWM3 | PB9 | PIO / 41 | UART0 TX (debug) |
| 10 | PH1 | 4 | 225 | UART0 RX, PWM4 | PB10 | PIO / 42 | UART0 RX (debug) |
| 11 | PH2 | 5 | 226 | TWI2 SCL, UART5 TX, PWM2 | PB0 | PIO / 32 | SPI2 CS0, UART2 TX |
| 12 | PI1 | 6 | 257 | — | PB5 | PIO / 37 | SPI2 CS2, PWM0-1¹ |
| 13 | PH3 | 7 | 227 | TWI2 SDA, UART5 RX, PWM1 | PB1 | PIO / 33 | SPI2 SCLK, UART2 RX |
| 15 | PI5 | 8 | 261 | TWI0 SCL, UART2 TX | PE11 | PIO / 139 | — |
| 16 | PI14 | 9 | 270 | UART4 RX, PWM4 | PL2 | R_PIO / 2 | S_UART0 TX |
| 18 | PH4 | 10 | 228 | TWI3 SCL | PL3 | R_PIO / 3 | S_UART0 RX |
| 19 | PH7 | 11 | 231 | SPI1 MOSI, TWI4 SDA, UART2 RTS | PE2 | PIO / 130 | SPI3 MOSI, TWI2 SDA, UART6 RTS |
| 21 | PH8 | 12 | 232 | SPI1 MISO, UART2 CTS | PE3 | PIO / 131 | SPI3 MISO, TWI3 SCL, UART6 CTS, PWM0-0¹ |
| 22 | PI6 | 13 | 262 | TWI0 SDA, UART2 RX | PD0 | PIO / 96 | PWM0-0¹ |
| 23 | PH6 | 14 | 230 | SPI1 SCLK, TWI4 SCL, UART2 RX | PE1 | PIO / 129 | SPI3 SCLK, TWI2 SCL, UART6 RX |
| 24 | PH5 | 15 | 229 | SPI1 CS0, TWI3 SDA, UART2 TX | PE0 | PIO / 128 | SPI3 CS0, UART6 TX |
| 26 | PH9 | 16 | 233 | SPI1 CS1 | PE4 | PIO / 132 | SPI3 CS1, TWI3 SDA, PWM0-1¹ |
| 27 | PI10 | 17 | 266 | TWI2 SDA, UART3 RX | PL5 | R_PIO / 5 | S_TWI2 SDA¹ |
| 28 | PI9 | 18 | 265 | TWI2 SCL, UART3 TX | PL4 | R_PIO / 4 | S_TWI2 SCL¹ |
| 29 | PI0 | 19 | 256 | — | PE12 | PIO / 140 | UART1 TX¹ |
| 31 | PI15 | 20 | 271 | UART4 RTS | PE13 | PIO / 141 | UART1 RX¹ |
| 32 | PI11 | 21 | 267 | UART3 RTS, PWM1 | PD1 | PIO / 97 | PWM0-1 |
| 33 | PI12 | 22 | 268 | UART3 CTS, PWM2 | PD3 | PIO / 99 | PWM0-3 |
| 35 | PI2 | 23 | 258 | — | PB6 | PIO / 38 | SPI2 CS3, PWM0-8, PWM0-2¹ |
| 36 | PC12 | 24 | 76 | — | PD2 | PIO / 98 | PWM0-2 |
| 37 | PI16 | 25 | 272 | UART4 CTS | PD4 | PIO / 100 | PWM0-4 |
| 38 | PI4 | 26 | 260 | — | PB8 | PIO / 40 | TWI1 SDA, PWM1-0¹ |
| 40 | PI3 | 27 | 259 | — | PB7 | PIO / 39 | TWI1 SCL, PWM0-9 |

¹ These A733 alternatives come from the pad mux and board drawings. The reference image routes S_TWI2 through PL10/PL11 to the HYM8563 RTC at 0x51, and UART1 through PG6–PG9 to Bluetooth. The numbered PWM0 channels belong to the main PWM0 controller. For instance, its stock channel 0 overlay uses PB4 and the silicon also permits PD0 or PE3. [Schematics and silicon audit][audit] · [Vendor pinmux][a733-mux]

## Choosing routes without double-booking pins

| Use case | Zero 2W | Zero 3W |
|---|---|---|
| I²C on header 3/5 (SDA/SCL) | TWI1 on PI8/PI7 serves the DFR0528 UPS. UART2 PI flow control occupies this pair. | TWI0 on PB3/PB2 shares the pair with SPI2 data and UART2 flow control. |
| SPI on 19/21/23 (MOSI/MISO/SCLK) | SPI1. Native CS0 is on 24 and CS1 on 26. The Waveshare SX1262 shield wires NSS to **40/PI3** and requires its matching chip-select overlay. | SPI3. Native CS0 on **24/PE0** conflicts with the enabled camera MCLK. UART6 uses that pad for TX. |
| Additional native chip selects | SPI0 in the reference DTB describes the board's flash. | SPI2 supports CS0–CS3 on **11/7/12/35**. CS3 on PB6 conflicts with the vendor pin-test wake IRQ. Each declared SPI2 client can select a frequency up to its own DT ceiling and a mode from 0 to 3. |
| UART with hardware flow control | TX/RX/RTS/CTS for UART2 PI are **15/22/5/3**, UART2 PH **24/23/19/21**, UART3 **28/27/32/33**, UART4 **7/16/31/37**. UART3 flow control conflicts with PWM1/PWM2 on PI11/PI12. | UART2 uses **11/13/5/3**, UART6 **24/23/19/21**. The standby UART on 16/18 is S_UART0 (vendor UART7) with TX/RX. |
| Hardware PWM | The WiringPi PWM API drives PWM1 on **32/PI11**. Armbian also exposes the other channel/pad routes in the table. PWM5 supplies the Ethernet clock in the reference DT. | The PWM entries describe SoC mux options for the A733 port. |

A hardware UART handshake requires both the four-pin mux and `CRTSCTS` on the tty. SPI clocks are peripheral ceilings and the controller chooses a divider. For SPI3 the audited A733 BSP minimum is 187,500 Hz and the silicon ceiling is 100 MHz. The H618 clock model and A733 clock model use different dividers. [UART and SPI acceptance][handshake] · [H618 SPI clock model][spi] · [A733 routes and clocks][a733] · [Zero 2W PWM channels][pwm]

## Peripheral operating modes

### I²C (TWI)

Both SoC manuals describe controller and target roles, 7-bit and 10-bit addressing, Standard-mode (100 kHz) and Fast-mode (400 kHz). WiringPi's Linux helpers initiate transactions as the controller. `wiringPiI2CSetupInterface(device, address)` selects the Linux adapter and a 7-bit target address such as `0x10` for the DFR0528 MCU. The SMBus byte, register-word and block helpers and the raw read/write helpers operate on the selected descriptor. The 16 in `wiringPiI2CReadReg16` and `wiringPiI2CWriteReg16` denotes the **data word** width. For a combined write/read with a repeated START, use Linux `I2C_RDWR` on an adapter reporting `I2C_FUNC_I2C`. Ten-bit transactions need adapter support and explicit Linux configuration. [WiringPi I²C implementation](../../wiringPi/wiringPiI2C.c) · [Linux I²C userspace interface](https://docs.kernel.org/i2c/dev-interface.html) · [SoC manuals](#soc-manuals-pdf)

SCL is shared by all devices on the bus. On Zero 2W, `I2C001` selects a whole-bus request up to 400 kHz. With the 24 MHz example parent, 100 and 400 kHz are exact and a 250 kHz request gives 240 kHz. On Zero 3W, `A733IO` offers 100 or 400 kHz on TWI0–3. The UPS profile was checked at both rates on the virtual device. The 400 kHz electrical margin with the actual HAT, pull-ups and wiring still needs a measurement. [H618 TWI clock model][h616-io] · [A733 bus controls][a733] · [Armbian test record][io-tests]

### SPI

Choose the peripheral's clock mode independently of the SCLK frequency and chip select. Leading means the first transition away from idle. [Linux SPI clock modes](https://docs.kernel.org/spi/spi-summary.html)

| SPI mode | CPOL | CPHA | Idle SCLK | Sample edge | Shift edge |
|---:|---:|---:|---|---|---|
| 0 | 0 | 0 | Low | Rising (leading) | Falling |
| 1 | 0 | 1 | Low | Falling (trailing) | Rising |
| 2 | 1 | 0 | High | Falling (leading) | Rising |
| 3 | 1 | 1 | High | Rising (trailing) | Falling |

Use `wiringPiSPIxSetupMode(bus, cs, speed_hz, mode)` for an explicit Linux spidev endpoint. It accepts modes 0–3 and sets 8 bits per word. `wiringPiSPIxSetup()` defaults to mode 0. `wiringPiSPIxDataRW()` performs a single full-duplex transfer, with received bytes replacing the transmit buffer. The SoC manuals also describe SPI target operation, three-wire transfers, and dual/quad data paths. The fork's setup API exposes conventional single-lane controller transfers. Wider lanes require matching pins, a device binding and driver support. [WiringPi SPI implementation](../../wiringPi/wiringPiSPI.c) · [SoC manuals](#soc-manuals-pdf)

On H618, the configurator models 3 kHz–100 MHz and limits each endpoint to its composed DT ceiling. The H616 clock controller sets MCLK and the SPI block divides it again. At an example 24 MHz MCLK, a 5 MHz request gives 4 MHz. On A733 SPI2/SPI3, the supported request range in the vendor profile is 187,500 Hz–100 MHz. For a given parent clock, the A733 CCU grid is `parent_hz / ((M + 1) × (N + 1))`, with M and N each in 0–31. Use the attached device's own limit. On a physical Zero 2W, the Waveshare SX1262 answered `GetStatus` at 500 kHz and passed receive-timeout checks on the 2 MHz path in mode 0. [H618 frequency grid][spi] · [A733 clock model][a733] · [SX1262 board results][spi-board]

### UART

Two-wire operation uses TX/RX. Hardware flow control adds RTS/CTS and requires the four pads in the route table above. Framing specifies data bits (5–8), parity (none, even or odd) and stop length (1 or 2, with 1.5 as the long stop for five data bits). For example, 8N1 is eight data bits, no parity and one stop bit. The nominal baud is `uart_clock_hz / (16 × divisor)`. Linux termios readback confirms the configured setting. The Waveshare GNSS receiver delivered NMEA on Zero 2W UART0 at 9,600 baud, 8N1. [H618 UART controls][h616-io] · [Four-wire route tests][handshake] · [GNSS board record][gnss]

**Opening the tty changes its settings.** `serialOpen(device, baud)` applies raw 8N1, clears `CRTSCTS`, and asserts RTS and DTR when supported. For 7E2 or automatic RTS/CTS, apply the required termios settings to the returned descriptor after opening it. The function accepts a discrete baud list from 50 through 4,000,000 and returns `-2` for an unlisted rate. The A733 manual gives 1.5 Mbaud for a 24 MHz input and 10 Mbaud for a 160 MHz input on the main UARTs. Its S_UART0 header route exposes TX/RX. Both manuals describe RS-485/9-bit operation. RS-485 direction control requires a compatible tty driver and an external transceiver. [WiringPi serial implementation](../../wiringPi/wiringSerial.c) · [A733 clock and framing settings][a733] · [Hardware audit][audit]

### PWM

On Zero 2W, the WiringPi hardware API supports continuous mark-space PWM (`PWM_MODE_MS`) on PWM1, pin 32/PI11. With `pwmSetClock(divisor)`, `pwmSetRange(range)` and `pwmWrite(pin, value)`, the model is `frequency_hz = 24000000 / (divisor × range)` and `active_duty = value / range`. Divisor 1, range 24,000 and value 6,000 request **1 kHz at 25% duty**. The backend accepts periods from 1 µs to 1 s. Requests for balanced PWM or hardware tone set `errno` to `ENOTSUP`. Software PWM uses a userspace worker and its timing depends on host scheduling. On the board, PWM1 was loopback-tested at 10, 100 and 1,000 Hz with 25%, 50% and 75% duty. [WiringPi PWM1 API and measurements](h618-support.md#public-hardware-pwm1-api--2026-10-27)

The companion `PWM001` menu covers H618 header channels 1–4. It saves 0–100% active duty and normal or inversed polarity. The tested driver forces normal polarity on enable, so an active-low profile requests the complementary high interval. At 1 kHz and 37.5% active-low this gives 625 µs high and 375 µs low. The H616 and A733 manuals also describe finite pulse trains, input capture and complementary pairs with dead time. These are silicon capabilities beyond this fork's PWM API. The A733 header entries document routes for the porting target. [PWM profiles and polarity][h616-io] · [H618 channel routes][pwm] · [A733 PWM reference][audit]

## Bias, pad drive and interrupt debounce

To print the Zero 2W physical-pin map with the matching library installed, use `python -m wiringpi_zero2w --map`. The mapping command works without opening a GPIO chip. [Python binding](../../python/README.md)

On Zero 2W, `pullUpDnControl()` and the Python GPIO line API configure bias on a kernel line request. `gpio readall` inspects line ownership. A peripheral can still own the pinmux when the line has no GPIO consumer. Use the composed device tree to determine the selected function. [H618 GPIO backend](h618-support.md#implemented-scope)

The companion configurator's `GPIO01` can save the board default, pull-up, pull-down or disabled bias, and nominal pad-drive settings of **10, 20, 30 or 40 mA**. Its boot helper takes an exclusive descriptor, applies pinconf and releases it. A later owner can reconfigure the pad. Drive strength and I/O bank supply voltage are separate electrical properties. On the Zero 3W drawings, sheet 6 associates SWOUT2 with 3.3 V while sheet 2 labels PE/PD supplies at 1.8 V. Confirm the actual bank voltage on the board revision before connecting a voltage-sensitive shield. [GPIO01 and driver sources][electrical] · [Schematic comparison][audit]

The hardware IRQ sampling period is shared by an **entire interrupt bank**. GPIO01 accepts **1–3906 µs** and converts to the boot DT units below. Two pins sharing a bank must agree on one requested period. The GPIO v2 line-request debounce attribute is expressed in microseconds and has its own lifetime.

| Boot DT property | Zero 2W, 6.18.57 | Zero 3W, vendor 6.6.98 |
|---|---|---|
| `input-debounce` units | µs | ns |
| Cell order | PIO A, C, D, E, F, G, H, I | PIO B through K. R_PIO L, M |
| 244 µs example | PI0 (pin 29) writes **244** in cell 7 | PD4 (pin 37) writes **244000** in cell 2 |
| Other IRQs affected by a bank change | PC12 (pin 36) shares bank C with the PMIC IRQ on PC9. | PL2 (pin 16) shares bank L with the Type-C controller IRQ on PL9. PB6 is the vendor pin-test IRQ in bank B. |

The drivers round the requested period to an available clock/divider. A zero cell leaves that bank's firmware setting in place. The A733 standby controller starts at bank L, so PL2 is **R_PIO offset 2**. [Electrical test record][electrical-tests] · [GPIO v2 ABI](https://docs.kernel.org/userspace-api/gpio/chardev.html)

## Evidence for this reference

| Board | Armbian DTB used for the audit | Validation |
|---|---|---|
| Zero 2W | `sun50i-h618-orangepi-zero2w.dtb`, 6.18.57-current-sunxi64, SHA-256 `4b90c01af0137b2297fb7243f200c5b32c33ce7e9b0e3d15fbde8c9733363758` | All 28 H618 mappings were checked against the board manual. Digital and IRQ loopback on 37↔29 and PWM1 on 32→29 were exercised on a physical Zero 2W. |
| Zero 3W | `sun60i-a733-orangepi-zero3w.dtb`, linux-dtb-vendor-sun60iw2 26.8.3 / 6.6.98, SHA-256 `1eb1160f92dd1d566af920002583c67fbbb6eb35d190a62fb8f0bc1236bb74e1` | Header nets were checked against the schematics. Routes, conflicts and rollback were tested on the distribution DTB, with UART flow-control readback on Linux PTYs. Electrical qualification on the Zero 3W is pending. |

For another OS image, check the root compatible, GPIO controller, pin groups and enabled consumers before reusing these routes. The audited A733 DTB identifies `xunlong,orangepi-zero3w` and `arm,sun60iw2p1`. An older vendor DTS uses a 4 Pro compatible for a Zero 3W source file. [Board manuals, schematics and revisions][manuals] · [H618 kernel mux](https://github.com/torvalds/linux/blob/v6.18/drivers/pinctrl/sunxi/pinctrl-sun50i-h616.c) · [A733 vendor mux][a733-mux] · [WiringPi physical tests](h618-support.md#physical-loopback-validation)

### SoC manuals (PDF)

- [H616 User Manual v1.0][h616-manual-pdf] — Chapter 9 covers TWI, UART, SPI, GPIO and PWM on the related H616 silicon.
- [A733 User Manual v1.00][a733-manual-pdf] — Sections 18.7–18.11 and 18.14 describe the peripheral controllers and GPIO registers.
- [A733 Datasheet v1.01][a733-datasheet-pdf] — Section 4.2 lists each pad’s alternate functions.

The SHA-256 digests identify the exact editions used for the pin audit. [Document checksums][manual-hashes]

[audit]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/tests/hardware/pin-manual-audit.md
[manuals]: https://github.com/shipmarty43/configng/blob/ba9342548a3d0e6436dd7b965799b2677d1792b9/tests/hardware/zero3w-port.md#offline-reference-library
[a733]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/hardware/a733-io/README.md
[handshake]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/tests/hardware/header-cs-rtscts.md
[spi]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/hardware/sx1262-mode/spi-clock.md
[pwm]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/hardware/h616-io/pwm-header.md
[electrical]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/hardware/header-gpio/README.md
[electrical-tests]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/tests/hardware/header-gpio-electrical.md
[a733-mux]: https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/pinctrl/pinctrl-sun60iw2.c
[h616-io]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/hardware/h616-io/README.md
[io-tests]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/tests/hardware/zero2w-twi-uart-pwm.md
[spi-board]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/tests/hardware/zero2w-radio-board-2026-11-20.md
[gnss]: https://github.com/shipmarty43/configng/blob/64e94903f82a88f30ab6a0c7f548569c4ac40fa7/tests/hardware/zero2w-console-tty.md
[manual-hashes]: https://github.com/shipmarty43/configng/blob/ba9342548a3d0e6436dd7b965799b2677d1792b9/tests/hardware/soc-reference.sha256
[h616-manual-pdf]: https://www.scs.stanford.edu/~zyedidia/docs/allwinner/h616.pdf
[a733-manual-pdf]: https://gitlab.com/tina5.0_aiot/product/docs/-/raw/d0dc4d615be00b76e3a0818d0818eff8f69ffa15/A733/Hardware%E7%A1%AC%E4%BB%B6%E7%B1%BB%E6%96%87%E6%A1%A3/%E8%8A%AF%E7%89%87%E6%89%8B%E5%86%8C/A733_User%20Manual_V1.00.pdf
[a733-datasheet-pdf]: https://gitlab.com/tina5.0_aiot/product/docs/-/raw/d0dc4d615be00b76e3a0818d0818eff8f69ffa15/A733/Hardware%E7%A1%AC%E4%BB%B6%E7%B1%BB%E6%96%87%E6%A1%A3/%E8%8A%AF%E7%89%87%E6%89%8B%E5%86%8C/A733_Datasheet_V1.01.pdf
