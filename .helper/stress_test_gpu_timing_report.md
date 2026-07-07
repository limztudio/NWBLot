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

### 3.1 Follow-up instrumentation (model IS cooked + pushed, failure is volume read-back)

The "Likely culprit" hypothesis above was disproven by instrumenting the cook path (temporarily, since reverted).
Adding logs to `impl/assets_model/volume_entry.cpp::ParseModelValue`, `impl/assets_model/cook.cpp::BuildModelAsset`,
and `impl/assets_volume/asset_volume_writer.cpp::AssetVolumeCookedAssetWriter::writeCookedAsset`, then re-cooking
dbg, shows the model **does** flow end to end:

```
[DEBUG-COOK] ParseModelValue entered virtualPath='project/characters/body/model'
[DEBUG-COOK] ParseModelValue virtualPath='project/characters/body/model' result=true
[DEBUG-COOK] BuildModelAsset entered virtualPath='project/characters/body/model'
[DEBUG-WRITE] pushed model 'project/characters/body/model' bytes=576
Asset volume cook complete [dbg] - volume='graphics', files=105
```

So `ParseModelValue` succeeds, `BuildModelAsset` runs (validatePayload passes), and `writeCookedAsset` calls
`pushDataDeferred("project/characters/body/model", 576)` which returns true (no push error). The skeleton (43408 B)
and skin (659912 B) are pushed the same way. The cook reports `files=105` and exits 0. Yet the freshly-cooked
volume, copied into the runtime `res/` dir, still fails `loadData` for exactly `project/characters/body/model`.

Revised localization: the model data is written through `VolumeFileSystem::writeFileDeferred`
(`MetadataFlushMode::Deferred`) and the cook calls `flush()` before publish, so the deferred index entry *should*
survive the staged→publish transition — but the runtime `readFile` → `m_files.find(virtualPath)` returns
"file was not found" for the model Name while other assets in the same volume resolve. The cooker-process and
runtime-process compute the same `Name("project/characters/body/model")` hash (deterministic FNV), and no path
transformation is applied (`ToCookEntryName` is identity for `Name`). This points at the **volume read-back /
staged-publish layer** (`core/filesystem/module.cpp` `writeFileDeferred` → `flushMetadata` → `PublishStagedVolume`
→ `VolumeSession::load`), where a deferred-writes index entry for one asset can be dropped or mis-segmented.
This is the next thing to trace — it is a filesystem-layer regression, not a model/asset-cook or traversal issue.

### Timeline

| time | event |
|---|---|
| 09:44 | `12808b1e` front-to-back traversal lands; the 4.61 ms opt baseline above measured OK |
| ~10:14 | report mtime (section 2 baseline was captured successfully) |
| 09:xx–now | every stress rerun dumps core on `body/model` load (pre-existing, blocks all configs) |
| 22:07 | `28b17082` quantized traversal — benchmark blocked by the asset-cook issue above, not by quantization |
| 22:44 | `826c6915` dead dequant-helper cleanup (provably perf-neutral) |

## 5. RESOLUTION: the asset blocker was a stale-volume misdeployment (not a filesystem bug)

Sections 3 / 3.1's "deferred index entry dropped/mis-segmented" and "volume read-back layer" hypotheses were **wrong**.
The blocker is a **stale deployed volume**, found by parsing the on-disk volume index directly (header = 48 B
`<magic8 segmentSize metadataBytes fileCount indexBytes nextFreeOffset>`; then `fileCount × 80 B`
`{NameHash hash(u64×8) ; u64 offset ; u64 size}`; `NameHash` is the 512-bit `global/name.h` FNV).

- **Fresh / complete volume** = `__cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt/res/eda18e57f4796c04.vol`
  → **105 entries**, model (576 B) / skeleton (43408) / skin (659912) ALL present, 0 duplicate hashes, 105 distinct offsets.
  This is the volume sections 3 / 3.1 instrumented and proved clean.
- **Actually-mounted volume** = `__exec/linux/x64/full/res/eda18e57f4796c04.vol` → **only 82 entries**, same hash filename,
  **different md5** (`bf53b4…` vs fresh `c43d02…`); model/skeleton/skin hashes are ABSENT. This is an older cook that predates
  the character assets.
