# Phase 6 Colored/Transmittance Shadows — Implementation Checklist (live, 2026-06-24)

DECISIONS (user): (1) FULL importance-ranked shadow-slot allocator now — K=8 RGBA16F Texture2DArray slots; rank lights by importance, top-K get a slot, tail unshadowed. (2) Authored `transmittance` float3 MATERIAL SCHEMA field; cook defaults it when unset. (3) Interpretation `lerp(white, tint, coverage)` for derivation/defaults.

SHAPE: visibility R32_UINT bitmask -> K-slot RGBA16F Texture2DArray + per-frame lightIndex->slot table (slot stored in SceneLightGpuData.params.z; -1 = no slot). Shared bindless instance->material table `NwbRtInstanceMaterial{ float3 transmittance; uint flags }` (16B) indexed by instanceID (HW TLAS instanceID == SW scene-BVH leaf order -> A/B parity). HW: float3 payload + new shadow_ahit.slang (transparent: tint+IgnoreHit; opaque: zero+AcceptHitAndEndSearch) on NON-opaque shadow geometry + NoDuplicateAnyHitInvocation, drop SKIP_CLOSEST_HIT/ACCEPT_FIRST_HIT, maxPayload=16, miss no-op. SW: bool->float3 product (transparent multiply+continue, opaque zero+terminate, overflow->0). Consumer multiplies per-light float3.

KEY FINDING: transparent occluders ALREADY in both accel structures (builders filter only on visible+ready, never material) — work is per-hit handling+flags, not gathering.

GOTCHAS:
- `s_FramebufferSubresources = TextureSubresourceSet(0,1,0,1)` = slice 0 ONLY (renderer_constants_private.h:40). Need `s_ShadowVisibilitySubresources = TextureSubresourceSet(0,1,0,NWB_SCENE_SHADOW_SLOT_COUNT)` for the K-layer clear + UAV/SRV binds.
- INCLUDE-ORDER: `nwbSceneApplyLighting` (scene/lighting.slangi) needs the visibility accessor, but it is declared LATER in lighting_framework.slangi -> MOVE the g_NwbShadowVisibility SRV + `nwbBxdfLightTransmittance` INTO scene/lighting.slangi (configurable NWB_SCENE_SHADOW_VISIBILITY_SET/BINDING macros, like the light list), declared before nwbSceneApplyLighting. Framework just sets the macros to deferred set / slot 7.
- BXDF signature flips `uint shadowMask` -> `int2 pixel`. Touches: cook generator (cook.cpp EmitDeferredBxdfDispatchModuleImpl ~1930 + ~1941), lighting_ps.slang:28-30, toon.bxdf:10/24, lambert.bxdf:10/17, smoke_lambert.bxdf, nwbSceneApplyLighting (lighting.slangi:77/97-98).

INC-1 (representation + importance allocator; per-light value BINARY-as-float3 for now, multi-light correct; colored comes INC-2..4). VERIFY: csg/testbed capture == before.
C++:
- scene/binding_slots.h: add `NWB_SCENE_SHADOW_SLOT_COUNT 8u` + `NWB_SCENE_SHADOW_VISIBILITY_DEFAULT_SET/BINDING`; remove `NWB_SCENE_SHADOW_VISIBILITY_LIGHT_COUNT` (32) + its comment.
- renderer_constants_private.h: add `s_ShadowVisibilitySubresources` (needs scene/binding_slots.h include for K).
- renderer_push_constants_private.h: SceneLightGpuData.params comment -> z = shadow slot (-1 none).
- renderer_scene_private.h ResolveSceneLights: after packing, compute importance (luminance*intensity; directional boosted; point/spot * coverage proxy range/distToCamera) -> assign top-K params.z = slot, rest -1. (Resolve camera via ResolveSceneCameraView.)
- raytracing_system.cpp createShadowVisibilityTarget(:584): format -> RGBA16_FLOAT + setArraySize(K) + setDimension(Texture2DArray).
- raytracing_system.cpp clearShadowVisibility(:634-636): clearTextureFloat white(1,1,1,1) + s_ShadowVisibilitySubresources (setTextureState too).
- raytracing_system.cpp ensureShadowBindingSet(:899-905) + ensureSwShadowBindingSet(:1026-1032): UAV -> Texture2DArray + s_ShadowVisibilitySubresources.
- deferred_targets.cpp(:419-425): SRV -> Texture2DArray + s_ShadowVisibilitySubresources.
SHADERS:
- occlusion.slangi: UAV RWTexture2D<uint> -> RWTexture2DArray<float4>.
- shadow_raygen.slang: bg -> white to all K slots; loop lights (cap NWB_SCENE_MAX_LIGHTS), slot=int(light.params.z), if slot>=0 trace + write float4(v,v,v,1) to layer slot.
- shadow_sw_traversal_cs.slang: UAV -> RWTexture2DArray<float4>; same slot writes; cap NWB_SCENE_MAX_LIGHTS.
- scene/lighting.slangi: add NWB_SCENE_SHADOW_VISIBILITY_SET/BINDING macros + g_NwbShadowVisibility (Texture2DArray<float4>) + `float3 nwbBxdfLightTransmittance(int2 pixel, uint lightIndex)` (reads g_NwbSceneLights[lightIndex].params.z slot; Load layer or white) BEFORE nwbSceneApplyLighting; nwbSceneApplyLighting(..., int2 pixel) componentwise multiply.
- lighting_framework.slangi: drop its g_NwbShadowVisibility + nwbBxdfLoadShadowMask + nwbBxdfLightVisibility; set NWB_SCENE_SHADOW_VISIBILITY_* to deferred set/slot 7 before including scene/lighting.slangi (via lighting.slangi).
- lighting_ps.slang: pass int2(input.position.xy) to nwbDeferredDispatchBxdf (no mask load).
- toon.bxdf/lambert.bxdf/smoke_lambert.bxdf: sig (surface, int2 pixel); toon nwbBxdfLightTransmittance(pixel,i) componentwise; lambert passes pixel to nwbSceneApplyLighting.
- cook.cpp EmitDeferredBxdfDispatchModuleImpl: emit `nwbDeferredDispatchBxdf(uint shadingModel, NwbBxdfSurface surface, int2 pixel)` + `return MODEL_id(surface, pixel);`.

