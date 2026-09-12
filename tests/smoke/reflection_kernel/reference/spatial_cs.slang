// limztudio@gmail.com
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

#define NWB_REFLECTION_SPATIAL_TILE_WIDTH (NWB_REFLECTION_SPATIAL_GROUP_SIZE + 2u * NWB_REFLECTION_SPATIAL_MAX_RADIUS)
#define NWB_REFLECTION_SPATIAL_TILE_TEXELS (NWB_REFLECTION_SPATIAL_TILE_WIDTH * NWB_REFLECTION_SPATIAL_TILE_WIDTH)

// Four float4 arrays occupy 196 * 64 = 12,544 bytes at the maximum radius. Depth shares the unused position lane;
// the runtime radius reduces cooperative loads to the actual tile plus halo while storage remains fixed.
groupshared float4 g_NwbReflectionSpatialRadiance[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared float4 g_NwbReflectionSpatialSpecular[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared float4 g_NwbReflectionSpatialPositionDepth[NWB_REFLECTION_SPATIAL_TILE_TEXELS];
groupshared float4 g_NwbReflectionSpatialNormal[NWB_REFLECTION_SPATIAL_TILE_TEXELS];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_SPATIAL_GROUP_SIZE, NWB_REFLECTION_SPATIAL_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex){
    const uint radius = min(g_NwbReflectionSpatial.radius, NWB_REFLECTION_SPATIAL_MAX_RADIUS);
    const uint tileWidth = NWB_REFLECTION_SPATIAL_GROUP_SIZE + 2u * radius;
    const uint tileTexels = tileWidth * tileWidth;
    const int2 tileOrigin = int2(groupId.xy * NWB_REFLECTION_SPATIAL_GROUP_SIZE) - int2(radius);
    const uint groupThreads = NWB_REFLECTION_SPATIAL_GROUP_SIZE * NWB_REFLECTION_SPATIAL_GROUP_SIZE;
    for(uint tileIndex = groupIndex; tileIndex < tileTexels; tileIndex += groupThreads){
        const int2 sourcePixel = tileOrigin + int2(tileIndex % tileWidth, tileIndex / tileWidth);
        float4 radiance = float4(0.0);
        float4 specular = float4(0.0);
        float4 positionDepth = float4(0.0, 0.0, 0.0, 1.0);
        float4 normal = float4(0.0);
        if(
            all(sourcePixel >= int2(0)) && sourcePixel.x < int(g_NwbReflectionSpatial.width)
            && sourcePixel.y < int(g_NwbReflectionSpatial.height)
        ){
            const int3 sourceTexel = int3(sourcePixel, 0);
            radiance = NwbHeapSampledImage2D(g_NwbReflectionSpatial.sourceRadianceSrvSlot).Load(sourceTexel);
            specular = NwbHeapSampledImage2D(g_NwbReflectionSpatial.opaqueSpecularSlot).Load(sourceTexel);
            positionDepth.xyz = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.z).Load(sourceTexel).xyz;
            positionDepth.w = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.w).Load(sourceTexel).r;
            normal = NwbHeapSampledImage2D(g_NwbReflectionSpatialResources.gbufferSlots.y).Load(sourceTexel);
        }
        g_NwbReflectionSpatialRadiance[tileIndex] = radiance;
        g_NwbReflectionSpatialSpecular[tileIndex] = specular;
        g_NwbReflectionSpatialPositionDepth[tileIndex] = positionDepth;
        g_NwbReflectionSpatialNormal[tileIndex] = normal;
    }
    // Keep the barrier in main and before every return. Partial image tails, mirror pixels and radius zero all
    // participate, and every cache entry a surviving lane can read has exactly one cooperative writer.
    GroupMemoryBarrierWithGroupSync();

    const uint2 pixel = dispatchThreadId.xy;
    if(pixel.x >= g_NwbReflectionSpatial.width || pixel.y >= g_NwbReflectionSpatial.height)
        return;
    const uint2 tilePixel = pixel - groupId.xy * NWB_REFLECTION_SPATIAL_GROUP_SIZE + radius;
    const uint centerIndex = tilePixel.y * tileWidth + tilePixel.x;
    const float4 center = g_NwbReflectionSpatialRadiance[centerIndex];
    const float4 specular = g_NwbReflectionSpatialSpecular[centerIndex];
    const float depth = g_NwbReflectionSpatialPositionDepth[centerIndex].w;
    if(
        radius == 0u || center.a <= 0.0 || !isfinite(depth) || depth < 0.0 || depth >= 1.0
        || !all(isfinite(specular)) || specular.a <= 0.0 || !any(specular.rgb > 0.0)
    ){
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

