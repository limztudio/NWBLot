#!/usr/bin/env python3
# Decode opt GPU timing dumps (hashed Name scope keys) against a namesym table,
# plus directly-computed hashes for any render.* scope the namesym predates.
import sys, re, statistics

FNV_OFFSET = 14695981039346656037
FNV_PRIME  = 1099511628211
MASK = (1 << 64) - 1
LANE_SEEDS = [
    FNV_OFFSET,
    FNV_OFFSET ^ 0x0123456789ABCDEF,
    FNV_OFFSET ^ 0xFEDCBA9876543210,
    FNV_OFFSET ^ 0x0F1E2D3C4B5A6978,
    FNV_OFFSET ^ 0x8796A5B4C3D2E1F0,
    FNV_OFFSET ^ 0xDEADBEEFCAFEBABE,
    FNV_OFFSET ^ 0x1234ABCD5678EF01,
    FNV_OFFSET ^ 0xA0B1C2D3E4F50617,
]

def canon(c):
    return '/' if c == '\\' else c.lower()

def name_hash(text):
    lanes = list(LANE_SEEDS)
    for ch in text:
        b = ord(canon(ch)) & 0xFF
        for i in range(8):
            lanes[i] = ((lanes[i] ^ b) * FNV_PRIME) & MASK
    return '_'.join(f'{q:016x}' for q in lanes)

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

SCOPES = """render.frame render.raster render.material_upload render.mesh_dispatch
render.deferred_clear render.deferred_lighting render.deferred_composite
render.opaque_regular render.opaque_csg render.opaque_csg_receiver_surface
render.shadow_visibility render.caustic_photons render.caustic_resolve
render.surfel_hash_build render.surfel_spawn render.surfel_age_free
render.surfel_resolve render.surfel_trace render.surfel_upsample
render.sw_bvh_sort
render.csg_upload render.csg_sample_state_upload render.csg_interval_peel
render.csg_interval_clear render.csg_interval_combine render.csg_cap_fill
render.csg_receiver_span_build render.transparent_csg_intervals
render.avboit_clear render.avboit_occupancy render.avboit_depth_warp
render.avboit_extinction render.avboit_integration render.avboit_accumulate""".split()

def main(timing_file, namesym_file):
    table = load_namesym(namesym_file)
    for s in SCOPES:
        table.setdefault(name_hash(s), s)

    scope_samples = {}
    line_re = re.compile(r'^\s*([0-9a-f_]+): avg=([\d.]+) min=([\d.]+) max=([\d.]+) samples=(\d+)')

    with open(timing_file) as f:
        for line in f:
            m = line_re.match(line)
            if not m:
                continue
            key, avg, samp = m.group(1), float(m.group(2)), int(m.group(5))
            name = table.get(key, '<'+key[:16]+'?>')
            if samp >= 2:
                scope_samples.setdefault(name, []).append(avg)

    print(f"Decoded scopes ({len(scope_samples)} active, intervals with samples>=2):")
    print(f"{'scope':40} {'intervals':>9} {'mean ms':>10} {'stdev ms':>10} {'min':>9} {'max':>9}")
    rows = []
    for name, vals in scope_samples.items():
        mean = statistics.fmean(vals)
        sd = statistics.pstdev(vals) if len(vals) > 1 else 0.0
        rows.append((mean, name, len(vals), mean, sd, min(vals), max(vals)))
    for mean, name, n, mean, sd, mn, mx in sorted(rows, reverse=True):
        print(f"{name:40} {n:9d} {mean:10.4f} {sd:10.4f} {mn:9.4f} {mx:9.4f}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
