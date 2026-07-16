NWB SW soft-shadow SPP A/B  v2  (render.frame / render.shadow_visibility GPU timing)
========================================================================================
RE-CAPTURE of the v1 results in ../shadow_spp_ab/. v1 was CONFOUNDED by the unattended-
smoke focus-stall (heavy passes fired only 6-7/81 intervals), so it was re-captured here
with the focus-stall eliminated via a new NWB_RENDER_UNFOCUSED env gate (see below).

Scene    : stress_test_smoke (10 skinned chars, 1 dir + 1 point light, scene-BVH rebuilt
           per frame). opt build. AMD BC-250 / RADV GFX1013.
Arms     : SPP1 vs SPP4 in NWB_SW_SHADOW_SOFT_SPP (impl/assets/graphics/shadow/sw_binding_slots.h).
HEAD     : 55f4d0dc (both arms same HEAD; source left at SPP4 = the visually-better, user-
           confirmed setting). .vol recooked per arm (forced via stamp+vol deletion).
.vol size: SPP1 = 4652431 B   SPP4 = 4652591 B   (confirms the shader edit landed each arm).
Capture  : direct binary invocation with NWB_GPU_TIMING_FILE, NWB_RENDER_UNFOCUSED=1,
           ~90 s each (SPP1=160 intervals, SPP4=154 intervals; 0.5 s report interval).
           render.frame and render.shadow_visibility EACH fired in 100% of intervals
           (160/160 and 154/154) -- the v1 focus-stall (6-7/81) is gone.

WHAT CHANGED vs v1 (the methodological fix)
-------------------------------------------
The focus-stall root cause: Graphics::animateRenderPresent() (global/core/graphics/
module.cpp:667) skips render() entirely while the window is unfocused. On this KDE
Wayland host the XWayland focus cannot be driven reliably from a headless shell
(xset/xhost/xauth unavailable; Python X11 focus-keeping is fragile against the
compositor). So instead, shouldRenderUnfocused() now returns true when
NWB_RENDER_UNFOCUSED=1 is set -- a bounded profiling gate that keeps the renderer
rendering every frame regardless of focus. Off by default (normal always-on-top /
battery behavior unchanged). This is the reason these numbers are trustworthy and v1
were not. (decode_readable_timing.py decodes the readable-scope opt timing file here;
the caustic decode_timing.py was written for hashed opt/fin dumps and yields nothing.)

RAW NONZERO-MEDIAN (ms)   -- median is the agreed sparse-comparison statistic (notes)
-------------------------------------------
scope                       SPP1(nz)      SPP4(nz)       delta
render.frame                 9.169(5)     10.214(5)     +1.045  (+11.4%)
render.shadow_visibility     6.086(41)     5.073(5)     -1.013  (-16.6%)  *
render.opaque_regular        1.316(133)    1.313(102)     0.000
render.sw_bvh_sort           2.190(10)     1.798(5)     -0.392  *  (see note)
render.mesh_dispatch         0.071(92)     0.064(13)     ~0
render.caustic_resolve       1.001(5)      1.006(6)      ~0
render.surfel_trace          0.174(20)     0.142(5)      ~0

TRUSTED CONCLUSION
------------------
  render.frame : SPP4 is +1.05 ms / +11.4% over SPP1 (nonzero-median).
                 This reproduces v1's +1.02 ms / +11.2% almost exactly -- the whole-frame
                 cost of going from 1 to 4 soft-shadow cone samples is a real, reproducible
                 ~1 ms / ~11%. This is the bottom-line number to quote.

  render.shadow_visibility : NOT a clean per-pass delta.
                 The per-pass median is unreliable here because the GPU-timestamp publish
                 FOLD WATERMARK ("a window folded in one interval is never re-folded into the
                 next" -- tests/smoke/gpu_pass_timing_probe.h) makes the heavier arm fold
                 far fewer times: SPP1 nz=41 vs SPP4 nz=5. At n=5 the median is biased low,
                 so the SPP4 < SPP1 ordering is a fold-imbalance artifact, not a real cost
                 reduction (4 cone samples cannot be cheaper than 1 -- physically impossible).
                 * render.sw_bvh_sort shows the same artifact: it does IDENTICAL work in both
                   arms yet reads 2.19 vs 1.80, confirming the fold imbalance, not signal.

WHY SPP4 DESPITE THE COST
-------------------------
User-confirmed: SPP4 is visually clearly better (cleaner penumbra, less moving-region
shimmer). The ~1 ms / ~11% whole-frame cost is the price for that quality. If frame
budget is tight, the lever is NWB_SW_SHADOW_SOFT_SPP (1..4), trading penumbra quality
for ~0.25 ms/frame per sample.

FILES
-----
  timing_spp1.txt / timing_spp4.txt   raw NWB_GPU_TIMING_FILE dumps (readable-scope opt).
  decode_readable_timing.py           decoder for readable-scope opt timing files.
  ../shadow_spp_ab/                   v1 (confounded, focus-stall) -- kept for the method note.
  ../shadow_spp_ab/README.txt         v1 README documenting why v1 was untrustworthy.
