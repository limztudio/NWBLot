// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_common.slangi"
#include "../raytrace/material_context_slots.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma warning(push)
#pragma warning(disable: 39001)
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbRayTraceMaterialContextSlots, Std140DataLayout> g_NwbHeapRefractionMaterialContexts[];
#pragma warning(pop)
#define g_NwbRefractionMaterialContext g_NwbHeapRefractionMaterialContexts[g_NwbRefractionPush.materialContextSlotsHeapSlot]

uint nwbMeshFrameMaterialTypedHeapSlot(){ return g_NwbRefractionMaterialContext.sceneSlots.w; }
uint nwbMeshFrameInstanceHeapSlot(){ return g_NwbRefractionMaterialContext.materialSlots.x; }
uint nwbMeshFrameViewHeapSlot(){ return g_NwbRefractionPush.viewHeapSlot; }
#include "../bindless/bindless_heap_mesh.slangi"

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbRefractionResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbRefractionResources.avboitSlots.w
#define NWB_SCENE_SHADOW_CAUSTIC_SAMPLING_DISABLED 1
#include "../raytrace/surface_hit.slangi"
#include "../scene/lighting.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbRefractionShadeHit(const NwbRayTraceSurfaceHit hit, const float3 direction){
    float3 normal = nwbSafeNormalize(hit.surface.normal, hit.geometricNormal);
    if(dot(normal, direction) > 0.0)
        normal = -normal;
    float3 radiance = float3(0.0);
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex){
        float3 lightVector;
        half3 lightRadiance;
        nwbSceneResolveLight(g_NwbSceneLights[lightIndex], hit.position, lightVector, lightRadiance);
        radiance += float3(lightRadiance) * max(dot(normal, lightVector), 0.0);
    }
    // Offscreen hits have no camera lighting cache. This deliberately bounded diffuse approximation shares the
    // authored surface contract with GI; full camera BRDF, shadows, and GI are not implicitly available at a ray hit.
    return radiance * float3(hit.surface.baseColor);
}

bool nwbRefractionCachedHit(const NwbRayTraceSurfaceHit hit, const NwbRefractionEntry entry, const bool includeTransparency, out float3 radiance){
    radiance = float3(0.0);
    uint2 pixel;
    float projectedDepth;
    if(!nwbRefractionProject(hit.position, pixel, projectedDepth))
        return false;
    const float opaqueDepth = NwbHeapSampledImage2D(g_NwbRefractionResources.gbufferSlots.w).Load(int3(pixel, 0)).r;
    if(opaqueDepth >= 1.0)
        return false;
    const float3 opaqueWorld = NwbHeapSampledImage2D(g_NwbRefractionResources.gbufferSlots.z).Load(int3(pixel, 0)).xyz;
    const float cameraDistance = length(hit.position - g_NwbRefractionView.cameraPosition.xyz);
    // Require both projected depth and world distance. A depth-only comparison becomes permissive in the far field
    // and incorrectly returns an opaque foreground object's lighting for a hidden RT hit.
    if(abs(projectedDepth - opaqueDepth) > 0.0002 || length(opaqueWorld - hit.position) > max(0.01, cameraDistance * 0.002))
        return false;
    radiance = includeTransparency ? nwbRefractionBackgroundForEntry(pixel, entry)
        : NwbHeapSampledImage2D(g_NwbRefractionResources.compositeSlots.x).Load(int3(pixel, 0)).rgb;
    return true;
}

