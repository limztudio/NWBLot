#!/usr/bin/env python3
# Gracefully close a known X11 top-level window by sending a WM_DELETE_WINDOW ClientMessage.
#
# The NWB render apps exit gracefully ONLY via the WM_DELETE_WINDOW protocol (their X11 event loop returns "stop" on
# that message, which unwinds to main()'s return and -- in a NWB_BUILDMODE build -- writes the .namesym sidecar).
# SIGINT/SIGTERM/TerminateProcess skip that path, so a plain kill produces no sidecar. This is the Linux analog of the
# window-capture smoke's Windows WM_CLOSE teardown.
#
# The capture smoke provides the exact X11 window ID it captured. Only the actual ClientMessage send needs libX11 via
# ctypes, so teardown cannot accidentally close another application's similarly titled window.
import ctypes
import ctypes.util
import sys

LIT_ERROR_INVALID_X11_WINDOW_ID = "error: invalid X11 window ID '{v}'"
LIT_MAIN = "__main__"

XLib = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")

DisplayP = ctypes.c_void_p
Window = ctypes.c_ulong
Atom = ctypes.c_ulong
Bool = ctypes.c_int

# Explicit result types prevent ctypes from truncating 64-bit pointers and atoms.
XLib.XOpenDisplay.restype = DisplayP
XLib.XOpenDisplay.argtypes = [ctypes.c_char_p]
XLib.XCloseDisplay.argtypes = [DisplayP]
XLib.XFlush.argtypes = [DisplayP]
XLib.XInternAtom.restype = Atom
XLib.XInternAtom.argtypes = [DisplayP, ctypes.c_char_p, Bool]


class XClientMessageEvent(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("serial", ctypes.c_ulong),
        ("send_event", Bool),
        ("display", DisplayP),
        ("window", Window),
        ("message_type", Atom),
        ("format", ctypes.c_int),
        ("data_l", ctypes.c_long * 5),
    ]


def send_wm_delete(disp, win):
    protocols = XLib.XInternAtom(disp, b"WM_PROTOCOLS", False)
    delete_window = XLib.XInternAtom(disp, b"WM_DELETE_WINDOW", False)
    if not protocols or not delete_window:
        return False
    XLib.XSendEvent.argtypes = [DisplayP, Window, Bool, ctypes.c_long, ctypes.POINTER(XClientMessageEvent)]
    # NoEventMask delivers directly even without a window manager. Send once: a retry can race window destruction.
    ev = XClientMessageEvent()
    ctypes.memset(ctypes.byref(ev), 0, ctypes.sizeof(ev))
    ev.type = 33  # ClientMessage
    ev.display = disp
    ev.window = win
    ev.message_type = protocols
    ev.format = 32
    ev.data_l[0] = delete_window
    ev.data_l[1] = 0  # timestamp
    if XLib.XSendEvent(disp, win, False, 0, ctypes.byref(ev)) == 0:
        return False
    XLib.XFlush(disp)
    return True


def main():
    if len(sys.argv) < 2:
        print("usage: x11_graceful_close.py <window-id>", file=sys.stderr)
        return 2

    try:
        window_id = int(sys.argv[1], 0)
    except ValueError:
        print(LIT_ERROR_INVALID_X11_WINDOW_ID.format(v=sys.argv[1]), file=sys.stderr)
        return 2
    if window_id <= 0:
        print(LIT_ERROR_INVALID_X11_WINDOW_ID.format(v=sys.argv[1]), file=sys.stderr)
        return 2

    disp = XLib.XOpenDisplay(None)
    if not disp:
        print("error: cannot open X display", file=sys.stderr)
        return 3

    sent = send_wm_delete(disp, Window(window_id))
    XLib.XCloseDisplay(disp)
    if not sent:
        print(f"error: failed to send WM_DELETE_WINDOW to 0x{window_id:x}", file=sys.stderr)
        return 1

    print(f"sent WM_DELETE_WINDOW to window 0x{window_id:x}", flush=True)
    return 0


if __name__ == LIT_MAIN:
    sys.exit(main())
