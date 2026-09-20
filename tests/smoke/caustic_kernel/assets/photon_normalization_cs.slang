// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The emission-only fixture has no raster material context or authored surface hook.
#define NWB_MESH_MATERIAL_CONTEXT_PROVIDED 1

#include "bindless/bindless_heap.slangi"
#include "caustic/caustic_trace.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbPhotonNormalizationPush{
    uint inputSlot;
    uint outputSlot;
    uint count;
};

[[vk::push_constant]]
ConstantBuffer<NwbPhotonNormalizationPush> g_NwbPhotonNormalization;

// This fixture calls only the production emission helper; the unused transport entry needs a complete trace hook.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    NwbCausticHit hit;
    hit.hit = false;
    hit.isRefractive = false;
    hit.t = 0.0;
    hit.ior = 1.0;
    hit.worldPos = origin;
    hit.shadingNormal = direction;
    hit.transmission = half3(1.0h);
    return hit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID){
    const uint index = dispatchId.x;
    if(index >= g_NwbPhotonNormalization.count)
        return;
    const ByteAddressBuffer inputs = NwbHeapRawBuffer(g_NwbPhotonNormalization.inputSlot);
    const uint base = index * 64u;
    NwbCausticEmission emission;
    emission.valid = true;
    emission.domainArea = asfloat(inputs.Load(base));
    emission.origin = asfloat(inputs.Load3(base + 32u));
    emission.direction = asfloat(inputs.Load3(base + 48u));
    const uint photonCount = inputs.Load(base + 4u);
    const uint targetCount = inputs.Load(base + 8u);
    const half3 light = half3(asfloat(inputs.Load3(base + 16u)));
    const NwbCausticPhoton photon = nwbCausticCreatePhoton(emission, light, targetCount, photonCount);
    const RWByteAddressBuffer output = g_NwbHeapStorageBuffers[g_NwbPhotonNormalization.outputSlot];
    const uint destination = index * 48u;
    output.Store4(destination, asuint(float4(photon.origin, photon.inside ? 1.0 : 0.0)));
    output.Store4(destination + 16u, asuint(float4(photon.direction, 0.0)));
    output.Store4(destination + 32u, asuint(float4(float3(photon.flux), 1.0)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