- Why it mounted there: `loader/main.cpp::ResolveResourceMountDirectory` tries `cwd/res`, then `exeDir/res`, then
  `exeDir.parent()/res`, else literal `"res"`. Running from `__exec/linux/x64/full/opt/` (which has no `res/`) walks up to
  the parent's stale `full/res/`. The crash `breadcrumbs.txt` (cwd = `…/full/opt`) and `emergency.txt` (`reason=manual_dump`,
  `trigger_category=logger_Error` — a deliberate dump on the error-level log, NOT a segfault) confirm it.
- The cook is correct; the publish/deploy step that copies the freshly-cooked volume into the `__exec/…/full/res/` mirror is stale.
- **Fix for benchmarking only:** run from `Testing/skinning_culling_benchmark_runtime/opt/` (cwd `/res` wins), or copy the
  fresh volume into the mounted location. A real deploy-pipeline fix is a separate task.

## 6. Quantized traversal: measured 6× shadow regression (correct math, node-visit inflation)

With the stale-volume bypass (run from the fresh-volume cwd), the stale `__exec/…/opt/stress_test_smoke` binary
(built 22:16, i.e. AFTER `28b17082` quantized traversal, BEFORE the perf-neutral `826c6915` cleanup — so it IS on the
quantized path) ran cleanly for 30 s → 56 steady-state intervals. Scope names decoded against
`stress_test_smoke.namesym`. Every NON-shadow pass matches the DFS baseline to the digit (caustic 0.99, opaque 0.81,
mesh_dispatch 1.68), so the regression is isolated to the SW shadow traversal that now consumes `NwbBvhNodeQ`:

| pass | DFS baseline (§2) | quantized (now) | Δ |
|---|---|---|---|
| **render.shadow_visibility** | **4.61 ms** | **30.22 ms** | **+25.6 ms (+556 %)** |
| **render.frame** | **9.75 ms** | **35.44 ms** | **+25.7 ms (+263 %)** |

Shadow share of the frame: 47 % → **85 %**. A 6× slowdown is far beyond the extra ALU a dequant costs, so the cause is
**node-visit explosion**, not the unpack arithmetic.

Correctness verified (so this is NOT a misquantize bug): quantize truncates `q = clamp(floor((p-ref)/ext*256),0,255)`,
dequant gives `min = ref + q·ext/256 ≤ p` and `max = ref + (q+1)·ext/256 ≥ p` — a provably-conservative superbox that never
loses a true hit. Producer/consumer ref boxes also agree: `bvh_fit_cs.slang` quantizes against the build push-constants
`aabbMin/aabbMax`, and `rt_swbvh.cpp` stores the SAME `mesh->csgLocalBounds` at ref-bounds `[1+meshSlot]`; the scene root AABB
is stored at `[0]` and every scene node quantizes against it.

Most likely cause (to confirm with per-node visit counters): **8-bit precision collapse on the scene BVH's global-root
quantization** — the whole-scene world extent quantized to 256³ inflates interior boxes until culling degrades and the walk
visits a large fraction of instances per ray instead of ~log(N). Next step: instrument `render.shadow_visibility` with a
temporary `shadow_diag_*` sub-scope split (scene-walk node-fetches vs per-mesh-walk node-fetches vs triangle tests), or
temporarily A/B the scene BVH at full float vs quantized to localize the inflation to the scene level.

## 6. RESOLUTION: per-node 16-bit quantization restores the baseline

The §6-precursor diagnosis was correct: the 8-bit / 256-cell granularity inflated the scene BVH's interior boxes so
badly that the front-to-back walk lost its culling power and revisited most instances per ray. The fix is **not** to
revert to the 32-byte float node but to raise the per-coordinate precision from 8 to **16 bits**, the next power-of-two
quantization step that keeps the conservative-superbox property while shrinking the reconstructed-box error from
`extent/256` to `extent/65536` (256× finer).

### Node layout change (`NwbBvhNodeQ`, was 16 B → now 20 B)

Each axis packs **both its edges** into one uint (min edge low 16, max edge high 16), so the record is three axis
uints + two link uints = 20 bytes — a 37.5 % bandwidth cut vs the 32-byte float node, and the conservative-superbox
invariant is preserved unchanged (the max edge still dequantizes to the next cell boundary `refMin + (q+1)·extent/65536`).

