NWB Meshlet-Bounds Reduction: Wave-Intrinsic A/B
========================================================================================
GOAL
  Measure whether the gated wave-intrinsic path in meshlet_bounds_cs.slang (commit
  e498c2b1, NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE) is faster than the groupshared-tree
  baseline on this device. Mirrors the surfel wave_ab methodology but targets a DIFFERENT
  shader and a DIFFERENT result -- the surfel reduce was PERF-NEUTRAL; this one is NOT.

THE REDUCTION (meshlet_bounds_cs.slang, one workgroup of 128 threads per meshlet)
  - Six per-meshlet reductions (min/max over positions -> AABB + sphere radius).
  - Baseline: each reduce is a groupshared tree of 7 strides (64..1) each behind its own
    GroupMemoryBarrierWithGroupSync => ~8 group-wide barriers per reduce x 6 = ~48 barriers.
  - Wave path: NWB_SKINNED_MESH_BOUNDS_GROUP_SIZE_X (128) > the wave (subgroup 64 on AMD
    BC-250 / RADV GFX1013), so -- unlike the surfel SH reduce (group == wave) -- one
    WaveActive* does NOT span the group. The wave path is a two-stage fold: one
    WaveActive* per wave (no barrier) stores a per-wave partial, then ONE barrier + a tiny
    cross-wave tree (2 waves) folds them. ~2 barriers per reduce x 6 = ~12 barriers.
    (Verified by SPIR-V inspection: 17 subgroup ops in the wave arm, 0 in baseline.)

WHY THIS PASS REACTS (unlike surfel_trace)
  surfel_trace is ~0.14 ms but the reduction is a tiny fraction of it (the per-lane cost is
  the BVH ray trace + bounce gather, not the 7 barriers) -> swapping barriers for a subgroup
  op was perf-neutral. meshlet_bounds is a ~0.1 ms pass whose work IS the reduction: each
  thread skins a few vertices then the group folds them. Barriers are a large fraction of
  the pass, so removing ~36 of them shows up as a real, measurable win.

SCENE    : stress_test_smoke (10 skinned chars, 5 transparent + 5 opaque, 1 dir + 1 point
           light, scene-BVH rebuilt per frame). opt binaries, AMD BC-250 / RADV GFX1013.
