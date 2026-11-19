# Python GPIO for Orange Pi Zero 2W

`wiringpi_zero2w` exposes the 28 GPIO contacts on the H618 board's 40-pin
header. Pin arguments are **physical header numbers**. The package uses
`ctypes` and version 1 of the [checked C API](../wiringPi/wiringPiH618Line.h)
in this fork. Each digital request owns a separate Linux GPIO v2 file
descriptor. Importing the module leaves the hardware untouched.

## Installation

From this fork's checkout, build the matching shared library on the target
with Linux GPIO v2 headers, a C compiler and libcrypt development headers.
A Python 3.9+ virtual environment with pip is sufficient for installing a wheel.

```sh
make -j2 -C wiringPi
sudo make -C wiringPi install
python3 -m venv ~/zero2w-venv
~/zero2w-venv/bin/pip install ./python
~/zero2w-venv/bin/python -m wiringpi_zero2w --map
~/zero2w-venv/bin/python -m wiringpi_zero2w
```

The first command-line report lists the header mapping without opening a
chip. The second also reads kernel ownership, consumer and direction. An
unclaimed GPIO's direction or an empty consumer cannot identify its pinmux,
voltage or alternate function. Ordinary applications need device permissions
for the relevant gpiochip. PWM1 additionally requires access to the kernel
PWM sysfs nodes. The library checks the board and SoC DT compatibles,
discovers the H618 controller, and propagates Linux errors through `OSError`.

For the optional **GPIO header** page in the configng fork, install the wheel
in a root-owned venv at `/opt/wiringpi-zero2w` (or in system Python).
The page runs the ownership report with Python isolated mode. The
`wiringpi-zero2w` wheel can be installed offline with
`/opt/wiringpi-zero2w/bin/pip install --no-index /path/to/wiringpi_zero2w-0.1.0-py3-none-any.whl`.

## Digital lines and events

This example uses physical 29 on a free input. Bias options are
`None` (kernel default), `"off"`, `"up"`, and `"down"`. Edge options are
`"rising"`, `"falling"`, and `"both"`. Optional debounce is in microseconds
and depends on the running kernel and controller.

```python
from wiringpi_zero2w import line_info, native_pin, request_input

print(native_pin(29), line_info(29))
with request_input(29, edge="both") as signal:
    print(signal.read())
    event = signal.wait_edge(timeout=1.0)
    if event is not None:
        print(event.edge, event.timestamp_ns, event.sequence)
```

Use `with request_output(pin, initial=0) as line:` on a contact wired for
output. `line.write(1)` changes the level. The initial level travels in the
kernel's original request. Requesting a busy line raises `OSError(EBUSY)`.
`close()` or context exit releases only that handle. The driver determines
the electrical state after release.

A timeout returns `None`. Events carry the monotonic kernel timestamp in
nanoseconds and a sequence for the request. A sequence gap raises
`OSError(EOVERFLOW)` and advances past the record that exposed the gap.
One thread can wait on a given line. Another thread can call `close()` to
cancel a blocked wait with `ECANCELED`. The wrapper creates no callback
threads. Do not use a parent's live handles after fork. An exec or process
termination drops the process's digital descriptors. An inherited copy in a
forked child also needs to be closed or exec'd before the kernel releases the
last reference. Use context managers for deterministic cleanup.

## Hardware PWM1

`PWM1` uses the qualified Armbian `pwm1-pi11` route on physical **32**. With
that route active, the Python digital API refuses to claim the same contact.
Period in nanoseconds is `round(range_counts × divisor × 125 / 3)`. Duty is a
count from 0 to `range_counts`. The kernel backend handles the sysfs updates.
An update can introduce a gap. A killed process may leave a sysfs channel
exported. A subsequent client reports contention and leaves that export alone.

```python
from wiringpi_zero2w import PWM1

with PWM1(range_counts=24000, divisor=1, duty=6000) as pwm:
    pwm.set_duty(12000)  # 1 kHz, 50%
```

## Verification

Run the package tests on a Linux host with the new library on the loader
path. The PWM fixture compiles the real backend against a temporary DT and
sysfs and tests Python ownership and cleanup after an I/O error.

```sh
PYTHONPATH=python python3 -m unittest discover -s python/tests -v
python/tests/test-pwm.sh
make -j1 -C wiringPi/test host-test
```

The kernel integration tests run only on the named QEMU fixture. Supply an
ARM64 Linux Image with GPIO character devices, gpio-sim and devtmpfs built in,
and a test initramfs with Python, this library, the installed wheel and
`tests/test_gpio.py`. Its `/init` mounts /dev, /proc and /sys, runs the tests
and prints `PASS-GPIO` only on success before powering down. The runner
constructs the fixture DT from the emulator's own machine description, checks
that the event test actually ran, and limits the guest to 256 MiB and one CPU.

```sh
python/tests/run-qemu.sh /path/to/Image /path/to/test-initramfs.gz
```

## Board acceptance

First run `python -m wiringpi_zero2w` and verify the connector orientation and
that both named contacts are free of other peripherals. Use physical header
numbers and a 1 kΩ series resistor. Power down before moving the resistor.
Choose one command for the wiring actually present.

| Physical wiring | Command | What must pass |
|---|---|---|
| **32 (PI11/PWM1) → 29 (PI0)**, booted with Armbian `pwm1-pi11` | `python python/tests/loopback.py --confirm-32-to-29` | 10, 100 and 1000 Hz, duty 25/50/75%, stable LOW/HIGH at 0/100%, release between frequencies |
| **37 (PI16) → 29 (PI0)** | `python python/tests/loopback.py --confirm-37-to-29` | Both initial levels, subsequent rising/falling edges at the receiving pin, busy output, quiet line and re-acquisition |

PWM measurements use the kernel timestamps of ten complete cycles per setting.
Frequency tolerance is the greater of 2% and 1 Hz; duty tolerance is five
percentage points. For independent verification of pulse widths and any short
transient during setup, capture the same signal with an external analyzer.
To qualify bias, use a free, electrically isolated input and compare the
measured level with `request_input(pin, bias="up")` and `bias="down"`. If
debounce matters to a peripheral, inject known pulse widths on its input and
record the filter threshold reported or supported by that kernel/controller.
After each test reopen the ownership page and verify the test lines are
released. A read-only inventory can be used with attached peripherals.
