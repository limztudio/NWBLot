// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1

#ifndef NWB_REFRACTION_DIAGNOSTICS
#define NWB_REFRACTION_DIAGNOSTICS 0
#endif

#include "resolve_common.slangi"
#include "../raytrace/material_context_slots.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma warning(push)
#pragma warning(disable: 39001)
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]] ConstantBuffer<NwbRayTraceMaterialContextSlots, Std140DataLayout> g_NwbHeapRefractionMaterialContexts[];
#pragma warning(pop)
#define g_NwbRefractionMaterialContext g_NwbHeapRefractionMaterialContexts[g_NwbRefractionPush.materialContextSlotsHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


uint nwbMeshFrameMaterialTypedHeapSlot(){ return g_NwbRefractionMaterialContext.sceneSlots.w; }
uint nwbMeshFrameInstanceHeapSlot(){ return g_NwbRefractionMaterialContext.materialSlots.x; }
uint nwbMeshFrameViewHeapSlot(){ return g_NwbRefractionPush.viewHeapSlot; }
#include "../mesh/bindless_heap_mesh.slangi"

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbRefractionResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbRefractionResources.avboitSlots.w
#define NWB_SCENE_SHADOW_CAUSTIC_SAMPLING_DISABLED 1
#if NWB_RT_CSG_ENABLED
#define NWB_RT_CSG_CONTEXT_SLOT g_NwbRefractionMaterialContext.materialSlots.w
#define NWB_RT_CSG_MATERIAL_SLOT g_NwbRefractionMaterialContext.sceneSlots.z
#endif
#include "interface_hit.slangi"
#include "../scene/lighting.slangi"
#include "diagnostics.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A hardware hit reuses the screen cache only when its depth and world distance match the cached opaque surface.
static const float NWB_REFRACTION_CACHED_HIT_DEPTH_TOLERANCE = 0.0002;
static const float NWB_REFRACTION_CACHED_HIT_DISTANCE_MIN = 0.01;
static const float NWB_REFRACTION_CACHED_HIT_DISTANCE_SCALE = 0.002;
static const float NWB_REFRACTION_ASSOCIATION_DISTANCE_SCALE = 0.0002;
static const float NWB_REFRACTION_CERTIFIED_ASSOCIATION_MAX_TOLERANCE = 0.012;
static const uint NWB_REFRACTION_NON_OPAQUE_INSTANCE_MATERIAL_FLAGS = NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT | NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE;
static const uint NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS = NWB_REFRACTION_NON_OPAQUE_INSTANCE_MATERIAL_FLAGS | NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTION_ENTRY_CERTIFIED;
static const uint NWB_REFRACTION_MAX_INTERNAL_BOUNCES = 2u;
static const uint NWB_REFRACTION_MAX_BACKGROUND_LAYERS = 3u;
static const half NWB_REFRACTION_OPAQUE_COVERAGE_MIN = 0.999h;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbRefractionShadeSurfaceHit(const NwbRayTraceSurfaceHit hit, const float3 direction){
    nwbRefractionCount(NWB_REFRACTION_COUNTER_materialShadeCalls);
    float3 normal = hit.surface.normal;
    if(dot(normal, direction) > 0.0)
        normal = -normal;
    half3 radiance = half3(0.0h);
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex){
        float3 lightVector;
        half3 lightRadiance;
        nwbSceneResolveLight(g_NwbSceneLights[lightIndex], hit.position, lightVector, lightRadiance);
        radiance += lightRadiance * half(max(dot(normal, lightVector), 0.0));
    }
    // Offscreen hits have no camera lighting cache and use the bounded diffuse surface approximation shared with GI.
    return radiance * hit.surface.baseColor;
}