INC-2: authored transmittance schema field + cook default lerp; shared NwbRtInstanceMaterial table lockstep in buildSceneTlas + buildSceneSwBvh.
INC-3: SW colored product. INC-4: HW any-hit colored. INC-5: (folded into INC-1 consumer). INC-6: verify A/B colored capture + review.

## INC-2/3/4 DETAILED SPEC (2026-06-24)

INC-1 is DONE+VERIFIED (representation RGBA16F Texture2DArray[K=8] + importance slot allocator in params.z + per-slot producers + per-light float3 consumer via nwbBxdfLightTransmittance(pixel,lightIndex) in scene/lighting.slangi). Values still binary; INC-2..4 add the color.

SHARED RECORD (INC-2): struct NwbRtInstanceMaterial { float3 transmittance; uint flags; }  (16 bytes, std430/Float4-aligned). flags bit0 = isTransparent (1=transparent occluder -> multiply tint + continue; 0=opaque -> zero transmittance + terminate). C++ mirror NwbRtInstanceMaterialGpu in renderer_csg_types.h-style or a renderer rt types header with static_assert(sizeof==16). ONE table StructuredBuffer<NwbRtInstanceMaterial> indexed by instanceID; built per frame by whichever backend runs.

INSTANCEID INVARIANT: buildSceneTlas sets instanceDesc.setInstanceID(instances.size()) (~raytracing_system.cpp:317); buildSceneSwBvh leaf = instance array index (push order). Append exactly ONE material record at the SAME point each builder pushes an instance, so array slot == HW InstanceID() == SW leaf index == same entity. Factor a shared helper ResolveInstanceShadowMaterial(RendererComponent) -> NwbRtInstanceMaterialGpu.

AUTHORED transmittance field: add `asset.transmittance` (float3) to the material .nwb schema (impl/assets_material metadata/parser + Material asset + cook MaterialCookEntry). Material::transmittance() accessor. COOK DEFAULT when unset: transmittance = lerp(float3(1), base_color.rgb, base_color.a) (coverage = alpha; clear glass alpha->white, opaque-ish alpha->tint). OPAQUE material (Material::transparent()==false): transmittance=(0,0,0), flags bit0=0.

BINDS: HW table at next free slot in shadow/binding_slots.h (after VISIBILITY_OUTPUT); SW at next free in sw_binding_slots.h. Add layout item (ensureShadowPipeline / ensureSwShadowPipeline layouts), set item (ensureShadowBindingSet / ensureSwShadowBindingSet), shader decl. Create/grow the table buffer like the existing instance buffer (double-never-shrink), upload per frame.

INC-3 (SW): shadow_sw_traversal_cs.slang -- nwbSwShadowOccluded/InstanceOccluded/LightVisible: bool -> accumulate inout float3 transmittance. transmittance=white; on a triangle hit read g_NwbSwShadowInstanceMaterial[instanceIndex]: if transparent -> transmittance *= mat.transmittance, CONTINUE; if opaque -> transmittance=0, return. Stack-overflow conservative path -> transmittance=0. Early-out when max(transmittance)<1e-3. nwbSwShadowLightVisible returns float3; main writes float4(transmittance,1) to the slot layer (slot logic already in place from INC-1; just swap the binary value for the float3).

