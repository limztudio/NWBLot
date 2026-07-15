NWB SW soft-shadow SPP A/B  (render.shadow_visibility / render.frame GPU timing)
================================================================================
Scene    : stress_test_smoke (10 skinned chars, 1 dir + 1 point light, scene-BVH
           rebuilt per frame). opt build. AMD BC-250 / RADV GFX1013.
Arms     : SPP1 vs SPP4 in NWB_SW_SHADOW_SOFT_SPP (sw_binding_slots.h).
Capture  : direct binary invocation, NWB_GPU_TIMING_FILE, ~45s each = 81 intervals
           (0.5s each). Decoded with decode_timing.py (nonzero-median intervals).
HEAD     : 55f4d0dc (both arms same HEAD, .vol recooked per arm).
.vol size: SPP1 = 4652431 B,  SPP4 = 4652591 B  (confirms shader edit landed each arm).

RAW NONZERO-MEDIAN (ms)
-------------------------------------------
scope                       SPP1        SPP4        delta
render.frame                9.157      10.181      +1.024  (+11.2%)
render.shadow_visibility    5.698       5.058      -0.640  (-11.2%)
render.opaque_regular       1.312       1.308       0.000
render.sw_bvh_sort          2.222       1.332      -0.890  (-40.1%)
render.surfel_trace         0.257       0.176      -0.081
render.caustic_resolve      1.003       0.999       0.000

** NOT TRUSTWORTHY AS-IS -- focus-stall confound **
The heavy passes were captured too sparsely (the known unattended-smoke problem:
the smoke window stalls unless actively focused, so a heavy pass fires only a
handful of times per run and its absolute ms swings with focus, not the SPP).

  render.shadow_visibility : SPP1 = 19 nonzero/81   SPP4 =  7 nonzero/81
  render.frame             : SPP1 =  6 nonzero/81   SPP4 =  7 nonzero/81

Proof the deltas are noise, not signal:
  * render.sw_bvh_sort does IDENTICAL work in both arms (bitonic sort cost is
    independent of shadow SPP), yet it shows SPP1 2.22ms vs SPP4 1.33ms = -40%.
    A real workload-independent 40% swing can only be capture noise.
  * render.shadow_visibility CANNOT be cheaper at 4x the rays: 4 spp traces
    >=4x the cone samples of 1 spp. The SPP4 < SPP1 ordering is impossible
    physically => confounded by focus stalls, not a real SPP effect.

To get a trustworthy shadow_visibility SPP1-vs-SPP4 delta, re-capture with the
smoke window kept actively focused (interactive capture) so every interval fires
the heavy pass, then compare nonzero-medians. Files in this dir are ready to
re-decode once a focused capture replaces them.

CONCLUSION (provisional): no reliable GPU-cost delta can be claimed from these
captures. SPP=4 is the visually-better setting (user-confirmed); its perf cost
must be re-measured under active focus.
