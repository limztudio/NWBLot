// limztudio@gmail.com


#include "bvh_triangle_input.slangi"
#include "bvh_heap_resources.slangi"


// One thread per triangle: quantize the triangle centroid into a 30-bit Morton code over the mesh bounds
// and emit the (code, original-triangle-index) sort pair. Padding slots beyond primitiveCount keep whatever
// sentinel the host cleared them to, so they sort to the end.
[numthreads(NWB_BVH_BUILD_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint triangle = dispatchThreadID.x;
    if(triangle >= g_NwbBvhBuildPushConstants.primitiveCount)
        return;

    float3 p0;
    float3 p1;
    float3 p2;
    nwbBvhLoadTrianglePositions(triangle, p0, p1, p2);

    const float3 centroid = (p0 + p1 + p2) * (1.0 / 3.0);
    const float3 boundsMin = g_NwbBvhBuildPushConstants.aabbMin.xyz;
    const float3 boundsExtent = max(g_NwbBvhBuildPushConstants.aabbMax.xyz - boundsMin, float3(1e-8, 1e-8, 1e-8));
    const float3 normalized = (centroid - boundsMin) / boundsExtent;

    g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBuildPushConstants.keysHeapSlot][triangle] = nwbBvhMorton3D(normalized);
    g_NwbHeapBvhWritableUintBuffers[g_NwbBvhBuildPushConstants.payloadHeapSlot][triangle] = triangle;
}