bool nwbRefractionCachedHit(const float3 position, const NwbRefractionEntry entry, const bool includeTransparency, out half3 radiance){
    radiance = half3(0.0h);
    uint2 pixel;
    float projectedDepth;
    if(!nwbRefractionProject(position, pixel, projectedDepth))
        return false;
    const float opaqueDepth = nwbHeapSampledImage2D(g_NwbRefractionResources.gbufferSlots.w).Load(int3(pixel, 0)).r;
    if(opaqueDepth >= 1.0 || abs(projectedDepth - opaqueDepth) > NWB_REFRACTION_CACHED_HIT_DEPTH_TOLERANCE)
        return false;
    const float3 opaqueWorld = nwbHeapSampledImage2D(g_NwbRefractionResources.gbufferSlots.z).Load(int3(pixel, 0)).xyz;
    const float cameraDistance = length(position - g_NwbRefractionView.cameraPosition.xyz);
    // Require both projected depth and world distance. A depth-only comparison becomes permissive in the far field and returns an opaque foreground object's lighting for a hidden RT hit.
    if(length(opaqueWorld - position) > max(NWB_REFRACTION_CACHED_HIT_DISTANCE_MIN, cameraDistance * NWB_REFRACTION_CACHED_HIT_DISTANCE_SCALE))
        return false;
    radiance = includeTransparency ? half3(nwbRefractionBackgroundForEntry(pixel, entry)) : half3(nwbRefractionOpaqueBackground(pixel));
    return true;
}

bool nwbRefractionTryAssociateCapturedEntry(const uint instanceMaterialHeapSlot, const NwbRefractionEntry entry, const float3 camera, const float entryDistance, const float associationTolerance, out uint primaryInstance, out uint primaryAttributeSlot){
    primaryInstance = 0u;
    primaryAttributeSlot = 0u;
#if NWB_RT_CSG_ENABLED
    return false;
#else
    const uint candidate = g_NwbRefractionPush.certifiedInstanceIndex;
    if(!entry.frontBoundary || candidate >= g_NwbRefractionMaterialContext.materialSlots.z || associationTolerance > NWB_REFRACTION_CERTIFIED_ASSOCIATION_MAX_TOLERANCE)
        return false;
    const float minDistance = max(entryDistance - associationTolerance, entry.bias);
    const float4 windowStartClip = mul(g_NwbRefractionView.worldToClip, float4(camera + entry.incident * minDistance, 1.0));
    if(windowStartClip.w <= NWB_REFRACTION_MIN_CLIP_W || windowStartClip.z < 0.0)
        return false;
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[candidate];
    if((material.flags & NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS) != NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS)
        return false;
    if(g_NwbMeshInstances[material.meshInstanceIndex].entityId != entry.primaryEntityId)
        return false;
    primaryInstance = candidate;
    primaryAttributeSlot = material.attributeSlot;
    return true;
#endif
}

