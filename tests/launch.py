#!/usr/bin/env python3
"""Dispatch a test launcher to one of the runnable test workflows."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from launcher.discovery import LauncherDiscovery  # noqa: E402


TESTS_DIR_NAME = "tests"
MAIN_ENTRY = "__main__"


def main(argv: Sequence[str]) -> int:
    return LauncherDiscovery.run_directory_launcher(Path(TESTS_DIR_NAME), argv)


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
