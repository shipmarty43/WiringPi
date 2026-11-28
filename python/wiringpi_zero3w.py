# SPDX-License-Identifier: LGPL-3.0-or-later
"""Orange Pi Zero 3W 40-pin header GPIO using main PIO and standby R_PIO."""
import ctypes as ct
import errno
from functools import lru_cache

import wiringpi_zero2w as common


@lru_cache(maxsize=1)
def _library():
    lib = common._library()
    i = ct.c_int
    for name, args in (("HeaderToNative",[i]),("LineInfo",[i,ct.POINTER(common._Info)]),
                       ("LineRequest",[i,i,i,i,i,ct.c_uint32])):
        try:
            fn = getattr(lib, "wiringPiA733" + name)
        except AttributeError as exc:
            raise RuntimeError("Install the A733 build of libwiringPi") from exc
        fn.argtypes, fn.restype, fn.errcheck = args, i, common._checked
    return lib


def native_pin(pin):
    common._integer(pin,1,40,"physical pin")
    try:
        return _library().wiringPiA733HeaderToNative(pin)
    except OSError as exc:
        if exc.errno == errno.EINVAL:
            raise ValueError(f"Header contact {pin} is not a GPIO") from exc
        raise


def header_map():
    for pin in range(1,41):
        try:
            n = native_pin(pin)
        except ValueError:
            continue
        yield pin, n, f"P{chr(65 + n//32)}{n%32}"


def line_info(pin):
    native_pin(pin)
    info = common._Info()
    _library().wiringPiA733LineInfo(pin,ct.byref(info))
    return common.LineInfo(bool(info.used),{0:"input",1:"output"}.get(info.direction,"unknown"),
                           info.consumer.decode(errors="replace"))


def request_input(pin, *, bias=None, edge=None, debounce_us=0):
    native_pin(pin)
    return common._input(pin,bias,edge,debounce_us,_library().wiringPiA733LineRequest)


def request_output(pin, *, initial):
    native_pin(pin)
    return common.Line(pin,1,common._level(initial),0,0,0,_request=_library().wiringPiA733LineRequest)
