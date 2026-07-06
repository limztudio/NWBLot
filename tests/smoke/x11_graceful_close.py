#!/usr/bin/env python3
# Gracefully close an X11 top-level window owned by a PID by sending a WM_DELETE_WINDOW ClientMessage.
#
# The NWB render apps exit gracefully ONLY via the WM_DELETE_WINDOW protocol (their X11 event loop returns "stop" on
# that message, which unwinds to main()'s return and -- in a NWB_BUILDMODE build -- writes the .namesym sidecar).
# SIGINT/SIGTERM/TerminateProcess skip that path, so a plain kill produces no sidecar. This is the Linux analog of the
# window-capture smoke's Windows WM_CLOSE teardown.
#
# Window discovery uses `xprop` (present on the host) to read _NET_CLIENT_LIST + match WM_NAME against a substring
# (the apps do not set _NET_WM_PID reliably). Only the actual ClientMessage send needs libX11 via ctypes.
import ctypes
import ctypes.util
import subprocess
import sys
import time

XLib = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")

DisplayP = ctypes.c_void_p
Window = ctypes.c_ulong
Atom = ctypes.c_ulong
Bool = ctypes.c_int

# Critical: XOpenDisplay/XInternAtom return pointers/atoms that are 64-bit; ctypes defaults restype to c_int (32-bit),
# which silently truncates the display pointer and crashes on first use. Bind them up front.
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


def list_windows():
    """Return [(wid_hex, wm_name_or_empty), ...] via xprop reading the root _NET_CLIENT_LIST."""
    out = subprocess.run(["xprop", "-root", "_NET_CLIENT_LIST"], capture_output=True, text=True).stdout
    windows = []
    for token in out.replace(",", " ").split():
        if token.startswith("0x"):
            wid = token.rstrip(",")
            name_out = subprocess.run(["xprop", "-id", wid, "WM_NAME"], capture_output=True, text=True).stdout
            name = ""
            if '"' in name_out:
                name = name_out[name_out.index('"') + 1:name_out.rindex('"')]
            windows.append((wid, name))
    return windows


def send_wm_delete(disp, win):
    protocols = XLib.XInternAtom(disp, b"WM_PROTOCOLS", False)
    delete_window = XLib.XInternAtom(disp, b"WM_DELETE_WINDOW", False)
    if not protocols or not delete_window:
        return False
    XLib.XSendEvent.argtypes = [DisplayP, Window, Bool, ctypes.c_long, ctypes.POINTER(XClientMessageEvent)]
    # Deliver directly to the window with NoEventMask (0): on a bare X server (no window manager, e.g. Xwayland headless
    # or a capture rig) the ClientMessage reaches the client's event queue only this way. Sending with the redirect mask
    # instead routes it to a WM that does not exist here, so the app never sees it. Send a few times in case the loop is
    # mid-SleepMS.
    for _ in range(3):
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
        time.sleep(0.15)
    return True


def main():
    if len(sys.argv) < 2:
        print("usage: x11_graceful_close.py <name-substring> [timeout_seconds]", file=sys.stderr)
        return 2
    needle = sys.argv[1].lower()
    timeout_s = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0

    disp = XLib.XOpenDisplay(None)
    if not disp:
        print("error: cannot open X display", file=sys.stderr)
        return 3

    deadline = time.time() + timeout_s
    sent = False
    while time.time() < deadline:
        for wid_hex, name in list_windows():
            if needle in name.lower():
                win = Window(int(wid_hex, 16))
                if send_wm_delete(disp, win):
                    print(f"sent WM_DELETE_WINDOW to window {wid_hex} ('{name}')", flush=True)
                    sent = True
        if sent:
            break
        time.sleep(0.3)

    XLib.XCloseDisplay(disp)
    if not sent:
        print(f"error: no window matching '{needle}' within {timeout_s}s", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
