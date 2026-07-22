#!/usr/bin/env python3
"""Capture a gi-test smoke window to a BMP for the GI bindless step-4 A/B.

GI twin of ../caustic_bindless_step4/capture.py: the exe basename picks the HW producer
(gi_test_smoke) or the forced-software-emulation sibling (gi_test_sw_smoke), so one script
captures both sides of the surfel-GI bindless-heap accessor migration. The GI test scene is
static (an open-top box with a red +X wall and a blue -X wall under a fixed directional light
-> indirect red/blue bleed onto the shadowed floor), so before/after captures line up
frame-for-frame; the DDGI probe field is left to converge across the settle window before the
read. Two identical runs still differ by a small temporal-blend (EMA) floor concentrated in
the bounced-lighting features.

    python capture.py <exe-basename> <output.bmp>
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from window_capture_runner import REPO, capture_smoke_window

RUNTIME = REPO / "__cmake/build/linux-clang-x64/Testing/smoke_runtime/opt"
TITLE = "NWB GI Test"
# DDGI probes converge over a temporal blend, so give the field time to settle before the read;
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
