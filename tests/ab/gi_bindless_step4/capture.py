#!/usr/bin/env python3
"""Capture a gi-test smoke window to a BMP.

The executable basename selects the HW producer or forced-software sibling. The static scene
keeps captures aligned while the settle window lets the surfel-GI temporal state converge; a
small EMA floor remains in bounced-lighting features.

    python capture.py <exe-basename> <output.bmp>
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from window_capture_runner import REPO, capture_smoke_window

# GI reuses the skinning-culling benchmark's cooked ground asset, so its smoke target
# runs from that runtime rather than the transparent-multi smoke runtime.
RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"
TITLE = "NWB GI Test"
# Surfel GI converges through temporal accumulation, so give its state time to settle before the read;
# two identical runs still differ by a small EMA floor in the bounced-lighting features.
SETTLE = float(os.environ.get("GI_SETTLE", "6.0"))


def main():
    if len(sys.argv) != 3:
        print("usage: capture.py <exe-basename> <output.bmp>", file=sys.stderr)
        return 2
    capture_smoke_window(
        runtime=RUNTIME,
        title=TITLE,
        settle=SETTLE,
        output_bmp=Path(sys.argv[2]).resolve(),
        exe=sys.argv[1],
        launch_label=f"exe={sys.argv[1]}",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
