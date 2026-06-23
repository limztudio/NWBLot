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
