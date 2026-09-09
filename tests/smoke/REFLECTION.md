The stage-two smooth reflection baseline is implemented. It traces one hardware
reflection ray for each queued eligible opaque or primary-glass sample and
composites the result with AVBOIT. The seven-capture smoke has passed on the
Qualcomm Adreno X2-90 with real hardware ray queries and `--gpudbg`: every capture
completed normally with clean runtime and GPU validation logs. This establishes
the cases described below. The 286-test ECS graphics suite, reflection/refraction
analysis suites, GPU-debug refraction comparison, 33-capture exact-duplicate
gallery, and caustic/refraction capture also passed. The refraction fixture
explicitly disables reflection to isolate its transmission comparisons.

The public `ReflectionSettings` contract is declared in
`impl/ecs_render/reflection/settings.h` and applied through
`RendererSystem::setReflectionSettings`. Material surface hooks explicitly supply
RGB specular F0 and perceptual roughness. These values have their own G-buffer and
primary-glass capture targets; project-defined BXDF `param0/param1` retain their
existing meanings. Fixtures use the typed `smoke_surface` mutable fields
`runtime.specular_f0` and `runtime.perceptual_roughness`.

At this stage `Hardware` traces the bounded ray queue, `ScreenSpace` supplies the
analytic environment without screen hits, `Hybrid` uses the available hardware
path, and `Disabled` produces identity reflection outputs. Hierarchical SSR and
hybrid continuation are subsequent stages. The smoke selects its mode explicitly
and sets both analytic environment colors to black, preventing environment color
from passing as a reflected marker. Its ray budget is 1,382,400, enough for both
960x720 layers; default budget quality is not established by this test.

The dedicated `nwb_reflection_smoke` target uses `reflection_project.cpp` and
`framebuffer_capture.cpp`. It reads back the actual 960x720 framebuffer after
16 graphics presentation frames, then exits through normal application shutdown.
Run from `Testing/smoke_runtime/<configuration>` so `cwd/res` selects the matching
packed smoke assets. The shared `nwb_transparent_multi_smoke_assets` cook includes
both `impl/assets` and `tests/smoke/assets`; rebuilding the target recooks changed
shader/material assets.

The cases are:

- `offscreen`: the camera is at `(0, 1.4, -6)`, facing `+Z` with a 60-degree
  vertical FOV. A smooth opaque mirror lies at `z=0`. Red and green sphere markers
  sit behind the camera at `z=-8`, so their mirror images have virtual depth
  `z=+8`, 14 units from the camera. The two reflected colors must appear in their
  geometrically predicted regions. Disabled and screen/environment captures must
  contain neither colored marker.
- `moved`: both offscreen markers shift right by 0.9 world units. Their images
  must move right by approximately 40.085 pixels at the fixed projection, with
  negligible vertical movement. This is a static before/after geometry test,
  not a temporal-history test.
- `opaque_glass`: adjacent opaque and clear glass spheres reflect red/green
  panels behind the camera. A grayscale striped backdrop remains visible through
  the glass, and a blue foreground AVBOIT strip crosses both objects. Glass uses
  IOR 3.8 and its corresponding dielectric F0 of approximately 0.34028. This is
  deliberately a high-index optical stress fixture, not ordinary window glass;
  coverage is zero and absorption is disabled.

`reflection_smoke.py` captures disabled, screen/environment, and hardware
`offscreen`; disabled and hardware `moved`; and disabled and hardware
`opaque_glass`. It requires the matching accepted-route diagnostic, normal
shutdown, nonempty actual framebuffer data, and strict runtime/validation logs.
Unsupported required hardware is a skip unless `--require-hardware` was selected.
The application must self-exit within each capture timeout. The outer runner
allows an additional 90 seconds for bounded child startup and process/logserver
cleanup, rather than interrupting that cleanup at the capture deadline.

The image assertions require each mirror marker to cover at least 25% of its
analytically projected disk area: 659 pixels at 960x720. At least 95% of each
marker must lie in its predicted region, and its centroid and movement must match
the fixed projection. Each opaque/glass receiver must acquire colored reflection
in at least 2% of its test region. The blue foreground mask must retain at least
97% of its original pixels and add no more than 3%; exterior changes are limited
to 0.5% of the sampled exterior regions. Twelve independent synthetic analysis
tests reject flat tint, missing or stationary markers, sparse correctly located
patches, missing glass reflection, expanded/lost foreground, exterior corruption,
malformed frames, and inherited fixture controls.

