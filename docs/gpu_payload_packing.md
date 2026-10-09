# GPU payload packing

Shader packing follows the configured compiler's actual layout and the transport that carries it. A `float3` followed by a scalar already fits a 16-byte lane in the checked layout. Replacing that pair with `float4` does not by itself reduce storage or shader work.

Preserve live FP32 values, integer widths, required alignment, resource lifetimes, and stage semantics. Change the CPU mirror, shared offsets and strides, allocations, uploads, shader readers and writers, and pipeline push ranges together. Raw buffers follow their explicit shared byte stride; a shader's local aggregate alignment does not determine that raw stride. Structured and constant buffers require their compiled layout.

## Current compact contracts

The October 9 follow-up compares these changes against `e9e2a51a5a549de9114aee3ae08f14574471eab8`.

| Payload | Previous bytes | Current bytes | Preserved data |
| --- | ---: | ---: | --- |
| Dedicated surfel frame constants | 80 | 64 | Four FP32 vector lanes, including cell size, radius, normal bias, accumulation cap and current frame values |
| Reflection temporal push constants | 48 | 40 | Ten live `uint` fields at offsets 0 through 36 |
| Main skinning push constants | 32 | 20 | Meshlet, skin and joint counts, skinning mode and descriptor selector |
| Meshlet bounds push constants | 16 | 8 | Meshlet count and descriptor selector |
| Local bounds push constants | 16 | 8 | Meshlet count and descriptor selector |
| Normal repack push constants | 16 | 8 | Meshlet count and descriptor selector |
| Cooked and runtime meshlet bounds | 24 | 20 | Full FP32 sphere at byte 0 and full packed cone word at byte 16 |

The surfel buffer removes an unread camera-position lane and moves its cell-size scalar into the previously reserved X component of `cellSizeRadiusBiasAccum`. `BuildSurfelFrameConstants` remains the sole producer. Frame values and their conversions remain unchanged. Persistent live and snapshot surfel records retain their 96-byte stride and exact signed cell ownership.

The skinning shader derives attribute counts from each meshlet. The unused pushed attribute count and its dead graph-plan producer are removed; the resource cache's live attribute counts remain. Five scalar push fields avoid the tail rounding caused by a `uint4` plus scalar layout.

Meshlet bounds use raw SRV/UAV buffers with four-byte alignment. Their sphere and cone reads end at byte 19 of each record. CPU types, cooker serialization, static and posed buffer uploads, culling readers, and the posed bounds writer share `NWB_MESHLET_BOUNDS_STRIDE`. Removing the old padding word also removes its GPU write. The internal mesh payload admits MSH7 and rejects older payloads at its existing header boundary. Recook incompatible assets through the launcher; asset authors have no version or layout fields to set. Skin payloads reference the separate mesh asset and retain their current contract.

## Qualification

Slang reflection and emitted SPIR-V establish the byte sizes and offsets. Full before/after GI and skinning programs preserve the counted live loads, stores, loops, and arithmetic except for the removed meshlet padding write and associated work. Reflection temporal function bodies are identical. Native layout assertions and the adjacent-record/truncated-cone regression check the CPU transport and failed decoding.

Requested payload and upload savings are exact. Physical GPU allocation savings depend on allocator granularity; a smaller constant buffer does not establish an equal reduction in committed VRAM. Likewise, compiler equivalence does not establish unchanged FPS. Runtime qualification must use fresh cooked assets, reject warnings and errors, check rendered output, and compare matched ordinary and animated-skinning workloads. Settled GI captures require at least 30 live rendering seconds after frame warm-up.

The optimized launcher build completed with freshly cooked assets. All 40 selected targets passed: five native regression suites and 35 runtime cases covering reflection history, CSG lighting, hardware/software GI, animated skinning, caustics and overlap paths. Qualified runs passed warning/error checks. The independent settled GI audit admitted 320 fresh framebuffer captures across ten cases; every sequence rendered for at least 30 live seconds after a minimum 360-frame warm-up. Indirect-only CSG core regions varied by at most one display level. Full/direct cases retain the documented small shadow-boundary variation; these captures do not establish CSG FPS.

Matched performance measurements compare the frozen baseline executable and cooked assets with the current executable and recooked assets on Windows ARM64, Qualcomm Adreno X2-90, at 1280 by 900. Ordinary GI runs discard at least 30 seconds and retain at least 30 seconds of positive timing intervals after separate cache warm-ups. The skinning benchmark runs 64 animated characters through 12 culling/view cases, repeated four times per run, with two complete runs per build in before/after/after/before order. Shader routes, views, asset and binary hashes, runtime diagnostics and relevant control scopes are recorded.

| Matched workload | Baseline | Current | Observed change |
| --- | ---: | ---: | ---: |
| Ordinary GI, default scene, presentation FPS | 281.55 | 281.26 | -0.10% |
| Ordinary GI, complex scene, presentation FPS | 210.46 | 212.24 | +0.85% |
| 64-character skinning, aggregate GPU frame time | 42.143 ms | 42.057 ms | -0.20% |
| 64-character skinning, meshlet-bounds dispatch time | 0.093394 ms | 0.093787 ms | +0.000393 ms |

These measurements show no material regression in the qualified ordinary and skinning workloads. The bounds difference is below the qualification's 0.015 ms timing floor. Ordinary GI has one matched pair per scene; the results do not establish a repeatable speedup, isolated CPU savings, or performance on other platforms. Aggregate GPU time is not presentation FPS. The compiled payload reductions and deleted padding write are the demonstrated benefits.

The follow-up's exact compiler artifacts, frozen baseline binaries/assets, runtime reports and performance acquisitions live under `tests/__cmake/slang_struct_packing_round2_20261009/`; optical compiler artifacts are under `tests/__cmake/slang_pack_optical_round2_20261009/`. These ignored local artifacts are verification evidence rather than runtime dependencies.

## Candidates retained at their current sizes

Removing the CSG cutter header padding leaves its compiled 96-byte stride unchanged. Mesh instance, software instance, selector-buffer and AVBOIT tails also retain their required aligned size. These cases do not justify spelling-only conversions.

Three `float2` fields can shrink caustic emission targets from 32 to 24 bytes, but the checked coordinate consumer adds a load and the target list is small. The current 32-byte layout remains until a full workload comparison establishes a useful benefit. Packing does not reduce precision or bake frame parameters into shader arithmetic merely to remove more words.
