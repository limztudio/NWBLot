#!/usr/bin/env python3
# Decode opt NWB_GPU_TIMING_FILE dumps against the HW stress_test_smoke.namesym
# (shared scope hashes), then aggregate per-scope nonzero-median interval averages.
import re, sys, statistics, subprocess

NAMESYM = "__exec/linux/x64/full/opt/stress_test_smoke.namesym"

def load_namesym():
    # line: HASH\tnamespace\tlabel ; HASH is underscore-joined 64-bit lanes
    m = {}
    text = open(NAMESYM, "r", encoding="utf-8", errors="replace").read()
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            h, ns, label = parts[0], parts[1], parts[2]
            if label.startswith("render.") or label.startswith("deferred"):
                m[h] = label
    return m

def parse_timing(path):
    # returns {scope_label: [interval_avgs]} using namesym decode; raw-hash fallback kept too
    namesym = load_namesym()
    raw = {}
    cur = None
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if line.startswith("=== interval:"):
            cur = []
            continue
        if line.startswith("  ") and ":" in line:
            h, rest = line.strip().split(":", 1)
            # parse avg=... ms
            mm = re.search(r"avg=([\d.]+)", rest)
            if mm and cur is not None:
                avg = float(mm.group(1))
                raw.setdefault(h, []).append(avg)
    # decode hashes
    decoded = {}
    undec = {}
    for h, avgs in raw.items():
        label = namesym.get(h)
        if label:
            decoded[label] = avgs
        else:
            undec[h] = avgs
    return decoded, undec

def nonzero_med(avgs, zero_eps=1e-6):
    nz = [a for a in avgs if a > zero_eps]
    if not nz:
        return None, 0, len(avgs)
    return statistics.median(nz), len(nz), len(avgs)

def report(path):
    dec, undec = parse_timing(path)
    print(f"\n### {path}")
    print(f"{'scope':<34} {'med(ms)':>9} {'mean(ms)':>9} {'nz':>5} {'tot':>5}")
    # order of interest
    order = [
        "render.caustic_photons", "render.caustic_resolve",
        "render.shadow_visibility", "render.opaque_regular",
        "render.mesh_dispatch", "render.surfel_trace", "render.surfel_resolve",
        "render.deferred_lighting", "render.raster", "render.frame",
    ]
    for label in order:
        if label in dec:
            med, nz, tot = nonzero_med(dec[label])
            mean = statistics.mean(a for a in dec[label] if a > 1e-6) if nz else 0.0
            print(f"  {label:<32} {med:>9.4f} {mean:>9.4f} {nz:>5} {tot:>5}")
    # deferred label may differ
    for label in list(dec):
        if label.startswith("deferred") and label not in order:
            med, nz, tot = nonzero_med(dec[label])
            mean = statistics.mean(a for a in dec[label] if a > 1e-6) if nz else 0.0
            print(f"  {label:<32} {med:>9.4f} {mean:>9.4f} {nz:>5} {tot:>5}")

for p in sys.argv[1:]:
    report(p)