bool nwbRefractionTryResolveHardware(const NwbRefractionEntry entry, out half4 outRadiance, out float3 outContinuationOrigin, out float3 outContinuationDirection, out half3 outContinuationAttenuation, out bool outVolumeExited){
    outRadiance = half4(0.0h, 0.0h, 0.0h, 1.0h);
    outContinuationOrigin = entry.position;
    outContinuationDirection = float3(0.0);
    outContinuationAttenuation = half3(0.0h);
    outVolumeExited = false;
    nwbRefractionCount(NWB_REFRACTION_COUNTER_hardwarePixels);
    const uint instanceMaterialHeapSlot = g_NwbRefractionMaterialContext.sceneSlots.z;
    const float3 camera = g_NwbRefractionView.cameraPosition.xyz;
    const float entryDistance = entry.cameraDistance;
    const float associationTolerance = max(entry.bias * NWB_REFRACTION_BIAS_TOLERANCE_SCALE, entryDistance * NWB_REFRACTION_ASSOCIATION_DISTANCE_SCALE);
    uint primaryInstance = 0u;
    uint primaryAttributeSlot = 0u;
    float3 internalOrigin = entry.position;
    if(!nwbRefractionTryAssociateCapturedEntry(instanceMaterialHeapSlot, entry, camera, entryDistance, associationTolerance, primaryInstance, primaryAttributeSlot)){
        nwbRefractionCount(NWB_REFRACTION_COUNTER_entryQueries);
        const NwbRayTraceSurfaceHit entryHit = nwbRefractionClosestEntryInterface(instanceMaterialHeapSlot, camera, entry.incident, max(entryDistance - associationTolerance, entry.bias), entryDistance + associationTolerance, primaryAttributeSlot);
        nwbRefractionCountQueryWork(entryHit);
#if NWB_RT_CSG_ENABLED
        if(entryHit.failure != 0u){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_entryFailures);
            return true;
        }
#endif
        if(!entryHit.hit || !nwbRefractionIorEligible(float(entryHit.surface.refractionIor))){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_entryAssociationMisses);
            return false;
        }
        if(g_NwbMeshInstances[entryHit.meshInstanceIndex].entityId != entry.primaryEntityId){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_entryWrongEntities);
            return false;
        }
        primaryInstance = entryHit.instance;
        internalOrigin = entryHit.position;
    }
    nwbRefractionCount(NWB_REFRACTION_COUNTER_associatedEntries);

    float3 internalDirection = refract(entry.incident, entry.normal, 1.0 / float(entry.ior));
    if(dot(internalDirection, internalDirection) < NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN){
        nwbRefractionCount(NWB_REFRACTION_COUNTER_invalidEntryDirections);
        return true;
    }
    float3 exitPosition = float3(0.0);
    float3 outgoingDirection = float3(0.0);
    // Exit cosine is a saturate-bounded (0, 1) factor feeding the bounded Fresnel attenuation below.
    half exitCosine = half(0.0h);
    float pathLength = 0.0;
    // Trace at most two internal reflections within the same primary instance, accumulating Beer-Lambert distance until transmission.
    for(uint internalBounce = 0u; internalBounce <= NWB_REFRACTION_MAX_INTERNAL_BOUNCES; ++internalBounce){
        nwbRefractionCount(NWB_REFRACTION_COUNTER_exitQueries);
        NwbRayTraceGeometryHit exitGeometry;
        float3 exitNormal;
        const bool exitValid = nwbRefractionClosestExitInterface(internalOrigin + internalDirection * entry.bias, internalDirection, entry.bias, NWB_REFRACTION_MAX_DISTANCE, primaryInstance, primaryAttributeSlot, exitGeometry, exitPosition, exitNormal);
        nwbRefractionCountQueryWork(exitGeometry);
#if NWB_RT_CSG_ENABLED
        if(exitGeometry.failure != 0u){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_exitFailures);
            return true;
        }
#endif
        // Stop when a different instance is hit inside the primary; open meshes use the sheet fallback.
        if(!exitValid){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_exitMisses);
            return false;
        }
        pathLength += length(exitPosition - internalOrigin);
        if(dot(exitNormal, internalDirection) < 0.0)
            exitNormal = -exitNormal;
        exitCosine = half(saturate(dot(internalDirection, exitNormal)));
        outgoingDirection = refract(internalDirection, -exitNormal, float(entry.ior));
        if(dot(outgoingDirection, outgoingDirection) >= NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN)
            break;
        nwbRefractionCount(NWB_REFRACTION_COUNTER_tirEvents);
        internalDirection = reflect(internalDirection, exitNormal);
        internalOrigin = exitPosition;
    }
    // Discard residual internally reflected energy at the bounce cap rather than leaking straight-through background.
    if(dot(outgoingDirection, outgoingDirection) < NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN){
        nwbRefractionCount(NWB_REFRACTION_COUNTER_tirCappedPixels);
        return true;
    }
    nwbRefractionCount(NWB_REFRACTION_COUNTER_volumeExits);
    const half3 attenuation = nwbRefractionAttenuation(entry.tint, pathLength, entry.ior, entry.cosine, exitCosine);

    float3 origin = exitPosition + outgoingDirection * entry.bias;
    half3 accumulated = half3(0.0h);
    // Layer transmittance and backdrop coverage are (0, 1) bounded factors, so half arithmetic is sufficient here.
    half transmittance = half(1.0h);
    // Normally one background query.
    // Up to three flat transparent hits may be evaluated before the bounded approximation.
    // Further glass or an incomplete continuation uses the exit-based approximation below without tracing a new volume.
    for(uint layer = 0u; layer < NWB_REFRACTION_MAX_BACKGROUND_LAYERS; ++layer){
        nwbRefractionCount(NWB_REFRACTION_COUNTER_backgroundQueries);
#if NWB_RT_CSG_ENABLED
        const NwbRayTraceGeometryHit backgroundGeometry = nwbRayTraceClosestGeometryHit(origin, outgoingDirection, entry.bias, NWB_REFRACTION_MAX_DISTANCE);
        nwbRefractionCountQueryWork(backgroundGeometry);
        if(!backgroundGeometry.hit){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_backgroundMisses);
            break;
        }
        if(backgroundGeometry.failure != 0u){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_backgroundFailures);
            return true;
        }
        const float3 backgroundPosition = origin + outgoingDirection * backgroundGeometry.distance;
        const NwbRtInstanceMaterial backgroundMaterial = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[backgroundGeometry.instance];
        if((backgroundMaterial.flags & NWB_REFRACTION_NON_OPAQUE_INSTANCE_MATERIAL_FLAGS) == 0u){
            half3 radiance;
            nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedHitAttempts);
            if(nwbRefractionCachedHit(backgroundPosition, entry, layer == 0u, radiance)){
                nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedOpaqueReturns);
                outRadiance = half4((accumulated + transmittance * half3(radiance)) * half3(attenuation), 1.0h);
                return true;
            }
            nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedHitMisses);
            nwbRefractionCount(NWB_REFRACTION_COUNTER_shadedOpaqueReturns);
        }
