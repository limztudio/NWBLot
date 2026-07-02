// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_reproject_merge_binding_slots.h"
#include "shadow_resolve_binding_slots.h" // NWB_SHADOW_RESOLVE_GROUP_SIZE (the merge shares the resolve's group size)
#include "sw_binding_slots.h"             // NWB_SW_SHADOW_SOFT_FACTOR + NWB_SCENE_SHADOW_SLOT_COUNT (via scene binding_slots)
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Soft OPAQUE shadow TEMPORAL reproject-merge (Stage 3). Runs at HALF resolution, one thread per half-res pixel, per shadow
// slot (the C++ dispatches one slot per call, lightSlotCount == 1), inserted between the soft trace and the a-trous resolve.
//
// WHY REPROJECTION, NOT MOTION VECTORS: the engine rebuilds the view fresh each frame (no prev-G-buffer, no motion vectors).
// So we STASH last frame's worldToClip on the CPU and reproject the CURRENT world position through it: clipPrev =
// prevWorldToClip * float4(Wc,1); uvPrev = ndc*0.5+0.5 (y flipped, matching the caustic splat's exact NDC->UV convention).
// For a STATIC receiver this reprojection is EXACT no matter how the OCCLUDER moved -- so the hard part (a large moving
// occluder must not smear) is NOT a motion problem at all; it is a VALUE-DISAGREEMENT problem, handled by gates B and C.
//
// THE FIVE STEPS (per slot):
//   1. REPROJECT: current world pos (read from the full-res world-position G-buffer at the half pixel's 2x texel, like the
//      geometry downsample) -> uvPrev -> history texel. Off-screen / behind camera / no-history -> n=0 (pure current).
//   2. GATE A (disocclusion / moving RECEIVER): compare current geometry to the PREV geometry cache at the history texel.
//      plane distance (|curr camDist - prev camDist| < k*worldSpacing) AND normal agreement AND on-screen; fail -> reset
//      n=0. Catches moving receivers + camera cuts; a no-op for a static-receiver/moving-occluder (that is gate B).
//   3. GATE B (variance clamp = THE anti-ghost): clamp the reprojected history into mu +/- GAMMA*sigma of the CURRENT 3x3
//      spatial visibility. A large occluder sweeping in drops the local mean; stale-bright history lands outside the band
//      and is clamped hard toward the new dark value -> the ghost collapses in 1-2 frames. Value-disagreement is the signal.
//   4. GATE C (temporal-gradient antilag): a large |lumaCur - lumaHistClamped| scales the effective history length toward 0
//      so a hard swept edge snaps to current instead of lagging.
//   5. BLEND: alpha = max(1/(nEff+1), ALPHA_MIN); out = lerp(histClamped, current, alpha). Write out (accumulated
//      visibility = a-trous input = next-frame history) + the next moments (m1, m2, n+1 clamped).
//
// FALLBACK: historyValid == 0 (first frame / after resize / temporal disabled at the C++ level never dispatches this) forces
// n=0 everywhere, so the output is the pure current sample -- never garbage/black. Invalid receiver texels also pass through
// the current sample. So a first frame degrades exactly to the pre-temporal single-frame trace.

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE, NWB_SHADOW_REPROJECT_MERGE_SET)]]
Texture2DArray<float4> g_NwbShadowMergeSoftTrace;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN, NWB_SHADOW_REPROJECT_MERGE_SET)]]
Texture2DArray<float4> g_NwbShadowMergeHistoryIn;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN, NWB_SHADOW_REPROJECT_MERGE_SET)]]
Texture2DArray<float4> g_NwbShadowMergeMomentsIn;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR, NWB_SHADOW_REPROJECT_MERGE_SET)]]
Texture2D<float4> g_NwbShadowMergeGeometryCurr;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV, NWB_SHADOW_REPROJECT_MERGE_SET)]]
Texture2D<float4> g_NwbShadowMergeGeometryPrev;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS, NWB_SHADOW_REPROJECT_MERGE_SET)]]
Texture2D<float4> g_NwbShadowMergeWorldPosition;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT, NWB_SHADOW_REPROJECT_MERGE_SET)]]
RWTexture2DArray<float4> g_NwbShadowMergeHistoryOut;

