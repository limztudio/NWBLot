// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_resolve_binding_slots.h"
#include "constants.h"
#include "sw_binding_slots.h"
#include "shadow_heap_resources.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbShadowGeometryDownsamplePushConstants{
    uint width;
    uint height;
    uint halfWidth;
    uint halfHeight;
    uint worldPositionSlot;
    uint normalSlot;
    uint depthSlot;
    uint outputStorageSlot;
    uint sceneShadingSlot;
    uint receiverFactor;
};

[[vk::push_constant]] ConstantBuffer<NwbShadowGeometryDownsamplePushConstants> g_NwbShadowGeometryDownsamplePushConstants;

#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbShadowGeometryDownsamplePushConstants.sceneShadingSlot
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT 0u
#include "../scene/buffer.slangi"

#define g_NwbShadowGeometryOutput nwbHeapStorageImage2DFloat4(g_NwbShadowGeometryDownsamplePushConstants.outputStorageSlot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfTexel = dispatchThreadID.xy;
    if(halfTexel.x >= g_NwbShadowGeometryDownsamplePushConstants.halfWidth || halfTexel.y >= g_NwbShadowGeometryDownsamplePushConstants.halfHeight)
        return;

    const int2 full = int2(
        min((int)halfTexel.x * g_NwbShadowGeometryDownsamplePushConstants.receiverFactor, (int)g_NwbShadowGeometryDownsamplePushConstants.width - 1),
        min((int)halfTexel.y * g_NwbShadowGeometryDownsamplePushConstants.receiverFactor, (int)g_NwbShadowGeometryDownsamplePushConstants.height - 1)
    );

    const float depth = nwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.depthSlot).Load(int3(full, 0)).r;
    if(depth >= NWB_SCENE_BACKGROUND_DEPTH){
        g_NwbShadowGeometryOutput[halfTexel] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const half3 worldSample = half3(nwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.worldPositionSlot).Load(int3(full, 0)).xyz);
    const float3 world = float3(worldSample);
    const half3 normalSample = half3(nwbHeapSampledImage2D(g_NwbShadowGeometryDownsamplePushConstants.normalSlot).Load(int3(full, 0)).xyz);
    const float3 normal = normalize(float3(normalSample) * 2.0 - 1.0);
    const half2 octNormal = half2(nwbOctEncodeNormal(normal));
    const float cameraDistance = length(world - g_NwbSceneCameraPosition.xyz);

    g_NwbShadowGeometryOutput[halfTexel] = float4(float2(octNormal), cameraDistance, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