INC-4 (HW): occlusion.slangi NwbShadowOcclusionPayload { float3 transmittance; } (was float visibility). raygen seeds payload.transmittance=float3(1) before TraceRay, writes payload.transmittance to slot layer. setMaxPayloadSize 4->16 (raytracing_system.cpp ~819). New impl/assets/graphics/shadow/shadow_ahit.slang [shader("anyhit")]: read InstanceID(), g_NwbShadowInstanceMaterial[InstanceID()]; transparent -> payload.transmittance *= mat.transmittance; IgnoreHit(); opaque -> payload.transmittance=float3(0); AcceptHitAndEndSearch(). Register it (load like shadow_miss/chit ~806-816) + add to hit group (RayTracingPipelineHitGroupDesc setAnyHitShader; ~831). Drop RAY_FLAG_SKIP_CLOSEST_HIT_SHADER|ACCEPT_FIRST_HIT_AND_END_SEARCH (shadow_raygen.slang) -> RAY_FLAG_NONE. buildMeshBlas (~719): build shadow geometry WITHOUT RayTracingGeometryFlags::Opaque + add NoDuplicateAnyHitInvocation so the any-hit fires + opaque zeroing runs. shadow_miss.slang -> no-op (leave seeded white). shadow_chit unused (keep stub or drop from hit group).

VERIFY (agent): build-to-green BOTH `cmake --build --preset windows-clang-engine-dbg --target nwb_csg_visible_smoke` (engine+smoke+smoke_lambert) AND `cmake --build --preset windows-clang-dbg` (testbed: lambert/toon project bxdf). Iterate edit->build->fix until cook + compile clean on both. The final HW/SW A/B colored-shadow capture + adversarial review is done by the orchestrator, not the agent.

## SHADER-RETURNED PER-HIT TRANSMITTANCE — DESIGN (2026-06-24, supersedes INC-2 constant)
USER DIRECTIVE: material declares ONLY opaque/transparent (asset.transparent boolean); the SURFACE shader RETURNS transmittance (float3, like the surface color); shadow trace evaluates it PER-HIT (spatially-varying), dispatched by shading-model id.
4 PILLARS: (1) NwbMeshSurface += float3 transmittance; nwbMaterialSurface returns it; nwbMaterialSurfaceAt(NwbMeshSurfaceInputs) attribute-parameterized so the trace can call the hook (statics become accessors). transparent materials compute it from .bind constants (nwbSmokeMaterialTransmittance). (2) Cook emits shadow/generated/transmittance_dispatch.slangi (mirror EmitDeferredBxdfDispatchModule): float3 nwbShadowDispatchTransmittance(uint model, NwbShadowHit hit); DEFAULT float3(1) NOT magenta. NwbShadowHit{instanceId,shadingModel,uv0,objectNormal,worldPosition,rayDirection}. (3) Both traces: PrimitiveIndex+barycentrics → interpolate per-vertex attrs from NEW per-mesh bindless ATTRIBUTE buffer (position-stream-indexed; AttribGpu{Half4U normal; Float2U uv0}=16B; BuildMeshletVertexAttributes lockstep w/ BuildMeshletTriangleIndices) → NwbShadowHit → dispatch. SW: nwbSwShadowRayTriangle returns barycentrics (u,v already computed). (4) INC-2 constant FULLY reverted; NwbRtInstanceMaterial -> {uint shadingModelId; uint flags; uint meshSlot; uint pad;}=16B; bump s_MaterialVersion.
KEY RISK: HW RT bindless attribute ARRAY (count>1 SRV) under AllRayTracing — engine descriptor-array may need uncooked-DXC/ResourceDescriptorHeap ([[rt-shadow-feature]]); UNKNOWN -> U5 spike GATES the HW path. SW already proves arrays under Compute.
TEXTURE sampling in trace = OUT of v1 (vertex-attr inputs only); needs a bindless material-texture+sampler table keyed by shadingModelId — deferred follow-up.
DECISIONS (recs): D1 dedicated shadowTransmittanceModelId (dedup over .surface); D2 generate per-material AVBOIT accumulate PS (fixes hardcoded-lambert + makes cast color == shaded color, U7); D3 uv0+normal 16B; D4 textures OUT v1; D5 SW-first then HW(spike).
UNITS: U0 INC-2 revert (table->{model,flags,meshSlot,pad}; trace temp *=float3(1)) -> U1 surface contract (transmittance + nwbMaterialSurfaceAt) -> U2 flat per-vertex attr buffer (static+skinned bind-pose) -> U3 cook transmittance dispatch (+shadow_surface.slangi, volume_prepare wiring) -> U4 SW per-hit dispatch -> U5 RT bindless-array spike -> U6 HW any-hit per-hit dispatch -> U7 AVBOIT per-material accumulate PS -> U8 cleanup+audit (nwb_assets_graphics_tests + HW/SW A/B).
SEAM caveat: position-indexed attrs are last-writer at UV seams (negligible for smooth transmittance; per-corner indices if exact needed). SKINNED: uv0 bind-pose OK; normal bind-pose approximate (re-skin deferred).

