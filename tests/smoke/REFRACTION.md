The refraction smoke renders a clear IOR 1.5 sphere with zero AVBOIT color coverage in
front of an opaque stripe wall. A red transparent panel sits in front of the glass;
a blue transparent panel sits behind it. The fixed camera and unlit authored BXDF
make the image comparison independent of lighting histories.

Build or open the scene through the normal asset cooking flow:

```powershell
python tests/smoke/launch.py refraction --config dbg
```

Run `nwb_refraction_capture_smoke` through CTest after building
`nwb_refraction_smoke`. `nwb_refraction_gpudbg_capture_smoke` runs the same sequence
with Vulkan validation enabled in `dbg`. Each test captures three swapchain images
after the same graphics frame count: refraction disabled, automatic tracing, and
explicit screen-space tracing. The latter uses the renderer's normal hardware
tracing preference, leaving device capability reporting intact.

The smoke requires successful resolve dispatch diagnostics, normal application
shutdown, and clean runtime/validation logs. Its visual checks require both dark
stripes becoming light and light stripes becoming dark inside the glass, stable
pixels outside its silhouette, and preservation of the foreground red panel.
Both edge directions are necessary so flat tinting or attenuation cannot pass as
refraction. Captures, amplified differences, and numeric measurements are written
under `Testing/smoke/<config>/refraction` in the selected CMake build directory.

To require a hardware ray-query dispatch on a known capable GPU, run
`refraction_capture_smoke.py` with `--require-hardware`, together with its required
`--executable`, `--working-directory`, and `--output-directory` paths. The normal
CTest accepts the engine's natural route on GPUs without ray queries and always
checks the explicitly selected screen-space route. Lack of swapchain readback
support is reported as a skip; validation failures remain failures.

Interactive fixture controls are `NWB_REFRACTION_SMOKE_ENABLED=0` for the baseline
and `NWB_REFRACTION_SMOKE_HARDWARE=0` for screen-space tracing. They call public
renderer settings and exist only in the smoke project.

The complex-case gallery is an additional visual inspection fixture. It covers
`single`, `separate`, `stacked`, `intersecting`, `nested`, `coincident`,
`coincident_tinted`, `torus`, `same_mesh`, and `prism`, plus twelve exact-duplicate
references and controls. Select a case interactively
with `--refraction-case <case>` on the smoke launcher. Add `--refraction-geometry`
to show the same arrangement with colored translucent materials, normal-based
shading, a neutral backdrop, and refraction disabled. This makes overlapping and
nested geometry easier to inspect.

```powershell
python tests/smoke/launch.py refraction --config dbg --refraction-case torus
```

Generate all eighty-eight actual framebuffer captures and a portable offline gallery:

```powershell
python tests/smoke/refraction_gallery_smoke.py `
  --executable __exec/windows/arm64/full/dbg/refraction_smoke.exe `
  --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg `
  --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe `
  --output-directory __artifacts/refraction-gallery `
  --require-hardware
