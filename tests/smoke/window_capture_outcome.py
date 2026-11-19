import sys


class SmokeSkip(Exception):
    pass


class WindowsDesktopUnavailable(SmokeSkip):
    pass


class SmokeFailure(Exception):
    pass


def write_status(message):
    print(message, file=sys.stderr, flush=True)
