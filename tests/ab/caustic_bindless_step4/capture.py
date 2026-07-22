#!/usr/bin/env python3
"""Capture a caustic-sphere-test smoke window to a BMP for the caustic bindless step-4 A/B.

Caustic twin of ../shadow_bindless_step4/capture.py: the exe basename picks the HW producer
(caustic_sphere_smoke) or the forced-software-emulation sibling (caustic_sphere_sw_smoke), so
one script captures both sides of the caustic bindless-heap accessor migration. The refractor
spin is pinned (NWB_TRANSPARENT_MULTI_SPIN_ANGLE) so before/after captures line up
frame-for-frame; the caustic accumulator is left to converge across the settle window before
the read.

    python capture.py <exe-basename> <output.bmp>
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from window_capture_runner import REPO, capture_smoke_window

RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/smoke_runtime/opt"
TITLE = "NWB Caustic Sphere Smoke"
# Caustics accumulate temporally, so give the accumulator longer than the shadow test to converge
# before the read; two identical runs still differ by a small photon-jitter floor.
SETTLE = float(os.environ.get("CAUSTIC_SETTLE", "6.0"))
FROZEN_ANGLE = os.environ.get("NWB_TRANSPARENT_MULTI_SPIN_ANGLE", "0.6")


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
        extra_env={"NWB_TRANSPARENT_MULTI_SPIN_ANGLE": FROZEN_ANGLE},
        launch_label=f"exe={sys.argv[1]} angle={FROZEN_ANGLE}",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
