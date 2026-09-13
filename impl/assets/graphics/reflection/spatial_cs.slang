// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_REFLECTION_SPATIAL_RADIUS
#define NWB_REFLECTION_SPATIAL_RADIUS 0
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "spatial_constants.h"
#include "../deferred/bindless_resources.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbReflectionSpatialPushConstants{
#define NWB_REFLECTION_SPATIAL_FIELD(name) uint name;
    NWB_REFLECTION_SPATIAL_UINT_FIELDS(NWB_REFLECTION_SPATIAL_FIELD)
#undef NWB_REFLECTION_SPATIAL_FIELD
};
[[vk::push_constant]] ConstantBuffer<NwbReflectionSpatialPushConstants> g_NwbReflectionSpatial;

// Mesh owns the view ABI; filtering only borrows its projection and camera-position prefix.
struct NwbReflectionSpatialView{
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float4 cameraPosition;
};
#pragma warning(push)
#pragma warning(disable: 39001)
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbReflectionSpatialView, Std140DataLayout> g_NwbHeapReflectionSpatialViews[];
#pragma warning(pop)

#define g_NwbReflectionSpatialResources g_NwbHeapDeferredBindlessResources[g_NwbReflectionSpatial.deferredResourcesSlot]
#define g_NwbReflectionSpatialView g_NwbHeapReflectionSpatialViews[g_NwbReflectionSpatial.viewSlot]

#if NWB_REFLECTION_SPATIAL_RADIUS < 0 || NWB_REFLECTION_SPATIAL_RADIUS > NWB_REFLECTION_SPATIAL_MAX_RADIUS
#error Unsupported reflection spatial radius specialization.
#endif
#if NWB_REFLECTION_SPATIAL_RADIUS == 0
#define NWB_REFLECTION_SPATIAL_CACHE_RADIUS NWB_REFLECTION_SPATIAL_MAX_RADIUS
#else
#define NWB_REFLECTION_SPATIAL_CACHE_RADIUS NWB_REFLECTION_SPATIAL_RADIUS
#endif
#define NWB_REFLECTION_SPATIAL_TILE_WIDTH (NWB_REFLECTION_SPATIAL_GROUP_SIZE + 2u * NWB_REFLECTION_SPATIAL_CACHE_RADIUS)
#define NWB_REFLECTION_SPATIAL_TILE_TEXELS (NWB_REFLECTION_SPATIAL_TILE_WIDTH * NWB_REFLECTION_SPATIAL_TILE_WIDTH)

