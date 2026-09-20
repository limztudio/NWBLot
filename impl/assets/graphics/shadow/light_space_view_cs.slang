// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_scene.slangi"
#include "light_space_cull.slangi"

#define NWB_LIGHT_SPACE_DEFERRED NwbHeapDeferredBindlessResources(g_NwbLightSpacePush.deferredResourcesSlot)
#define NWB_SCENE_SHADING_HEAP_SLOT NWB_LIGHT_SPACE_DEFERRED.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT NWB_LIGHT_SPACE_DEFERRED.avboitSlots.w
#include "../scene/buffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbLightSpaceStoreView(const uint index, const NwbLightSpaceView view){
    const uint address = index * NWB_LIGHT_SPACE_VIEW_BYTES;
    for(uint row = 0u; row < 4u; ++row)
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.viewSlot].Store4(address + row * 16u, asuint(view.worldToClipRows[row]));
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.viewSlot].Store4(address + 64u, asuint(view.originOrDirection));
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.viewSlot].Store4(address + 80u, asuint(view.depthRange));
}

float3 nwbLightSpaceCorner(const NwbBvhNode root, const uint corner){
    return float3((corner & 1u) != 0u ? root.aabbMax.x : root.aabbMin.x,
        (corner & 2u) != 0u ? root.aabbMax.y : root.aabbMin.y,
        (corner & 4u) != 0u ? root.aabbMax.z : root.aabbMin.z);
}

