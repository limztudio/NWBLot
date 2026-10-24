// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFRACTION_GROUP_SIZE, NWB_REFRACTION_GROUP_SIZE, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID){
    uint2 pixel;
    if(!nwbRefractionResolvePixel(groupId, groupThreadId, pixel))
        return;

    const int3 pixelTexel = int3(pixel, 0);
    const float depth = nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots0.x).Load(pixelTexel).r;
    if(depth >= 1.0){
        g_NwbRefractionOutput[pixel] = float4(0.0);
        return;
    }
    const float4 normalIor = nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots0.y).Load(pixelTexel);
    if(!nwbRefractionIorEligible(normalIor.w)){
        g_NwbRefractionOutput[pixel] = float4(0.0);
        return;
    }

    NwbRefractionEntry entry = (NwbRefractionEntry)0;
    entry.position = nwbRefractionReconstruct(pixel, depth);
    const float3 cameraRay = entry.position - g_NwbRefractionView.cameraPosition.xyz;
    entry.cameraDistance = length(cameraRay);
    entry.incident = nwbSafeNormalize(cameraRay, float3(0.0, 0.0, 1.0));
    const float3 rawNormal = nwbSafeNormalize(normalIor.xyz, -entry.incident);
    const float incidentDotNormal = dot(entry.incident, rawNormal);
    entry.normal = select(bool3(incidentDotNormal > 0.0), -rawNormal, rawNormal);
    entry.ior = half(clamp(normalIor.w, 1.0, NWB_REFRACTION_IOR_MAX));
    entry.cosine = half(saturate(abs(incidentDotNormal)));
    entry.bias = max(NWB_REFRACTION_MIN_DISTANCE, entry.cameraDistance * NWB_REFRACTION_BIAS_DISTANCE_SCALE);

    uint2 samplePixel = pixel;
    bool displaced = false;
    if(g_NwbRefractionPush.refractionEnabled != 0u){
        const float3 direction = refract(entry.incident, entry.normal, 1.0 / float(entry.ior));
        displaced = dot(direction, direction) > NWB_REFRACTION_DIRECTION_LENGTH_SQUARED_MIN && nwbRefractionScreenBackgroundPixel(entry, direction, samplePixel);
    }

    const uint2 resolvedPixel = select(bool2(displaced), samplePixel, pixel);
    const int3 resolvedTexel = int3(resolvedPixel, 0);
    const half3 opaque = nwbRefractionOpaqueBackground(resolvedPixel);
    const half behindExtinction = half(nwbHeapSampledImage2D(g_NwbRefractionResources.compositeSlots.z).Load(resolvedTexel).r);
    half3 background = opaque;
    if(behindExtinction > half(0.0h)){
        bool allowTransparency = true;
        if(displaced && any(samplePixel != pixel)){
            if(nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots0.x).Load(resolvedTexel).r >= 1.0)
                allowTransparency = false;
            else{
                const uint primaryEntityId = nwbAvboitUnpackRefractionEntityId(nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots1.w).Load(pixelTexel).rg);
                if(nwbAvboitUnpackRefractionEntityId(nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots1.w).Load(resolvedTexel).rg) != primaryEntityId)
                    allowTransparency = false;
            }
        }
        if(allowTransparency)
            background = nwbRefractionCompositeBehindBackground(resolvedTexel, opaque, behindExtinction);
    }
    const half3 tint = saturate(half3(nwbHeapSampledImage2D(g_NwbRefractionResources.refractionSlots0.z).Load(pixelTexel).rgb));
    g_NwbRefractionOutput[pixel] = half4(background * nwbRefractionFallbackAttenuation(tint, entry.ior, entry.cosine), 1.0h);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

