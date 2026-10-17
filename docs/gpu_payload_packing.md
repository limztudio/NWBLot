# GPU payload packing

Shader packing follows the configured compiler's actual layout and the transport that carries it. A `float3` followed by a scalar already fits a 16-byte lane in the checked layout. Replacing that pair with `float4` does not by itself reduce storage or shader work.

Preserve live FP32 values, integer widths, required alignment, resource lifetimes, and stage semantics. Change the CPU mirror, shared offsets and strides, allocations, uploads, shader readers and writers, and pipeline push ranges together. Raw buffers follow their explicit shared byte stride; a shader's local aggregate alignment does not determine that raw stride. Structured and constant buffers require their compiled layout.

## Current compact contracts

| Payload | Bytes | Preserved data |
| --- | ---: | --- |
| Dedicated surfel frame constants | 64 | Four FP32 vector lanes, including cell size, radius, normal bias, accumulation cap and current frame values |
| Reflection temporal push constants | 40 | Ten live `uint` fields at offsets 0 through 36 |
| Main skinning push constants | 20 | Meshlet, skin and joint counts, skinning mode and descriptor selector |
| Meshlet bounds push constants | 8 | Meshlet count and descriptor selector |
| Local bounds push constants | 8 | Meshlet count and descriptor selector |
| Normal repack push constants | 8 | Meshlet count and descriptor selector |
| Cooked and runtime meshlet bounds | 20 | Full FP32 sphere at byte 0 and full packed cone word at byte 16 |

The dedicated surfel constants store cell size in the X component of `cellSizeRadiusBiasAccum`. `BuildSurfelFrameConstants` is the sole producer. Frame values retain their defined conversions. Persistent live and snapshot surfel records have a 96-byte stride and exact signed cell ownership.

The skinning shader derives attribute counts from each meshlet; the resource cache retains the live attribute counts. Five scalar push fields avoid the tail rounding caused by a `uint4` plus scalar layout.

Meshlet bounds use raw SRV/UAV buffers with four-byte alignment. Their sphere and cone reads end at byte 19 of each record. CPU types, cooker serialization, static and posed buffer uploads, culling readers, and the posed bounds writer share `NWB_MESHLET_BOUNDS_STRIDE`. The internal mesh payload admits MSH7 and rejects older payloads at its existing header boundary. Recook incompatible assets through the launcher; asset authors have no version or layout fields to set. Skin payloads reference the separate mesh asset and retain their current contract.

## Verification and measurement

Use Slang reflection and emitted SPIR-V to establish byte sizes and offsets. Compare complete GI, skinning and reflection programs for live loads, stores, loops and arithmetic when changing packing. Check CPU transport through native layout assertions and the adjacent-record/truncated-cone regression; a failed decode must preserve the previously admitted data.

Compiled payload and upload byte counts are exact. Physical GPU allocation depends on allocator granularity; a smaller constant buffer does not establish an equal reduction in committed VRAM. Compiler equivalence alone does not establish unchanged FPS. Runtime qualification uses fresh cooked assets, rejects warnings and errors, checks rendered output, and compares matched ordinary and animated-skinning workloads. Settled GI captures require at least 30 live rendering seconds after a minimum 360-frame warm-up.

Freeze baseline executables and matching cooked assets before rebuilding the candidate, then match host, driver, resolution, shader routes and views. Warm each ordinary GI cache separately, discard at least 30 seconds and retain at least 30 seconds of positive timing intervals. The skinning comparison runs 64 animated characters through 12 culling/view cases, repeated four times per run, with two complete runs per build in before/after/after/before order. Record asset/binary hashes, diagnostics and relevant control scopes. Keep the 0.015 ms skinning timing floor explicit when interpreting small dispatch differences; aggregate GPU time is distinct from presentation FPS. A single matched pair does not establish a repeatable speedup or cross-platform performance.

Recorded layout comparisons, rendering qualification and matched timing evidence are retained in the [GPU payload packing measurement report](reports/gpu_payload_packing_measurement_report.md).

## Candidates retained at their current sizes

Removing the CSG cutter header padding leaves its compiled 96-byte stride unchanged. Mesh instance, software instance, selector-buffer and AVBOIT tails also retain their required aligned size. These cases do not justify spelling-only conversions.

Three `float2` fields can shrink caustic emission targets from 32 to 24 bytes, but the checked coordinate consumer adds a load and the target list is small. The current 32-byte layout remains until a full workload comparison establishes a useful benefit. Packing does not reduce precision or bake frame parameters into shader arithmetic merely to remove more words.
