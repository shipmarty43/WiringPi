# SPDX-License-Identifier: LGPL-3.0-or-later
"""Loopback acceptance on an Orange Pi Zero 2W with a verified 1 kΩ connection."""
import argparse
import errno
import statistics
import time

from wiringpi_zero2w import PWM1, request_input, request_output


def check_digital():
    for initial in (0, 1):
        with request_output(37, initial=initial) as output, request_input(29, edge="both") as sense:
            if sense.read() != initial:
                raise RuntimeError(f"Pin 29 did not follow the initial level {initial} on pin 37")
            try:
                other = request_input(37)
            except OSError as exc:
                if exc.errno != errno.EBUSY:
                    raise
            else:
                other.close()
                raise RuntimeError("Second request unexpectedly acquired the output")
            for level in (1 - initial, initial):
                output.write(level)
                edge = sense.wait_edge(1)
                expected = "rising" if level else "falling"
                if edge is None or edge.edge != expected or sense.read() != level:
                    raise RuntimeError(f"Missing {expected} transition to {level} on pin 29")
                print(f"37 → 29: {edge.edge}, level {level}, t={edge.timestamp_ns} ns")
            if sense.wait_edge(.05) is not None:
                raise RuntimeError("Unexpected edge on a steady output")
    print("PASS digital loopback, both initial levels, edges and re-acquisition")


def measure(sense, frequency, percent):
    periods, widths = [], []
    start = fall = None
    deadline = time.monotonic() + 1 + 15 / frequency
    while len(periods) < 10:
        remaining = deadline - time.monotonic()
        event = sense.wait_edge(remaining) if remaining > 0 else None
        if event is None:
            raise RuntimeError("Timed out waiting for PWM edges on physical 29")
        if event.edge == "falling":
            if start is not None:
                if fall is not None:
                    raise RuntimeError("Two falling edges in one PWM cycle")
                fall = event.timestamp_ns
        else:
            if start is not None:
                if fall is None or not start < fall < event.timestamp_ns:
                    raise RuntimeError("Incomplete PWM cycle")
                periods.append(event.timestamp_ns - start)
                widths.append(fall - start)
            start, fall = event.timestamp_ns, None
    hz = 1e9 / statistics.median(periods)
    duty = statistics.median(100 * w / p for w, p in zip(widths, periods))
    print(f"Requested {frequency} Hz / {percent}%, measured {hz:.2f} Hz / {duty:.1f}%")
    if abs(hz - frequency) > max(1, frequency * .02) or abs(duty - percent) > 5:
        raise RuntimeError("Measured PWM is outside the loopback limits")


def check_pwm():
    for divisor in (100, 10, 1):
        hz = 1000 // divisor
        with PWM1(range_counts=24000, divisor=divisor) as pwm:
            for percent in (0, 25, 50, 75, 100):
                pwm.set_duty(240 * percent)
                time.sleep(2 / hz)
                # A fresh request excludes the edges caused by reconfiguring PWM.
                with request_input(29, edge="both") as sense:
                    if percent in (0, 100):
                        expected = int(percent == 100)
                        if sense.read() != expected or sense.wait_edge(2 / hz) is not None:
                            raise RuntimeError(f"Pin 29 did not hold {expected} at {percent}%")
                    else:
                        measure(sense, hz, percent)
    print("PASS PWM1 loopback at 10, 100 and 1000 Hz, including steady 0% and 100%")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    wired = parser.add_mutually_exclusive_group(required=True)
    wired.add_argument("--confirm-32-to-29", action="store_true",
                       help="only physical 32 and 29 joined through 1 kΩ; test hardware PWM1")
    wired.add_argument("--confirm-37-to-29", action="store_true",
                       help="only physical 37 and 29 joined through 1 kΩ; test digital I/O")
    args = parser.parse_args()
    if args.confirm_32_to_29:
        check_pwm()
    else:
        check_digital()


if __name__ == "__main__":
    main()
