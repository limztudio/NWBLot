// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel trace: ONE workgroup per surfel, 64 threads = 64 hemisphere rays. Each ray closest-hits the SW BVH (reusing
// gi_sw_trace.slangi verbatim) and Lambert-shades the hit with the per-instance base colour, so a red wall bounces
// red. The 64 shaded radiances reduce in GROUPSHARED (one workgroup owns one surfel -> zero cross-workgroup
// contention -> NO float atomics) into the cosine-weighted hemispherical irradiance, then accumulate into the surfel's
// meanIrradiance in place as a BOUNDED RUNNING MEAN (weight 1/(n+1) until n hits NWB_SURFEL_MAX_ACCUM, then a bounded
// EMA). A true running mean -- not a fixed-alpha EMA -- is required because each frame rotates the ray set, so the
// estimates are decorrelated Monte-Carlo samples: the running mean drives variance -> 0 (a fixed EMA leaves a
// permanent noise residual = flicker). n==0 on the first trace seeds directly (no dark bias). Round-robin: 1/Nth of
// surfels per frame (divisor), ALL on the bootstrap frame (divisor 1). See .helper/surfel_gi_plan.md (Ray budget).


#include "../gi_sw_trace.slangi"     // BVH bindings (0-10) + nwbGiSwTraceClosest / nwbGiSwShadeHit
#include "../sampling.slangi"        // nwbGiFibonacciDirection + nwbGiFrameRotation
#include "surfel_record.slangi"
#include "surfel_constants.slangi"
#include "surfel_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_BINDING_CONSTANTS, NWB_SURFEL_SET)]]
ConstantBuffer<NwbSurfelConstants> g_NwbSurfelConstants;

[[vk::binding(NWB_SURFEL_BINDING_POOL, NWB_SURFEL_SET)]]
RWStructuredBuffer<NwbSurfel> g_NwbSurfelPool;


groupshared float3 s_surfelRadiance[NWB_SURFEL_RAYS_PER_SURFEL];
groupshared float s_surfelWeight[NWB_SURFEL_RAYS_PER_SURFEL];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_RAYS_PER_SURFEL, 1, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID){
    const uint rayIndex = groupThreadID.x;
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    const uint frameIndex = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.z);
    const uint updateDivisor = max(uint(g_NwbSurfelConstants.hashPoolFrameDivisor.w), 1u);

    // Round-robin: this workgroup owns surfel (cursor + groupID.x*divisor). Divisor is 1 on the bootstrap frame, so
    // every surfel is traced + seeded in one frame.
    const uint cursor = frameIndex % updateDivisor;
    const uint surfelIndex = cursor + groupID.x * updateDivisor;

    s_surfelRadiance[rayIndex] = float3(0.0, 0.0, 0.0);
    s_surfelWeight[rayIndex] = 0.0;

    bool active = surfelIndex < poolCapacity;
    NwbSurfel surfel;
    if(active){
        surfel = g_NwbSurfelPool[surfelIndex];
        active = surfel.alive != 0u;
    }

    if(active){
        const float normalBias = g_NwbSurfelConstants.coverageRadiusBiasHyst.z;
        const float2 rotation = nwbGiFrameRotation(frameIndex);

        // Fibonacci direction on the sphere in a canonical frame (z = spiral axis); fold to the upper hemisphere and
        // rotate into the surfel-normal TBN so every ray leaves the surface.
        const float3 n = surfel.normal;
        const float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        const float3 tangent = normalize(cross(up, n));
        const float3 bitangent = cross(n, tangent);
        float3 dirLocal = nwbGiFibonacciDirection(rayIndex, NWB_SURFEL_RAYS_PER_SURFEL, rotation);
        dirLocal.z = abs(dirLocal.z);
        const float3 rayDir = normalize(dirLocal.x * tangent + dirLocal.y * bitangent + dirLocal.z * n);
        const float weight = max(dot(n, rayDir), 0.0);

        const float3 origin = surfel.position + n * normalBias;
        const NwbGiProbeHit hit = nwbGiSwTraceClosest(origin, rayDir, 0.001, 1.0e4);
        float3 radiance = float3(0.0, 0.0, 0.0);
        if(hit.hit)
            radiance = nwbGiSwShadeHit(hit, rayDir);   // miss = sky = black (U0; multi-bounce lands in U4)

        s_surfelRadiance[rayIndex] = radiance * weight;
        s_surfelWeight[rayIndex] = weight;
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 reduces the 64 cosine-weighted samples -> hemispherical irradiance -> running-mean into the surfel.
    if(rayIndex == 0u && active){
        float3 weightedRadianceSum = float3(0.0, 0.0, 0.0);
        float weightSum = 0.0;
        [unroll]
        for(uint i = 0u; i < NWB_SURFEL_RAYS_PER_SURFEL; ++i){
            weightedRadianceSum += s_surfelRadiance[i];
            weightSum += s_surfelWeight[i];
        }
        const float3 irradiance = weightSum > 1e-4 ? (weightedRadianceSum / weightSum) : float3(0.0, 0.0, 0.0);

        // Bounded running mean. Each frame rotates the ray set (nwbGiFrameRotation), so successive estimates are
        // DECORRELATED Monte-Carlo samples of the same hemispherical integral. Blend as a true incremental average --
        // weight 1/(n+1) on the new sample -- so the variance falls like 1/n toward zero (a fixed-alpha EMA would leave
        // a permanent ~sqrt(alpha) residual = the corner flicker). n is capped at coverageRadiusBiasHyst.w so past the
        // cap it becomes a bounded EMA (weight 1/(cap+1)), keeping a finite memory for future dynamic lighting.
        // sampleCount here is the count BEFORE this sample (incremented below), so n==0 on the first trace -> weight 1
        // (seed directly from the first estimate, no dark bias -- this subsumes the old sampleCount==0 special case).
        const uint accumCap = max(uint(g_NwbSurfelConstants.coverageRadiusBiasHyst.w), 1u);
        const uint n = min(surfel.sampleCount, accumCap);
        const float historyWeight = float(n) / float(n + 1u);
        surfel.meanIrradiance = lerp(irradiance, surfel.meanIrradiance, historyWeight);
        surfel.sampleCount = min(surfel.sampleCount + 1u, 0xFFFFu);
        g_NwbSurfelPool[surfelIndex] = surfel;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
