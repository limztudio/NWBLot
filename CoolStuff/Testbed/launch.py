#!/usr/bin/env python3
"""Build and run the Testbed app through the repository launcher."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


TESTBED_TARGET = "testbed"
MAIN_ENTRY = "__main__"


TARGET = TESTBED_TARGET


def main(argv: Sequence[str]) -> int:
    return ROOT_LAUNCHER.run_target_launcher(TARGET, argv)


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
