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


#define g_NwbRefractionMaterialContext nwbHeapMaterialContextSlots(g_NwbRefractionPush.materialContextSlotsHeapSlot)


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


#include "hardware_background_trace.slangi"

bool nwbRefractionTryAssociateCapturedEntry(const uint instanceMaterialHeapSlot, const NwbRefractionEntry entry, const float3 camera, const float entryDistance, const float associationTolerance, out uint primaryInstance, out uint primaryAttributeSlot){
    primaryInstance = 0u;
    primaryAttributeSlot = 0u;
#if NWB_RT_CSG_ENABLED
    return false;
#else
    const uint candidate = g_NwbRefractionPush.certifiedInstanceIndex;
    if(!entry.frontBoundary || candidate >= g_NwbRefractionMaterialContext.materialSlots.z || associationTolerance > NWB_REFRACTION_CERTIFIED_ASSOCIATION_MAX_TOLERANCE)
        return false;
    if((nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[candidate].flags & NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS) != NWB_REFRACTION_CERTIFIED_INSTANCE_MATERIAL_FLAGS)
        return false;
    const uint meshInstanceIndex = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[candidate].meshInstanceIndex;
    if(g_NwbMeshInstances[meshInstanceIndex].entityId != entry.primaryEntityId)
        return false;
    const float minDistance = max(entryDistance - associationTolerance, entry.bias);
    const float4 windowStartClip = mul(g_NwbRefractionView.worldToClip, float4(camera + entry.incident * minDistance, 1.0));
    if(select(windowStartClip.w <= NWB_REFRACTION_MIN_CLIP_W, true, windowStartClip.z < 0.0))
        return false;
    primaryInstance = candidate;
    primaryAttributeSlot = nwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[candidate].attributeSlot;
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
        const float exitDot = dot(exitNormal, internalDirection);
        exitNormal = select(bool3(exitDot < 0.0), -exitNormal, exitNormal);
        exitCosine = half(saturate(abs(exitDot)));
        outgoingDirection = refract(internalDirection, -exitNormal, float(entry.ior));
        if(dot(outgoingDirection, outgoingDirection) >= NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN)
            break;
        nwbRefractionCount(NWB_REFRACTION_COUNTER_tirEvents);
        // Discard residual internally reflected energy at the bounce cap rather than leaking straight-through background.
        if(internalBounce == NWB_REFRACTION_MAX_INTERNAL_BOUNCES){
            nwbRefractionCount(NWB_REFRACTION_COUNTER_tirCappedPixels);
            return true;
        }
        internalDirection = reflect(internalDirection, exitNormal);
        internalOrigin = exitPosition;
    }
    nwbRefractionCount(NWB_REFRACTION_COUNTER_volumeExits);
    const half3 attenuation = nwbRefractionAttenuation(entry.tint, pathLength, entry.ior, entry.cosine, exitCosine);

    if(NwbRefractionHardwareBackgroundTrace.trace(entry, instanceMaterialHeapSlot, exitPosition, outgoingDirection, attenuation, outRadiance))
        return true;
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
    NwbRefractionEntry continuationEntry = entry;
    if(volumeExited){
        continuationEntry.position = continuationOrigin;
        continuationEntry.cameraDistance = length(continuationOrigin - g_NwbRefractionView.cameraPosition.xyz);
    }
    else
        continuationAttenuation = nwbRefractionFallbackAttenuation(entry.tint, entry.ior, entry.cosine);
    uint2 samplePixel = pixel;
    const bool displaced = dot(continuationDirection, continuationDirection) > NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN && nwbRefractionScreenBackgroundPixel(continuationEntry, continuationDirection, samplePixel);
    if(volumeExited)
        nwbRefractionCount(displaced ? NWB_REFRACTION_COUNTER_exitScreenBackgroundReturns : NWB_REFRACTION_COUNTER_exitStraightFallbackPixels);
    const half3 backgroundRadiance = nwbRefractionBackgroundForEntry(select(bool2(displaced), samplePixel, pixel), entry, true, displaced && any(samplePixel != pixel));
    return half4(backgroundRadiance * continuationAttenuation, 1.0h);
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