#else
        RayDesc backgroundRay;
        backgroundRay.Origin = origin;
        backgroundRay.Direction = outgoingDirection;
        backgroundRay.TMin = entry.bias;
        backgroundRay.TMax = NWB_REFRACTION_MAX_DISTANCE;
        RayQuery<RAY_FLAG_FORCE_OPAQUE> backgroundQuery;
        backgroundQuery.TraceRayInline(nwbHeapAccelStruct(), RAY_FLAG_FORCE_OPAQUE, NWB_RAY_TRACING_ALL_INSTANCE_MASK, backgroundRay);
        while(backgroundQuery.Proceed()){}
        nwbRefractionCount(NWB_REFRACTION_COUNTER_physicalQueries);
        nwbRefractionCount(NWB_REFRACTION_COUNTER_queryBudgetUnits);
        if(backgroundQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_backgroundMisses);
            break;
        }
        const uint backgroundInstance = backgroundQuery.CommittedInstanceID();
        const float backgroundDistance = backgroundQuery.CommittedRayT();
        const float3 backgroundPosition = origin + outgoingDirection * backgroundDistance;
        const NwbRtInstanceMaterial backgroundMaterial = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[backgroundInstance];
        if((backgroundMaterial.flags & NWB_REFRACTION_NON_OPAQUE_INSTANCE_MATERIAL_FLAGS) == 0u){
            half3 radiance;
            nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedHitAttempts);
            if(nwbRefractionCachedHit(backgroundPosition, entry, layer == 0u, radiance)){
                nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedOpaqueReturns);
                outRadiance = half4((accumulated + transmittance * half3(radiance)) * half3(attenuation), 1.0h);
                return true;
            }
            nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedHitMisses);
            nwbRefractionCount(NWB_REFRACTION_COUNTER_shadedOpaqueReturns);
        }
        NwbRayTraceGeometryHit backgroundGeometry = (NwbRayTraceGeometryHit)0;
        backgroundGeometry.hit = true;
        backgroundGeometry.instance = backgroundInstance;
        backgroundGeometry.primitive = backgroundQuery.CommittedPrimitiveIndex();
        backgroundGeometry.distance = backgroundDistance;
        backgroundGeometry.barycentrics = backgroundQuery.CommittedTriangleBarycentrics();
        backgroundGeometry.worldToObject = backgroundQuery.CommittedWorldToObject3x4();
