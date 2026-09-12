#!/usr/bin/env python3
"""Root launcher entry point (`python launcher/cli.py`)."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root))
    import launcher

    return launcher.main(argv)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
