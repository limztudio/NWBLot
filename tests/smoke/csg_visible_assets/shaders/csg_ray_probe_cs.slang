// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_RAY_HARDWARE 0
#define NWB_CSG_RAY_STRICT_NORMALS 1
#include "csg/ray_geometry.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgRayProbePush{
    uint outputSlot;
};

[[vk::push_constant]] ConstantBuffer<NwbCsgRayProbePush> g_NwbCsgRayProbe;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbCsgRayProbePair(const float3 opposite, const float3 origin, const float3 direction, const bool tangent){
    const float3 a = float3(0.0, -1.0, 0.0);
    const float3 b = float3(0.0, 1.0, 0.0);
    const NwbCsgRayTriangleIntersection first = nwbCsgRayTriangleIntersection(a, b, float3(-1.0, 0.0, 1.0), origin, direction, true);
    const NwbCsgRayTriangleIntersection second = nwbCsgRayTriangleIntersection(b, a, opposite, origin, direction, true);
    const int winding = (first.hit && first.owned ? first.windingDelta : 0) + (second.hit && second.owned ? second.windingDelta : 0);
    return first.failure == 0u && second.failure == 0u && (tangent ? winding == 0 : abs(winding) == 1);
}

bool nwbCsgRayProbeVertex(const bool reversed){
    const float3 positions[8] = { float3(-1.0, -1.0, -1.0), float3(1.0, -1.0, -1.0), float3(1.0, 1.0, -1.0), float3(-1.0, 1.0, -1.0), float3(-1.0, -1.0, 1.0), float3(1.0, -1.0, 1.0), float3(1.0, 1.0, 1.0), float3(-1.0, 1.0, 1.0) };
    const uint3 triangles[12] = { uint3(0u, 2u, 1u), uint3(0u, 3u, 2u), uint3(4u, 5u, 6u), uint3(4u, 6u, 7u), uint3(0u, 1u, 5u), uint3(0u, 5u, 4u), uint3(3u, 7u, 6u), uint3(3u, 6u, 2u), uint3(0u, 4u, 7u), uint3(0u, 7u, 3u), uint3(1u, 2u, 6u), uint3(1u, 6u, 5u) };
    uint entryCount = 0u;
    uint exitCount = 0u;
    int winding = 0;
    for(uint i = 0u; i < 12u; ++i){
        const uint3 triangle = reversed ? triangles[i].zyx : triangles[i];
        const NwbCsgRayTriangleIntersection hit = nwbCsgRayTriangleIntersection(positions[triangle.x], positions[triangle.y], positions[triangle.z], float3(2.0), float3(-1.0), true);
        if(hit.failure != 0u)
            return false;
        if(!hit.hit || !hit.owned)
            continue;
        winding += hit.windingDelta;
        entryCount += hit.distance == 1.0 && hit.windingDelta == (reversed ? -1 : 1) ? 1u : 0u;
        exitCount += hit.distance == 3.0 && hit.windingDelta == (reversed ? 1 : -1) ? 1u : 0u;
    }
    return winding == 0 && entryCount == 1u && exitCount == 1u;
}

