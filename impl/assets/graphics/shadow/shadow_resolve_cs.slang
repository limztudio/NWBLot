#include "shadow_resolve_binding_slots.h"
#include "sw_binding_slots.h" // NWB_SW_SHADOW_SOFT_FACTOR + NWB_SCENE_SHADOW_SLOT_COUNT reach (via scene binding_slots)
#include "../scene/binding_slots.h"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A-trous WAVELET channel count: 1 = SCALAR (soft OPAQUE grayscale visibility -- the default), 3 = RGB (soft
// COLORED TRANSPARENT transmittance). The RGB pipeline is cooked from shadow_resolve_rgb_cs.slang, which #defines this =3
// then #includes THIS file. Only the wavelet's accumulator vector type + the tap read/write channel count depend on it;
// everything else (kernel, geometry edge-stops, LDS layout, PREPARE/UPSAMPLE which are already .rgb) is shared. Scalar
// stays exactly 1x ALU + 1x LDS so the common opaque path is never charged for a signal that is identically gray.
#ifndef NWB_SHADOW_RESOLVE_CHANNELS
#define NWB_SHADOW_RESOLVE_CHANNELS NWB_SHADOW_RESOLVE_CHANNELS_SCALAR
#endif

#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    typedef half   NwbShadowResolveVis;      // per-channel LDS storage element (kept half to match the scalar tile budget)
    typedef half3  NwbShadowResolveVisVec;   // the wavelet accumulator / tap value type
    typedef float3 NwbShadowResolveVisFVec;  // the float working type inside the wavelet sums
#else
    typedef half   NwbShadowResolveVis;
    typedef half   NwbShadowResolveVisVec;
    typedef float  NwbShadowResolveVisFVec;
#endif


// Soft shadow RESOLVE = an EDGE-AVOIDING A-TROUS WAVELET denoise (Dammertz et al.), run at HALF resolution as N
// ping-pong compute passes, bracketed by the shadow geometry downsample pre-pass + an edge-aware upsample. Shadow-specific
// signal rules:
//   (a) SIGNAL: a per-slot 0..1 VISIBILITY, not irradiance -- so ALL the caustic area-Jacobian / photon-density /
//       exposure math is dropped. PREPARE just COPIES the half-res traced visibility.
//   (b) MULTIPLICATIVE IDENTITY = 1.0 (fully LIT): background/invalid taps and the all-taps-rejected sentinel write
//       1.0, never 0.0 (the caustic's ADDITIVE identity -- 0.0 here would leak black shadow into the penumbra).
//   (c) EDGE-STOP = world-distance (camera distance, like the caustic's world spacing) PLUS a NORMAL term (dot of the
//       geometry-cache normals), so the penumbra never smooths across a depth discontinuity OR an orientation crease.
//
// PER-SLOT: the half-res visibility is a Texture2DArray, one layer per shadow slot. The push carries the active
// SOFT slot range [lightSlotStart, lightSlotStart+lightSlotCount); every stage loops that range so a scene with several
// soft-shadowed lights (directional, point, or spot) denoises each layer in one dispatch. Slots not in the soft set are
// left untouched (their hard shadow, written elsewhere, survives -- this pass only OVERWRITES the soft slots it upsamples).
//
// HALF-RES PIPELINE (3 stages, selected by pushConstants.stage):
//   stage 0 PREPARE (half-res dispatch): per slot, copy the half-res traced visibility (SOFT_HALF) into the ping-pong
//     buffer the wavelet reads. Background/invalid -> 1.0 (the LIT identity).
//   stage 1 WAVELET (half-res dispatch): per slot, edge-avoiding a-trous step at dilation stepWidth (1,2,4,8,16 half-
//     res texels), edge-stopped by the geometry cache's camera-distance + normal. LDS-tiled for stepWidth <= LDS_MAX.
//   stage 2 UPSAMPLE (full-res dispatch): per slot, edge-aware (joint-bilateral) resample of the final half-res
//     visibility into the FULL-res visibility buffer the deferred lighting samples. Background pixels write 1.0.

[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF, NWB_SHADOW_RESOLVE_SET)]]
Texture2DArray<float4> g_NwbShadowResolveSoftHalf;

[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_GEOMETRY, NWB_SHADOW_RESOLVE_SET)]]
Texture2D<float4> g_NwbShadowResolveGeometry;

[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH, NWB_SHADOW_RESOLVE_SET)]]
Texture2D<float4> g_NwbShadowResolveGBufferDepth;

// The pass output (UAV). For PREPARE + WAVELET this is a HALF-res ping-pong layer (soft-A / soft-B); the C++ swaps the
// bound texture per pass. (Only the UPSAMPLE writes the full-res visibility below, so this stays half-res here.)
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_OUTPUT, NWB_SHADOW_RESOLVE_SET)]]
RWTexture2DArray<float4> g_NwbShadowResolveOutput;

// The input color (SRV): for WAVELET this is the previous pass's HALF-res layer (the OTHER ping-pong buffer); for
// UPSAMPLE it is the final half-res visibility. PREPARE reads SOFT_HALF instead but this is always bound.
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR, NWB_SHADOW_RESOLVE_SET)]]
Texture2DArray<float4> g_NwbShadowResolveInputColor;

