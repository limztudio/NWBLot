#!/usr/bin/env python3
# Decode an NWB_GPU_TIMING_FILE dump whose scope lines already carry READABLE scope names
# (opt builds whose namesym is resolved by the logserver at runtime -- unlike the hashed
# opt/fin dumps the caustic decode_timing.py was written for). Reports per-scope nonzero-
# median interval averages, so an A/B delta can be trusted even if a heavy pass is sparse.
import re, sys, statistics

def parse_timing(path):
    raw = {}
    cur = None
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if line.startswith("=== interval:"):
            cur = []
            continue
        if line.startswith("  ") and ":" in line:
            label, rest = line.strip().split(":", 1)
            mm = re.search(r"avg=([\d.]+)", rest)
            if mm and cur is not None:
                raw.setdefault(label, []).append(float(mm.group(1)))
    return raw

def nonzero_med(avgs, zero_eps=1e-6):
    nz = [a for a in avgs if a > zero_eps]
    if not nz:
        return None, 0, len(avgs)
    return statistics.median(nz), len(nz), len(avgs)

def report(path):
    raw = parse_timing(path)
    print(f"\n### {path}")
    print(f"{'scope':<34} {'med(ms)':>9} {'mean(ms)':>9} {'nz':>5} {'tot':>5}")
    order = [
        "render.shadow_visibility", "render.shadow_resolve",
        "render.opaque_regular", "render.mesh_dispatch",
        "render.surfel_trace", "render.surfel_resolve",
        "render.caustic_photons", "render.caustic_resolve",
        "render.deferred_lighting", "render.sw_bvh_sort",
        "render.frame",
    ]
    seen = set()
    for label in order:
        if label in raw:
            med, nz, tot = nonzero_med(raw[label])
            mean = statistics.mean(a for a in raw[label] if a > 1e-6) if nz else 0.0
            print(f"  {label:<32} {med:>9.4f} {mean:>9.4f} {nz:>5} {tot:>5}")
            seen.add(label)
    for label in sorted(raw):
        if label.startswith("render.") and label not in seen:
            med, nz, tot = nonzero_med(raw[label])
            mean = statistics.mean(a for a in raw[label] if a > 1e-6) if nz else 0.0
            print(f"  {label:<32} {med:>9.4f} {mean:>9.4f} {nz:>5} {tot:>5}")

for p in sys.argv[1:]:
    report(p)
