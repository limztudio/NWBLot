// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SCENE_SHADING_SET NWB_SHADOW_SDF_SET
#define NWB_SCENE_SHADING_BINDING NWB_SHADOW_SDF_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SHADOW_SDF_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SHADOW_SDF_BINDING_LIGHT_LIST

#include "binding_slots.h"
#include "../scene/buffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One occluder instance: the inverse (world->object) affine transform rows + the mesh object-space AABB.
// v1 approximates each mesh as its bounding box; baked per-mesh SDF textures replace the box term later.
struct NwbSdfInstance{
    float4 worldToObject0;
    float4 worldToObject1;
    float4 worldToObject2;
    float4 objAabbMin;
    float4 objAabbMax;
};

[[vk::binding(NWB_SHADOW_SDF_BINDING_INSTANCES, NWB_SHADOW_SDF_SET)]]
StructuredBuffer<NwbSdfInstance, Std430DataLayout> g_NwbSdfInstances;

[[vk::binding(NWB_SHADOW_SDF_BINDING_GBUFFER_WORLD_POSITION, NWB_SHADOW_SDF_SET)]]
Texture2D<float4> g_NwbSdfGBufferWorldPosition;

[[vk::binding(NWB_SHADOW_SDF_BINDING_GBUFFER_NORMAL, NWB_SHADOW_SDF_SET)]]
Texture2D<float4> g_NwbSdfGBufferNormal;

[[vk::binding(NWB_SHADOW_SDF_BINDING_GBUFFER_DEPTH, NWB_SHADOW_SDF_SET)]]
Texture2D<float4> g_NwbSdfGBufferDepth;

[[vk::binding(NWB_SHADOW_SDF_BINDING_VISIBILITY_OUTPUT, NWB_SHADOW_SDF_SET)]]
RWTexture2D<uint> g_NwbSdfVisibility;

struct NwbSdfParams{
    uint instanceCount;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

[[vk::binding(NWB_SHADOW_SDF_BINDING_PARAMS, NWB_SHADOW_SDF_SET)]]
ConstantBuffer<NwbSdfParams, Std140DataLayout> g_NwbSdfParams;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Signed distance to an axis-aligned box in object space (negative inside). Distances are object-space,
// which equals world-space for rigid (rotation + translation, unit-scale) instances — the v1 assumption.
float nwbSdfBox(float3 position, float3 aabbMin, float3 aabbMax){
    const float3 center = (aabbMin + aabbMax) * 0.5;
    const float3 extent = (aabbMax - aabbMin) * 0.5;
    const float3 q = abs(position - center) - extent;
    return length(max(q, float3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// Nearest signed distance over all occluder instances, evaluated at a world-space point.
float nwbSdfSceneDistance(float3 worldPosition){
    float nearest = 1.0e9;
    const uint count = g_NwbSdfParams.instanceCount;
    for(uint i = 0u; i < count; ++i){
        const NwbSdfInstance inst = g_NwbSdfInstances[i];
        const float4 hp = float4(worldPosition, 1.0);
        const float3 objectPosition = float3(
            dot(inst.worldToObject0, hp),
            dot(inst.worldToObject1, hp),
            dot(inst.worldToObject2, hp)
        );
        nearest = min(nearest, nwbSdfBox(objectPosition, inst.objAabbMin.xyz, inst.objAabbMax.xyz));
    }
    return nearest;
}

// Sphere-march the global field; returns true if an occluder lies between the origin and tMax along dir.
bool nwbSdfTraceOcclusion(float3 origin, float3 direction, float tMax){
    const float hitEpsilon = 0.01;
    float t = 0.02;
    [loop]
    for(uint step = 0u; step < 64u; ++step){
        if(t >= tMax)
            break;
        const float distance = nwbSdfSceneDistance(origin + direction * t);
        if(distance < hitEpsilon)
            return true;
        t += max(distance, 0.01);
    }
    return false;
}

// Per light: form the occlusion ray (directional aims to "infinity"; point/spot to the light at tMax =
// distance) with a normal bias, then march it. Mirrors the hardware nwbShadowLightVisible semantics.
bool nwbSdfLightVisible(float3 worldPosition, float3 normal, NwbSceneLight light){
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

    const float tMin = 0.02;
    const float tEnd = tMax - 0.04;
    if(tEnd <= tMin)
        return true;

    return !nwbSdfTraceOcclusion(worldPosition + normal * tMin, toLight, tEnd);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadId.xy;
    uint width, height;
    g_NwbSdfVisibility.GetDimensions(width, height);
    if(pixel.x >= width || pixel.y >= height)
        return;

    const int3 texel = int3(int2(pixel), 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway.
    if(g_NwbSdfGBufferDepth.Load(texel).r >= 0.999999){
        g_NwbSdfVisibility[pixel] = 0xFFFFFFFFu;
        return;
    }

    const float3 worldPosition = g_NwbSdfGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSdfGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_SHADOW_VISIBILITY_LIGHT_COUNT);
    uint visibilityMask = 0u;
    for(uint i = 0u; i < lightCount; ++i){
        if(nwbSdfLightVisible(worldPosition, normal, g_NwbSceneLights[i]))
            visibilityMask |= (1u << i);
    }

    g_NwbSdfVisibility[pixel] = visibilityMask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