// The FULL-res visibility Texture2DArray the deferred lighting samples -- the UPSAMPLE stage's output.
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_VISIBILITY, NWB_SHADOW_RESOLVE_SET)]]
RWTexture2DArray<float4> g_NwbShadowResolveVisibility;

// TEMPORAL moments SRV (.x = m1, .y = m2, .z = history length n) from the reproject-merge. Read by the WAVELET stage's
// variance-coupled luminance edge-stop, but ONLY when g_NwbShadowResolvePushConstants.momentsValid == 1; otherwise this
// is a valid-but-unused dummy (never sampled -- the shader takes the spatial-variance fallback). See binding-slot doc.
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_MOMENTS, NWB_SHADOW_RESOLVE_SET)]]
Texture2DArray<float4> g_NwbShadowResolveMoments;

// FULL-res G-buffer world position + normal SRVs -- the UPSAMPLE stage's joint-bilateral CENTRE (see binding-slot doc).
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS, NWB_SHADOW_RESOLVE_SET)]]
Texture2D<float4> g_NwbShadowResolveGBufferWorldPos;
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL, NWB_SHADOW_RESOLVE_SET)]]
Texture2D<float4> g_NwbShadowResolveGBufferNormal;

// Scene-shading CB: only the camera world position is needed (the full-res upsample centre's camera distance). Declared
// directly (not via scene/buffer.slangi) for the same reason the geometry downsample does -- to layout-match the C++
// binding set exactly (which binds only this CB) without depending on dead-code-stripping the unused light-list SRV.
struct NwbShadowResolveSceneShading{
    float4 cameraPosition; // xyz = camera world position (w = active light count, unused here)
};
[[vk::binding(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, NWB_SHADOW_RESOLVE_SET)]]
ConstantBuffer<NwbShadowResolveSceneShading, Std140DataLayout> g_NwbShadowResolveSceneShading;

struct NwbShadowResolvePushConstants{
    uint width;          // FULL-res width (UPSAMPLE dispatch/output dim)
    uint height;         // FULL-res height
    uint halfWidth;      // HALF-res width (PREPARE/WAVELET dispatch dim)
    uint halfHeight;     // HALF-res height
    uint stepWidth;      // a-trous dilation for this wavelet pass (1,2,4,8,16), in HALF-res texels
    uint stage;          // 0 = PREPARE, 1 = WAVELET (half-res), 2 = UPSAMPLE (-> full-res visibility)
    uint lightSlotStart; // first active SOFT shadow slot to process (any light type)
    uint lightSlotCount; // number of contiguous active SOFT slots to process
    uint momentsValid;   // 1 = the MOMENTS SRV holds this-frame integrated temporal moments (the merge ran) -> the wavelet
                         // may use the temporal variance where n is high; 0 = temporal off / first frame -> the wavelet
                         // NEVER samples the (dummy) MOMENTS SRV and uses the spatial-variance fallback everywhere.
    uint upsampleFold;   // UPSAMPLE fold mode: 0 = OVERWRITE the full-res visibility (soft OPAQUE);
                         // 1 = MULTIPLY the denoised result onto the visibility (soft COLORED TRANSPARENT fold-multiply,
                         // read-modify-write). Background pixels skip the write entirely when fold==1 (leaving the opaque
                         // result untouched -- white would clobber it). Ignored by PREPARE/WAVELET. See the UPSAMPLE stage.
};

[[vk::push_constant]]
ConstantBuffer<NwbShadowResolvePushConstants> g_NwbShadowResolvePushConstants;

