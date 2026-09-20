// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LIGHT_SPACE_OCCLUDER 1
#include "shadow/light_space_resolve.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Inject one unavailable neighboring search texel after optical shading; the central optical sample remains valid.
[numthreads(1, 1, 1)]
void main(){
    const float3 position = NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.z).Load(int3(0, 0, 0)).xyz;
    const float3 normal = normalize(NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.y).Load(int3(0, 0, 0)).xyz * 2.0 - 1.0);
    const float3 origin = position + normal * NWB_SHADOW_RAY_MIN_DISTANCE;
    const NwbSceneLight light = g_NwbSceneLights[0u];
    NwbLightSpaceView view;
    if(!nwbLightSpaceFindView(0u, origin, view))
        return;
    const float radius = tan(light.params2.x) * nwbLightSpaceLinearDepth(view, origin);
    float3 tangent;
    float3 bitangent;
    nwbShadowBuildBasis(view.originOrDirection.xyz, tangent, bitangent);
    uint2 centerPixel;
    uint2 neighborPixel;
    float depth;
    if(
        !nwbLightSpaceTexel(view, origin, centerPixel, depth)
        || !nwbLightSpaceTexel(view, origin + radius * tangent, neighborPixel, depth)
        || all(centerPixel == neighborPixel)
    )
        return;
    const uint pixelIndex = nwbLightSpacePixelIndex(view, neighborPixel);
    const uint count = NwbHeapRawBuffer(g_NwbLightSpacePush.countsSlot).Load(pixelIndex * 4u);
    if(count == 0u || count > NWB_LIGHT_SPACE_EVENTS_PER_TEXEL)
        return;
    const uint address = pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL * NWB_LIGHT_SPACE_EVENT_BYTES;
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address, 0x7fc00000u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