float4 nwbRefractionResolveHardware(const uint2 pixel, const NwbRefractionEntry entry){
    // Raster capture respects CSG cuts, but the shared triangle TLAS contains the original uncut mesh. Its exit
    // would produce the wrong optical thickness, so CSG primaries use the bounded screen-space approximation.
    if(entry.csgPrimary)
        return nwbRefractionResolveScreen(pixel, entry);
    const float3 camera = g_NwbRefractionView.cameraPosition.xyz;
    const float entryDistance = length(entry.position - camera);
    const float associationTolerance = max(entry.bias * 8.0, entryDistance * 0.0002);
    const NwbRayTraceSurfaceHit entryHit = nwbRayTraceClosestSurfaceHit(
        g_NwbRefractionMaterialContext.sceneSlots.z, camera, entry.incident,
        max(entryDistance - associationTolerance, entry.bias), entryDistance + associationTolerance
    );
    if(!entryHit.hit || !entryHit.frontFace || float(entryHit.surface.refractionIor) <= 1.001
        || abs(float(entryHit.surface.refractionIor) - entry.ior) > 0.02)
        return nwbRefractionResolveScreen(pixel, entry);

    float3 internalDirection = refract(entry.incident, entry.normal, 1.0 / entry.ior);
    if(dot(internalDirection, internalDirection) < 0.5)
        return float4(0.0, 0.0, 0.0, 1.0);
    NwbRayTraceSurfaceHit exitHit = (NwbRayTraceSurfaceHit)0;
    float3 internalOrigin = entryHit.position;
    float3 outgoingDirection = float3(0.0);
    float exitCosine = 0.0;
    float pathLength = 0.0;
    // At most two total-internal-reflection continuations, staying inside the same primary instance. Their Fresnel
    // reflectance is one, so only the accumulated Beer-Lambert distance changes until the ray finally transmits.
    for(uint internalBounce = 0u; internalBounce <= 2u; ++internalBounce){
        exitHit = nwbRayTraceClosestSurfaceHit(
            g_NwbRefractionMaterialContext.sceneSlots.z, internalOrigin + internalDirection * entry.bias,
            internalDirection, entry.bias, NWB_REFRACTION_MAX_DISTANCE
        );
        // A different closest instance may be a nested volume or geometry inside the primary. Do not ignore it and
        // pretend the next same-instance intersection was the nearest interface. Open meshes take the sheet fallback.
        if(!exitHit.hit || exitHit.instance != entryHit.instance || exitHit.frontFace)
            return nwbRefractionResolveScreen(pixel, entry);
        pathLength += length(exitHit.position - internalOrigin);
        float3 exitNormal = nwbSafeNormalize(exitHit.surface.normal, exitHit.geometricNormal);
        if(dot(exitNormal, internalDirection) < 0.0)
            exitNormal = -exitNormal;
        exitCosine = saturate(dot(internalDirection, exitNormal));
        outgoingDirection = refract(internalDirection, -exitNormal, entry.ior);
        if(dot(outgoingDirection, outgoingDirection) >= 0.5)
            break;
        internalDirection = reflect(internalDirection, exitNormal);
        internalOrigin = exitHit.position;
    }
    // Residual internally reflected energy at the fixed cap is discarded. Returning a straight-through leak here
    // would violate the optical boundary; a future reflected-radiance pass can supply the missing contribution.
    if(dot(outgoingDirection, outgoingDirection) < 0.5)
        return float4(0.0, 0.0, 0.0, 1.0);
    const float3 attenuation = nwbRefractionAttenuation(entry.tint,
        pathLength, entry.ior, entry.cosine, exitCosine);

    float3 origin = exitHit.position + outgoingDirection * entry.bias;
    float3 accumulated = float3(0.0);
    float transmittance = 1.0;
    // Normally one background query. Up to two flat transparent hits may be evaluated before a final opaque query.
    // Further glass or an incomplete continuation uses the exit-based approximation below without tracing a new volume.
    for(uint layer = 0u; layer < 3u; ++layer){
        const NwbRayTraceSurfaceHit background = nwbRayTraceClosestSurfaceHit(
            g_NwbRefractionMaterialContext.sceneSlots.z, origin, outgoingDirection, entry.bias, NWB_REFRACTION_MAX_DISTANCE
        );
        if(!background.hit)
            break;
        if(float(background.surface.refractionIor) > 1.001)
            break;
        const float coverage = saturate(float(background.surface.renderCoverage));
        if(coverage >= 0.999){
            float3 radiance;
            if(!nwbRefractionCachedHit(background, entry, layer == 0u, radiance))
                radiance = nwbRefractionShadeHit(background, outgoingDirection);
            return float4((accumulated + transmittance * radiance) * attenuation, 1.0);
        }
        accumulated += transmittance * coverage * nwbRefractionShadeHit(background, outgoingDirection);
        transmittance *= 1.0 - coverage;
        origin = background.position + outgoingDirection * entry.bias;
    }
    // Keep the completed primary volume's exit direction, measured thickness, and Fresnel transmission. The bounded
    // screen lookup retains its depth and primary-instance checks; secondary glass contributes only its cached AVBOIT
    // appearance, without additional bending or optical absorption. Replace the entire background estimate here so
    // partially traced transparent layers are not also counted through the cached AVBOIT accumulation.
    NwbRefractionEntry exitEntry = entry;
    exitEntry.position = exitHit.position;
    float3 backgroundRadiance;
    if(!nwbRefractionScreenBackground(exitEntry, outgoingDirection, backgroundRadiance))
        backgroundRadiance = nwbRefractionBackground(pixel);
    return float4(backgroundRadiance * attenuation, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFRACTION_GROUP_SIZE, NWB_REFRACTION_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbRefractionPush.width || pixel.y >= g_NwbRefractionPush.height)
        return;
    NwbRefractionEntry entry;
    if(!nwbRefractionLoadEntry(pixel, entry)){
        g_NwbRefractionOutput[pixel] = float4(0.0);
        return;
    }
    g_NwbRefractionOutput[pixel] = nwbRefractionResolveHardware(pixel, entry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