bool nwbCsgRayProbeSpans(const bool cavity){
    NwbCsgRayCrossing crossings[NWB_CSG_RAY_CROSSING_CAPACITY];
    const uint count = cavity ? 4u : 2u;
    for(uint i = 0u; i < count; ++i){
        crossings[i] = (NwbCsgRayCrossing)0;
        crossings[i].distance = asfloat(0x3f800000u + i);
        crossings[i].windingDelta = (i == 0u || i == 2u) ? 1 : -1;
    }
    NwbBvhNode root = (NwbBvhNode)0;
    root.aabbMin = float3(-1.0);
    root.aabbMax = float3(1.0);
    NwbCsgRaySolidIterator iterator;
    if(nwbCsgRayBeginSolidWalk(crossings, count, float3(0.0, 0.0, -2.0), root, iterator) != 0u)
        return false;
    NwbCsgRayBoundary nearBoundary;
    NwbCsgRayBoundary farBoundary;
    if(!nwbCsgRayNextSolidSpan(crossings, count, iterator, nearBoundary, farBoundary) || nearBoundary.distance != crossings[0].distance || farBoundary.distance != crossings[1].distance)
        return false;
    if(cavity && (!nwbCsgRayNextSolidSpan(crossings, count, iterator, nearBoundary, farBoundary) || nearBoundary.distance != crossings[2].distance || farBoundary.distance != crossings[3].distance))
        return false;
    if(nwbCsgRayNextSolidSpan(crossings, count, iterator, nearBoundary, farBoundary))
        return false;
    crossings[0].windingDelta = -1;
    return nwbCsgRayBeginSolidWalk(crossings, count, float3(0.0, 0.0, -2.0), root, iterator) == NWB_CSG_RAY_FAILURE_TOPOLOGY;
}

bool nwbCsgRayProbeWitness(const bool ambiguous){
    NwbCsgRayCrossing crossings[NWB_CSG_RAY_CROSSING_CAPACITY];
    for(uint i = 0u; i < 3u; ++i)
        crossings[i] = (NwbCsgRayCrossing)0;
    crossings[0].distance = 1.0;
    crossings[0].windingDelta = 1;
    crossings[0].normalAmbiguous = ambiguous;
    crossings[1].distance = 1.0;
    crossings[1].normalAmbiguous = ambiguous;
    crossings[2].distance = 2.0;
    crossings[2].windingDelta = -1;
    NwbBvhNode root = (NwbBvhNode)0;
    root.aabbMin = float3(-1.0);
    root.aabbMax = float3(1.0);
    NwbCsgRaySolidIterator iterator;
    if(nwbCsgRayBeginSolidWalk(crossings, 3u, float3(0.0, 0.0, -2.0), root, iterator) != 0u)
        return false;
    NwbCsgRayBoundary nearBoundary;
    NwbCsgRayBoundary farBoundary;
    if(!nwbCsgRayNextSolidSpan(crossings, 3u, iterator, nearBoundary, farBoundary) || nearBoundary.distance != 1.0 || farBoundary.distance != 2.0 || nearBoundary.normalAmbiguous != ambiguous || farBoundary.normalAmbiguous)
        return false;
    return !nwbCsgRayNextSolidSpan(crossings, 3u, iterator, nearBoundary, farBoundary);
}

