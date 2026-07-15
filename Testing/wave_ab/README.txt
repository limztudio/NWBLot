NWB Surfel-Trace SH Reduction: Wave-Intrinsic A/B
========================================================================================
GOAL
  Replace the per-surfel 6-stride barriered groupshared tree reduction in
  surfel_trace_common.slangi with a single WaveActiveSum, gated behind a define so the
  two arms compare at one HEAD. This GPU is the textbook candidate: AMD BC-250 / RADV
  (GFX1013) subgroup size is 64, and NWB_SURFEL_RAYS_PER_SURFEL == 64, so the workgroup
  == the wave and the whole barriered reduce collapses to one subgroup op with no
  barriers and (optionally) no groupshared memory.

THE REDUCTION (surfel_trace_common.slangi, one workgroup of 64 threads per surfel)
  - groupshared float4[64] x3 (s_surfelShR/G/B).
  - Each active lane writes radiance * nwbSurfelShBasis(rayDir); inactive lanes hold the
    zero from the top-of-kernel init. `active` is UNIFORM across the wave (it depends
    only on surfelIndex, which is per-group, not per-lane), so inactive waves sum zeros
    and the result is identical either way.
  - Baseline: GroupMemoryBarrierWithGroupSync then a 6-iter stride tree (32..1), each
    iter its own GroupMemoryBarrierWithGroupSync => 7 group-wide barriers.
  - Wave path: WaveActiveSum on the float4 (maps to a SPIR-V subgroup op), then
    WaveReadLaneFirst to get lane 0's copy. 0 barriers, 0 shared memory.

SCENE    : stress_test_smoke (10 skinned chars, 1 dir + 1 point light, scene-BVH
           rebuilt per frame). opt binaries, AMD BC-250 / RADV GFX1013.
ARMS     : NWB_SURFEL_USE_WAVE_REDUCE 0u (baseline, groupshared tree) vs 1u (WaveActiveSum).
           impl/assets/graphics/gi/surfel/surfel_binding_slots.h -- shader-only define, so
           each arm = a fresh .vol recook of the same namesym-domain opt binary (no per-arm
           C++ rebuild).
HEAD     : 66cf9ce8 ("Add wave-intrinsic A/B switch for surfel SH reduction"). Wave
           discovery plumbing (queryWaveLaneCount + VkPhysicalDeviceSubgroupProperties)
           is COMMITTED at this HEAD and does NOT touch the surfel shader at runtime.
           Both arms built from this exact tree (clean namesym opt rebuild per arm).
CAPTURE  : Testing/shadow_spp_ab_v3/capture.sh (direct binary, namesym logserver for
           READABLE scope names, NWB_RENDER_UNFOCUSED=1, NWB_GPU_TIMING_FILE, ~90s,
           0.5s report interval). baseline=143 intervals (frame in 100%), wave=133
           intervals (frame in 100%). Clean captures, no focus-stall.

RAW NONZERO-MEDIAN (ms)   -- median is the agreed sparse-comparison statistic
                                baseline          wave             delta_ms   delta_%
  render.frame                   10.1592 (n=10)    10.2109 (n=6)     +0.0517   +0.51%
  render.surfel_trace             0.1397 (n=10)     0.1387 (n=6)     -0.0010   -0.72%
  render.surfel_resolve           0.2556 (n=10)     0.2545 (n=6)     -0.0010   -0.41%
  render.shadow_visibility        5.0368 (n=10)     5.0733 (n=6)     +0.0366   +0.73% (noise)

  (n is low because of the GPU-timestamp fold watermark; render.frame is the trustworthy
   whole-frame sum. render.shadow_visibility is the heaviest pass and swings +/-0.04 ms
   between arms for IDENTICAL work -- that is the fold artifact, not a wave effect.)

CONCLUSION (PERF-NEUTRAL)
  The wave-intrinsic reduction is correctly EQUIVALENT and compiles/runs cleanly, but it
  is PERF-NEUTRAL on this scene:
    render.surfel_trace : 0.1397 -> 0.1387 ms   (-0.001 ms / -0.7%, within fold noise)
    render.frame        : 10.159 -> 10.211 ms   (+0.05 ms / +0.5%, within fold noise)

  WHY neutral -- surfel_trace is ~1.4% of the 10 ms frame, and the reduction is a tiny
  fraction of that 0.14 ms (the per-lane cost is the BVH ray trace + bounce gather, not
  the 7 barriers). The reduction already had no bank conflicts and 6 fully-coalesced
  strides; replacing it with a subgroup op removes ~7 barriers but the surfel pass was
  never barrier-bound. The whole-frame number confirms no regression.

DECISION
  Left OFF (NWB_SURFEL_USE_WAVE_REDUCE 0u) in the committed baseline: perf-neutral means
  there is no measured win to justify the subgroup-size coupling (the wave path assumes
  subgroup==workgroup==64, which is true here but NOT portable to a future 32-lane or
  128-thread configuration without a fallback). The switch + the WaveActiveSum body are
  KEPT in the shader so the path is one define-flip away if a later scene/profiler shows
  the reduction becoming a hotspot, or if a portable wave-fallback is added.
