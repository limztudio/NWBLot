# Renderer CPU gathering benchmark

The optional fixture measures preparation and graph/render callback CPU work separately, with complete successful-frame and GPU-source coverage. It is intended for comparing CPU-only renderer changes. The six fixed scenes contain 64 objects and one opaque reflective backdrop:

| Workload | Objects |
| --- | --- |
| `opaque` | 64 opaque objects |
| `hybrid` | 32 opaque and 32 transparent objects |
| `shared` | 64 transparent objects sharing mesh and material identities |
| `unique` | 64 transparent objects with distinct mesh and material identities but identical geometry and material payloads |
| `overrides` | 64 transparent objects with distinct mutable tint overrides |
| `runtime` | 56 transparent objects and eight actual runtime mesh/skeleton owners held at distinct poses |

The runtime case verifies eight resolved runtime renderers and eight owners. It does not measure continuously animated deformation. All scenes use 960x720, fixed delta 0.016666667, sampling seed zero, hardware reflection budget 4096, optical query cap 16, and refraction enabled. Temporal/spatial reflection filtering, feedback, diagnostics and caustics are disabled. Missing hardware support or incomplete scene preparation fails acquisition.

## Build

```text
cmake --preset windows-clang-arm64 -DNWB_BUILD_RENDERER_GATHER_BENCHMARK=ON
cmake --build --preset windows-clang-arm64-opt --target nwb_renderer_gather_benchmark
ctest --preset windows-clang-arm64-opt --output-on-failure -R "^nwb_renderer_gather_benchmark_analysis_unit$" -j1
```

Use the debug build/test presets for debug qualification. The fixture is opt-in and no acquisition is registered in the default CTest run. The generator mirrors ordinary project assets into a private combined project root, then adds the benchmark identities and project-owned surface. It validates templates and the exact generated file set, rejecting stale files before writing. A separate short cache root and runtime keep its generated material dispatch separate from ordinary smoke assets.

For this preset, the executable is `__exec/windows/arm64/full/opt/renderer_gather_benchmark.exe`; the private runtime is `__cmake/build/windows-clang-arm64/Testing/gather_benchmark_runtime/opt`. Generated input identity is recorded in the adjacent `gather_benchmark_generated/opt/generation_identity.json`.

## Measurement and comparison

Every launch completes 96 successful warm-up frames, exactly 256 measured frames, and 32 drain frames. CPU scopes must describe the same contiguous successful frames and publication indices. `graphics.prepare_resources` sums successful preparation callbacks; `graphics.render_passes` sums their render callbacks. The parent `graphics.render` includes both and their scheduling. Do not add nested scopes. Failed preparation has its own marker and invalidates the fixture result.

GPU durations are divided by actual completed range counts whose complete source spans lie inside the measured CPU window. Required scopes need at least 90% coverage and satisfy the two-range-or-2% count bound. Opaque's initial AVBOIT clear is allowed only strictly before the measured window; inactive temporal/spatial/depth scopes must remain absent. Incomplete controlled shutdown retains available raw rows and deliberately omits the completion footer.

Freeze each independently built executable with its loader dependencies, authored runtime resources and physical source snapshots. A source manifest contains `revision` and a nonempty `files` mapping from snapshot paths to SHA256 strings. Verify that instrumentation, fixture, helpers and all changes outside the intended optimization are identical across arms; the runner requires identical authored volumes but cannot establish build provenance from a revision label alone. Source/build correctness and image/native qualification remain separate requirements.

```text
python -B tests/smoke/renderer_gather_benchmark.py --baseline-executable <baseline-exe> --baseline-runtime <baseline-runtime> --baseline-source-manifest <baseline-source.json> --candidate-executable <candidate-exe> --candidate-runtime <candidate-runtime> --candidate-source-manifest <candidate-source.json> --logserver-executable <shared-logserver-exe> --output-directory <new-directory> --workload shared --mode timing
```

Each workload uses eight balanced AB/BA blocks, 16 independent launches, by default. Source, binary, loader, interpreter, helper and authored-resource identities are verified around every trial. Only canonical contiguous runtime pipeline-cache volumes may change. Explicit nonempty Vulkan layer overrides are rejected. Build, cook, GPU validation, capture and other acquisitions must not overlap timing.

The primary useful-reduction gate is `max(0.02 ms, 3% of baseline graphics.render)`. Whole-frame CPU time has a separate non-regression tolerance. GPU frame, opaque, shadow, lighting, composite and present controls require equivalence before a CPU benefit can be attributed to the change. Intervals are per workload, with no pooled six-workload confidence claim. Keep every failed, unfavorable and incomplete attempt; do not selectively extend a campaign. `--plan-only` validates identities and writes the plan without launching; a later acquisition needs a new output directory.

## Memory observations

Use another output directory with `--mode memory`. This enables per-owner allocation/reallocation/deallocation counters, used/reserved bytes and historical individual-arena peaks. Counters use the end-of-warm-up baseline rather than process-lifetime totals. Required owners must be present; unavailable optional owners are null, not zero savings. Historical arena peaks are neither per-frame nor concurrent process peaks. Memory instrumentation changes the workload, so its CPU/GPU timings must never be pooled with a timing campaign or presented as a measured CPU gain.
