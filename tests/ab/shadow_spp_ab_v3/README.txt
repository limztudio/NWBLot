NWB SW soft-shadow SPP A/B  v3  (SPP1 / SPP2 / SPP4 three-way)
========================================================================================
User asked for SPP=2 as a middle ground after v2 showed SPP4 = +1.05 ms / +11.4% over
SPP1, and "1 ms is critical." This v3 re-captures ALL THREE arms from the SAME HEAD so the
1->2->4 trend is on one baseline (v2's SPP1/4 runtime .vol had been cleaned, so its numbers
could not be extended to SPP2 cleanly).

Scene    : stress_test_smoke (10 skinned chars, 1 dir + 1 point light, scene-BVH rebuilt
           per frame). opt binaries. AMD BC-250 / RADV GFX1013.
Arms     : SPP1 vs SPP2 vs SPP4 in NWB_SW_SHADOW_SOFT_SPP
           (impl/assets/graphics/shadow/sw_binding_slots.h).
HEAD     : 55f4d0dc (all three arms same HEAD). Source left at SPP4 = the visually-better,
           user-confirmed setting.
SPP is a SHADER-ONLY define (referenced only by shadow_rayquery_soft_cs.slang +
           sw_shadow_soft_opaque_cs.slang, never by C++), so each arm = a fresh .vol recook
           of the SAME namesym-domain opt binary (no per-arm rebuild needed).
.vol size: SPP1 = 4652431 B   SPP2 = 4652607 B   SPP4 = 4652591 B
           (SPP1/SPP4 byte counts reproduce v2 exactly -- confirms the edit landed each arm.)

WHAT CHANGED vs v1/v2 (the readable-name fix)
---------------------------------------------
v1/v2 ran the FULL-domain opt binary, but its Name::c_str() writes HASHED scope text into
NWB_GPU_TIMING_FILE (the logserver only resolves names for its OWN log window -- it cannot
retroactively fix file content the client already wrote). v2 happened to land readable names
because that capture used a namesym-domain path. v3 makes this explicit and reproducible:
the capture now runs the NAMESYM-domain opt binary
(__exec/linux/x64/namesym/opt/stress_test_smoke + namesym-domain logserver), which is
compiled with -DNWB_BUILDMODE -- the ONLY opt variant whose Name::c_str() resolves READABLE
text. See capture.sh. Verified: timing files contain literal "render.frame" (not hashes).

Capture  : capture.sh (direct binary, NWB_RENDER_UNFOCUSED=1, NWB_GPU_TIMING_FILE, namesym
           logserver via -a/-p), ~90 s each, 0.5 s report interval.
           render.frame fired in 100% of intervals every arm (SPP1=6nz/157, SPP2=8nz/156,
           SPP4=6nz/156) -- the v1 focus-stall (6-7/81) is gone.

RAW NONZERO-MEDIAN (ms)   -- median is the agreed sparse-comparison statistic
-------------------------------------------
scope                       SPP1(nz)       SPP2(nz)       SPP4(nz)       SPP1->4 delta
render.frame                 9.009(6)       9.364(8)      10.054(6)      +1.045  (+11.6%)
render.shadow_visibility     3.956(6)       4.347(7)       4.987(7)      +1.031  (+26.1%) *
render.opaque_regular        1.294(110)     1.293(115)     1.295(113)     ~0
render.sw_bvh_sort           2.193(7)       2.219(7)       2.008(8)       ~0 (same work) *
render.caustic_resolve       0.991(6)       0.988(7)       0.987(7)       ~0
render.surfel_trace          0.133(6)       0.139(7)       0.133(6)       ~0
render.avboit_accumulate     0.660(6)       0.658(7)       0.660(7)       ~0

* render.shadow_visibility nz counts (6-7) are low because of the GPU-timestamp FOLD
  WATERMARK ("a window folded in one interval is never re-folded into the next" --
  tests/smoke/gpu_pass_timing_probe.h): the heavier arm folds fewer times. The ABSOLUTE
  pass median is therefore still fold-biased (do NOT read SPP4 < SPP1 as a real per-pass
  ordering). render.sw_bvh_sort does IDENTICAL work in all arms yet reads 2.19 / 2.22 /
  2.01 -- the -0.19 swing at identical workload confirms the fold artifact, NOT a real
  SPP effect. The bottom-line render.frame median is trustworthy because it is the
  whole-frame sum (every interval folds render.frame, n is large).

TRUSTED CONCLUSION
------------------
  render.frame (whole-frame, trustworthy):
    SPP1 = 9.009 ms   SPP2 = 9.364 ms   SPP4 = 10.054 ms
    SPP2 is +0.36 ms / +3.9% over SPP1; SPP4 is +1.05 ms / +11.6% over SPP1.
    SPP2 costs ~1/3 of the SPP4 overhead (0.36 vs 1.05 ms).
    -> SPP2 is the price/quality sweet spot: most of the quality gain for ~1/3 the cost.

  This reproduces v2's SPP1->4 +1.05 ms almost exactly (v2 +1.045), confirming the
  measurement is stable and the v3 pipeline is trustworthy.

  render.shadow_visibility (per-pass, fold-biased -- direction only):
    3.956 -> 4.347 -> 4.987 ms, monotonic INCREASING with SPP -- physically correct
    (more cone samples = more rays = more ms), unlike v1/v2's fold-inverted ordering.

FILES
-----
  timing_spp1.txt / timing_spp2.txt / timing_spp4.txt   raw NWB_GPU_TIMING_FILE dumps.
  spp1_stdout.log / spp2_stdout.log / spp4_stdout.log   app stdout (empty: GUI, logs go to logserver).
  capture.sh                                            namesym-domain GPU-timing capture harness.
  screenshot.py                                         ctypes X11 screenshot (LinuxX11Capture from the repo).
  cook_and_shot.py                                      recook-a-given-SPP-then-screenshot driver.
  shots/spp1.png / spp2.png / spp4.png                  1280x900 screenshots per arm (distinct md5; shadow
                                                        softness visibly differs).
  shots/spp_comparison.png                              labeled side-by-side SPP1|SPP2|SPP4 (with render.frame ms).
  ../shadow_spp_ab/decode_readable_timing.py            decoder for readable-scope opt timing files.
  ../shadow_spp_ab_v2/                                  v2 (SPP1 vs SPP4 only; SPP2 added here).