bool nwbLightSpaceBuildView(inout NwbLightSpaceView view){
    view.depthRange = float4(0.0);
    for(uint row = 0u; row < 4u; ++row)
        view.worldToClipRows[row] = float4(0.0);
    if((view.light.w & NWB_LIGHT_SPACE_FLAG_ELIGIBLE) == 0u || view.light.x >= min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS))
        return false;
    const NwbBvhNode root = NwbHeapBvhNodes(g_NwbLightSpacePush.sceneRootSlot)[0u];
    if(!all(isfinite(root.aabbMin)) || !all(isfinite(root.aabbMax)) || any(root.aabbMin > root.aabbMax))
        return false;
    const NwbSceneLight light = g_NwbSceneLights[view.light.x];
    const bool point = (view.light.w & NWB_LIGHT_SPACE_FLAG_POINT) != 0u;
    float3 forward;
    float3 right;
    float3 up;
    if(point){
        const float3 faceDirections[6] = {
            float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
            float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1),
        };
        if(view.light.z >= 6u || !all(isfinite(light.position.xyz)))
            return false;
        forward = faceDirections[view.light.z];
        const float3 helper = abs(forward.y) > 0.9 ? float3(0, 0, 1) : float3(0, 1, 0);
        right = normalize(cross(helper, forward));
        up = cross(forward, right);
        view.originOrDirection = float4(light.position.xyz, 1.0);
        const float minimumNearPlane = NWB_SHADOW_RAY_MIN_DISTANCE * 0.25;
        float farPlane = minimumNearPlane;
        for(uint corner = 0u; corner < 8u; ++corner)
            farPlane = max(farPlane, length(nwbLightSpaceCorner(root, corner) - light.position.xyz));
        farPlane += max(0.001, farPlane * 0.00001);
        if(!isfinite(farPlane) || farPlane <= minimumNearPlane)
            return false;
        float minimumCasterDistance = farPlane;
        // A near-plane-clipped caster needs ray traversal. Current posed mesh roots prove this without CPU readback.
        for(uint instanceIndex = 0u; instanceIndex < g_NwbLightSpacePush.instanceCount; ++instanceIndex){
            const NwbRtInstanceMaterial material = nwbLightSpaceMaterial(instanceIndex);
            const NwbSwBvhInstance instance = nwbLightSpaceInstance(instanceIndex);
            const NwbBvhNode meshRoot = NwbHeapBvhNodes(material.nodeSlot)[0u];
            const float3 origin = nwbSwBvhTransformPoint(instance, light.position.xyz);
            const float inverseNorm = sqrt(dot(instance.worldToObjectRow0.xyz, instance.worldToObjectRow0.xyz)
                + dot(instance.worldToObjectRow1.xyz, instance.worldToObjectRow1.xyz)
                + dot(instance.worldToObjectRow2.xyz, instance.worldToObjectRow2.xyz));
            const float3 outside = max(max(meshRoot.aabbMin - origin, origin - meshRoot.aabbMax), 0.0);
            const float conservativeDistance = length(outside) / inverseNorm;
            if(!all(isfinite(origin)) || !all(isfinite(meshRoot.aabbMin)) || !all(isfinite(meshRoot.aabbMax))
                || any(meshRoot.aabbMin > meshRoot.aabbMax) || !isfinite(conservativeDistance)
                || conservativeDistance <= minimumNearPlane)
                return false;
            minimumCasterDistance = min(minimumCasterDistance, conservativeDistance);
        }
        // Inside a 90-degree cube face view-Z >= radialDistance / sqrt(3). Keep a further factor-two caster margin.
        const float nearPlane = max(minimumNearPlane, minimumCasterDistance * (0.5 / sqrt(3.0)));
        if(!isfinite(nearPlane) || nearPlane >= farPlane)
            return false;
        const float zScale = farPlane / (farPlane - nearPlane);
        view.worldToClipRows[0] = float4(right, -dot(right, light.position.xyz));
        view.worldToClipRows[1] = float4(up, -dot(up, light.position.xyz));
        view.worldToClipRows[2] = float4(forward * zScale, -dot(forward, light.position.xyz) * zScale - nearPlane * zScale);
        view.worldToClipRows[3] = float4(forward, -dot(forward, light.position.xyz));
        view.depthRange = float4(nearPlane, farPlane, minimumCasterDistance, 1.0);
    }
    else{
        if(!all(isfinite(light.direction.xyz)) || dot(light.direction.xyz, light.direction.xyz) <= 0.0)
            return false;
        forward = -normalize(light.direction.xyz);
        const float3 helper = abs(forward.y) > 0.9 ? float3(0, 0, 1) : float3(0, 1, 0);
        right = normalize(cross(helper, forward));
        up = cross(forward, right);
        float3 lower = float3(asfloat(0x7f7fffffu));
        float3 upper = -lower;
        for(uint corner = 0u; corner < 8u; ++corner){
            const float3 p = nwbLightSpaceCorner(root, corner);
            const float3 projected = float3(dot(right, p), dot(up, p), dot(forward, p));
            lower = min(lower, projected);
            upper = max(upper, projected);
        }
        const float3 padding = max(float3(0.001), max(abs(lower), abs(upper)) * 0.00001);
        lower -= padding;
        upper += padding;
        const float3 extent = upper - lower;
        if(!all(isfinite(extent)) || any(extent <= 0.0))
            return false;
        view.worldToClipRows[0] = float4(right * (2.0 / extent.x), -(lower.x + upper.x) / extent.x);
        view.worldToClipRows[1] = float4(up * (2.0 / extent.y), -(lower.y + upper.y) / extent.y);
        view.worldToClipRows[2] = float4(forward / extent.z, -lower.z / extent.z);
        view.worldToClipRows[3] = float4(0, 0, 0, 1);
        view.originOrDirection = float4(-forward, 0.0);
        view.depthRange = float4(0.0, extent.z, lower.z, 1.0);
    }
    for(uint row = 0u; row < 4u; ++row){
        if(!all(isfinite(view.worldToClipRows[row])))
            return false;
    }
    return true;
}

[numthreads(NWB_LIGHT_SPACE_VIEW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    if(dispatchThreadID.x >= g_NwbLightSpacePush.viewCount)
        return;
    NwbLightSpaceView view = nwbLightSpaceLoadView(dispatchThreadID.x);
    if(!nwbLightSpaceBuildView(view))
        view.depthRange.w = 0.0;
    nwbLightSpaceStoreView(dispatchThreadID.x, view);
    nwbLightSpaceWriteDrawArguments(view, dispatchThreadID.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

