#!/usr/bin/env python3
# Gracefully close a known X11 top-level window by sending a WM_DELETE_WINDOW ClientMessage.
#
# Send WM_DELETE_WINDOW so X11 apps exit cleanly and write .namesym sidecars.
#
# Send the ClientMessage to the exact captured X11 window ID.
import ctypes
import ctypes.util
import sys

LIT_ERROR_INVALID_X11_WINDOW_ID = "error: invalid X11 window ID '{v}'"
LIT_MAIN = "__main__"

xlib = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")

DisplayP = ctypes.c_void_p
Window = ctypes.c_ulong
Atom = ctypes.c_ulong
Bool = ctypes.c_int

# Explicit result types prevent ctypes from truncating 64-bit pointers and atoms.
xlib.XOpenDisplay.restype = DisplayP
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XCloseDisplay.argtypes = [DisplayP]
xlib.XFlush.argtypes = [DisplayP]
xlib.XInternAtom.restype = Atom
xlib.XInternAtom.argtypes = [DisplayP, ctypes.c_char_p, Bool]


X11_CLIENT_MESSAGE_TYPE = 33
X11_CLIENT_MESSAGE_FORMAT_BITS = 32


class XClientMessageEvent(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", Bool), ("display", DisplayP), ("window", Window), ("message_type", Atom), ("format", ctypes.c_int), ("data_l", ctypes.c_long * 5),]


def send_wm_delete(disp, win):
    protocols = xlib.XInternAtom(disp, b"WM_PROTOCOLS", False)
    delete_window = xlib.XInternAtom(disp, b"WM_DELETE_WINDOW", False)
    if not protocols or not delete_window:
        return False
    xlib.XSendEvent.argtypes = [DisplayP, Window, Bool, ctypes.c_long, ctypes.POINTER(XClientMessageEvent)]
    # Send once with NoEventMask so delivery works without a window manager.
    ev = XClientMessageEvent()
    ev.type = X11_CLIENT_MESSAGE_TYPE  # ClientMessage
    ev.display = disp
    ev.window = win
    ev.message_type = protocols
    ev.format = X11_CLIENT_MESSAGE_FORMAT_BITS
    ev.data_l[0] = delete_window
    ev.data_l[1] = 0  # timestamp
    if xlib.XSendEvent(disp, win, False, 0, ctypes.byref(ev)) == 0:
        return False
    xlib.XFlush(disp)
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

    disp = xlib.XOpenDisplay(None)
    if not disp:
        print("error: cannot open X display", file=sys.stderr)
        return 3

    sent = send_wm_delete(disp, Window(window_id))
    xlib.XCloseDisplay(disp)
    if not sent:
        print(f"error: failed to send WM_DELETE_WINDOW to 0x{window_id:x}", file=sys.stderr)
        return 1

    print(f"sent WM_DELETE_WINDOW to window 0x{window_id:x}", flush=True)
    return 0


if __name__ == LIT_MAIN:
    sys.exit(main())
