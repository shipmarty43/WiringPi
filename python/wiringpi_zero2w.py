# SPDX-License-Identifier: LGPL-3.0-or-later
"""Orange Pi Zero 2W. All pin numbers refer to the physical 40-pin header."""
import ctypes as ct
import errno
import math
import os
import select
import threading
import weakref
from functools import lru_cache
from typing import NamedTuple


class EdgeEvent(NamedTuple):
    edge: str
    timestamp_ns: int
    sequence: int


class LineInfo(NamedTuple):
    used: bool
    direction: str
    consumer: str


class _Info(ct.Structure):
    _fields_ = [("used", ct.c_int), ("direction", ct.c_int), ("consumer", ct.c_char * 32)]


class _Event(ct.Structure):
    _fields_ = [("timestamp_ns", ct.c_uint64), ("sequence", ct.c_uint32), ("edge", ct.c_uint32)]


def _checked(result, function, args):
    if result < 0:
        err = ct.get_errno()
        raise OSError(err, f"{function.__name__}: {os.strerror(err)}")
    return result


@lru_cache(maxsize=1)
def _library():
    try:
        lib = ct.CDLL("libwiringPi.so", use_errno=True)
    except OSError as exc:
        raise RuntimeError("Install the Zero 2W build of libwiringPi on the loader path") from exc
    i, u = ct.c_int, ct.c_uint
    signatures = {
        "LineABI": [], "HeaderToNative": [i], "LineRequest": [i, i, i, i, i, ct.c_uint32],
        "LineInfo": [i, ct.POINTER(_Info)], "LineRead": [i], "LineWrite": [i, i], "LineEvent": [i, ct.POINTER(_Event)],
        "PwmClaim": [i, u, u, u], "PwmSet": [i, u], "PwmRelease": [i],
    }
    try:
        for name, params in signatures.items():
            func = getattr(lib, "wiringPiH618" + name)
            func.argtypes, func.restype, func.errcheck = params, i, _checked
    except AttributeError as exc:
        raise RuntimeError("libwiringPi is missing the Zero 2W line API") from exc
    if lib.wiringPiH618LineABI() != 1:
        raise RuntimeError("Unsupported Zero 2W line ABI (version 1 required)")
    return lib


def _integer(value, low, high, name):
    if not isinstance(value, int) or isinstance(value, bool) or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in {low}..{high}")
    return value


def _level(value):
    if not isinstance(value, int) or value not in (0, 1):
        raise ValueError("GPIO level must be 0 or 1")
    return int(value)


def native_pin(pin):
    """Resolve a physical header pin to its sunxi number without opening a device."""
    pin = _integer(pin, 1, 40, "physical pin")
    try:
        return _library().wiringPiH618HeaderToNative(pin)
    except OSError as exc:
        if exc.errno == errno.EINVAL:
            raise ValueError(f"Header contact {pin} is not a GPIO") from exc
        raise


def header_map():
    """Yield (physical pin, sunxi number, signal name) for each GPIO contact."""
    for pin in range(1, 41):
        try:
            native = native_pin(pin)
        except ValueError:  # Fixed power, ground or control contact.
            continue
        bank, offset = divmod(native, 32)
        yield pin, native, f"P{chr(ord('A') + bank)}{offset}"


def line_info(pin):
    """Read kernel metadata without taking ownership of the line."""
    native_pin(pin)
    info = _Info()
    _library().wiringPiH618LineInfo(pin, ct.byref(info))
    return LineInfo(bool(info.used), {0: "input", 1: "output"}.get(info.direction, "unknown"),
                    info.consumer.decode(errors="replace"))


def _close_fds(fds):
    for fd in reversed(fds):
        os.close(fd)


