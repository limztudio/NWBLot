// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow/light_space_resolve.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Actual production main is dispatched separately; this observer exposes its map/SW selections and emitted rays.
// The CPU intersects those FP32 rays against boxes in FP64, independently of map records and software BVH traversal.
[numthreads(1, 1, 1)]
void main(){
    const float3 position = NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.z).Load(int3(0, 0, 0)).xyz;
    const float3 normal = normalize(NwbHeapSampledImage2D(NWB_LIGHT_SPACE_DEFERRED.gbufferSlots.y).Load(int3(0, 0, 0)).xyz * 2.0 - 1.0);
    const NwbSceneLight light = g_NwbSceneLights[0u];
    const float3 origin = position + normal * NWB_SHADOW_RAY_MIN_DISTANCE;
    NwbLightSpaceView view;
    const bool viewReady = nwbLightSpaceFindView(0u, origin, view);
    float blockerDistance = 0.0;
    const bool soft = light.params.y < NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX ? light.params2.x > 0.0 : light.params2.y > 0.0;
    bool blockerReady = viewReady;
    if(blockerReady && soft)
        blockerReady = nwbLightSpaceBlockerDistance(0u, origin, normal, light, view, blockerDistance);
    if(!blockerReady)
        blockerDistance = 0.0;
    const uint sampleCount = clamp(g_NwbLightSpacePush.sampleCount, 1u, NWB_SW_SHADOW_TRANSPARENT_SPP);
    float3 softwareMean = float3(0.0);
    float3 selectedMean = float3(0.0);
    bool allMapped = true;
    uint faceMask = 0u;
    uint fallbackMask = 0u;
    RWByteAddressBuffer output = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.sceneRootSlot];
    for(uint sample = 0u; sample < sampleCount; ++sample){
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_TRANSPARENT
        const uint salt = NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT;
#else
        const uint salt = 0u;
#endif
        const float2 jitter = nwbShadowLowDiscrepancy2D(uint2(0u), g_NwbLightSpacePush.frameIndex * NWB_SW_SHADOW_TRANSPARENT_SPP + sample + salt);
        float3 direction;
        float minimum;
        float maximum;
        const bool rayReady = nwbShadowBuildLightRay(position, normal, light, jitter, direction, minimum, maximum);
        const float3 software = float3(nwbSwShadowLightVisible(position, normal, light, jitter));
        float3 selected = float3(1.0);
        const bool accepted = rayReady && blockerReady && nwbLightSpaceEvaluate(0u, origin, normal, direction, blockerDistance, selected);
        if(!accepted){
            selected = software;
            fallbackMask |= 1u << sample;
        }
        allMapped = allMapped && accepted;
        uint face = 0xffffffffu;
        float3 reprojected = float3(1.0);
        bool reprojectedAccepted = false;
        if(rayReady && blockerReady){
            NwbLightSpaceView displaced;
            if(nwbLightSpaceFindView(0u, origin + direction * blockerDistance, displaced)){
                face = displaced.light.z;
                faceMask |= 1u << face;
                uint2 displacedPixel = uint2(0u);
                float displacedDepth = 0.0;
                const float4 receiverClip = nwbLightSpaceProject(displaced, origin);
                if(
                    all(isfinite(receiverClip))
                    && receiverClip.w > 0.0
                    && nwbLightSpaceTexel(displaced, origin + direction * blockerDistance, displacedPixel, displacedDepth)
                ){
                    // Independent composition of face selection and sampling detects a producer that clamps displaced taps to the center face.
#if NWB_LIGHT_SPACE_OCCLUDER == NWB_SW_SHADOW_OCCLUDER_TRANSPARENT
                    reprojectedAccepted = nwbLightSpaceSample(displaced, displacedPixel,
                        nwbLightSpaceLinearDepth(displaced, origin), receiverClip.z / receiverClip.w, reprojected);
#else
                    float receiverProjected;
                    float receiverLinear;
                    if(nwbLightSpaceReceiverDepth(displaced, displacedPixel, origin, normal, receiverProjected, receiverLinear))
                        reprojectedAccepted = nwbLightSpaceSample(displaced, displacedPixel, receiverLinear, receiverProjected, reprojected);
#endif
                }
            }
        }
        softwareMean += software / float(sampleCount);
        selectedMean += selected / float(sampleCount);
        const uint address = 64u + sample * 64u;
        output.Store4(address, uint4(asuint(direction), asuint(maximum)));
        output.Store4(address + 16u, uint4(asuint(software), face));
        output.Store4(address + 32u, uint4(asuint(selected), accepted ? 1u : 0u));
        output.Store4(address + 48u, uint4(asuint(reprojected), reprojectedAccepted ? 1u : 0u));
    }
    uint2 pixel = uint2(0u);
    float projectedDepth = 0.0;
    const bool projected = viewReady && nwbLightSpaceTexel(view, origin, pixel, projectedDepth);
    const uint pixelIndex = projected ? nwbLightSpacePixelIndex(view, pixel) : 0u;
    const uint count = projected ? NwbHeapRawBuffer(g_NwbLightSpacePush.countsSlot).Load(pixelIndex * 4u) : 0u;
    if(!allMapped)
        selectedMean = softwareMean;
    output.Store4(0u, uint4(asuint(softwareMean), allMapped ? 1u : 0u));
    output.Store4(16u, uint4(asuint(selectedMean), count));
    output.Store4(32u, uint4(pixelIndex, view.light.z, viewReady ? 1u : 0u, projected ? 1u : 0u));
    output.Store4(48u, uint4(asuint(blockerDistance), blockerReady ? 1u : 0u, faceMask, fallbackMask));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

