#!/usr/bin/env python3
"""Build and run the fbx_to_nwb utility through the repository launcher."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


TARGET = "nwb_fbx_to_nwb"


def main(argv: Sequence[str]) -> int:
    return ROOT_LAUNCHER.run_target_launcher(TARGET, argv)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
