# Stress-Test Scene — GPU Timing Infrastructure + Opt Baseline

Date: 2026-07-07 (updated)
Target: `python3 launcher.py smoke stress-test` (HW backend, `opt` config)
GPU: AMD Cyan Skillfish (Vulkan), 1280×900 window, half-res (640×450) shadow work.

## 0. What was blocking profiling on Linux (now fixed)

Two prerequisites had to land before **opt**/fin GPU timing could be measured with readable
scope names on this Linux host. Both are done:

### 0.1 libmicrohttpd vendored for Linux (logserver now builds)
`nwb::microhttpd` previously built from vendored source **only on Windows**; on Linux it did a
`find_package(libmicrohttpd)` that failed (no system package, no passwordless sudo), so
`nwb_logserver` was silently skipped — the wall the prior report hit.

Fix (nwb manner, mirroring the existing Windows vendored build): `3rd_parties/libmicrohttpd/CMakeLists.txt`
and `MHD_config.h.in` now generate a `MHD_config.h` and build a `nwb_vendor_microhttpd` static lib on
**both** platforms behind the single `nwb::microhttpd` alias. The config generator probes the platform
with CMake's `check_*` modules (POSIX headers/functions on Linux: `eventfd`/`pipe2` ITC selection,
`MHD_USE_POSIX_THREADS`, `HAVE_LINUX_SENDFILE`, `pthread_setname_np`, etc.); the `.in` template's
`#cmakedefine` directives simply stay undefined for whichever platform half does not apply. Linux adds
`mhd_itc.c` + `mhd_compat.c` to the source set and links `pthread`. `connection_https.c` stays excluded
(plain HTTP logserver; no gnutls).

Result: `nwb_logserver` builds and runs on Linux (`logserver -p <port>` listens and serves).

### 0.2 Name-symbol sidecars generated + loaded (opt scope names now readable)
In opt/fin, `Name::c_str()` returns a hash unless a `.namesym` sidecar is loaded. Three things were needed:

1. **`generate_name_symbols.py` now honors `CMAKE_COMMAND`** (it hardcoded bare `cmake`/`ctest`, which
   aren't on PATH — the project's CMake lives in a local venv). It derives a matching `ctest` from the
   same override and resolves the venv's wrapper layout.
2. **`nwb_namesym` produced sidecars** for dbg + opt: `resource_cooker.namesym`, `logserver.namesym`,
   `crash_handler.namesym`, and — captured by running the buildmode stress binary directly and closing
   its X11 window gracefully (`tests/smoke/x11_graceful_close.py`, sends `WM_DELETE_WINDOW`; SIGTERM
   skips the buildmode sidecar write) — `stress_test_smoke.namesym` (the render/skinned/surfel scopes).
   Copied into the release output roots so the non-buildmode exes load them.
3. **Name symbols are a server-side concern only.** Clients emit debug-hash tokens; the log server is the
   sole resolver: it loads all `.namesym` sidecars from its exe dir (`NameSymbols::LoadDefaultFile`) and
   ingests client uploads (`LoadFromMemory`), then rewrites every debug-hash token in received messages to
   readable text (`DecodeHashTokens`) before logging them. `loader/main.cpp` deliberately does **not** load
   sidecars — `Name::c_str()` falls back to the hash hex in opt/fin on the client.

   **Caveat for local benchmarking:** `NWB_GPU_TIMING_FILE` is written by the client process via
   `scopeName.c_str()`, so in opt/fin its per-pass labels are now raw hash tokens (e.g.
   `0a1b2c3d..._...`) rather than readable names. To read them, decode the file against the log server's
   symbol table (`NameSymbols::DecodeHashTokens` over each line), or benchmark via the server's log output
   instead of the local file. In dbg, `c_str()` still returns readable text directly.

## 1. How opt GPU timing is measured

```
cd __cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt
NWB_GPU_TIMING_FILE=/tmp/gpu.txt DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 \
  timeout --signal=INT 20 __exec/linux/x64/full/opt/stress_test_smoke
```
Per-pass GPU-ms fold to `NWB_GPU_TIMING_FILE` (the renderer's built-in `GpuPassTimingProbe`,
independent of the logserver). Steady state = intervals with `render.frame` avg > 1.0 ms and ≥20 samples
(drops the startup warmup tail where async GPU timing publishes near-zero).

