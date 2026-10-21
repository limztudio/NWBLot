"""Smoke outcome signalling shared by the window-capture harness and its scripts: skip and failure exceptions plus stderr status lines."""


import sys


class SmokeSkip(Exception):
    pass


class WindowsDesktopUnavailable(SmokeSkip):
    pass


class SmokeFailure(Exception):
    pass


def write_status(message):
    print(message, file=sys.stderr, flush=True)
