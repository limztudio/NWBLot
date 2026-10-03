#!/usr/bin/env python3
"""Generate the default UI skin through the repository launcher."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from utilities.ui_skin import generate_default as DEFAULT_SKIN_GENERATOR  # noqa: E402


ARGUMENT_SEPARATOR = "--"
MAIN_ENTRY = "__main__"


def main(argv: Sequence[str]) -> int:
    arguments = list(argv)
    if arguments and arguments[0] == ARGUMENT_SEPARATOR:
        arguments = arguments[1:]
    return DEFAULT_SKIN_GENERATOR.main(arguments)


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