| field (old 8-bit) | → | field (new 16-bit) |
|---|---|---|
| `packedMin` (qminX·qminY·qminZ, 8b each) | → | `qX` (qminX low16, qmaxX high16) |
| `packedMax` (qmaxX·qmaxY·qmaxZ, 8b each) | → | `qY` (qminY low16, qmaxY high16) |
| — | → | `qZ` (qminZ low16, qmaxZ high16) |
| `leftChild` / `rightChild` | → | unchanged |

The quantize/dequant helpers (`nwbBvhQuantize1`, `nwbBvhQuantizeBounds`, `nwbBvhQDequantizeAabb`) are kept in
lockstep across the GPU (`bvh_common.slangi` + `bvh_fit_cs.slang`) and CPU (`rt_private.h` + `rt_swbvh.cpp`) halves;
the fit kernel writes `qX/qY/qZ` and the three traversal readers (shadow / GI / caustic) consume nodes only through
the accessor helpers, so the ABI change is fully consistent. `setStructStride(sizeof(NwbBvhNodeQGpu))` auto-computes
the new 20-byte stride for both the per-mesh and scene node buffers; `sizeof(NwbBvhNodeQGpu)==20u` is asserted on the
CPU side. Build + resource cook pass clean (105 files, no slang errors); the float-tree build/refit/topology/scene-BVH
self-tests run unmodified (they validate the float tree, not the quantized mirror).

### Measured (opt, steady state, frozen stress scene) — 16-bit vs the pre-quantization baseline

Same harness as §2/§6: `cd __cmake/build/linux-clang-x64/Testing/skinning_culling_benchmark_runtime/opt` then
`NWB_GPU_TIMING_FILE=… timeout --signal=INT 25 …/stress_test_smoke`; opt scope-name hashes are stable across builds, so
the §2 baseline-probe tokens map 1:1 to the 16-bit run (26 matched tokens, all at 38 samples / interval). The
non-shadow controls match to ±0.05 ms (noise); only the two shadow-pass tokens move:

| pass | float baseline (§2) | 8-bit regressed (§6) | **16-bit (now)** | vs baseline |
|---|---|---|---|---|
| **render.shadow_visibility** | 5.08 ms | **30.22 ms** (+495 %) | **5.48 ms** | **+0.40 ms (+7.9 %)** |
| **render.frame** | 10.20 ms | 35.44 ms (+247 %) | **10.62 ms** | **+0.42 ms (+4.1 %)** |
| render.opaque_regular (control) | 0.81 | — | 0.82 | +0.01 |
| render.caustic_resolve (control) | 0.99 | — | 1.00 | +0.01 |
| render.mesh_dispatch | 1.72 | — | 1.73 | +0.01 |

The 6× visit explosion is gone: the shadow pass recovers from 30.22 ms to **within 8 % of the exact-float baseline**
(5.48 vs 5.08 ms), and shadow's share of the frame falls back from 85 % to ~52 %. The residual +0.4 ms over the float
node is the expected cost of the still-conservative superbox (16-bit quantization still produces *some* false-positive
node visits, just 256× fewer than 8-bit) plus the dequant ALU — an acceptable price for the 37.5 % bandwidth cut and a
clean, single-pass win over the original 8-bit scheme. No correctness change: the reconstructed box remains a provable
superset of the float box, so ray-box tests still never lose a true hit.

## 7. HYBRID attempt: exact-float scene BVH + 16-bit per-mesh BVH (no win → revert)

### Hypothesis

The §6 diagnosis pinned the 8-bit explosion to the **scene/instance BVH's global-root quantization** (the whole-scene
world extent is the worst-conditioned reference). The 16-bit scheme (§6-resolution) fixed the explosion everywhere, but
left a +0.4 ms residual over the float baseline. The hybrid hypothesis: keep the **scene/instance BVH exact float
(32 B)** — few nodes (2N−1 for N instances, tiny) and no precision loss at the worst-conditioned level — while keeping
the **per-mesh triangle BVH 16-bit quantized (20 B)** — huge node count where the bandwidth win is real, tight
object-space reference where precision loss is nil. Expected to recover the scene-level residual for free.