## 2. opt baseline (steady state)

| pass | GPU ms | % of frame |
|---|---|---|
| **render.frame** | **10.3** | 100% (~97 fps) |
| **render.shadow_visibility** | **4.6 – 5.2** | **~45%** |
| render.mesh_dispatch | 1.5 | 15% |
| render.caustic_resolve | 0.9 | 9% |
| render.opaque_regular | 0.7 | 7% |
| render.raster | 0.7 | 7% |
| render.avboit_accumulate | 0.6 | 6% |
| render.avboit_occupancy | 0.6 | 6% |
| render.avboit_extinction | 0.6 | 6% |
| render.caustic_photons | 0.3 | 3% |
| render.surfel_resolve | 0.2 | 2% |
| (everything else) | < 0.2 each | — |

opt is ~33% faster than the dbg baseline in the prior report (15.2 → 10.3 ms/frame). The frame is still
single-thread-GPU-bound on the **shadow path** (~45% of the frame), consistent with the dbg analysis.

## 3. Optimization target (carried over from dbg analysis)

The prior dbg localization (temporary `shadow_diag_*` sub-scopes) broke the 6.3 ms shadow into its stages;
the dominant cost was the **software colored-transparent shadow trace** (`sw_shadow_traverse` Moeller-Trumbore
BVH walk) at ~54% of the shadow pass / ~22% of the whole frame, with the HW opaque RayQuery trace next to it
at only 0.5 ms (7× cheaper). The same hotspot holds in opt. Every "reduce SPP / pass count / resolution"
lever is already pulled (each carries a measured quality tradeoff in its comment), so further wins must
come from the traversal algorithm itself, not from spending fewer samples.

## 4. Optimization 1 — SW BVH traversal: front-to-back descent + coalesced index fetch (DONE)

The shadow SW BVH walk (`sw_shadow_traverse.slangi::nwbSwShadowInstanceOccluded` per-mesh loop +
`nwbSwShadowOccluded` scene loop) was an **arbitrary-order DFS**: both children were pushed to the stack with no
regard for which is nearer the ray, so the first opaque hit and the near-zero-transmittance early-out fired only
after a full ray-agnostic deep dive. Two quality-neutral changes landed:

1. **Front-to-back descent.** Each internal node now reads both children, ranks them by ray-entry distance
   (`nwbSwShadowRayAabbEntryT`, a fetch-neutral ordering twin of the existing bool slab test — same math, same miss
   set, only ranks sibling boxes), descends the NEAR child inline, and defers ONLY the FAR child to the stack. The
   near subtree is always processed first, so the opaque `return` and the transmittance-ε floor fire at the earliest
   possible node, skipping farther subtrees. The stack holds only deferred far nodes (one per internal node, half
   the old two), so the exhaustion guard dropped `+2 → +1` and the same stack depth covers a 2× deeper tree.
2. **Coalesced triangle index fetch.** The per-leaf triangle fetch was three separate `ByteAddressBuffer.Load()`
   over three contiguous u32; replaced with one `Load3()` (a single 12-byte transaction). Same bytes, 1/3 the index
   fetch traffic. Helps both the opaque and the transparent trace.

**Order-independence proof (so this is pure perf, never a quality change):** the integrator
(`shadow_integrate.slangi::nwbShadowIntegrateCrossing` + `nwbShadowFinalizeVisibility`) bins crossings PER OCCLUDER
and the finalize SORTS each bin's hit distances before pairing them into chords, so the result depends only on the
SET of crossings found in `[tMin,tMax]` — never their visit order. Reordering the walk only changes WHICH nodes are
visited: an opaque hit stops the walk the instant that triangle is tested (as before), and a transparent trace
must still collect every crossing, so near-first only interleaves them earlier. The far child's deferral is bounded
by the same slab test, so the miss set is identical. **tMax is NOT narrowed** (a transparent crossing cannot bound
later occluders, so narrowing it would drop real crossings and change the result). The non-shadow control passes
are bit-identical before/after (timing-confirmed below), which a corrupting change could not be.