// Groupshared (LDS) tile for the small-dilation wavelet passes: the group cooperatively loads its half-res tile + halo
// (for ONE slot at a time) here once, then every thread taps from LDS. Sized for the largest LDS-tiled step
// (24x24 = 576 texels). Reloaded per slot inside the wavelet's slot loop (a barrier brackets each reload).
groupshared NwbShadowResolveVisVec s_NwbShadowResolveTileVis[NWB_SHADOW_RESOLVE_TILE_TEXELS];  // the visibility being smoothed (scalar for opaque, RGB for colored transparent)
groupshared half2  s_NwbShadowResolveTileNormal[NWB_SHADOW_RESOLVE_TILE_TEXELS]; // octahedral normal
groupshared float  s_NwbShadowResolveTileDist[NWB_SHADOW_RESOLVE_TILE_TEXELS];  // camera distance
groupshared uint   s_NwbShadowResolveTileValid[NWB_SHADOW_RESOLVE_TILE_TEXELS]; // 1 = receiver


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Half-res geometry cache accessors (see shadow_geometry_downsample_cs): .xy = octahedral normal, .z = camera
// distance, .w = validity.
float4 nwbShadowResolveGeomAtHalf(const int2 halfTexel){
    return g_NwbShadowResolveGeometry.Load(int3(halfTexel, 0));
}
bool nwbShadowResolveValidAtHalf(const int2 halfTexel){
    return g_NwbShadowResolveGeometry.Load(int3(halfTexel, 0)).w > 0.5;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// 5x5 B-spline a-trous kernel (separable [1,4,6,4,1]/16). Edge-AVOIDING: each tap is weighted by the spatial kernel
// AND a camera-distance edge-stop AND a normal edge-stop, so the single-sample penumbra smooths into a gradient
// without bleeding across a receiver depth/silhouette discontinuity or an orientation crease.
static const float s_NwbShadowResolveKernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

// Normal edge-stop exponent: taps whose receiver normal diverges from the centre's fall off as pow(max(dot,0), N). This is
// deliberately gentle: the receiver normal is a HALF-res point-sampled cache, and on a smoothly
// CURVING receiver (a character torso/hip) adjacent half-res texels legitimately differ by ~10-20deg while the far a-trous
// taps (dilation up to 16) diverge much more. A pow-32 stop (half-weight at ~11deg) treated that smooth curvature as an
// orientation crease and starved every reconstruction stage (wavelet + upsample) down to 1-2 near-parallel taps, so the
// residual per-frame trace noise was never averaged -> the clustered self-shadow grain. pow-8 (half-weight at ~45deg)
// restores the tap support needed to average that noise on the curve, while a REAL discontinuity is still rejected by the
// camera-DISTANCE stop (a silhouette/crease almost always carries a depth jump) and the shadow terminator is still held
// sharp by the VALUE stop -- so smoothing on curvature costs no cross-surface bleed. Flat receivers are unaffected (their
// taps are parallel -> dot~=1 -> pow-8 == pow-32 == 1).
#define NWB_SHADOW_RESOLVE_NORMAL_POWER 8.0

// Camera-distance edge-stop fraction: the distance sigma is this fraction of the camera distance (a few percent keeps
// same-surface taps while rejecting a silhouette depth jump), clamped away from zero. Two named twins so the UPSAMPLE
// and the WAVELET distance stops read the SAME documented constant rather than a bare 0.02 magic literal.
#define NWB_SHADOW_RESOLVE_UPSAMPLE_DIST_FRAC 0.02
#define NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC 0.02

// FAST GUIDED FILTER upsample (He 2010) parameters. The stage-2 upsample fits a local linear model vis ~= a*dist + b over a
// (2*RADIUS+1)^2 half-res window from its box-filtered moments and evaluates it at the full-res pixel, replacing the 2x2
// joint-bilateral that COLLAPSED to a single noisy tap on curvature (the clustered self-shadow grain). RADIUS = half-res
// window radius (1 -> 3x3 half-res = 6x6 full-res footprint).
#define NWB_SHADOW_RESOLVE_GUIDED_RADIUS 1
// GUIDED_EPS_FRAC: the guided-filter regularisation eps = (this * cameraDistance)^2 -- the "degree of smoothing". Larger ->
// a -> 0 -> the window box-averages (kills grain where camera distance does not correlate with the shadow, i.e. within a
// surface); a real depth-correlated silhouette has window variance >> eps so its `a` term still tracks that edge sharply.
#define NWB_SHADOW_RESOLVE_GUIDED_EPS_FRAC 0.02
// GUIDED_NORMAL_GATE: a WIDE binary same-surface gate (min dot(centreNormal, tapNormal) to admit a tap into the model).
// Excludes a genuinely different surface (a crease / silhouette the scalar distance guide cannot see) while admitting the
// whole smooth-curvature neighbourhood -- a binary cut, NOT a pow falloff, so it never starves the fit to one tap on a curve.
#define NWB_SHADOW_RESOLVE_GUIDED_NORMAL_GATE 0.5

// Normal edge-stop: taps whose receiver normal diverges from the centre's fall off as pow(max(dot,0), N). Takes an
// ALREADY-DECODED centre normal (constant across the wavelet kernel + the guided upsample) against an octahedral-packed
// tap normal (the half-res geometry cache), so the centre is oct-decoded ONCE, never per tap.
float nwbShadowResolveNormalWeightVec(const float3 centerNormal, const float2 tapOct){
    const float3 tn = nwbOctDecodeNormal(tapOct);
    return pow(max(dot(centerNormal, tn), 0.0), NWB_SHADOW_RESOLVE_NORMAL_POWER);
}

// Value (visibility) edge-stop: taps whose visibility diverges from the centre's fall off exponentially, so the wavelet
// smooths WITHIN the umbra and WITHIN the lit region but does NOT bleed the dark umbra across the shadow boundary into
// the lit plane. The geometry edge-stop alone cannot do this -- a FLAT receiver has no depth/normal discontinuity AT the
// shadow edge, so without this term a wide a-trous Gaussian-blurs the shadow across the plane until it washes out.
//
// STAGE 4A (SVGF variance-coupled): the sigma is driven per pixel by the luminance variance instead of a single fixed
// constant. sigma_l = SIGMA_LUM * sqrt(variance) + EPS. HIGH variance (freshly disoccluded / noisy) -> large sigma ->
// loose stop -> aggressive fill; LOW variance (temporally converged) -> sigma ~= EPS -> tight stop -> the sharp penumbra
// is preserved. The variance is the temporal moment variance (max(m2 - m1^2, 0)) where history is long enough to trust
// it, else a small-neighbourhood SPATIAL luma variance (mirroring the merge's gate-B 3x3 metric) for low-history pixels.

// The history length n at/above which the temporal moment variance is statistically trustworthy; below it (freshly
// disoccluded) the temporal m2-m1^2 is built from too few samples, so fall back to the SPATIAL luma variance.
#define NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY 4.0
// Per-variant value-edge-stop TIGHTNESS scale. The COLORED TRANSPARENT (RGB) glass tint is a WEAK, low-contrast signal that a
// loose value stop + wide a-trous spreads thin over the soft penumbra until it WASHES OUT to gray -- the reported "soft glass
// shadow is hard to recognise" while a near-hard one reads fine. So the RGB variant TIGHTENS the value stop (0.20 = a 5x tighter
// luminance sigma) to reject taps across the tint edge and KEEP the colored shadow's per-pixel contrast, recovering its
// visibility toward the near-hard reference. The OPAQUE (scalar) path is UNCHANGED (1.0): its sharp binary edge already has
// strong contrast and wants the fuller fill. Applied to BOTH the variance scale and the EPS floor so a converged transparent
// pixel also gets the tighter stop. Measured on the soft-shadow test scene (glass green-tint density vs the near-hard
// NWB_SOFT_SHADOW_TEST_ANGLE=0.002 reference): scale 0.20 lifts the soft glass shadow's green-tint mass from ~1.05 (loose) to
// ~1.18, essentially the near-hard reference's ~1.19, with NO measurable added speckle (the 3x3 high-pass RMS in the glass band
// is unchanged vs the loose config and vs the near-hard reference -- the temporal reproject-merge, not the value stop, governs
// the residual per-frame noise). 0.35 was also tried (~1.15-1.17); 0.20 recovers a touch more contrast at the same noise.
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    #define NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE 0.20
#else
    #define NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE 1.0
#endif
// Luminance edge-stop scale: sigma_l = SIGMA_LUM * sqrt(variance) + EPS. Larger -> looser value stop -> more fill.
#define NWB_SHADOW_RESOLVE_SIGMA_LUM (4.0 * NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE)
// Epsilon floor so a fully-converged (variance -> 0) pixel still has a finite, tight sigma (no divide-by-zero and no
// infinitely-sharp stop that would reject every neighbour). Tuned at the spatial fallback's noise-floor scale.
#define NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS (0.05 * NWB_SHADOW_RESOLVE_SIGMA_LUM_SCALE)

float nwbShadowResolveValueWeight(const float centerVis, const float tapVis, const float sigmaLum){
    return exp(-abs(tapVis - centerVis) / sigmaLum);
}

// Rec.709 luma of a visibility value -- the scalar the value edge-stop compares. For SCALAR (CH1) it is the value itself;
// for RGB (CH3) it is the luma of the colored transmittance, so the wavelet stops on the same perceptual delta the
// temporal merge's moments track (both Rec.709). Overloaded so the wavelet body is channel-agnostic.
float nwbShadowResolveLuma(const float v){
    return v;
}
float nwbShadowResolveLuma(const float3 c){
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Value (visibility) edge-stop on the wavelet's vector value type: exp(-|luma(tap)-luma(center)|/sigmaLum). Reduces to the
// scalar nwbShadowResolveValueWeight above for CH1 (luma is identity), and for CH3 stops on the LUMA of the RGB delta so
// the colored penumbra smooths WITHIN the tint / WITHIN the lit region without bleeding across the colored shadow edge.
float nwbShadowResolveValueWeightVec(const NwbShadowResolveVisFVec centerVis, const NwbShadowResolveVisFVec tapVis, const float sigmaLum){
    return exp(-abs(nwbShadowResolveLuma(tapVis) - nwbShadowResolveLuma(centerVis)) / sigmaLum);
}

// PREPARE/WAVELET tap read of the current INPUT_COLOR (or SOFT_HALF) at a half-res texel + slot, as the wavelet vector
// type: .r broadcast for SCALAR (CH1), .rgb for RGB (CH3). Centralised so the LDS-load + large-step paths read identically.
NwbShadowResolveVisVec nwbShadowResolveLoadInput(const int2 tap, const uint slot){
    const float4 s = g_NwbShadowResolveInputColor.Load(int4(tap, (int)slot, 0));
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    return NwbShadowResolveVisVec(s.rgb);
#else
    return NwbShadowResolveVisVec(s.r);
#endif
}

// WAVELET output store: broadcast the smoothed vector into float4(v,v,v,1) for SCALAR (grayscale) or float4(v,1) for RGB.
float4 nwbShadowResolveStore(const NwbShadowResolveVisFVec v){
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
    return float4(v, 1.0);
#else
    return float4(v, v, v, 1.0);
#endif
}

// Per-centre-pixel luminance edge-stop sigma for the wavelet (§ Stage 4A). Chooses the TEMPORAL moment variance when the
// history is long (momentsValid == 1 AND n >= MIN_HISTORY) and otherwise a 3x3 SPATIAL luma variance of the current
// INPUT_COLOR over valid receiver taps. Crucially, when momentsValid == 0 the moments SRV is NEVER sampled (the temporal
// branch is guarded by it), so a dummy-bound moments texture on the non-temporal path is safe.
float nwbShadowResolveCenterSigmaLum(const int2 texel, const uint slot, const int hw, const int hh, const uint momentsValid){
    if(momentsValid != 0u){
        const float3 mom = g_NwbShadowResolveMoments.Load(int4(texel, (int)slot, 0)).xyz;
        const float n = mom.z;
        if(n >= NWB_SHADOW_RESOLVE_VARIANCE_MIN_HISTORY){
            const float varTemporal = max(mom.y - mom.x * mom.x, 0.0);
            return NWB_SHADOW_RESOLVE_SIGMA_LUM * sqrt(varTemporal) + NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
        }
        // low n: fall through to the spatial estimate (temporal variance unreliable when freshly disoccluded).
    }
    // SPATIAL fallback: 3x3 luma variance of the current INPUT_COLOR (.r visibility) over VALID receiver taps only,
    // matching the merge's gate-B neighbourhood. Never touches the moments SRV -> safe when momentsValid == 0.
    float mean = 0.0;
    float mean2 = 0.0;
    int cnt = 0;
    [unroll] for(int dy = -1; dy <= 1; ++dy){
        [unroll] for(int dx = -1; dx <= 1; ++dx){
            const int2 t = int2(clamp(texel.x + dx, 0, hw - 1), clamp(texel.y + dy, 0, hh - 1));
            if(g_NwbShadowResolveGeometry.Load(int3(t, 0)).w <= 0.5)
                continue; // background tap -> no receiver, skip
            // SCALAR: .r visibility; RGB: the LUMA of the colored transmittance, so the spatial variance tracks the same
            // perceptual signal the temporal moments (also luma) do -- keeping the sigma metric consistent for both variants.
            const float4 sColor = g_NwbShadowResolveInputColor.Load(int4(t, (int)slot, 0));
#if NWB_SHADOW_RESOLVE_CHANNELS == NWB_SHADOW_RESOLVE_CHANNELS_RGB
            const float v = nwbShadowResolveLuma(sColor.rgb);
#else
            const float v = sColor.r;
#endif
            mean += v;
            mean2 += v * v;
            ++cnt;
        }
    }
    if(cnt <= 0)
        return NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS; // no valid neighbourhood -> tightest sigma (a lone receiver texel)
    const float invCount = 1.0 / (float)cnt;
    const float varSpatial = max(mean2 * invCount - (mean * invCount) * (mean * invCount), 0.0);
    return NWB_SHADOW_RESOLVE_SIGMA_LUM * sqrt(varSpatial) + NWB_SHADOW_RESOLVE_SIGMA_LUM_EPS;
}


[numthreads(NWB_SHADOW_RESOLVE_GROUP_SIZE, NWB_SHADOW_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID){
    const uint stage = g_NwbShadowResolvePushConstants.stage;
    const uint slotStart = g_NwbShadowResolvePushConstants.lightSlotStart;
    const uint slotEnd = min(slotStart + g_NwbShadowResolvePushConstants.lightSlotCount, NWB_SCENE_SHADOW_SLOT_COUNT);

    // UPSAMPLE (stage 2, FULL-res dispatch): per slot, a FAST GUIDED FILTER (He 2010) resample of the final half-res
    // visibility into the full-res visibility buffer. For each output pixel we fit a local linear model vis ~= a*dist + b
    // over a small half-res window (valid same-surface taps) from its box-filtered moments, then evaluate it at THIS full-res
    // pixel's own camera distance. Every window tap feeds the moment sums, so on a curved receiver the output is a smooth
    // regression that averages the trace noise out, while a depth-correlated silhouette is still tracked by the `a` term
    // and a crease is excluded by the wide same-surface gate.
    if(stage == 2u){
        const uint2 pixel = dispatchThreadID.xy;
        if(pixel.x >= g_NwbShadowResolvePushConstants.width || pixel.y >= g_NwbShadowResolvePushConstants.height)
            return;

        const int2 fullTexel = int2(pixel);
        const bool background = g_NwbShadowResolveGBufferDepth.Load(int3(fullTexel, 0)).r >= 0.999999;

        const int hw = (int)g_NwbShadowResolvePushConstants.halfWidth;
        const int hh = (int)g_NwbShadowResolvePushConstants.halfHeight;
        const float2 src = (float2(pixel) + 0.5) / float(NWB_SW_SHADOW_SOFT_FACTOR) - 0.5; // full centre -> half texel space
        const int2 baseTexel = int2(round(src)); // nearest half-res texel = the guided-filter window centre

        // FULL-RES-GUIDED CENTRE: this output pixel's own full-res world position + normal. The camera distance is the model
        // GUIDE (SAME length(world - cameraPos) space as the half-res cache .z), and the full-res centre distance is what the
        // linear model is evaluated at, so the reconstruction snaps to the full-res silhouette rather than the half-res grid;
        // the centre normal drives the wide same-surface gate. The guide is referenced to the centre INSIDE the loop for
        // fp32 stability (relative distances are ~cm, absolute ~m -> squaring the absolute would lose the variance).
        const float3 centerWorld = g_NwbShadowResolveGBufferWorldPos.Load(int3(fullTexel, 0)).xyz;
        const float3 centerNormal = normalize(g_NwbShadowResolveGBufferNormal.Load(int3(fullTexel, 0)).xyz * 2.0 - 1.0);
        const float centerDist = length(centerWorld - g_NwbShadowResolveSceneShading.cameraPosition.xyz);
        const float guidedEps = NWB_SHADOW_RESOLVE_GUIDED_EPS_FRAC * centerDist;
        const float guidedEpsSq = guidedEps * guidedEps;

        // Fold mode: 0 = OVERWRITE the visibility (soft OPAQUE); 1 = MULTIPLY the denoised colored transmittance onto it
        // (soft TRANSPARENT fold, read-modify-write). One texel per thread, VISIBILITY is UAV-only in this set, so the
        // fold==1 RMW is a safe self-UAV update; the caller orders the opaque UPSAMPLE writes before this pass's reads
        // with a UAV barrier on the visibility image.
        const uint fold = g_NwbShadowResolvePushConstants.upsampleFold;
        for(uint slot = slotStart; slot < slotEnd; ++slot){
            if(background){
                // OVERWRITE mode writes the LIT identity for a background pixel; FOLD mode leaves the prior (opaque) value
                // UNTOUCHED -- writing white here would clobber the opaque result at a background full-res texel.
                if(fold == 0u)
                    g_NwbShadowResolveVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0); // LIT identity
                continue;
            }
            // Box-filtered guided-filter moments over the same-surface window: guide g = tapDist - centerDist, input p = tap
            // visibility (RGB; the scalar guide gives per-channel a,b). `anySum` is an ungated on-surface fallback for a thin
            // feature where the same-surface gate would exclude every tap.
            float sumG = 0.0;
            float sumG2 = 0.0;
            float3 sumP = float3(0.0, 0.0, 0.0);
            float3 sumGP = float3(0.0, 0.0, 0.0);
            float count = 0.0;
            float3 anySum = float3(0.0, 0.0, 0.0);
            float anyCount = 0.0;
            [unroll] for(int dy = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dy <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dy){
                [unroll] for(int dx = -NWB_SHADOW_RESOLVE_GUIDED_RADIUS; dx <= NWB_SHADOW_RESOLVE_GUIDED_RADIUS; ++dx){
                    const int2 tap = int2(clamp(baseTexel.x + dx, 0, hw - 1), clamp(baseTexel.y + dy, 0, hh - 1));
                    const float4 tapGeom = nwbShadowResolveGeomAtHalf(tap);
                    if(tapGeom.w <= 0.5)
                        continue; // background tap -> no receiver
                    const float3 tapVis = g_NwbShadowResolveInputColor.Load(int4(tap, slot, 0)).rgb;
                    anySum += tapVis;
                    anyCount += 1.0;
                    if(dot(centerNormal, nwbOctDecodeNormal(tapGeom.xy)) < NWB_SHADOW_RESOLVE_GUIDED_NORMAL_GATE)
                        continue; // genuinely different surface (crease/silhouette) -> not part of this linear model
                    const float g = tapGeom.z - centerDist; // centre-referenced camera-distance guide (fp32-stable)
                    sumG += g;
                    sumG2 += g * g;
                    sumP += tapVis;
                    sumGP += g * tapVis;
                    count += 1.0;
                }
            }
            float3 result;
            if(count > 0.0){
                const float invN = 1.0 / count;
                const float meanG = sumG * invN;
                const float3 meanP = sumP * invN;
                const float varG = max(sumG2 * invN - meanG * meanG, 0.0);
                const float3 covGP = sumGP * invN - meanG * meanP;
                const float3 a = covGP / (varG + guidedEpsSq);
                // Evaluate at the centre: the guide is referenced to centerDist so the centre guide value is 0 ->
                // q = a*0 + b = b = meanP - a*meanG. The linear model can mildly overshoot -> clamp into [0,1].
                result = clamp(meanP - a * meanG, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
            }
            else{
                // Same-surface gate excluded every tap (a thin sub-half-res feature): fall back to the plain on-surface mean;
                // a truly receiver-less (all-background) block resolves to the LIT identity.
                result = (anyCount > 0.0) ? (anySum / anyCount) : float3(1.0, 1.0, 1.0);
            }
            if(fold == 0u){
                g_NwbShadowResolveVisibility[uint3(pixel, slot)] = float4(result, 1.0); // OVERWRITE (opaque)
            }
            else{
                // MULTIPLY the denoised colored transmittance onto the (soft opaque) visibility -- the Stage-5 fold. A tap-
                // rejected `result` == 1.0 (untinted identity) leaves the opaque value unchanged, so an all-rejected fold
                // texel is a no-op rather than a black leak.
                const float3 prior = g_NwbShadowResolveVisibility[uint3(pixel, slot)].rgb;
                g_NwbShadowResolveVisibility[uint3(pixel, slot)] = float4(prior * result, 1.0);
            }
        }
        return;
    }

    // PREPARE + WAVELET run at HALF resolution.
    const uint2 pixel = dispatchThreadID.xy;
    const int hw = (int)g_NwbShadowResolvePushConstants.halfWidth;
    const int hh = (int)g_NwbShadowResolvePushConstants.halfHeight;
    const int2 texel = int2(pixel);
    const bool inBounds = (pixel.x < g_NwbShadowResolvePushConstants.halfWidth && pixel.y < g_NwbShadowResolvePushConstants.halfHeight);

    // PREPARE (stage 0): per slot, copy the half-res traced visibility into the ping-pong buffer. No wavelet; all
    // threads that are in bounds write, then return (no LDS/barrier here).
    if(stage == 0u){
        if(inBounds){
            const bool valid = nwbShadowResolveValidAtHalf(texel);
            for(uint slot = slotStart; slot < slotEnd; ++slot){
                const float3 vis = valid ? g_NwbShadowResolveSoftHalf.Load(int4(texel, slot, 0)).rgb : float3(1.0, 1.0, 1.0);
                g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(vis, 1.0);
            }
        }
        return;
    }

    // WAVELET (stage 1), at HALF resolution. step is in half-res texels.
    const int step = (int)max(g_NwbShadowResolvePushConstants.stepWidth, 1u);

    // Distance edge-stop sigma from THIS half pixel's camera distance (scaled by the dilation so wider passes tolerate
    // more spread), computed once (same receiver for every slot). invalid centre pixels write the LIT identity below.
    const float4 centerGeom = inBounds ? nwbShadowResolveGeomAtHalf(texel) : float4(0.0, 0.0, 0.0, 0.0);
    const float sigmaDist = max(centerGeom.z * NWB_SHADOW_RESOLVE_WAVELET_DIST_FRAC * (float)step, 1e-3);
    const float invTwoSigmaDistSq = 1.0 / (2.0 * sigmaDist * sigmaDist);
    // Decode the centre receiver normal ONCE (it is constant for every slot AND every tap); the per-tap normal edge-stop
    // below then uses the ...Vec variant so the octahedral decode is not repeated across the 25-tap x 5-pass kernel.
    const float3 centerNormalWavelet = nwbOctDecodeNormal(centerGeom.xy);

    // Per-centre-pixel SVGF luminance edge-stop sigma (Stage 4A), computed ONCE (same receiver for every slot). NOTE this
    // reads the moments SRV / the current INPUT_COLOR at THIS texel -- only meaningful for an in-bounds receiver; invalid /
    // background centre pixels write the LIT identity below without using it. Guarded by momentsValid inside the helper.
    const uint momentsValid = g_NwbShadowResolvePushConstants.momentsValid;

    // SMALL-dilation passes (LDS): per slot, cooperatively load the group's tile + 2*step halo (for THAT slot) into
    // LDS, barrier, then tap from LDS. ALL threads must reach every barrier, so NO early `return` inside the slot loop.
    if(step <= NWB_SHADOW_RESOLVE_LDS_MAX_STEP){
        const int tileW = NWB_SHADOW_RESOLVE_GROUP_SIZE + 4 * step; // group + 2*step halo each side
        const int2 tileOrigin = int2(groupID.xy) * NWB_SHADOW_RESOLVE_GROUP_SIZE - int2(2 * step, 2 * step);
        const uint threadCount = NWB_SHADOW_RESOLVE_GROUP_SIZE * NWB_SHADOW_RESOLVE_GROUP_SIZE;
        const uint threadIndex = groupThreadID.y * NWB_SHADOW_RESOLVE_GROUP_SIZE + groupThreadID.x;
        const uint tileTexels = (uint)(tileW * tileW);

        for(uint slot = slotStart; slot < slotEnd; ++slot){
            // Cooperative load of this slot's tile (visibility + geometry). A receiver-only tap contributes; others are
            // marked invalid so the wavelet skips them (matches the texture path's OOB + background skips).
            for(uint i = threadIndex; i < tileTexels; i += threadCount){
                const int2 t = tileOrigin + int2((int)(i % (uint)tileW), (int)(i / (uint)tileW));
                NwbShadowResolveVisVec tapVis = NwbShadowResolveVisVec(1.0); // LIT / UNTINTED identity for OOB/background taps
                half2 tapNormal = half2(0.0, 0.0);
                float tapDist = 0.0;
                uint tapValid = 0u;
                if(t.x >= 0 && t.y >= 0 && t.x < hw && t.y < hh){
                    const float4 g = nwbShadowResolveGeomAtHalf(t);
                    if(g.w > 0.5){
                        tapVis = NwbShadowResolveVisVec(nwbShadowResolveLoadInput(t, slot));
                        tapNormal = half2(g.xy);
                        tapDist = g.z;
                        tapValid = 1u;
                    }
                }
                s_NwbShadowResolveTileVis[i] = tapVis;
                s_NwbShadowResolveTileNormal[i] = tapNormal;
                s_NwbShadowResolveTileDist[i] = tapDist;
                s_NwbShadowResolveTileValid[i] = tapValid;
            }
            GroupMemoryBarrierWithGroupSync();

            if(inBounds){
                if(centerGeom.w <= 0.5){
                    g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0); // background: LIT identity
                }
                else{
                    const uint centerIdx = (uint)((int(groupThreadID.y) + 2 * step) * tileW + (int(groupThreadID.x) + 2 * step));
                    const NwbShadowResolveVisFVec centerVis = (NwbShadowResolveVisFVec)s_NwbShadowResolveTileVis[centerIdx];
                    // Per-slot SVGF luminance sigma for THIS receiver pixel (temporal-when-converged, else spatial fallback).
                    const float sigmaLum = nwbShadowResolveCenterSigmaLum(texel, slot, hw, hh, momentsValid);
                    NwbShadowResolveVisFVec sum = (NwbShadowResolveVisFVec)0.0;
                    float weightSum = 0.0;
                    [unroll] for(int ky = -2; ky <= 2; ++ky){
                        [unroll] for(int kx = -2; kx <= 2; ++kx){
                            const uint idx = (uint)((int(groupThreadID.y) + (ky + 2) * step) * tileW + (int(groupThreadID.x) + (kx + 2) * step));
                            if(s_NwbShadowResolveTileValid[idx] == 0u)
                                continue;
                            const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)s_NwbShadowResolveTileVis[idx];
                            const float dDist = s_NwbShadowResolveTileDist[idx] - centerGeom.z;
                            const float distWeight = exp(-dDist * dDist * invTwoSigmaDistSq);
                            const float normalWeight = nwbShadowResolveNormalWeightVec(centerNormalWavelet, float2(s_NwbShadowResolveTileNormal[idx]));
                            const float valueWeight = nwbShadowResolveValueWeightVec(centerVis, tapVis, sigmaLum);
                            const float weight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distWeight * normalWeight * valueWeight;
                            sum += weight * tapVis;
                            weightSum += weight;
                        }
                    }
                    // all taps rejected -> LIT / UNTINTED identity (never black).
                    const NwbShadowResolveVisFVec vis = (weightSum > 0.0) ? (sum / weightSum) : (NwbShadowResolveVisFVec)1.0;
                    g_NwbShadowResolveOutput[uint3(pixel, slot)] = nwbShadowResolveStore(vis);
                }
            }
            // Barrier before the NEXT slot reloads the shared tile (WAR on the LDS arrays). Every thread reaches it.
            GroupMemoryBarrierWithGroupSync();
        }
        return;
    }

    // LARGE-dilation passes (step > LDS max): taps far apart -> tap the half-res textures directly. Identical math.
    if(!inBounds)
        return;
    for(uint slot = slotStart; slot < slotEnd; ++slot){
        if(centerGeom.w <= 0.5){
            g_NwbShadowResolveOutput[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
            continue;
        }
        const NwbShadowResolveVisFVec centerVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(texel, slot);
        // Per-slot SVGF luminance sigma for THIS receiver pixel (temporal-when-converged, else spatial fallback).
        const float sigmaLum = nwbShadowResolveCenterSigmaLum(texel, slot, hw, hh, momentsValid);
        NwbShadowResolveVisFVec sum = (NwbShadowResolveVisFVec)0.0;
        float weightSum = 0.0;
        [unroll] for(int ky = -2; ky <= 2; ++ky){
            [unroll] for(int kx = -2; kx <= 2; ++kx){
                const int2 tap = int2(texel.x + kx * step, texel.y + ky * step);
                if(tap.x < 0 || tap.y < 0 || tap.x >= hw || tap.y >= hh)
                    continue;
                const float4 tapGeom = nwbShadowResolveGeomAtHalf(tap);
                if(tapGeom.w <= 0.5)
                    continue;
                const NwbShadowResolveVisFVec tapVis = (NwbShadowResolveVisFVec)nwbShadowResolveLoadInput(tap, slot);
                const float dDist = tapGeom.z - centerGeom.z;
                const float distWeight = exp(-dDist * dDist * invTwoSigmaDistSq);
                const float normalWeight = nwbShadowResolveNormalWeightVec(centerNormalWavelet, tapGeom.xy);
                const float valueWeight = nwbShadowResolveValueWeightVec(centerVis, tapVis, sigmaLum);
                const float weight = s_NwbShadowResolveKernel[kx + 2] * s_NwbShadowResolveKernel[ky + 2] * distWeight * normalWeight * valueWeight;
                sum += weight * tapVis;
                weightSum += weight;
            }
        }
        const NwbShadowResolveVisFVec vis = (weightSum > 0.0) ? (sum / weightSum) : (NwbShadowResolveVisFVec)1.0;
        g_NwbShadowResolveOutput[uint3(pixel, slot)] = nwbShadowResolveStore(vis);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

