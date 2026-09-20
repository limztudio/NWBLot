# Software ray tracing qualification

Loader-based applications accept `--disable-hardware-ray-tracing`. This selects
`Core::HardwareRayTracingPolicy::Disabled` before graphics instance/device
creation. Normal startup keeps automatic hardware ray tracing selection.

The Vulkan backend omits optional RT feature extensions, rejects conflicting
required RT extensions, and creates the logical device without RayQuery,
ray-tracing pipelines, acceleration structures, or an AS descriptor layout.
Generic pipeline-library and deferred-host-operation extension requests remain
available to independent callers. The setting cannot change after instance
creation; use a new process to compare automatic and disabled policies.

Successful disabled startup reports the actual enabled capabilities and heap:

```text
Loader: hardware ray tracing disabled before device creation
Vulkan: hardware ray tracing policy=disabled; RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0
```

The second marker is emitted only after all five values have been checked.
This exercises the real no-RT logical-device route on an RT-capable adapter.
It does not substitute for qualification on other physical GPUs and drivers.
The renderer's other minimum requirements, including descriptor buffers, still
apply.

## Expected fallback behavior

| Effect | No-RT implementation |
| --- | --- |
| Opaque and transparent shadows | GPU triangle BVHs and compute traversal |
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

```text
ctest --test-dir <build-directory> -C opt -L software_raytracing --output-on-failure
```

The seven scene tests cover overlapping transparent meshes, two clipped CSG poses,
converged sphere caustics with refraction, animated skinned glass, the full
20-body scene before/after an odd-sized resize, and the dedicated GI scene.
They require the disabled device proof and software shadow dispatch, reject
hardware traversal/producer markers, and run with Vulkan validation. Caustic
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
coverage. Native kernel and descriptor/graph tests provide more focused
numeric and resource-state coverage; a visually nonempty capture alone is not
an exact optical equivalence test.
