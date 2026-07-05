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
#include "surfel_sh.slangi"          // nwbSurfelShBasis / nwbSurfelShProjectionScale (U3 directional irradiance)
#include "surfel_hash.slangi"        // nwbSurfelCellCoord / nwbSurfelHashCell (U4 bounce gather over the snapshot)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_BINDING_CONSTANTS, NWB_SURFEL_SET)]]
ConstantBuffer<NwbSurfelConstants> g_NwbSurfelConstants;

[[vk::binding(NWB_SURFEL_BINDING_POOL, NWB_SURFEL_SET)]]
RWStructuredBuffer<NwbSurfel> g_NwbSurfelPool;

// Snapshot of the PREVIOUS frame's converged field (U4 bounce): the bounce gather reads THESE (SRVs), never the live
// pool this frame's trace is writing -> the surfel->surfel feedback reads a stable frame-start field, no torn read.
[[vk::binding(NWB_SURFEL_BINDING_SNAPSHOT_POOL, NWB_SURFEL_SET)]]
StructuredBuffer<NwbSurfel> g_NwbSurfelSnapshotPool;

[[vk::binding(NWB_SURFEL_BINDING_SNAPSHOT_CELL_HEAD, NWB_SURFEL_SET)]]
StructuredBuffer<uint> g_NwbSurfelSnapshotCellHead;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// U4 infinite bounce: gather the surfel field's INDIRECT irradiance arriving at a ray HIT point, from the previous
// frame's snapshot. Mirrors nwbSurfelGather (surfel_gather.slangi) but reads the snapshot buffers + the trace's own
// constants CB, and is queried with the FACE-FORWARDED hit normal (a back-face wall hit must not reject every neighbour
// + sample the opposite hemisphere). Adding hit.albedo * this to each ray's radiance makes the surfel integrate
// multi-bounce -> the running mean converges to the infinite-bounce solution (Jacobi radiosity; stable at albedo < 1).
float3 nwbSurfelBounceGather(const float3 worldPosition, const float3 queryNormal){
    const float cellSize = g_NwbSurfelConstants.cameraPositionCellSize.w;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    const float normalBias = g_NwbSurfelConstants.coverageRadiusBiasHyst.z;
    const float3 biasedPosition = worldPosition + queryNormal * normalBias;

    float3 irradianceSum = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    const int3 baseCell = nwbSurfelCellCoord(biasedPosition, cellSize);
    for(int dz = -1; dz <= 1; ++dz)
    for(int dy = -1; dy <= 1; ++dy)
    for(int dx = -1; dx <= 1; ++dx){
        const uint bucket = nwbSurfelHashCell(baseCell + int3(dx, dy, dz), cellCount);
        uint id = g_NwbSurfelSnapshotCellHead[bucket];
        uint walk = 0u;
        [loop] while(id < poolCapacity && walk < NWB_SURFEL_MAX_WALK){
            const NwbSurfel surfel = g_NwbSurfelSnapshotPool[id];
            if(surfel.alive != 0u && surfel.sampleCount != 0u){
                const float dist = length(biasedPosition - surfel.position);
                const float normalWeight = saturate(dot(queryNormal, surfel.normal));
                const float distanceWeight = saturate(1.0 - dist / max(surfel.radius, 1e-4));
                const float confidence = saturate(float(surfel.sampleCount) / 8.0);
                const float weight = normalWeight * distanceWeight * confidence;
                irradianceSum += nwbSurfelShReconstruct(surfel.shR, surfel.shG, surfel.shB, queryNormal) * weight;
                totalWeight += weight;
            }
            id = surfel.nextInCell;
            ++walk;
        }
    }
    return totalWeight > 1e-4 ? (irradianceSum / totalWeight) : float3(0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-ray 2-band SH projection of the incoming radiance (one float4 of coefficients per colour). Reduced by thread 0
// into the surfel's running-mean SH. No cosine weight here -- the SH stores RAW radiance; the gather's reconstruction
// applies the cosine-lobe convolution (surfel_sh.slangi).
groupshared float4 s_surfelShR[NWB_SURFEL_RAYS_PER_SURFEL];
groupshared float4 s_surfelShG[NWB_SURFEL_RAYS_PER_SURFEL];
groupshared float4 s_surfelShB[NWB_SURFEL_RAYS_PER_SURFEL];


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

    s_surfelShR[rayIndex] = float4(0.0, 0.0, 0.0, 0.0);
    s_surfelShG[rayIndex] = float4(0.0, 0.0, 0.0, 0.0);
    s_surfelShB[rayIndex] = float4(0.0, 0.0, 0.0, 0.0);

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
        dirLocal.z = abs(dirLocal.z);   // fold to the upper hemisphere -> every ray leaves the surface (pdf 1/(2 pi))
        const float3 rayDir = normalize(dirLocal.x * tangent + dirLocal.y * bitangent + dirLocal.z * n);

        const float3 origin = surfel.position + n * normalBias;
        const NwbGiProbeHit hit = nwbGiSwTraceClosest(origin, rayDir, 0.001, 1.0e4);
        float3 radiance = float3(0.0, 0.0, 0.0);
        if(hit.hit){
            radiance = nwbGiSwShadeHit(hit, rayDir);   // reflected DIRECT = albedo * E_direct (miss = sky = black)
            // U4 infinite bounce: add the hit surface's reflected INDIRECT -- the PREVIOUS frame's surfel field arriving
            // at the hit, reconstructed at the FACE-FORWARDED normal (matching nwbGiSwShadeHit; a back-face wall hit must
            // not reject every neighbour) and clamped (bounds the SH-rectification energy injection). radiance now =
            // albedo * (E_direct + E_indirect), so the running mean integrates multi-bounce -> converges (albedo 0.8 < 1).
            const float3 facingNormal = dot(hit.normal, rayDir) < 0.0 ? hit.normal : -hit.normal;
            const float3 bounce = min(nwbSurfelBounceGather(hit.worldPos, facingNormal), float3(NWB_SURFEL_BOUNCE_CLAMP, NWB_SURFEL_BOUNCE_CLAMP, NWB_SURFEL_BOUNCE_CLAMP));
            radiance += hit.albedo * bounce;
        }

        // Project RAW radiance into 2-band SH (no cosine weight -- the reconstruction convolves the cosine lobe). Each
        // ray contributes radiance * SH-basis(rayDir); thread 0 sums + scales by 2*pi/N (the hemisphere pdf).
        const float4 basis = nwbSurfelShBasis(rayDir);
        s_surfelShR[rayIndex] = radiance.r * basis;
        s_surfelShG[rayIndex] = radiance.g * basis;
        s_surfelShB[rayIndex] = radiance.b * basis;
    }

    GroupMemoryBarrierWithGroupSync();

    // Thread 0 reduces the 64 per-ray SH projections -> this frame's directional-irradiance SH estimate -> running-mean
    // into the surfel's SH.
    if(rayIndex == 0u && active){
        float4 sumR = float4(0.0, 0.0, 0.0, 0.0);
        float4 sumG = float4(0.0, 0.0, 0.0, 0.0);
        float4 sumB = float4(0.0, 0.0, 0.0, 0.0);
        [unroll]
        for(uint i = 0u; i < NWB_SURFEL_RAYS_PER_SURFEL; ++i){
            sumR += s_surfelShR[i];
            sumG += s_surfelShG[i];
            sumB += s_surfelShB[i];
        }
        const float projectionScale = nwbSurfelShProjectionScale(NWB_SURFEL_RAYS_PER_SURFEL);   // 2*pi / N
        const float4 frameShR = sumR * projectionScale;
        const float4 frameShG = sumG * projectionScale;
        const float4 frameShB = sumB * projectionScale;

        // Bounded running mean over the SH coefficients (linear -> averaging coefficients averages the field). Each frame
        // rotates the ray set (nwbGiFrameRotation), so successive estimates are DECORRELATED Monte-Carlo samples of the
        // same projection integral. Blend as a true incremental average -- weight 1/(n+1) on the new sample -- so the
        // variance falls like 1/n toward zero (a fixed-alpha EMA would leave a permanent ~sqrt(alpha) residual = flicker).
        // n is capped at coverageRadiusBiasHyst.w so past the cap it becomes a bounded EMA. sampleCount here is the count
        // BEFORE this sample (incremented below), so n==0 on the first trace -> weight 1 (seed directly, no dark bias).
        const uint accumCap = max(uint(g_NwbSurfelConstants.coverageRadiusBiasHyst.w), 1u);
        const uint n = min(surfel.sampleCount, accumCap);
        const float historyWeight = float(n) / float(n + 1u);
        surfel.shR = lerp(frameShR, surfel.shR, historyWeight);
        surfel.shG = lerp(frameShG, surfel.shG, historyWeight);
        surfel.shB = lerp(frameShB, surfel.shB, historyWeight);
        surfel.sampleCount = min(surfel.sampleCount + 1u, 0xFFFFu);
        g_NwbSurfelPool[surfelIndex] = surfel;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
