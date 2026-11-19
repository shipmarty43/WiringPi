# SPDX-License-Identifier: LGPL-3.0-or-later
"""Exercise the hardware test's measurement and limits with scripted events."""
import contextlib
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import loopback
from wiringpi_zero2w import EdgeEvent


def pulse_train(hz, duty):
    period = round(1e9 / hz)
    t = 0
    for i in range(11):
        yield EdgeEvent("rising", t, 2*i + 1)
        yield EdgeEvent("falling", t + period*duty//100, 2*i + 2)
        t += period


class Measurement(unittest.TestCase):
    def measure(self, events, hz=100, duty=25):
        line = Mock()
        line.wait_edge.side_effect = events
        loopback.measure(line, hz, duty)

    def test_qualified_range(self):
        for hz in (10, 100, 1000):
            for duty in (25, 50, 75):
                self.measure(pulse_train(hz, duty), hz, duty)

    def test_bad_signals(self):
        for events in ([None], list(pulse_train(100, 25))[::2],
                       pulse_train(120, 25), pulse_train(100, 75)):
            with self.assertRaises(RuntimeError):
                self.measure(events)


    def test_confirm_one_wiring(self):
        with patch.object(loopback, "check_pwm") as pwm, patch.object(loopback, "check_digital") as digital:
            for args in ([], ["--confirm-37-to-29", "--confirm-32-to-29"]):
                with patch("sys.argv", ["loopback.py", *args]), self.assertRaises(SystemExit) as cm:
                    loopback.main()
                self.assertEqual(cm.exception.code, 2)
            pwm.assert_not_called()
            digital.assert_not_called()


class DigitalWire(unittest.TestCase):
    def test_simulated_wire(self):
        from test_gpio import is_fixture
        if not is_fixture():
            self.skipTest("requires isolated QEMU gpio-sim DT")
        import wiringpi_zero2w as gpio
        pull = next(Path("/sys/devices/platform").glob("gpio-sim*/gpiochip*/sim_gpio19/pull"))

        @contextlib.contextmanager
        def wired_output(pin, *, initial):
            with gpio.request_output(pin, initial=initial) as out:
                def mirror():
                    pull.write_text("pull-up" if out.read() else "pull-down")

                def write(level):
                    out.write(level)
                    mirror()

                mirror()
                yield Mock(write=write)

        with patch.object(loopback, "request_output", wired_output):
            loopback.check_digital()


if __name__ == "__main__":
    unittest.main()
