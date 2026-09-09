# Shared ray-hit lighting

`surface_hit.slangi` owns nearest geometric hit reconstruction. It completes a force-opaque ray query, reconstructs triangle positions, interpolated normals and UVs, and evaluates the cook-generated material surface hook. It takes the instance-material table heap slot explicitly. `surface_lighting.slangi` evaluates the hit with the material's actual BXDF ID and an explicit direction from the hit toward the observer.

The caller must establish its frozen material context before including the helpers:

1. Declare the heap alias for `NwbRayTraceMaterialContextSlots` and select its buffer through the pass's own immutable parameters.
2. Define `nwbMeshFrameMaterialTypedHeapSlot()`, `nwbMeshFrameInstanceHeapSlot()`, and `nwbMeshFrameViewHeapSlot()` for that same frame context, then include `bindless/bindless_heap_mesh.slangi`.
3. Define `NWB_SCENE_SHADING_HEAP_SLOT` and `NWB_SCENE_LIGHT_LIST_HEAP_SLOT` from retained scene-light resources.
4. Include `raytrace/surface_lighting.slangi` before any direct inclusion of `scene/lighting.slangi`. It includes `surface_hit.slangi` itself. The shader cook needs both generated include roots: `shadow/generated/transmittance_dispatch.slangi` and `deferred/generated/bxdf_dispatch.slangi`.
5. Trace with `nwbRayTraceClosestSurfaceHit(instanceMaterialHeapSlot, origin, direction, minDistance, maxDistance)` and evaluate an accepted hit with `nwbRayTraceSurfaceRadiance(hit, -direction)`.

The CPU ray-tracing domain supplies `RayTracingSceneGraphResources` after `prepareSceneQueryResources()`. Its handles and descriptor selectors are immutable during graph recording. Graph declarations must also retain and declare the existing frozen trace-geometry and material sampled-texture bundles, plus the view and light buffers selected by the caller. Snapshot validity permits scheduling; a hardware dispatch additionally requires the shared scene-preparation task to have succeeded. Effects own their fallback policy.

Authored BXDFs consume `NwbBxdfSurface.viewVector`; they must not reconstruct that direction from the primary camera. They return linear radiance when `nwbBxdfRequiresLinearOutput()` is true, which all current deferred, AVBOIT and ray-hit harnesses require. The ray-hit harness dispatches the actual material BXDF rather than substituting a fixed engine BRDF. It disables screen-space shadow and caustic sampling and passes a negative pixel coordinate, since those primary-camera textures do not describe a general secondary hit. Indirect light uses the existing hemispheric fallback. Secondary shadow, GI and recursive reflection rays are outside this harness.

Reconstruction retains the existing RT attribute contract: normals and UVs are available; material surface inputs receive neutral color and tangent defaults. Materials depending on richer vertex attributes, raster derivatives, or raster-only instance semantics require a richer ray surface contract before they can match raster shading exactly. Force-opaque traversal returns geometry regardless of coverage; the calling effect decides how to handle transparent or refractive hits.

# Linear transport and display migration

Deferred lighting, AVBOIT, refraction and reflection exchange nonnegative linear scene radiance in floating-point targets. The final presentation shader applies exposure and then the selected display transform. `PresentationSettings` defaults to SDR Reinhard with exposure `1` and shoulder `0.65`; `LinearClamp` supports projects that want untonemapped SDR output. HDR10 keeps the existing reference-white, mastering-peak and PQ mapping, with the same exposure input and without an additional SDR curve.

Moving the common Lambert curve from individual materials to final presentation preserves that curve for an isolated opaque Lambert surface. It intentionally changes compositions because tone mapping does not commute with transparency or reflection blending. Toon and unlit materials also share the selected final display policy. Projects that previously returned display-mapped BXDF colors must migrate those outputs to linear radiance; the renderer does not invert a material tone map or evaluate a BXDF twice.