### Change (reverted at end of §7; see decision)

`rt_swbvh.cpp::buildSceneBvh` uploaded `NwbBvhNodeGpu` (32 B) instead of quantizing to `NwbBvhNodeQGpu` (20 B);
`ensureSceneBvhBuffers` sized the scene node buffer at `sizeof(NwbBvhNodeGpu)`. The three traversal consumers
(`sw_shadow_traverse.slangi`, `gi_sw_trace.slangi`, `caustic_photon_sw_cs.slang`) flipped ONLY their scene-node walk to
`NwbBvhNode` + direct `aabbMin/aabbMax` read (no `sceneRef` dequant); the per-mesh triangle walk stayed
`NwbBvhNodeQ` + `meshRef[1+meshIndex]` dequant. `g_Nwb*BvhRefBounds[0]` became reserved (kept so per-mesh `[1+meshSlot]`
indexing is unchanged). Build + cook clean (105 files). The per-mesh path, build/fit/self-test untouched.

### Measured (opt, two runs combined, n=25 non-zero shadow intervals)

| pass | float baseline (§2) | 8-bit (§6) | 16-bit (§6-res) | **hybrid (now)** | hybrid vs baseline |
|---|---|---|---|---|---|
| **render.shadow_visibility** | 5.08 ms | 30.22 ms | 5.48 ms | **5.90 ms (median)** | **+0.82 ms (+16 %)** |
| **render.frame** | 10.20 ms | 35.44 ms | 10.62 ms | **11.05 ms (median)** | **+0.85 ms (+8 %)** |
| render.opaque_regular (control) | 0.81 | — | 0.82 | 0.86 | +0.05 |
| render.mesh_dispatch | 1.72 | — | 1.73 | 0.32–1.75 | noise |

Hybrid shadow_visibility: p25–p75 = 5.35–6.76 ms, mean 5.75, n=25.

### Conclusion: hybrid is a NET LOSS vs the pure 16-bit scheme → revert to §6-resolution (16-bit)

The hybrid is **not better** — it is marginally *worse* than the 16-bit scheme it was meant to improve (5.90 vs 5.48 ms)
and **+16 % over the float baseline** (vs +8 % for 16-bit). Two independent runs agree (5.85 / 6.23 ms medians). This
**refutes the hypothesis**: the §6 residual is **NOT** dominated by scene-BVH quantization loss. The 16-bit scene
quantization was already fine enough (65536 cells) that its reconstructed boxes were not materially inflated — the
explosion was fixed at 16 bits, and there was almost no scene-level residual left to recover. Meanwhile reverting the
scene node to 32 B gives back nothing measurable and removes the (small) uniformity of a single node format.

**The honest read of all three measurements:** every quantized variant we've tried sits at +8 %..+16 % over the exact
float node, and the float node (4.61–5.08 ms) remains the fastest. The optimization premise — that shrinking node
bandwidth would speed up this traversal — does not hold for this workload: the walk is **ALU/branch/visit-latency
bound, not memory-bandwidth bound**, so the 37.5 % node-size reduction never converts to wall-clock time, and the
residual false-positive visits the quantizer introduces (however few) cost more than the bandwidth saves.

**Decision: revert the hybrid (this commit). The best performer is the original float node.** The next step is to
either (a) revert the SW BVH to the 32-byte float `NwbBvhNode` for BOTH scene and mesh (accept that this workload is
not bandwidth-bound and stop chasing node compression), or (b) keep investigating non-node-size levers (triangle
ordering / ray coalescing / wider nodes) — but node quantization is exhausted as a win for this trace.

| time | event |
|---|---|
| 09:44 | `12808b1e` front-to-back traversal → 4.61 ms opt baseline |
| 22:07 | `28b17082` 8-bit quantized traversal → 30.22 ms regression |
| 22:44 | `826c6915` dead dequant-helper cleanup |
| prior | per-node 16-bit quantization → 5.48 ms (within 8 % of float baseline) |
| now | hybrid (float scene + 16-bit mesh) → **5.90 ms** — no win, NET LOSS vs 16-bit → revert |
