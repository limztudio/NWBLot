# Software ray tracing qualification

Loader-based applications accept `--disable-hardware-ray-tracing`. This selects
`Core::HardwareRayTracingPolicy::Disabled` before graphics instance/device
creation. Normal startup keeps automatic hardware ray tracing selection.

The selected backend applies the disabled hardware ray tracing policy before
device creation. The setting cannot change after instance creation; use a new
process to compare automatic and disabled policies.

The scene tests require selected policy and actual software traversal evidence:

```text
Loader: hardware ray tracing disabled before device creation
RendererSystem: dispatched software shadow traversal
```

The renderer record follows actual software traversal recording. Feature-specific
registrations also require software caustic, surfel tracing or screen-space
optical evidence. These checks exercise the selected software route on a
hardware ray tracing capable adapter.

## Expected fallback behavior

| Effect | No-RT implementation |
| --- | --- |
| Opaque and transparent shadows | Indexed light-space capture and compute resolve, with GPU triangle-BVH traversal for unresolved receivers |
| Caustics | Software BVH photon producer and shared temporal/filtering passes |
| Surfel GI | Software BVH trace and the shared surfel cache/resolve |
| Camera reflection | Screen-space tracing and existing miss handling |
| Camera refraction | Screen-space resolve |
| Transparency | AVBOIT remains enabled |

Screen-space reflection/refraction cannot recover arbitrary off-screen geometry.
The policy preserves the existing fallback behavior and does not promise image
identity with hardware RT or hardware-path frame rates.

## Running the scene matrix

Build these targets in the chosen configuration so their matching runtime assets
are cooked: `nwb_transparent_multi_smoke`, `nwb_transparent_csg_smoke`,
`nwb_caustic_sphere_smoke`, `nwb_skinned_caustic_smoke`,
`nwb_stress_test_smoke`, and `nwb_gi_test_smoke`.

From the repository root, select a matching platform/architecture configure preset and build without launching a scene:

```text
python -m launcher build nwb_transparent_multi_smoke nwb_transparent_csg_smoke nwb_caustic_sphere_smoke nwb_skinned_caustic_smoke nwb_stress_test_smoke nwb_gi_test_smoke --configure-preset <configure-preset> --build-dir <build-directory> --config opt -D NWB_BUILD_TESTS=ON
ctest --test-dir <build-directory> -C opt -L software_raytracing --output-on-failure
```

The seven scene tests cover overlapping transparent meshes, two clipped CSG poses,
converged sphere caustics with refraction, animated skinned glass, the full
20-body scene before/after an odd-sized resize, and the dedicated GI scene.
They require the Loader disabled policy and software shadow dispatch, reject
hardware traversal/producer markers, and request selected GPU validation
outside the Final configuration. Caustic
cases additionally require the software photon producer. Warnings, errors,
assertions and validation diagnostics fail the capture harness.

These explicit-policy tests do not skip merely because the physical adapter
supports RayQuery. The existing natural-capability software smoke remains
available for adapters that lack RayQuery without an override. Captures are
written under `<build-directory>/Testing/smoke/<configuration>`.

An eighth test, `nwb_software_raytracing_optical_smoke`, compares four converged
sphere captures with reflection, refraction and caustics independently disabled.
A fixed striped opaque backdrop supplies screen-space depth and visible
refractive displacement in all four captures. The test requires each optical
contribution to be visible and the caustic gain to concentrate on the receiver. It explicitly excludes the hardware-only exterior
ray-miss radiometric oracle: projected screen-space hits do not establish the
same off-screen transport. Its report records that distinction.

The scene captures establish startup, traversal-route and rendered-output
coverage. The ECS graphics unit tests retain neutral empty-input, geometry
layout, identity, shadow quality and history edge cases. Native metadata-only
fixtures and their resource-dependent cases have been removed. A visually
nonempty capture alone is not an exact optical equivalence test.

## CSG indirect-light regression

Build `nwb_gi_test_smoke` through the launcher, then run:

```text
ctest --test-dir <build-directory> -C opt -L csg_gi --output-on-failure
```

The natural and disabled hardware ray tracing policies use the same static scene
and image oracle after normal backend selection. Each compares ordinary carved
mesh geometry, equivalent CSG with two overlapping box cutters, and an uncut
control. One region measures blue indirect light passing through an opening;
another measures green light bounced from a generated wall into the removed
cavity. An unobstructed panel must receive GI in every arm, and the ordinary
reference must differ from the uncut control before CSG equivalence is admitted.
Projected grids also require at least 95% coverage of the through opening and
back wall in both the ordinary reference and CSG images, with at most 5% in the
uncut image. These gates detect partial openings and internal overlap walls that
a small regional GI average can miss.

A third assembly places a closed box and an open quad in one mesh. The same
cutter removes a stripe from both. Its reference uses matching carved geometry
and retained open side quads; an uncut green stripe supplies the false-positive
control. The oracle checks passage coverage and GI, red floor light from retained
open triangles, visible retained-surface brightness, and RGB agreement that rejects
light from removed green triangles.

Captures wait for 360 successful presentations and retain the three images,
runtime logs, and regional color metrics under
`Testing/smoke/<configuration>/csg_gi_<route>`. The harness rejects runtime and
validation warnings through the existing capture workflow. Natural policy
selection qualifies whichever traversal the actual adapter selects. The same
scene also runs with frame-lagged async lighting enabled and requires accepted
bootstrap and active-history submissions before applying the image gates.
Adapters without a dedicated compute queue skip this async qualification through
the existing capability diagnostic. Lagged artifacts use the _lagged suffix.

GI uses current CSG geometry independently of retained light-space shadow maps.
Both traversal implementations subtract the cutter union from closed-component
spans and shade the selected surviving triangle or generated wall with the receiver
material. Cooked membership bits identify closed components in the final ray-triangle
order. Zero-volume components remain surfaces, with volume measured relative to a
stable component origin. Open components in the same mesh remain clipped surface
hits and do not generate walls. Light-space shadows consume the same membership
without interpreting open events as unmatched volume exits. Nonzero oriented
winding preserves reversed closed shells and nested cavity cancellation. Shared
built-in shape and bounded cutter support follow the current CSG snapshot contract;
unsupported
or over-budget geometry reports a diagnostic and uses conservative occlusion.

## Animated scene bounds

Runtime software scenes refit the scene BVH from the current GPU mesh roots
after the per-mesh build/refit work. The CPU topology and instance order remain
frozen for that frame. Static-only scenes retain their existing reuse path.

The skinned scene smoke exercises the production shader through the selected
disabled hardware ray tracing route.

Caustic photon emission-target domains remain a separate approximation based
on CPU geometry bounds; these smoke scenes do not establish coverage for every
possible skeletal deformation.
