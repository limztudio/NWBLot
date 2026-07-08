#!/usr/bin/env python3
# Side-by-side HW vs SW per-pass GPU timing. Uses median of NONZERO interval-averages,
# because 0-valued intervals (pass didn't fire / no GPU sample that window) are "no data",
# not "0 ms", and averaging them in dilutes each arm by a different firing rate.
import sys, re, statistics

def parse(path):
    samples = {}
    line_re = re.compile(r'^\s*([0-9a-f_]+): avg=([\d.]+) min=([\d.]+) max=([\d.]+) samples=(\d+)')
    with open(path) as f:
        for line in f:
            m = line_re.match(line)
            if not m:
                continue
            key, avg, samp = m.group(1), float(m.group(2)), int(m.group(5))
            if samp >= 2:
                samples.setdefault(key, []).append(avg)
    return samples

# FNV-64 8-lane hash, matching decode_gpu_timing.py / global/name.h
FNV_OFFSET = 14695981039346656037
FNV_PRIME  = 1099511628211
MASK = (1 << 64) - 1
LANE_SEEDS = [FNV_OFFSET ^ s for s in (0,
    0x0123456789ABCDEF,0xFEDCBA9876543210,0x0F1E2D3C4B5A6978,
    0x8796A5B4C3D2E1F0,0xDEADBEEFCAFEBABE,0x1234ABCD5678EF01,0xA0B1C2D3E4F50617)]

def name_hash(text):
    lanes = list(LANE_SEEDS)
    for ch in text:
        b = (ord('/' if ch == '\\' else ch.lower())) & 0xFF
        for i in range(8):
            lanes[i] = ((lanes[i] ^ b) * FNV_PRIME) & MASK
    return '_'.join(f'{q:016x}' for q in lanes)

SCOPES = """render.frame render.raster render.material_upload render.mesh_dispatch
render.deferred_clear render.deferred_lighting render.deferred_composite
render.opaque_regular render.shadow_visibility render.caustic_photons render.caustic_resolve
render.surfel_hash_build render.surfel_spawn render.surfel_age_free render.surfel_resolve
render.surfel_trace render.surfel_upsample
render.avboit_clear render.avboit_occupancy render.avboit_depth_warp
render.avboit_extinction render.avboit_integration render.avboit_accumulate
mesh_skinning.skinning mesh_skinning.repack_normals mesh_skinning.meshlet_bounds""".split()

def load_namesym(path):
    m = {}
    with open(path, 'r', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('nwb_namesym'):
                continue
            parts = line.split('\t')
            if len(parts) >= 3:
                m[parts[0]] = parts[2]
    return m

def med(vals):
    nz = [v for v in vals if v > 0.0]
    if not nz:
        return None, 0, 0
    return statistics.median(nz), len(nz), len(vals)

def main(hw_file, sw_file, hw_namesym, sw_namesym):
    hw = parse(hw_file); sw = parse(sw_file)
    hwtbl = load_namesym(hw_namesym) if hw_namesym else {}
    try:
        swtbl = load_namesym(sw_namesym) if sw_namesym else {}
    except FileNotFoundError:
        swtbl = {}  # render.* hashes are computed directly below, so namesym is optional
    for s in SCOPES:
        hwtbl.setdefault(name_hash(s), s); swtbl.setdefault(name_hash(s), s)

    # build name-keyed nonzero-medians
    def by_name(raw, tbl):
        out = {}
        for k, vals in raw.items():
            name = tbl.get(k, '<'+k[:16]+'?>')
            m, nz, tot = med(vals)
            out[name] = (m, nz, tot)
        return out
    hwm = by_name(hw, hwtbl); swm = by_name(sw, swtbl)

    print(f"{'scope':32} {'HW med ms':>10} {'SW med ms':>10} {'SW/HW':>7}   {'HW nz/tot':>9} {'SW nz/tot':>9}")
    print("-"*92)
    rows = []
    for s in SCOPES:
        h = hwm.get(s); w = swm.get(s)
        hm = h[0] if h else None; wm = w[0] if w else None
        rows.append((s, hm, wm))
    for s, hm, wm in rows:
        hs = f"{hm:.4f}" if hm is not None else "   —   "
        ws = f"{wm:.4f}" if wm is not None else "   —   "
        ratio = f"{wm/hm:.2f}x" if (hm and wm and hm>0) else "—"
        hinfo = hwm.get(s); winfo = swm.get(s)
        hstat = f"{hinfo[1]}/{hinfo[2]}" if hinfo else "—"
        wstat = f"{winfo[1]}/{winfo[2]}" if winfo else "—"
        flag = ""
        if hm and wm and hm > 0:
            flag = "  << SW faster" if wm < hm else "  >> HW faster"
        print(f"{s:32} {hs:>10} {ws:>10} {ratio:>7}   {hstat:>9} {wstat:>9}{flag}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