The completed stage-two hardware captures contained 2,643-2,687 pixels per mirror
marker, with no marker pixels outside the predicted regions. The measured
horizontal movements were approximately 40.2 pixels for red and 40.1 for green,
against the 40.085-pixel prediction. Disabled and screen/environment controls had
zero colored marker pixels. The paired spheres gained 3,880 opaque and 3,881 glass
reflection pixels. All 12,811 foreground pixels were retained, with zero added
foreground pixels and zero changed exterior pixels. These are fixture-specific
observations from the completed capture; they are not general image-quality or
performance guarantees.

Build and run on the Windows ARM64 debug preset:

```powershell
cmake --build --preset windows-clang-arm64-dbg --target nwb_reflection_smoke
ctest --test-dir __cmake/build/windows-clang-arm64 -C dbg --output-on-failure -R '^nwb_reflection_capture_analysis_unit$'
python tests/smoke/reflection_smoke.py --executable __exec/windows/arm64/full/dbg/reflection_smoke.exe --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg --output-directory __cmake/build/windows-clang-arm64/Testing/smoke/dbg/reflection_gpudbg --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe --require-hardware --application-arg=--gpudbg
```

The portable CTest entry is `nwb_reflection_capture_smoke`. The launcher scene is
`reflection`, with `--reflection-case offscreen|moved|opaque_glass` and
`--reflection-mode disabled|screen|hardware|hybrid`. All controls remain test-local
and forward to the typed renderer API.

Artifacts include the original BMPs, PNG conversions with identical RGB pixels,
`reflection_manifest.json`, and a self-contained offline `reflection.html`.
Images and an unvalidated report are saved before the final image assertions so
failed visual comparisons remain inspectable. A passing report records the
resulting metrics. The report uses actual rendered pixels; it contains no
illustrated or generated replacement images.

Remaining work and limits are explicit:

- Only smooth single-bounce reflection is validated. Roughness currently gates
  trace eligibility; rough lobe sampling and reconstruction remain future work.
- Hardware hits use the authored BXDF with linear radiance and bounded lighting
  inputs. Additional reflection, shadow, GI, and transparent-volume transport at
  a secondary hit are not traced. A second transparent glass surface can therefore
  hide content that full optical transport would reveal.
- Primary glass reflection is composited behind foreground AVBOIT. The paired
  fixture checks this order and visible reflection on glass; it is not a numerical
  proof of full reflection/refraction energy conservation or multilayer optics.
- Hierarchical SSR needs on-camera marker panels whose direct and mirror images
  occupy separate predicted regions. Hardware/SSR/hybrid comparison, frustum-edge
  fallback, and motion/disocclusion/history tests follow that implementation.
- The existing strict refraction, exact-duplicate, and caustic tests passed as
  separate regression gates. Combined reflection/duplicate and more complex
  reflected secondary transport cases remain part of the subsequent matrix.
- No reflection performance improvement has been measured. The GPU counter buffer
  counts candidates, actual hardware rays/hits, eligible opaque/glass samples, and
  fallback samples; completion-associated CPU publication and benchmark assertions
  remain future work.

For subsequent performance runs, enable `NWB_REFLECTION_SMOKE_TIMING=1` and use
`NWB_GPU_TIMING_FILE`. The fixture forwards real frame delta to `FpsProbe` and
`GpuPassTimingProbe`, while fixed simulation delta remains separate. The current
reflection scopes are `render.reflection_classify`, `render.reflection_build_args`,
and `render.reflection_hardware`; more scopes will accompany SSR and filtering.
The probe has a 64-scope cap, so the benchmark must require all expected scopes
rather than silently accepting missing timings. Decode opt/fin scope hashes with
the matching `.namesym` file using `tests/ab/gpu_timing_parse.py`.

Collect repeated, interleaved route runs after shader/history warm-up. Compare the
`render.frame` critical path and affected reflection scopes alongside unaffected
controls such as `render.opaque_regular`, `render.shadow_visibility`, and applicable
caustic scopes; do not sum overlapping packet envelopes. CPU-visible counters
must retain their originating frame/route and come only from completed accepted
submissions. Ray savings require actual ray-count evidence, and timing changes
that also appear in unaffected controls indicate system variance.
