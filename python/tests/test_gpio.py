# SPDX-License-Identifier: LGPL-3.0-or-later
import errno
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import unittest

import wiringpi_zero2w as gpio


def is_fixture():
    model = Path("/proc/device-tree/model")
    return model.is_file() and model.read_bytes().rstrip(b"\0") == b"WiringPi Zero 2W gpio-sim fixture"


class Header(unittest.TestCase):
    def test_all_contacts(self):
        mapping = dict(zip(
            (3, 5, 7, 8, 10, 11, 12, 13, 15, 16, 18, 19, 21, 22, 23, 24, 26, 27, 28, 29, 31, 32, 33, 35, 36, 37, 38, 40),
            (264, 263, 269, 224, 225, 226, 257, 227, 261, 270, 228, 231, 232, 262, 230, 229, 233, 266, 265, 256, 271, 267, 268, 258, 76, 272, 260, 259),
        ))
        for pin in range(1, 41):
            if pin in mapping:
                self.assertEqual(gpio.native_pin(pin), mapping[pin])
            else:
                with self.assertRaises(ValueError):
                    gpio.native_pin(pin)
        for invalid in (False, True, 0, 41, 2**32 + 29, 3.5, "3"):
            with self.assertRaises(ValueError):
                gpio.native_pin(invalid)

    def test_argument_validation(self):
        for kwargs in ({"bias": "floating"}, {"edge": "level"}, {"debounce_us": 1},
                       {"edge": "both", "debounce_us": 2**32}):
            with self.assertRaises(ValueError):
                gpio.request_input(29, **kwargs)
        with self.assertRaises(ValueError):
            gpio.request_output(29, initial=2)

    @unittest.skipIf(Path("/proc/device-tree/compatible").exists() or
                     Path("/sys/firmware/devicetree/base/compatible").exists(), "host has a device tree")
    def test_without_board(self):
        with self.assertRaises(OSError) as cm:
            gpio.request_input(29)
        self.assertEqual(cm.exception.errno, errno.ENODEV)
        with self.assertRaises(OSError) as cm:
            gpio.PWM1(range_counts=24000, divisor=1)
        self.assertEqual(cm.exception.errno, errno.ENODEV)


@unittest.skipUnless(is_fixture(), "requires isolated QEMU gpio-sim DT")
class KernelLines(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.line = next(Path("/sys/devices/platform").glob("gpio-sim*/gpiochip*/sim_gpio19"))
        cls.pull = cls.line / "pull"
        cls.value = cls.line / "value"

    def test_initial_value_ownership_and_cleanup(self):
        for initial in (1, 0):
            with gpio.request_output(29, initial=initial) as first:
                self.assertEqual(int(self.value.read_text()), initial)
                with self.assertRaises(OSError) as cm:
                    gpio.request_input(29)
                self.assertEqual(cm.exception.errno, errno.EBUSY)
                with gpio.request_input(37):
                    first.write(1 - initial)
                self.assertEqual(first.read(), 1 - initial)
            first.close()
            with self.assertRaises(ValueError):
                first.read()
        with self.assertRaisesRegex(RuntimeError, "partial"):
            with gpio.request_output(29, initial=True):
                raise RuntimeError("partial setup")
        with gpio.request_input(29):
            pass

    def test_info_preserves_ownership(self):
        self.assertFalse(gpio.line_info(29).used)
        with gpio.request_output(29, initial=1):
            self.assertEqual(gpio.line_info(29), (True, "output", "wiringpi-h618-line"))
        self.assertFalse(gpio.line_info(29).used)

    def test_edge_filter_and_debounce(self):
        self.pull.write_text("pull-down")
        with gpio.request_input(29, edge="rising", debounce_us=1000) as line:
            self.pull.write_text("pull-up")
            self.assertEqual(line.wait_edge(1).edge, "rising")
            self.pull.write_text("pull-down")
            self.assertIsNone(line.wait_edge(.02))

    def test_bias_and_direction(self):
        for bias, value in (("up", 1), ("down", 0)):
            with gpio.request_input(29, bias=bias) as line:
                self.assertEqual(line.read(), value)
                with self.assertRaises(PermissionError):
                    line.write(1)
                with self.assertRaises(ValueError):
                    line.wait_edge(0)

    def test_events_and_timeout(self):
        self.pull.write_text("pull-down")
        with gpio.request_input(29, edge="both") as line:
            self.assertIsNone(line.wait_edge(0.005))
            self.pull.write_text("pull-up")
            a = line.wait_edge(1)
            self.pull.write_text("pull-down")
            b = line.wait_edge(1)
            self.assertEqual((a.edge, b.edge), ("rising", "falling"))
            self.assertEqual((a.sequence, b.sequence), (1, 2))
            self.assertLess(a.timestamp_ns, b.timestamp_ns)

    def test_event_loss_is_reported(self):
        self.pull.write_text("pull-down")
        with gpio.request_input(29, edge="both") as line:
            # The kernel queue holds 64 records. Generate more transitions
            # before the first read, then a fresh edge after draining it.
            for i in range(160):
                self.pull.write_text("pull-down" if i % 2 else "pull-up")
                time.sleep(.003)
            with self.assertRaises(OSError) as cm:
                while line.wait_edge(0) is not None:
                    pass
                self.pull.write_text("pull-up")
                line.wait_edge(1)
            self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_close_wakes_waiter(self):
        with gpio.request_input(29, edge="both") as line:
            result = []
            def worker():
                try:
                    result.append(line.wait_edge())
                except OSError as exc:
                    result.append(exc.errno)
            waiter = threading.Thread(target=worker, daemon=True)
            waiter.start()
            deadline = time.monotonic() + 2
            while not line._reader.locked() and time.monotonic() < deadline:
                time.sleep(.001)
            self.assertTrue(line._reader.locked())
            with self.assertRaises(OSError) as cm:
                line.wait_edge(0)
            self.assertEqual(cm.exception.errno, errno.EBUSY)
            line.close()
            waiter.join(2)
            self.assertEqual(result, [errno.ECANCELED])

    def test_process_exit_and_contention(self):
        code = ("import sys, wiringpi_zero2w as g; "
                "line=g.request_output(29, initial=1); print('owned', flush=True); sys.stdin.read()")
        with subprocess.Popen([sys.executable, "-c", code], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, text=True) as other:
            try:
                self.assertEqual(other.stdout.readline().strip(), "owned")
                with self.assertRaises(OSError) as cm:
                    gpio.request_output(29, initial=0)
                self.assertEqual(cm.exception.errno, errno.EBUSY)
            finally:
                other.kill()
                other.wait()
        with gpio.request_output(29, initial=0) as line:
            self.assertEqual(line.read(), 0)
            child = os.fork()
            if not child:
                try:
                    line.write(1)
                except ChildProcessError:
                    line.close()
                    os._exit(0)
                os._exit(1)
            self.assertEqual(os.waitpid(child, 0)[1], 0)
            line.write(1)
            self.assertEqual(line.read(), 1)

    def test_missing_pwm_route(self):
        with self.assertRaises(OSError) as cm:
            gpio.PWM1(range_counts=24000, divisor=1)
        self.assertEqual(cm.exception.errno, errno.ENODEV)


if __name__ == "__main__":
    unittest.main(verbosity=2)
