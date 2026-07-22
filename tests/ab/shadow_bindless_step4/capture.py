#!/usr/bin/env python3
"""Capture a soft-shadow-test smoke window to a BMP for the SW-shadow bindless step-4b A/B.

Parameterized twin of ../shadow_opaque_fastpath/capture_soft_shadow.py: the exe basename
picks the HW hybrid (soft_shadow_test_smoke) or the full-software (soft_shadow_test_sw_smoke)
path, so one script captures both sides of the bindless-heap accessor migration. The caster
yaw is pinned (NWB_SOFT_SHADOW_TEST_SPIN_ANGLE) so before/after captures line up
pixel-for-pixel.

    python capture.py <exe-basename> <output.bmp>
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from window_capture_runner import REPO, capture_smoke_window

RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"
TITLE = "NWB Soft Shadow Test"
SETTLE = float(os.environ.get("SHADOW_SETTLE", "6.0"))
FROZEN_YAW = os.environ.get("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE", "0.6")


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
        extra_env={"NWB_SOFT_SHADOW_TEST_SPIN_ANGLE": FROZEN_YAW},
        launch_label=f"exe={sys.argv[1]} yaw={FROZEN_YAW}",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
