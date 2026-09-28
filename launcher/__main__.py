#!/usr/bin/env python3
"""Package entry point (`python -m launcher`)."""

import sys

from launcher import main


MAIN_ENTRY = "__main__"


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
