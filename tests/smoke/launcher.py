#!/usr/bin/env python3
import sys
from pathlib import Path


def _load_launcher():
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / "scripts"))
    import nwb_launcher

    return nwb_launcher


def main(argv):
    return _load_launcher().smoke_main(argv)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