ARMS     : NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE 0u (baseline, groupshared tree) vs 1u
           (two-stage wave fold). impl/assets/graphics/skinned_mesh/constants.h -- SHADER-
           ONLY define (constants.h is #include'd only by the shader), so each arm = a fresh
           .vol recook of the SAME namesym-domain opt binary (no per-arm C++ rebuild).
HEAD     : e498c2b1 (wave-intrinsic A/B switch for meshlet-bounds reductions). Define left
          at 0u (baseline) at HEAD; the wave arm was captured by a transient flip + recook
          then restored to 0u.
CAPTURE  : capture.sh (namesym-domain opt binary + namesym logserver for READABLE scope
           names, NWB_RENDER_UNFOCUSED=1, NWB_GPU_TIMING_FILE, 90 s, 0.5 s report
           interval). RT points at the namesym build's skinning_culling_benchmark_runtime
          (where the just-flipped .vol recook lands). baseline=143 intervals, wave=154.

STATISTIC
  Nonzero-median (the agreed sparse-comparison statistic; see Testing/wave_ab/README.txt
  and Testing/shadow_spp_ab_v3/README.txt). Means are also valid here because the pass
  under test fired 100% nonzero in BOTH arms (no fold-zeroing on it).

RAW RESULTS (ms)
-------------------------------------------
scope                       baseline(nz)      wave(nz)       delta_ms     delta_%
mesh_skinning.meshlet_bounds 0.1040 (96)      0.0810 (119)    -0.0229     -22.08%  *
mesh_skinning.skinning       0.3080 (104)     0.3061 (123)    -0.0019      -0.62%  (control)
mesh_skinning.repack_normals 0.1349 (102)     0.1349 (123)    +0.0000      +0.00%  (control)
render.frame                 9.5098 (4)       9.4600 (8)      -0.0498      -0.52%  (FOLD)
render.shadow_visibility     4.6472 (8)       4.3437 (8)      -0.3035      -6.53%  (FOLD)
render.mesh_dispatch         0.0532 (12)      1.7375 (10)     +1.6843   +3165.98%  (FOLD)
render.opaque_regular        1.3045 (67)      1.2989 (81)     -0.0056      -0.43%  (control)
render.caustic_resolve       1.0017 (5)       0.9958 (8)      -0.0060      -0.59%  (control)
render.surfel_trace          0.1424 (4)       0.1432 (8)      +0.0008      +0.56%  (FOLD)
render.avboit_accumulate     0.6685 (5)       0.6625 (8)      -0.0060      -0.89%  (control)

* THE PASS UNDER TEST. meshlet_bounds fired 100% nonzero in both arms (96/96 baseline,
  119/119 wave -- zero fold-zeroing), and the ENTIRE distribution shifted down:
    baseline: min0.0263 p25_0.0709 med0.1040 p75_0.1569 p90_0.1871 max0.2665 mean0.1148
    wave:     min0.0183 p25_0.0476 med0.0810 p75_0.1198 p90_0.1699 max0.2559 mean0.0925
  mean delta = -0.0222 ms, pooled SE = +/-0.0078 ms => z = -2.85 (significant, >2 sigma).

FOLD / CONTROL NOTES
  - render.frame is FOLD-DECIMATED here: 139/143 baseline intervals report avg=0.0000 (the
    GPU-timestamp fold watermark -- "a window folded in one interval is never re-folded into
    the next"; tests/smoke/gpu_pass_timing_probe.h). Only 4-8 nonzero samples survive, so
    render.frame is NOT a trustworthy whole-frame sum THIS run. This is WORSE fold behavior
    than the prior wave_ab / shadow_spp_ab_v3 captures (which had more surviving frame
    samples); likely capture-to-capture variance in how the timestamp ring folded.
  - render.mesh_dispatch +3166% and render.shadow_visibility -6.5% are FOLD ARTIFACTS on
    passes that do IDENTICAL work in both arms (mesh_dispatch baseline 0.053 is absurdly low
    vs its normal ~1.8 ms -- it folded badly in baseline, not in wave). DO NOT read them.
  - CONTROL PASSES ARE FLAT, confirming the two captures were in comparable machine state
    (so the meshlet_bounds delta is a real shader effect, not a thermal/load difference):
    skining -0.6%, repack_normals +0.0%, opaque_regular -0.4%, caustic_resolve -0.6%,
    avboit_accumulate -0.9%.

CONCLUSION
  The wave-intrinsic two-stage fold is a REAL, MODEST WIN on this pass:
    mesh_skinning.meshlet_bounds : 0.1040 -> 0.0810 ms  (-0.023 ms / -22%, z=-2.85)
  ...unlike the surfel SH reduce (perf-neutral), because this pass IS barrier-heavy. The win
  is small in absolute terms (-0.023 ms on a ~10 ms frame = -0.2%), and whole-frame
  confirmation was not possible this run due to render.frame fold-decimation, but the pass-
  under-test signal is clean: 100% firing, full-distribution shift, controls flat.

CAVEAT
  Single capture pair (90 s each). The -22% magnitude has uncertainty (high per-pass stdev
  ~0.057 ms on a ~0.1 ms pass -- the pass is bursty). A repeat capture would tighten the
  number, but the direction (faster) is well-established by the full-distribution shift and
  the z-score.

DEFINE STATE AT HEAD: NWB_SKINNED_MESH_BOUNDS_USE_WAVE_REDUCE = 0u (baseline). The wave arm
  is opt-in; flipping the default to 1u is a separate decision (see follow-up).
========================================================================================
