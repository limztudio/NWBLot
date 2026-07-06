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
3. **`loader/main.cpp` now calls `NameSymbols::LoadDefaultFile()` at startup.** Previously only the
   logserver loaded sidecars; the runtime apps (stress test, testbed, …) never did, so their opt logs
   stayed hashed. This one-line best-effort load makes every loader-based app resolve its hashes.

Result: opt GPU-timing output is now fully readable (`render.shadow_visibility`, `render.mesh_dispatch`, …).

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

## 4. Open: the optimization (pending direction)

Reducing the shadow GPU time *without sacrificing quality* is a deep, shader-level change with several
valid directions (e.g. early-exit/occupancy pruning in the SW BVH walk, better ray ordering, moving more
of the transparent trace onto HW RayQuery where the silhouette-disagreement risk is acceptable). Awaiting
confirmation on which direction to pursue before the larger, harder-to-reverse shader edits.
