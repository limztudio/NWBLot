#!/usr/bin/env python3
"""Cook a given SPP, capture its screenshot. SPP passed as argv[1]; output bmp as argv[2]."""
import re, subprocess, sys, time
from pathlib import Path

REPO = Path("/home/limstudio/WorkStation/NWBLot")
SPP = sys.argv[1]
OUT_BMP = Path(sys.argv[2])
HDR = REPO / "impl/assets/graphics/shadow/sw_binding_slots.h"
RT = REPO / "__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt"

# 1. set SPP
txt = HDR.read_text()
txt = re.sub(r"#define NWB_SW_SHADOW_SOFT_SPP \d+u", f"#define NWB_SW_SHADOW_SOFT_SPP {SPP}u", txt)
HDR.write_text(txt)
print(f"set SPP={SPP}")

# 2. force recook
stamp = RT / "res/.nwb_skinning_culling_benchmark_assets_opt.stamp"
for f in [stamp, *list((RT/"res").glob("*.vol"))]:
    if f.exists(): f.unlink()
r = subprocess.run(["ninja","-f","build-opt.ninja","nwb_skinning_culling_benchmark_assets"],
                   cwd=str(REPO/"__cmake/build/linux-clang-x64"), capture_output=True, text=True, timeout=120)
print("recook exit", r.returncode, "vol", list((RT/"res").glob("*.vol"))[0].stat().st_size)

# 3. screenshot
r2 = subprocess.run([sys.executable, str(REPO/"Testing/shadow_spp_ab_v3/screenshot.py"), str(OUT_BMP)],
                    capture_output=True, text=True, timeout=90)
print(r2.stdout.strip().splitlines()[-1] if r2.stdout.strip() else r2.stderr.strip().splitlines()[-1:])
print("bmp", OUT_BMP, OUT_BMP.stat().st_size if OUT_BMP.exists() else "MISSING")