## USER DECISIONS (2026-06-24, MAXIMAL scope chosen)
D1 dispatch key: dedicated shadowTransmittanceModelId (dedup over .surface). [my default, not contested]
D2 AVBOIT: YES generate per-material accumulate PS (fixes hardcoded lambert; cast color == shaded color). [U7]
D3 attribute record: uv0+normal 16B. [my default]
D4 TEXTURES: IN SCOPE for v1 -> bindless material-texture+sampler table keyed by shadingModelId, bound into BOTH shadow trace passes. ALSO need a NEW textured transparent test material (texture asset + surface hook sampling it -> transmittance) to actually demonstrate per-texel colored shadows (smoke materials are flat-color). 
D5 ROLLOUT: BOTH HW + SW together (not SW-first).
FEASIBILITY RESOLUTION: bounded descriptor arrays (the SW pattern, [64]) are pipeline-agnostic in Vulkan; the RT pipeline + the texture table use the SAME bounded-array mechanism (NOT ResourceDescriptorHeap/unbounded, which is the memory caveat). Confirm empirically at the HW build.
EXTRA UNIT: U9 material-texture bindless table (textures+samplers keyed by shadingModelId) bound to both traces + a textured transparent test material for the demo. Interleaves with U3/U4/U6 (the dispatch fn samples it).

## MATERIAL-CONSTANTS CONTEXT IN TRACE (crux for U3/U4/U6, found 2026-06-24)
The .surface hook reads material constants, so to evaluate it in the trace the trace must replicate the material-constants binding context:
- base_color: nwbMaterialBind* -> nwbMeshMaterialConstantByteOffset() + g_NwbMaterialTypedWords (StructuredBuffer<uint,Std430> @ NWB_MESH_BINDING_MATERIAL_TYPED, NWB_MESH_SET; material_typed_bindings.slangi:28).
- color_tint (mutable): nwbMeshLoadInstance() -> g_NwbMeshInstances[nwbMeshInstanceIndex()].materialMutableByteOffset (g_NwbMeshInstances @ NWB_MESH_BINDING_INSTANCE; material_typed_bindings.slangi:29).
- nwbMeshMaterialConstantByteOffset()/nwbMeshInstanceIndex()/nwbMeshShadingModelId() are defined in gbuffer_io.slangi:60-100 (3 gates: NWB_CSG_ENABLED / NWB_GRAPHICS_MESH_SHADER_RUNTIME_SLANGI / else=push-constant) + runtime.slangi:132.
TRACE REQUIREMENTS:
- bind g_NwbMaterialTypedWords + g_NwbMeshInstances (single global buffers) to BOTH trace passes (HW shadow set + SW shadow set), new slots.
- NwbRtInstanceMaterial grows to carry per-hit: shadowTransmittanceModelId, flags, meshSlot, materialConstantByteOffset, meshInstanceIndex (-> 8x u32 / 32B; keep std430 16-aligned). NwbShadowHit carries the same. buildSceneTlas/SwBvh populate them (the renderable gather already has the instance + its material constant offset + the mesh-instance index used for g_NwbMeshInstances -- confirm the index mapping; RT instance order may differ from g_NwbMeshInstances order, so store the actual mesh-instance index, not the RT index).
- ADD a 4th gate to gbuffer_io.slangi + runtime.slangi: `#if defined(NWB_MESH_MATERIAL_CONTEXT_PROVIDED)` -> the includer (the shadow trace context header) supplies nwbMeshMaterialConstantByteOffset()/nwbMeshInstanceIndex()/nwbMeshShadingModelId() reading per-invocation globals set by the dispatch from NwbShadowHit; the existing push-constant/CSG/mesh-shader branches stay for the rasterizer.
- the cook dispatch per-id wrapper: set the trace context globals from hit -> include .bind + .surface -> build NwbMeshSurfaceInputs from hit -> return nwbMaterialSurfaceAt(inputs).transmittance.
NOTE: for a flat material (smoke), transmittance = lerp(white, base_color*color_tint, coverage) is per-instance UNIFORM (no spatial variation) until TEXTURES (U9) sample by uv0. Per-hit eval gives the uniform result for flat materials (== the old constant visually) but is shader-computed; textures add the spatial variation. NwbRtInstanceMaterial is now 32B -> bump material/struct asserts accordingly.
