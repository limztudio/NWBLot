#!/usr/bin/env python3
# Compare meshlet_bounds A/B GPU timing arms (baseline groupshared-tree vs wave-intrinsic).
# Timing files already carry READABLE scope names (captured with the namesym-domain binary),
# so no name-hash decoding is needed.
#
# Statistic = nonzero-median (the agreed sparse-comparison statistic; see
# tests/ab/wave_ab/README.txt and tests/ab/shadow_spp_ab_v3/README.txt).
import sys, re, statistics

BASELINE = "baseline_timing.txt"
WAVE     = "wave_timing.txt"

# Scopes to report. mesh_skinning.meshlet_bounds is THE pass under test; render.frame is
# the trustworthy whole-frame sum. The render.* entries are context passes to confirm the
# change is local (they should be flat).
SCOPES = [
    "mesh_skinning.meshlet_bounds",
    "mesh_skinning.skinning",
    "mesh_skinning.repack_normals",
    "render.frame",
    "render.shadow_visibility",
    "render.mesh_dispatch",
    "render.opaque_regular",
    "render.caustic_resolve",
    "render.surfel_trace",
    "render.avboit_accumulate",
]

line_re = re.compile(r'^\s+(\S+):\s+avg=([\d.]+)\s+min=([\d.]+)\s+max=([\d.]+)\s+samples=(\d+)')

def parse(path):
    per_key = {}
    with open(path) as f:
        for line in f:
            m = line_re.match(line)
            if not m:
                continue
            key = m.group(1)
            avg = float(m.group(2))
            per_key.setdefault(key, []).append(avg)
    return per_key

def nz_median(avgs):
    nz = [a for a in avgs if a > 0.0]
    return (statistics.median(nz) if nz else 0.0), len(nz), len(avgs)

def main():
    b = parse(BASELINE)
    w = parse(WAVE)
    print("Meshlet-bounds wave-intrinsic A/B   (nonzero-median ms)")
    print("=" * 92)
    print(f"{'scope':<28} {'baseline(nz)':>16} {'wave(nz)':>16} {'delta_ms':>10} {'delta_%':>9}")
    print("-" * 92)
    for s in SCOPES:
        bm, bn, bt = nz_median(b.get(s, []))
        wm, wn, wt = nz_median(w.get(s, []))
        if bm > 0:
            d_ms = wm - bm
            d_pct = d_ms / bm * 100.0
            dms = f"{d_ms:+.4f}"
            dpct = f"{d_pct:+.2f}%"
        else:
            dms = "n/a"; dpct = "n/a"
        print(f"{s:<28} {bm:>10.4f}({bn:>3}) {wm:>10.4f}({wn:>3}) {dms:>10} {dpct:>9}")
    print("-" * 92)
    # Interval counts
    def count_intervals(path):
        with open(path) as f:
            return sum(1 for line in f if line.startswith("=== interval"))
    print(f"\nbaseline intervals: {count_intervals(BASELINE)}   wave intervals: {count_intervals(WAVE)}")

if __name__ == "__main__":
    main()