// Four float4 arrays occupy 6,400/9,216/12,544 bytes for static radii 1/2/3. The dynamic reference retains the maximum.
// Depth shares the unused position lane; specialization changes storage extent without changing the tile's writers.
groupshared float4 g_NwbReflectionSpatialRadiance[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared float4 g_NwbReflectionSpatialSpecular[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared float4 g_NwbReflectionSpatialPositionDepth[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared float4 g_NwbReflectionSpatialNormal[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared uint g_NwbReflectionSpatialTileEligible;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_SPATIAL_GROUP_SIZE, NWB_REFLECTION_SPATIAL_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex){
    const uint2 pixel = dispatchThreadId.xy;
    const bool insideImage = pixel.x < g_NwbReflectionSpatial.width && pixel.y < g_NwbReflectionSpatial.height;
#if NWB_REFLECTION_SPATIAL_RADIUS == 0
    const uint radius = min(g_NwbReflectionSpatial.radius, NWB_REFLECTION_SPATIAL_MAX_RADIUS);
#else
    const uint radius = NWB_REFLECTION_SPATIAL_RADIUS;
#endif
    float4 center = float4(0.0);
    float4 specular = float4(0.0);
    float depth = 1.0;
    if(insideImage){
        const int3 sourceTexel = int3(pixel, 0);
        center = NwbHeapSampledImage2D(g_NwbReflectionSpatial.sourceRadianceSrvSlot).Load(sourceTexel);
        if(radius != 0u){
            specular = NwbHeapSampledImage2D(g_NwbReflectionSpatial.opaqueSpecularSlot).Load(sourceTexel);
            depth = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.w).Load(sourceTexel).r;
        }
    }
    const bool filterEligible = insideImage && !(
        radius == 0u || center.a <= 0.0 || !isfinite(depth) || depth < 0.0 || depth >= 1.0
        || !all(isfinite(specular)) || specular.a <= 0.0 || !any(specular.rgb > 0.0)
    );
    if(groupIndex == 0u)
        g_NwbReflectionSpatialTileEligible = 0u;
    GroupMemoryBarrierWithGroupSync();
    if(filterEligible)
        InterlockedOr(g_NwbReflectionSpatialTileEligible, 1u);
    GroupMemoryBarrierWithGroupSync();
    // The tile decision is uniform after both barriers. Ineligible tiles only copy their current samples;
    // they cannot consume neighbors, so no halo, position or normal fetch is needed.
    if(g_NwbReflectionSpatialTileEligible == 0u){
        if(insideImage)
            NwbHeapStorageImage2DFloat4(g_NwbReflectionSpatial.destinationRadianceUavSlot)[pixel] = center;
        return;
    }

    const uint tileWidth = NWB_REFLECTION_SPATIAL_GROUP_SIZE + 2u * radius;
    const uint tileTexels = tileWidth * tileWidth;
    const uint2 groupPixel = pixel - groupId.xy * NWB_REFLECTION_SPATIAL_GROUP_SIZE;
    const uint2 tilePixel = groupPixel + radius;
    const uint centerIndex = tilePixel.y * tileWidth + tilePixel.x;
    float4 centerPositionDepth = float4(0.0, 0.0, 0.0, depth);
    float4 centerNormal = float4(0.0);
    if(insideImage){
        const int3 sourceTexel = int3(pixel, 0);
        centerPositionDepth.xyz = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.z).Load(sourceTexel).xyz;
        centerNormal = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.y).Load(sourceTexel);
    }
    // Reuse each lane's eligibility inputs; every center entry has one writer, including partial-image tails.
    g_NwbReflectionSpatialRadiance[centerIndex] = center;
    g_NwbReflectionSpatialSpecular[centerIndex] = specular;
    g_NwbReflectionSpatialPositionDepth[centerIndex] = centerPositionDepth;
    g_NwbReflectionSpatialNormal[centerIndex] = centerNormal;

    const int2 tileOrigin = int2(groupId.xy * NWB_REFLECTION_SPATIAL_GROUP_SIZE) - int2(radius);
    const uint groupThreads = NWB_REFLECTION_SPATIAL_GROUP_SIZE * NWB_REFLECTION_SPATIAL_GROUP_SIZE;
    for(uint tileIndex = groupIndex; tileIndex < tileTexels; tileIndex += groupThreads){
        const uint2 haloPixel = uint2(tileIndex % tileWidth, tileIndex / tileWidth);
        if(all(haloPixel >= radius) && all(haloPixel < radius + NWB_REFLECTION_SPATIAL_GROUP_SIZE))
            continue;
        const int2 sourcePixel = tileOrigin + int2(haloPixel);
        float4 radiance = float4(0.0);
        float4 neighborSpecular = float4(0.0);
        float4 positionDepth = float4(0.0, 0.0, 0.0, 1.0);
        float4 normal = float4(0.0);
        if(
            all(sourcePixel >= int2(0)) && sourcePixel.x < int(g_NwbReflectionSpatial.width)
            && sourcePixel.y < int(g_NwbReflectionSpatial.height)
        ){
            const int3 sourceTexel = int3(sourcePixel, 0);
            radiance = NwbHeapSampledImage2D(g_NwbReflectionSpatial.sourceRadianceSrvSlot).Load(sourceTexel);
            neighborSpecular = NwbHeapSampledImage2D(g_NwbReflectionSpatial.opaqueSpecularSlot).Load(sourceTexel);
            positionDepth.xyz = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.z).Load(sourceTexel).xyz;
            positionDepth.w = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.w).Load(sourceTexel).r;
            normal = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.y).Load(sourceTexel);
        }
        g_NwbReflectionSpatialRadiance[tileIndex] = radiance;
        g_NwbReflectionSpatialSpecular[tileIndex] = neighborSpecular;
        g_NwbReflectionSpatialPositionDepth[tileIndex] = positionDepth;
        g_NwbReflectionSpatialNormal[tileIndex] = normal;
    }
    // No lane in an active tile returns before the cache barrier. Center and halo writers are disjoint,
    // and every cache entry a surviving lane can read has exactly one cooperative writer.
    GroupMemoryBarrierWithGroupSync();
    if(!insideImage)
        return;
    if(!filterEligible){
        NwbHeapStorageImage2DFloat4(g_NwbReflectionSpatial.destinationRadianceUavSlot)[pixel] = center;
        return;
    }

    const float3 position = g_NwbReflectionSpatialPositionDepth[centerIndex].xyz;
    const float3 normal = nwbSafeNormalize(
        g_NwbReflectionSpatialNormal[centerIndex].xyz * 2.0 - 1.0, float3(0.0, 1.0, 0.0)
    );
    const float4 clip = mul(g_NwbReflectionSpatialView.worldToClip, float4(position, 1.0));
    if(!all(isfinite(position)) || !all(isfinite(clip))){
        NwbHeapStorageImage2DFloat4(g_NwbReflectionSpatial.destinationRadianceUavSlot)[pixel] = center;
        return;
    }
    const float focalX = length(g_NwbReflectionSpatialView.worldToClip[0].xyz);
    const float focalY = length(g_NwbReflectionSpatialView.worldToClip[1].xyz);
    const float pixelFootprint = max(
        2.0 * abs(clip.w) / max(float(g_NwbReflectionSpatial.width) * focalX, 1e-6),
        2.0 * abs(clip.w) / max(float(g_NwbReflectionSpatial.height) * focalY, 1e-6)
    );
    const float3 viewDirection = nwbSafeNormalize(g_NwbReflectionSpatialView.cameraPosition.xyz - position, normal);
    const float tangentFootprint = pixelFootprint / max(abs(dot(normal, viewDirection)), 0.05);
    const float roughness = saturate(specular.a);
    // Rougher lobes tolerate more spatial variance reduction; the mirror limit continuously approaches one texel.
    const float sigma = float(radius) * roughness;
    const float spatialDenominator = max(2.0 * sigma * sigma, 1e-8);
    float3 sum = center.rgb;
    float weightSum = 1.0;
    for(int y = -int(radius); y <= int(radius); ++y){
        for(int x = -int(radius); x <= int(radius); ++x){
            if(x == 0 && y == 0)
                continue;
            const float spatialExponent = -float(x * x + y * y) / spatialDenominator;
            // Every other exponent term is nonpositive, so an exactly zero Gaussian cannot contribute. Retain the
            // original combined exponent below; this shortcut introduces no epsilon or altered weight rounding.
            if(exp(spatialExponent) == 0.0)
                continue;
            const int2 neighborPixel = int2(pixel) + int2(x, y);
            if(
                any(neighborPixel < int2(0))
                || neighborPixel.x >= int(g_NwbReflectionSpatial.width)
                || neighborPixel.y >= int(g_NwbReflectionSpatial.height)
            )
                continue;
            const int2 neighborTilePixel = int2(tilePixel) + int2(x, y);
            const uint neighborIndex = uint(neighborTilePixel.y) * tileWidth + uint(neighborTilePixel.x);
            const float neighborDepth = g_NwbReflectionSpatialPositionDepth[neighborIndex].w;
            const float4 neighborSpecular = g_NwbReflectionSpatialSpecular[neighborIndex];
            if(
                !isfinite(neighborDepth) || neighborDepth < 0.0 || neighborDepth >= 1.0 || !all(isfinite(neighborSpecular))
                || neighborSpecular.a <= 0.0 || !any(neighborSpecular.rgb > 0.0)
            )
                continue;
            const half roughnessDifference = half(abs(neighborSpecular.a - roughness));
            const half3 f0Difference = half3(abs(neighborSpecular.rgb - specular.rgb));
            const half maxF0Difference = max(max(f0Difference.x, f0Difference.y), f0Difference.z);
            if(roughnessDifference > half(0.1h) || maxF0Difference > half(0.05h))
                continue;
            const float3 neighborNormal = nwbSafeNormalize(
                g_NwbReflectionSpatialNormal[neighborIndex].xyz * 2.0 - 1.0, normal
            );
            // The normal-agreement gate is a (0, 1) bounded factor, so half arithmetic is sufficient here.
            const half normalAgreement = half(dot(normal, neighborNormal));
            if(normalAgreement < half(0.95h))
                continue;
            const float3 neighborPosition = g_NwbReflectionSpatialPositionDepth[neighborIndex].xyz;
            if(!all(isfinite(neighborPosition)))
                continue;
            const float3 displacement = neighborPosition - position;
            // Bound the raster FP16 positions componentwise, then test both receiver tangent planes. This is a
            // receiver-edge filter, not a secondary-hit reprojection or a claim of motion-vector validation.
            const float3 positionUncertainty = (abs(position) + abs(neighborPosition)) * 0.0009765625;
            const float planeTolerance = max(
                1e-4, max(dot(positionUncertainty, abs(normal)), dot(positionUncertainty, abs(neighborNormal)))
                    + pixelFootprint * 0.25
            );
            const float planeDistance = max(abs(dot(displacement, normal)), abs(dot(displacement, neighborNormal)));
            if(
                planeDistance > planeTolerance * 2.0
                || length(displacement) > tangentFootprint * (length(float2(x, y)) + 1.5) + length(positionUncertainty)
            )
                continue;
            const float4 neighbor = g_NwbReflectionSpatialRadiance[neighborIndex];
            if(neighbor.a <= 0.0 || !all(isfinite(neighbor.rgb)))
                continue;
            // The combined bilateral weight is a (0, 1) bounded filter factor, so half arithmetic is sufficient here.
            const half normalizedPlaneDistance = half(planeDistance / planeTolerance);
            const half weight = half(exp(
                spatialExponent
                -0.5 * normalizedPlaneDistance * normalizedPlaneDistance
                -half(80.0h) * (half(1.0h) - saturate(normalAgreement))
                -half(200.0h) * maxF0Difference * maxF0Difference
                -half(50.0h) * roughnessDifference * roughnessDifference
            ));
            // True zero samples participate normally. Rejecting black samples or clamping bright samples biases
            // energy, and filtered output must never be used as the next temporal-history bank.
            sum += neighbor.rgb * weight;
            weightSum += weight;
        }
    }
    NwbHeapStorageImage2DFloat4(g_NwbReflectionSpatial.destinationRadianceUavSlot)[pixel] = float4(sum / weightSum, center.a);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

