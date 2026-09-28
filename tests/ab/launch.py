#!/usr/bin/env python3
"""Dispatch an A/B test launcher to one of its runnable validation workflows."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


TESTS_DIR_NAME = "tests"
AB_SUBDIR_NAME = "ab"
MAIN_ENTRY = "__main__"


def main(argv: Sequence[str]) -> int:
    return ROOT_LAUNCHER.run_directory_launcher(Path(TESTS_DIR_NAME) / AB_SUBDIR_NAME, argv)


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
