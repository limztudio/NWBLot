# Fixed renderer baselines

This test-owned flow takes a current renderer image as a fixed reference.
Then it compares a later renderer image with that reference. It adds no
production switch and brings back no old renderer paths.
`current-renderer-v1` is the formal forward reference for the graph renderer.
It guards approved current output from later change. It is not an old-output
match set.

Take a reference from the repo root:

```text
python -m launcher renderer-baseline transparent-avboit
python -m launcher renderer-baseline static-csg
python -m launcher renderer-baseline skinned-csg
python -m launcher renderer-baseline stress
```

Each run builds only its set smoke target. It writes `baseline.bmp`,
`runtime.log`, and `manifest.json` under
`.cozter/out/ab-results/renderer-baseline/<profile>/<timestamp>/`. The manifest
holds the source version, exe check sum, fixed env, capture settings, and
image check sum. Baseline build refuses a dirty source tree. It never writes
over an old result folder. The launcher asks for GPU validation by default.
Use `dbg` or `opt` for checked captures. A `fin` capture needs
`--no-gpu-validation` because its loader rejects `--gpudbg`. Check mode is
part of reference ID. A Final capture cannot compare with the formal set
below, which needs checks on.

Compare a later build with a stored reference:

```text
python -m launcher renderer-baseline transparent-avboit -- \
  --reference-dir .cozter/out/ab-results/renderer-baseline/transparent-avboit/<timestamp> \
  --maximum-mean-abs 2.0 --maximum-changed-fraction 0.02
```

The new folder gets `candidate.bmp`, `difference.bmp`, `comparison.json`,
and its own capture manifest. With no limits the compare is report-only.
`--require-exact` fits only fixed paths. A reference with a changed profile,
fixed env, or check mode is rejected, not quietly compared.

For surfel-GI A/B work, `surfel-gi-complex` takes the closed black room with
a small sky light and twenty fixed bodies. After at least 360 good presents,
it waits at least 30 real seconds while it may render. Then it takes an app
frame readback. The run log notes warm-up start/end frames, present counts,
time passed, and the true done readback source frame. The runner rejects
missing, short, fully idle, or wrong readback proof. It stores the used time
in the capture manifest. This rule needs present counts. Its end counts do not
prove smooth render or light settle. Start-up and frame warm-up do not count
in this live time. Complex captures use the `settled-readback` mode and do
not use `--settle-seconds`. Set the post-warm time through
`NWB_GI_SMOKE_MIN_SETTLE_SECONDS`. To ask for 100 live seconds after frame
warm-up, set the profile env value and raise the ready-marker time-out:

```powershell
$env:NWB_GI_SMOKE_MIN_SETTLE_SECONDS = "100"
python -m launcher renderer-baseline surfel-gi-complex --no-gpu-validation -- --startup-timeout 300
```

Run this from a clean source state to make a fixed reference. Then repeat with
`--reference-dir` set to that reference. Keep the settle value and check
choice the same for both runs. The formal `current-renderer-v1` set keeps the
first open-box `surfel-gi` profile. The complex A/B profile is separate. Its
fixture-only GI mats are cooked only into
`Testing/gi_smoke_runtime/<config>`. So `surfel-gi-complex` runs from that
runtime root, and its cooked proof root differs from the base profile.

Published frozen references under `tests/ab/renderer_baseline/`:

| Profile | Directory | Source | Covered captures / comparison |
| --- | --- | --- | --- |
| `transparent-avboit` | `v1/` | `0403c1831`, Adreno X2-90, Windows ARM64 `opt` | Transparent-glass depth-peel smoke artifacts as the changed-subject gate: source image, exact-duplicate check against earlier review copy, budget/zoom/clear/heat captures, per-view and whole-gallery numeric tables, `resolve.ok` / `resolve.fail` markers, `refraction.ok` markers, reuse A/B check, and burst stress logs |
| `static-csg` | `v1/` | `0403c1831`, Adreno X2-90, Windows ARM64 `opt` | `nwb_csg_visible_smoke` window capture plus its runtime log, markers, and cooked asset manifest |
| `skinned-csg` | `v1/` | `0403c1831`, Adreno X2-90, Windows ARM64 `opt` | `nwb_csg_skinned_visible_smoke` window capture plus its runtime log, markers, and cooked asset manifest |
| `stress` | `v1/` | `0403c1831`, Adreno X2-90, Windows ARM64 `opt` | `nwb_stress_test_smoke` window capture plus its runtime log, markers, and cooked asset manifest |

Each `v1/` folder holds `baseline.bmp`, `manifest.json`, and `runtime.log`.
Check hashes before use. These files prove the subject state for the listed
source and setup. They do not re-prove a later source.

`surfel-gi` and `surfel-gi-complex` runs need a GPU with ray queries. With no
ray-query support, the GI scene reports a skip and makes no capture. The
`surfel-gi-complex` fixture-only mats give a closed black room with soot-like
walls. The warm room walls are mid-grey with a small sky light. Fresh runs
use the same scene and the current cooker. So new captures may differ from the
old v1 proof files.

## Self-check

Run `python tests/ab/renderer_baseline/run.py --self-test` to check flow
order, missing proof, and fail cleanup with no graphics app. CTest also runs
these checks.
