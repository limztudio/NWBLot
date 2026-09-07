#!/usr/bin/env python3
"""Dispatch a test launcher to one of the runnable test workflows."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


def main(argv: Sequence[str]) -> int:
    return ROOT_LAUNCHER.run_directory_launcher(Path("tests"), argv)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
