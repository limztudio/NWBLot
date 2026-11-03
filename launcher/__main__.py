#!/usr/bin/env python3
"""Package entry point (`python -m launcher`)."""

import sys

from launcher.cli import LauncherCli


MAIN_ENTRY = "__main__"


if __name__ == MAIN_ENTRY:
    raise SystemExit(LauncherCli.main(sys.argv[1:]))