[[vk::binding(NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT, NWB_SHADOW_REPROJECT_MERGE_SET)]]
RWTexture2DArray<float4> g_NwbShadowMergeMomentsOut;

struct NwbShadowReprojectMergePushConstants{
    // The STASHED previous-frame worldToClip. row_major to match the C++ Float44U row-major raw dump (and the caustic
    // view CB convention). 64 bytes; the 8 u32 scalars below bring the push to 96 bytes (within the 128-byte guarantee).
    row_major float4x4 prevWorldToClip;
    uint width;          // FULL-res width  (to read the world-position G-buffer at the 2x texel)
    uint height;         // FULL-res height
    uint halfWidth;      // HALF-res width  (dispatch / output dim)
    uint halfHeight;     // HALF-res height
    uint lightSlotStart; // first active SOFT shadow slot to process
    uint lightSlotCount; // number of contiguous active SOFT slots (the C++ dispatches one slot per call -> 1)
    uint historyValid;   // 0 = no valid history this frame (first frame / after resize) -> force n=0 (pure current)
    uint pad0;           // pad to a 16-byte multiple (8 u32 = 32 bytes after the 64-byte matrix -> 96 total)
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowReprojectMergePushConstants> g_NwbShadowMergePush;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Rec.709 luma of a (possibly colored transmittance) visibility triple -- the scalar the moments + gates B/C operate on.
float nwbShadowMergeLuma(const float3 color){
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Camera-distance-proportional world spacing, matching the a-trous resolve's edge-stop metric (camDist * scale, clamped
// away from zero). Gate A scales its plane tolerance by this so it tracks camera distance exactly like the wavelet does.
float nwbShadowTemporalWorldSpacing(const float cameraDistance){
    return max(cameraDistance * NWB_SHADOW_TEMPORAL_WORLD_SPACING_SCALE, NWB_SHADOW_TEMPORAL_WORLD_SPACING_MIN);
}

// Half-res geometry cache accessors (see shadow_geometry_downsample_cs): .xy = octahedral normal, .z = camera distance,
// .w = validity.
float4 nwbShadowMergeGeomCurr(const int2 halfTexel){
    return g_NwbShadowMergeGeometryCurr.Load(int3(halfTexel, 0));
}


[numthreads(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 h = dispatchThreadID.xy;
    const uint halfWidth = g_NwbShadowMergePush.halfWidth;
    const uint halfHeight = g_NwbShadowMergePush.halfHeight;
    if(h.x >= halfWidth || h.y >= halfHeight)
        return;

    const int hw = (int)halfWidth;
    const int hh = (int)halfHeight;
    const int2 texel = int2(h);

    const uint slotStart = g_NwbShadowMergePush.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbShadowMergePush.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    // This half pixel's receiver geometry (curr).
    const float4 gc = nwbShadowMergeGeomCurr(texel);
    const bool valid = gc.w > 0.5;
    const float3 Nc = nwbOctDecodeNormal(gc.xy);

    // Full-res world position at the block's top-left 2x texel (clamped), matching the geometry downsample's 2x sample.
    const int2 full = int2(
        min((int)h.x * NWB_SW_SHADOW_SOFT_FACTOR, (int)g_NwbShadowMergePush.width - 1),
        min((int)h.y * NWB_SW_SHADOW_SOFT_FACTOR, (int)g_NwbShadowMergePush.height - 1)
    );
    const float3 Wc = g_NwbShadowMergeWorldPosition.Load(int3(full, 0)).xyz;

    // STEP 1 REPROJECT: project the current world pos through the STASHED previous-frame worldToClip. This is EXACT for a
    // static receiver regardless of how the occluder moved (NDC->UV: y flipped, matching the caustic splat convention).
    const float4 clipPrev = mul(g_NwbShadowMergePush.prevWorldToClip, float4(Wc, 1.0));
    bool onScreen = false;
    int2 hp = int2(0, 0);
    if(clipPrev.w > 0.0 && g_NwbShadowMergePush.historyValid != 0u){
        const float3 ndc = clipPrev.xyz / clipPrev.w;
        const float2 uvPrev = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if(uvPrev.x >= 0.0 && uvPrev.x <= 1.0 && uvPrev.y >= 0.0 && uvPrev.y <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0){
            const float2 histTexelF = uvPrev * float2((float)halfWidth, (float)halfHeight) - 0.5;
            hp = int2(clamp((int)round(histTexelF.x), 0, hw - 1), clamp((int)round(histTexelF.y), 0, hh - 1));
            onScreen = true;
        }
    }

    // STEP 2 GATE A (disocclusion / moving RECEIVER): the reprojected history is trustworthy only if the receiver at the
    // history texel matches this one -- same plane (camera-distance proxy within k*worldSpacing) AND same orientation. A
    // moving receiver / camera cut fails this and resets to the pure current sample. (A moving OCCLUDER over a STATIC
    // receiver PASSES here by construction -- that is precisely why it is handled by the value gate B, not this one.)
    bool historyAccepted = false;
    if(onScreen && valid){
        const float4 gp = g_NwbShadowMergeGeometryPrev.Load(int3(hp, 0));
        if(gp.w > 0.5){
            const float3 Np = nwbOctDecodeNormal(gp.xy);
            const bool planeOk = abs(gc.z - gp.z) < (NWB_SHADOW_TEMPORAL_PLANE_K * nwbShadowTemporalWorldSpacing(gc.z));
            const bool normalOk = dot(Nc, Np) > NWB_SHADOW_TEMPORAL_NORMAL_MIN;
            historyAccepted = planeOk && normalOk;
        }
    }

    for(uint slot = slotStart; slot < slotEnd; ++slot){
        // The current raw sample for this slot.
        const float3 cur = g_NwbShadowMergeSoftTrace.Load(int4(texel, slot, 0)).rgb;

        // A background/invalid receiver has no meaningful shadow to accumulate -- pass the current sample straight through
        // and reset the moments with n=0 (so a receiver that appears next frame starts fresh, not from a stale history).
        // The written (m1,m2) here are of the current sample luma; because n=0 the resolve treats this texel as low-history
        // and takes the SPATIAL variance fallback, so this degenerate m2-m1^2==0 is never consumed as "converged variance".
        if(!valid){
            g_NwbShadowMergeHistoryOut[uint3(h, slot)] = float4(cur, 1.0);
            const float lumaCur0 = nwbShadowMergeLuma(cur);
            g_NwbShadowMergeMomentsOut[uint3(h, slot)] = float4(lumaCur0, lumaCur0 * lumaCur0, 0.0, 0.0);
            continue;
        }

        // Reprojected history + its moments (only when gate A accepted; else pure current, n=0 -> the moment integration
        // below degenerates to alpha=1, i.e. the current-sample moments, which is exactly right for a reset/disoccluded texel).
        float3 hist = cur;
        float n = 0.0;
        float m1Prev = 0.0; // prev integrated luma mean       (moments.x); stays 0 when history rejected -> pure-current
        float m2Prev = 0.0; // prev integrated luma 2nd-moment (moments.y); stays 0 when history rejected -> pure-current
        if(historyAccepted){
            hist = g_NwbShadowMergeHistoryIn.Load(int4(hp, slot, 0)).rgb;
            const float3 mom = g_NwbShadowMergeMomentsIn.Load(int4(hp, slot, 0)).xyz;
            m1Prev = mom.x;
            m2Prev = mom.y;
            n = mom.z;
        }

        // STEP 3 GATE B (variance clamp): mean + stddev of the CURRENT trace over the 3x3 half-res neighborhood (valid
        // receiver taps only). histClamped = clamp(hist, mu - GAMMA*sigma, mu + GAMMA*sigma) per channel. THE anti-ghost:
        // stale-bright history over a newly-shadowed texel lands outside the band and snaps to the dark current mean.
        float3 mu = float3(0.0, 0.0, 0.0);
        float3 mu2 = float3(0.0, 0.0, 0.0);
        int cnt = 0;
        [unroll] for(int dy = -1; dy <= 1; ++dy){
            [unroll] for(int dx = -1; dx <= 1; ++dx){
                const int2 t = int2(clamp(texel.x + dx, 0, hw - 1), clamp(texel.y + dy, 0, hh - 1));
                if(g_NwbShadowMergeGeometryCurr.Load(int3(t, 0)).w <= 0.5)
                    continue; // background tap -> no receiver, skip
                const float3 s = g_NwbShadowMergeSoftTrace.Load(int4(t, slot, 0)).rgb;
                mu += s;
                mu2 += s * s;
                ++cnt;
            }
        }
        float3 histClamped = hist;
        if(cnt > 0){
            const float invCount = 1.0 / (float)cnt;
            mu *= invCount;
            const float3 variance = max(mu2 * invCount - mu * mu, float3(0.0, 0.0, 0.0));
            const float3 sigma = sqrt(variance);
            const float3 lo = mu - NWB_SHADOW_TEMPORAL_GAMMA * sigma;
            const float3 hi = mu + NWB_SHADOW_TEMPORAL_GAMMA * sigma;
            histClamped = clamp(hist, lo, hi);
        }

        // STEP 4 GATE C (temporal-gradient antilag): a large per-frame luma gradient (a hard swept shadow edge) shrinks the
        // effective history length so the blend snaps to current. saturate keeps nEff in [0, n].
        const float lumaCur = nwbShadowMergeLuma(cur);
        const float lumaHist = nwbShadowMergeLuma(histClamped);
        const float tempGrad = abs(lumaCur - lumaHist) / max(max(lumaCur, lumaHist), 1e-3);
        const float nEff = n * saturate(1.0 - NWB_SHADOW_TEMPORAL_ANTILAG * tempGrad);

        // STEP 5 BLEND: alpha (weight of the current sample) is 1/(nEff+1) capped from below by ALPHA_MIN, so the temporal
        // lag never exceeds ~1/ALPHA_MIN frames (the global anti-ghost backstop). out is the accumulated visibility.
        const float alpha = max(1.0 / (nEff + 1.0), NWB_SHADOW_TEMPORAL_ALPHA_MIN);
        const float3 outVis = lerp(histClamped, cur, alpha);
        const float nNext = min(nEff + 1.0, NWB_SHADOW_TEMPORAL_MAX_HISTORY);

        // Temporally integrate the CURRENT sample's luma moments with the SAME alpha the colour used, so (m2 - m1^2) is a
        // REAL running variance of the noisy per-frame signal (SVGF), NOT the 0 the old (lumaOut, lumaOut^2) self-pair
        // produced (m2 - m1^2 == 0 identically). lumaCur is the current raw-trace luma computed at gate C above; the
        // integration input is that noisy per-frame value, NOT the blended outVis. When history was rejected (m1Prev =
        // m2Prev = 0, n = 0 -> nEff = 0 -> alpha = 1) this collapses to the pure current-sample moments -- a clean reset.
        const float m1 = lerp(m1Prev, lumaCur, alpha);
        const float m2 = lerp(m2Prev, lumaCur * lumaCur, alpha);

        g_NwbShadowMergeHistoryOut[uint3(h, slot)] = float4(outVis, 1.0);
        g_NwbShadowMergeMomentsOut[uint3(h, slot)] = float4(m1, m2, nNext, 0.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