```

Use the matching executable and runtime paths for another architecture or build
configuration. `--cases nested,torus,prism` limits the scenes;
`--variants geometry,automatic,screen,disabled` selects the captures. Both options
accept comma-separated values. Each child run captures after sixteen graphics frames by
default and has a sixty-second capture timeout. `--application-arg=--gpudbg` enables
GPU validation. The runner clears inherited case, geometry, capture, and freeze
settings so a previous interactive launch does not alter its cases.

Open `gallery.html` from the output directory to switch cases, compare variants
side by side, zoom the scene, or inspect original pixels. The self-contained HTML
embeds losslessly encoded PNGs with exactly the framebuffer's RGB values. Original
BMP readbacks and individual PNG copies remain beside it, and `gallery_manifest.json`
records the cases, capture settings, application arguments, and descriptive
differences from the disabled baseline. The geometry preview is excluded from
those differences because its materials and backdrop intentionally differ.

The gallery checks clean logs, successful route dispatch, orderly shutdown,
nontrivial frames, and visible differences when the disabled baseline is selected.
It does not assert physical correctness for complex media. The current renderer
retains one primary refractor per pixel and has bounded internal reflection and
transparent continuation. Once the first volume has a valid exit, additional
glass uses a background approximation from that exit, preserving the primary
exit direction, measured thickness, absorption, and Fresnel without tracing extra
rays. It does not trace the remaining chain of volumes. Invalid first exits,
intersections, and nested media still use a conservative fallback from the entry.
Each case includes its relevant limitation. `--require-hardware` confirms that a
hardware resolve was dispatched; it does not imply every pixel stayed on that
path. The original single-sphere smoke remains the stricter regression test for
stripe displacement and foreground transparency preservation.

The default `RendererComponent::opticalVolumeCoincidence` policy is `Independent`,
which preserves authored surfaces. `IdenticalMaterial` opts into grouping exact
static duplicates with the same mesh, material, effective surface inputs, and
transform. `SharedGroup` additionally requires a nonempty `opticalVolumeGroup` and
permits differing mutable surface inputs. Both merge modes assert that geometry and surface hooks are independent of dense
instance identity; `SharedGroup` also asserts that the
differing inputs preserve the same boundary. The material asset, mesh, and exact
transform must still match. Higher `opticalVolumePriority` wins, with the lowest
full entity identity breaking equal-priority ties. Runtime/deformed meshes and CSG
are excluded.
Grouping also prevents duplicate tint when camera refraction is disabled; the
nonrefractive geometry preview continues to show every authored entity.

The strict duplicate regression uses radius-1.1 cool and warm single references,
with both zero coverage and coverage 0.3. It compares opted-in identical duplicates,
explicitly grouped conflicting tints, reversed creation order, and swapped
priorities with the selected single reference. A translated near-overlap and
independent coincident surfaces must retain visible separate contributions. The
existing radius-1.15 `single` gallery case is not used as their reference.

```powershell
python tests/smoke/refraction_duplicate_smoke.py `
  --executable __exec/windows/arm64/full/dbg/refraction_smoke.exe `
  --working-directory __cmake/build/windows-clang-arm64/Testing/smoke_runtime/dbg `
  --logserver-executable __exec/windows/arm64/full/dbg/logserver.exe `
  --output-directory __artifacts/refraction-duplicates `
  --require-hardware --application-arg=--gpudbg
```

The default run captures thirty-three framesets: fourteen cases on each of the
automatic and screen-space routes, plus five refraction-disabled controls.
`--variants automatic` or `--variants screen` limits the optical route. Every
capture uses sixteen graphics presentation frames, completed GPU readback, clean
logs, and normal shutdown. Duplicate
matches permit at most two levels of channel rounding and a mean absolute RGB
error of 0.02/255 across the full frame. Distinct controls must change at least
sixty-four pixels by more than two channel levels; separate cool/warm reference
checks prevent a priority test from passing with indistinguishable materials.
`duplicate_comparisons.json` records successful comparisons, while `gallery.html`
and raw BMP/PNG captures are written before image comparison so a failed oracle
can be inspected. The corresponding CTest is
`nwb_refraction_duplicate_capture_smoke`; CPU-only oracle tests are registered as
`nwb_refraction_duplicate_analysis_unit`.

The caustic-sphere fixture also explicitly enables camera refraction while keeping
its original glass material, light, receiver, and photon caustics. Launch it with:

```powershell
python tests/smoke/launch.py caustic-sphere --config dbg
```

`NWB_REFRACTION_SMOKE_ENABLED=0` disables only camera refraction for a comparison;
the glass continues casting its refractive shadow and photon caustic. Set
`NWB_REFRACTION_SMOKE_HARDWARE=0` to compare screen-space camera refraction while
leaving the caustic producer on the device's natural route. Both preferences
default to enabled when the environment variables are absent.

`nwb_caustic_sphere_capture_smoke` reads back the application framebuffer after
360 graphics presentation frames, once GPU readback has completed, with a fixed
1/60-second simulation delta. It requires both
a refraction resolve and a caustic producer dispatch, clean runtime logs, a
nonempty image, and orderly shutdown. Its output is
`Testing/smoke/<config>/caustic_sphere_capture.bmp`. The existing renderer-baseline
freeze remains available for interactive/reference captures; an active framebuffer
observer takes precedence so a stale freeze setting cannot stop its readback.
