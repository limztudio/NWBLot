import statistics, re
def parse(path):
    samples={}; line_re=re.compile(r'^\s*([0-9a-f_]+): avg=([\d.]+) min=([\d.]+) max=([\d.]+) samples=(\d+)')
    for line in open(path):
        m=line_re.match(line)
        if m and int(m.group(5))>=2:
            samples.setdefault(m.group(1),[]).append(float(m.group(2)))
    return samples
FNV_OFFSET=14695981039346656037; FNV_PRIME=1099511628211; MASK=(1<<64)-1
LANE_SEEDS=[FNV_OFFSET^s for s in (0,0x0123456789ABCDEF,0xFEDCBA9876543210,0x0F1E2D3C4B5A6978,0x8796A5B4C3D2E1F0,0xDEADBEEFCAFEBABE,0x1234ABCD5678EF01,0xA0B1C2D3E4F50617)]
def name_hash(text):
    lanes=list(LANE_SEEDS)
    for ch in text:
        b=(ord('/' if ch=='\\' else ch.lower()))&0xFF
        for i in range(8): lanes[i]=((lanes[i]^b)*FNV_PRIME)&MASK
    return '_'.join(f'{q:016x}' for q in lanes)
def med(vals):
    nz=[v for v in vals if v>0.0]
    return (statistics.median(nz),len(nz),len(vals)) if nz else (None,0,len(vals))
SCOPES=["render.shadow_visibility","render.frame","render.raster","render.mesh_dispatch","render.opaque_regular","render.caustic_photons","render.caustic_resolve","render.surfel_trace","render.deferred_lighting","mesh_skinning.skinning"]
base=parse(".helper/sw_gpu_timing.txt")
new=parse(".helper/sw_after_traverseopt_timing2.txt")
print(f"{'scope':30} {'baseline':>14} {'after':>14} {'delta':>10}")
print("-"*74)
for s in SCOPES:
    h=name_hash(s)
    bm,bn,bt=med(base.get(h,[])); nm,nn,nt=med(new.get(h,[]))
    bs=f"{bm:.4f}({bn}/{bt})" if bm else "  —   "
    ns=f"{nm:.4f}({nn}/{nt})" if nm else "  —   "
    d=f"{(nm-bm):+.4f}" if (bm and nm) else "—"
    star="  <-- TARGET" if s=="render.shadow_visibility" else ""
    print(f"{s:30} {bs:>14} {ns:>14} {d:>10}{star}")