#endif
        const NwbRayTraceSurfaceHit background = nwbRayTraceReconstructValidatedSurfaceHit(instanceMaterialHeapSlot, origin, outgoingDirection, backgroundGeometry);
        half coverage = half(1.0h);
        bool opaqueHit = true;
        if((backgroundMaterial.flags & NWB_REFRACTION_NON_OPAQUE_INSTANCE_MATERIAL_FLAGS) != 0u){
            if(nwbRefractionIorEligible(float(background.surface.refractionIor))){
                nwbRefractionCount(NWB_REFRACTION_COUNTER_secondaryGlassPixels);
                break;
            }
            coverage = half(saturate(float(background.surface.renderCoverage)));
            opaqueHit = coverage >= half(NWB_REFRACTION_OPAQUE_COVERAGE_MIN);
            if(opaqueHit){
                nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedHitAttempts);
                nwbRefractionCount(NWB_REFRACTION_COUNTER_cachedHitMisses);
                nwbRefractionCount(NWB_REFRACTION_COUNTER_shadedOpaqueReturns);
            }
            else{
                nwbRefractionCount(NWB_REFRACTION_COUNTER_transparentHits);
                if(layer + 1u == NWB_REFRACTION_MAX_BACKGROUND_LAYERS)
                    nwbRefractionCount(NWB_REFRACTION_COUNTER_backgroundLayerCapPixels);
            }
        }
        const half3 shadedRadiance = nwbRefractionShadeSurfaceHit(background, outgoingDirection);
        if(opaqueHit){
            outRadiance = half4((accumulated + transmittance * half3(shadedRadiance)) * half3(attenuation), 1.0h);
            return true;
        }
        accumulated += transmittance * coverage * shadedRadiance;
        transmittance *= half(1.0h) - coverage;
        origin = backgroundPosition + outgoingDirection * entry.bias;
    }
    // Keep the primary volume's exit (direction/thickness/Fresnel); secondary glass adds cached AVBOIT appearance only.
    // Full background replace: no double-count of partially traced layers.
    outContinuationOrigin = exitPosition;
    outContinuationDirection = outgoingDirection;
    outContinuationAttenuation = attenuation;
    outVolumeExited = true;
    return false;
}

half4 nwbRefractionResolveHardware(const uint2 pixel, const NwbRefractionEntry entry){
    half4 radiance;
    float3 continuationOrigin = entry.position;
    float3 continuationDirection = float3(0.0);
    half3 continuationAttenuation = half3(0.0h);
    bool volumeExited = false;
    if(g_NwbRefractionPush.refractionEnabled != 0u){
        if(nwbRefractionTryResolveHardware(entry, radiance, continuationOrigin, continuationDirection, continuationAttenuation, volumeExited))
            return radiance;
        if(!volumeExited)
            continuationDirection = refract(entry.incident, entry.normal, 1.0 / float(entry.ior));
    }
    if(!volumeExited)
        continuationAttenuation = nwbRefractionAttenuation(entry.tint, NWB_REFRACTION_FALLBACK_THICKNESS, entry.ior, entry.cosine, entry.cosine);
    NwbRefractionEntry continuationEntry = entry;
    continuationEntry.position = continuationOrigin;
    half3 backgroundRadiance;
    if(dot(continuationDirection, continuationDirection) <= NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN || !nwbRefractionScreenBackground(continuationEntry, continuationDirection, backgroundRadiance)){
        if(volumeExited)
            nwbRefractionCount(NWB_REFRACTION_COUNTER_exitStraightFallbackPixels);
        backgroundRadiance = half3(nwbRefractionBackground(pixel));
    }
    else if(volumeExited)
        nwbRefractionCount(NWB_REFRACTION_COUNTER_exitScreenBackgroundReturns);
    return half4(half3(backgroundRadiance) * half3(continuationAttenuation), 1.0h);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFRACTION_GROUP_SIZE, NWB_REFRACTION_GROUP_SIZE, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID, uint groupIndex : SV_GroupIndex){
#if NWB_REFRACTION_DIAGNOSTICS
    nwbRefractionInitializeDiagnostics(groupIndex);
    uint2 pixel;
    if(nwbRefractionResolvePixel(groupId, groupThreadId, pixel)){
        NwbRefractionEntry entry;
        if(nwbRefractionLoadEntry(pixel, entry))
            g_NwbRefractionOutput[pixel] = nwbRefractionResolveHardware(pixel, entry);
        else
            g_NwbRefractionOutput[pixel] = float4(0.0);
    }
    nwbRefractionFlushDiagnostics(groupIndex);
#else
    uint2 pixel;
    if(!nwbRefractionResolvePixel(groupId, groupThreadId, pixel))
        return;
    NwbRefractionEntry entry;
    if(!nwbRefractionLoadEntry(pixel, entry)){
        g_NwbRefractionOutput[pixel] = float4(0.0);
        return;
    }
    g_NwbRefractionOutput[pixel] = nwbRefractionResolveHardware(pixel, entry);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

