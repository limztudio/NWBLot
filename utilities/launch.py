#!/usr/bin/env python3
"""Dispatch a utility launcher to one of the utility workflows."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from launcher.discovery import LauncherDiscovery  # noqa: E402


MAIN_ENTRY = "__main__"


def main(argv: Sequence[str]) -> int:
    return LauncherDiscovery.run_directory_launcher(Path("utilities"), argv)


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
