// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_SHADING_BINDING NWB_SW_SHADOW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SW_SHADOW_BINDING_LIGHT_LIST

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferWorldPosition;

[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferNormal;

[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferDepth;

[[vk::binding(NWB_SW_SHADOW_BINDING_SCENE_NODES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbSwShadowSceneNodes;

[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2D<uint> g_NwbSwShadowVisibility;

struct NwbSwShadowPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint pad0;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowPushConstants> g_NwbSwShadowPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slab ray/AABB overlap test over the [tMin, tMax] interval. invDir may contain +/-inf for axis-aligned
// directions; the min/max ordering keeps that well-defined for all but the measure-zero on-slab case.
bool nwbSwShadowRayAabb(const float3 origin, const float3 invDir, const float tMin, const float tMax, const float3 boxMin, const float3 boxMax){
    const float3 t0 = (boxMin - origin) * invDir;
    const float3 t1 = (boxMax - origin) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);
    const float tNear = max(max(tSmall.x, tSmall.y), max(tSmall.z, tMin));
    const float tFar = min(min(tLarge.x, tLarge.y), min(tLarge.z, tMax));
    return tNear <= tFar;
}

// Scene-level occlusion: walk the software scene/instance BVH and report a hit as soon as the ray crosses
// any instance's world AABB (a BVH leaf box). This is the coarse stage; the per-mesh triangle traversal
// that refines each leaf hit to an exact ray/triangle test is added in the next stage.
bool nwbSwShadowOccluded(const float3 origin, const float3 direction, const float tMin, const float tMax){
    if(g_NwbSwShadowPushConstants.instanceCount == 0u)
        return false;

    const float3 invDir = 1.0 / direction;

    uint stack[32];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbSwShadowSceneNodes[nodeIndex];
        if(!nwbSwShadowRayAabb(origin, invDir, tMin, tMax, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node))
            return true;

        if((stackPointer + 2u) <= 32u){
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
    return false;
}

// Per light: cast one occlusion ray from the surface toward the light. Mirrors the hardware raygen
// (directional aims along the toward-light axis to "infinity"; point/spot aim at the light with tMax = the
// distance; geometric-normal bias + tMin avoids self-intersection; a light inside the bias shell is lit).
bool nwbSwShadowLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light){
    float3 toLight;
    float tMax;
    if(light.params.y < 0.5){
        toLight = normalize(light.direction.xyz);
        tMax = 1.0e7;
    }
    else{
        const float3 delta = light.position.xyz - worldPosition;
        tMax = length(delta);
        toLight = (tMax > 1.0e-5) ? (delta / tMax) : float3(0.0, 1.0, 0.0);
    }

    const float tMin = 0.001;
    const float tEnd = tMax - 0.002;
    if(tEnd <= tMin)
        return true;

    const float3 origin = worldPosition + normal * tMin;
    return !nwbSwShadowOccluded(origin, toLight, tMin, tEnd);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPushConstants.width || pixel.y >= g_NwbSwShadowPushConstants.height)
        return;

    const int3 texel = int3(int2(pixel), 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999){
        g_NwbSwShadowVisibility[pixel] = 0xFFFFFFFFu;
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_SHADOW_VISIBILITY_LIGHT_COUNT);
    uint visibilityMask = 0u;
    for(uint i = 0u; i < lightCount; ++i){
        if(nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i]))
            visibilityMask |= (1u << i);
    }

    g_NwbSwShadowVisibility[pixel] = visibilityMask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
