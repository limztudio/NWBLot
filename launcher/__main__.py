#!/usr/bin/env python3
"""Package entry point (`python -m launcher`)."""

import sys

from launcher import main


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