class Line:
    """A kernel-owned single-line request. Construct with request_input/output."""

    def __init__(self, pin, output, initial, bias, edge, debounce):
        native_pin(pin)
        self._lib = _library()
        self._pid = os.getpid()
        self._lock = threading.Lock()
        self._reader = threading.Lock()
        self._sequence = 0
        self._edge = edge
        self._cancel = ()
        self._fd = self._lib.wiringPiH618LineRequest(pin, output, initial, bias, edge, debounce)
        try:
            if edge:
                self._cancel = os.pipe2(os.O_CLOEXEC | os.O_NONBLOCK)
            self._release = weakref.finalize(self, _close_fds, (self._fd, *self._cancel))
        except BaseException:
            _close_fds((self._fd, *self._cancel))
            raise

    def _owner(self):
        if os.getpid() != self._pid:
            raise ChildProcessError(errno.ECHILD, "GPIO line belongs to the parent process")

    def _open(self):
        if not self._release.alive:
            raise ValueError("GPIO line is closed")
        return self._fd

    def __enter__(self):
        self._owner()
        with self._lock:
            self._open()
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        """Release this request and wake a thread blocked in wait_edge."""
        if os.getpid() != self._pid:
            self._release()  # Drop only the child's inherited descriptors.
            return
        with self._lock:
            if not self._release.alive:
                return
            if self._edge:
                os.write(self._cancel[1], b"x")
            with self._reader:
                self._release()

    def read(self):
        self._owner()
        with self._lock:
            return self._lib.wiringPiH618LineRead(self._open())

    def write(self, value):
        self._owner()
        with self._lock:
            self._lib.wiringPiH618LineWrite(self._open(), _level(value))

    def wait_edge(self, timeout=None):
        """Return EdgeEvent or None on timeout (seconds). One waiter per request."""
        self._owner()
        if timeout is not None:
            if not math.isfinite(timeout) or not 0 <= timeout <= 2147483:
                raise ValueError("Invalid edge timeout in seconds")
            timeout = math.ceil(timeout * 1000)
        with self._lock:
            fd = self._open()
            if not self._edge:
                raise ValueError("Request an input with edge detection first")
            if not self._reader.acquire(blocking=False):
                raise OSError(errno.EBUSY, "Another thread is waiting for this line")
        try:
            poll = select.poll()
            poll.register(fd, select.POLLIN)
            poll.register(self._cancel[0], select.POLLIN)
            ready = dict(poll.poll(timeout))
            if self._cancel[0] in ready:
                raise OSError(errno.ECANCELED, "GPIO wait cancelled by close")
            if fd not in ready:
                return None
            if not ready[fd] & select.POLLIN:
                raise OSError(errno.EIO, "GPIO event request disconnected")
            event = _Event()
            self._lib.wiringPiH618LineEvent(fd, ct.byref(event))
            expected = (self._sequence + 1) & 0xffffffff
            self._sequence = event.sequence
            if expected != event.sequence:
                raise OSError(errno.EOVERFLOW, f"Expected edge {expected}, received {event.sequence}")
            return EdgeEvent("rising" if event.edge == 1 else "falling", event.timestamp_ns, event.sequence)
        finally:
            self._reader.release()


def request_input(pin, *, bias=None, edge=None, debounce_us=0):
    """Claim a physical GPIO input. Bias is None, 'off', 'up' or 'down'."""
    biases = (None, "off", "up", "down")
    edges = (None, "rising", "falling", "both")
    debounce = _integer(debounce_us, 0, 0xffffffff, "debounce_us")
    if debounce and edge is None:
        raise ValueError("debounce_us requires edge detection")
    return Line(pin, 0, 0, biases.index(bias), edges.index(edge), debounce)


def request_output(pin, *, initial):
    """Claim a physical GPIO output with its initial level in the same ioctl."""
    return Line(pin, 1, _level(initial), 0, 0, 0)


def _release_pwm(lib, pid, token):
    if os.getpid() == pid:
        lib.wiringPiH618PwmRelease(token)


class PWM1:
    """Kernel PWM on pin 32. Range and duty are counts, divisor uses a 24 MHz clock."""

    def __init__(self, *, range_counts, divisor, duty=0):
        self._range = _integer(range_counts, 1, 0xffffffff, "range_counts")
        divisor = _integer(divisor, 1, 4095, "divisor")
        duty = _integer(duty, 0, self._range, "duty")
        self._pid = os.getpid()
        self._lock = threading.Lock()
        self._lib = _library()
        self._token = self._lib.wiringPiH618PwmClaim(32, self._range, divisor, duty)
        try:
            self._release = weakref.finalize(self, _release_pwm, self._lib, self._pid, self._token)
        except BaseException:
            self._lib.wiringPiH618PwmRelease(self._token)
            raise

    def _owner(self):
        if os.getpid() != self._pid:
            raise ChildProcessError(errno.ECHILD, "PWM belongs to the parent process")

    def __enter__(self):
        self._owner()
        with self._lock:
            if not self._release.alive:
                raise ValueError("PWM is closed")
        return self

    def __exit__(self, *exc):
        self.close()

    def set_duty(self, duty):
        self._owner()
        with self._lock:
            if not self._release.alive:
                raise ValueError("PWM is closed")
            self._lib.wiringPiH618PwmSet(self._token, _integer(duty, 0, self._range, "duty"))

    def close(self):
        self._owner()
        with self._lock:
            if self._release.alive:
                try:
                    self._lib.wiringPiH618PwmRelease(self._token)
                except OSError as exc:
                    if exc.errno != errno.ESTALE:
                        raise  # Keep the cleanup available for a retry.
                self._release.detach()


def main():
    """Print the physical header map and optional kernel ownership information."""
    import argparse
    parser = argparse.ArgumentParser(description=main.__doc__)
    parser.add_argument("--map", action="store_true", help="print pin names offline")
    args = parser.parse_args()
    print("Header  Sunxi  Name" + ("" if args.map else "  Used  Direction  Consumer"))
    try:
        for pin, native, name in header_map():
            row = f"{pin:6}  {native:5}  {name:<4}"
            if not args.map:
                info = line_info(pin)
                row += f"  {str(info.used):5} {info.direction:9}  {info.consumer or '-'}"
            print(row)
    except (RuntimeError, OSError) as exc:
        parser.exit(1, f"{parser.prog}: {exc}\n")


if __name__ == "__main__":
    main()