### Measured (opt, steady state, frozen stress scene)

| pass | before (DFS) | after (front-to-back + Load3) | Δ |
|---|---|---|---|
| **render.shadow_visibility** | **5.16 ms** | **4.61 ms** | **−0.55 ms (−10.7%)** |
| **render.frame** | **10.32 ms** | **9.75 ms** | **−0.57 ms (−5.5%)** |
| render.opaque_regular (control) | 0.81 | 0.81 | 0.00 |
| render.caustic_resolve (control) | 0.99 | 0.99 | 0.00 |
| render.mesh_dispatch | 1.72 | 1.68 | −0.04 |

Shadow share of the frame dropped 50% → 47%. The win is concentrated in `render.shadow_visibility` (the only pass
the traversal runs in), and the control passes are flat — confirming it is the algorithm change, not noise. The
shadow path is still the frame's single largest cost, so further gains (e.g. quantized-AABB node compression, or a
refit→rebuild scene-BVH quality pass) remain open as future items.

## 3. BLOCKER — quantized-node rerun cannot start (asset-cook, not traversal)

The post-quantization rerun (commit `28b17082` "Migrate SW BVH traversal to quantized nodes", then cleanup
`826c6915`) never reaches a frame: the opt and dbg stress binaries both `trace/breakpoint trap` at startup with
the crash-handler breadcrumb

```
VolumeSession::loadData failed to read 'project/characters/body/model'
  core/filesystem/module.h:333
```

This is **not** the quantization work. Diagnosis:

- The failure is at asset load — before any render/BVH code runs — and is **pre-existing**: identical breadcrumbs
  appear in the 09:xx crash dumps today, i.e. before commit `28b17082` (22:07). So it blocks every stress run,
  not just the quantized one.
- `tests/smoke/assets/characters/body.nwb` declares six top-level assets (`mesh`, `skeleton`, `skin`,
  `skinned_mesh`, `mesh_wrapper`, `model`) wrapped in an `asset_bunch bunch = [mesh, skeleton, skin, model]`.
  Running the resource cooker directly (`resource_cooker --asset-root impl/assets --asset-root tests/smoke/assets …`)
  in **both** opt and dbg emits only the **mesh** meta for `body.nwb` and packs `volume='graphics', files=105` —
  the model/skeleton/skin entries never land, so `project/characters/body/model` has nothing to resolve at runtime.
  The opt asset cache confirms it: `model` cache entries = 0 (vs `mesh`=20, `skin`=10).
- The loader mounts only the single `graphics` volume (`graphicsVolume.load("graphics", …)`, loader/main.cpp:316),
  and the cooker emits only that volume, so the missing model is not in a sibling volume — it was simply not
  written. The bunch-expander path (`impl/assets_bunch/cook.cpp::ExpandAssetBunchForAssetCook` → `ExpandAssetBunch`)
  recognises the `asset_bunch` declaration and resolves all four items without error, yet only `mesh` survives
  into the cooked output. The model-entry cook (`impl/assets_model/volume_entry.cpp` + `cook.cpp`) produces no
  "model" log line at all.

Likely culprit to investigate next: the model-cook entry's `BuildModelCookedAsset` is silently dropping the entry
(or the model asset is being filtered before volume write) — the bunch expander hands the model through, but
nothing downstream logs or writes it. The mesh/skin entries clearly take the same path and do get written, so the
regression is specific to the `model` asset-type cook, not the bunch machinery.

Note: the quantization change itself is sound by inspection — `bvh_common.slangi::nwbBvhQDequantizeAabb` is the sole
unpack (min edge `refMin + q·extent/256`, max edge `refMin + (q+1)·extent/256`, a conservative superbox), and the
duplicate dequant helpers removed in `826c6915` were dead code. Once the asset-cook blocker is fixed, the
quantized-vs-DFS shadow-timing comparison (the actual point of this rerun) can be measured.