[numthreads(1, 1, 1)]
void main(){
    uint results[25];
    const float3 origin = float3(0.0, 0.0, -1.0);
    const float3 direction = float3(0.0, 0.0, 1.0);
    results[0] = nwbCsgRayProbePair(float3(1.0, 0.0, -1.0), origin, direction, false);
    results[1] = nwbCsgRayProbePair(float3(1.0, 0.0, 1.0), origin, direction, false);
    results[2] = nwbCsgRayProbePair(float3(-1.0, 0.0, -1.0), origin, direction, true);
    results[3] = nwbCsgRayProbeVertex(false);
    const NwbCsgRayTriangleIntersection outside = nwbCsgRayTriangleIntersection(float3(0.0, -1.0, 0.0), float3(0.0, 1.0, 0.0), float3(-1.0, 0.0, 1.0), float3(0.0000001, 0.0, -1.0), direction, true);
    results[4] = !outside.hit && outside.failure == 0u;
    const NwbCsgRayTriangleIntersection small = nwbCsgRayTriangleIntersection(float3(-1e-20, -1e-20, 0.0), float3(1e-20, -1e-20, 0.0), float3(0.0, 1e-20, 0.0), origin, direction, true);
    results[5] = small.hit && small.owned && small.failure == 0u && small.distance == 1.0;
    const NwbCsgRayTriangleIntersection closedDegenerate = nwbCsgRayTriangleIntersection(float3(0.0), float3(0.0), float3(1.0), origin, direction, true);
    const NwbCsgRayTriangleIntersection openDegenerate = nwbCsgRayTriangleIntersection(float3(0.0), float3(0.0), float3(1.0), origin, direction, false);
    results[6] = closedDegenerate.failure == NWB_CSG_RAY_FAILURE_TOPOLOGY && !openDegenerate.hit && openDegenerate.failure == 0u;
    results[7] = nwbCsgRayProbeSpans(false);
    results[8] = nwbCsgRayProbeSpans(true);
    float entry;
    results[9] = nwbBvhRayConservativeAabb(float3(0.0), float3(1e-9, 0.0, 1.0), 0.0, 2e9, float3(0.999, -1.0, 0.999e9), float3(1.001, 1.0, 1.001e9), entry);
    results[10] = nwbBvhRayConservativeAabb(float3(0.0), direction, 0.0, 2.0, float3(0.0, -1.0, 1.0), float3(1.0, 1.0, 2.0), entry);
    results[11] = !nwbBvhRayConservativeAabb(float3(-1.0, 0.0, 0.0), direction, 0.0, 2.0, float3(0.0, -1.0, 1.0), float3(1.0, 1.0, 2.0), entry);
    results[12] = nwbBvhRayConservativeAabb(float3(0.0), float3(-1e-9, 0.0, -1.0), 0.0, 2e9, float3(-1.001, -1.0, -1.001e9), float3(-0.999, 1.0, -0.999e9), entry);
    results[13] = nwbBvhRayConservativeAabb(float3(0.0), float3(1.0), 0.0, 3.0, float3(1.0, 1.0, 2.0), float3(2.0, 2.0, 3.0), entry);
    const float next = asfloat(0x3f800001u);
    results[14] = nwbCsgRayEdgeSign(float2(1.0, next), float2(next, asfloat(0x3f800002u))) == -1;
    results[15] = nwbCsgRayEdgeSign(float2(next, asfloat(0x3f800002u)), float2(1.0, next)) == 1;
    results[16] = nwbCsgRayEdgeSign(float2(1e-25, 2e-25), float2(2e-25, 4e-25)) == 0;
    results[17] = nwbCsgRayEdgeSign(float2(1e-25, 2e-25), float2(2e-25, 3e-25)) == -1;
    results[18] = nwbCsgRayProbePair(float3(1.0, 0.0, 1.0), float3(0.0, 0.0, 1.0), -direction, false);
    const NwbCsgRayTriangleIntersection nonfinite = nwbCsgRayTriangleIntersection(float3(asfloat(0x7f800000u), 0.0, 0.0), float3(0.0), float3(1.0), origin, direction, true);
    results[19] = nonfinite.failure == NWB_CSG_RAY_FAILURE_TOPOLOGY;
    const float needleA = asfloat(0x3fc00001u);
    const float needleB = asfloat(0x3fc00002u);
    const NwbCsgRayTriangleIntersection needle = nwbCsgRayTriangleIntersection(float3(needleA, needleB, 1.0), float3(-3.0, -3.0, 1.0), float3(needleB, needleA, 1.0), float3(0.0), direction, true);
    results[20] = needle.hit && needle.owned && needle.failure == 0u && needle.distance == 1.0;
    results[21] = nwbCsgRayProbeVertex(true);
    const float huge = asfloat(0x71800000u);
    const float tiny = asfloat(0x0d800000u);
    const NwbCsgRayTriangleIntersection mixed = nwbCsgRayTriangleIntersection(float3(tiny, -tiny, huge), float3(-huge, -huge, 1.0), float3(huge, huge, 1.0), float3(0.0), direction, true);
    results[22] = mixed.hit && mixed.failure == 0u && mixed.distance == 1.0;
    results[23] = nwbCsgRayProbeWitness(false);
    results[24] = nwbCsgRayProbeWitness(true);
    for(uint i = 0u; i < 25u; ++i)
        g_NwbHeapStorageBuffers[g_NwbCsgRayProbe.outputSlot].Store(i * 4u, results[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

