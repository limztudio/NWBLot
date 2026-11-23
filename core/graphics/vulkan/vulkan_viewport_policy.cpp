// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_viewport_policy.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


b8 VulkanViewportPolicy::IsViewportValid(const Viewport& viewport, const VkPhysicalDeviceLimits& limits)noexcept{
    const SIMDVectorSingle xyBounds = VectorSet(viewport.minX, viewport.maxX, viewport.minY, viewport.maxY);
    const SIMDVectorSingle zBounds = VectorSet(viewport.minZ, viewport.maxZ, viewport.minZ, viewport.maxZ);
    if(!VectorIsFinite(xyBounds, VectorComponentMask::s_XYZW) || !VectorIsFinite(zBounds, VectorComponentMask::s_XY))
        return false;
    if(viewport.maxX <= viewport.minX || viewport.maxY < viewport.minY || viewport.minZ < 0.0f || viewport.minZ > 1.0f || viewport.maxZ < 0.0f || viewport.maxZ > 1.0f)
        return false;

    const SIMDVectorSingle extent = VectorSubtract(
        VectorSet(viewport.maxX, viewport.maxY, viewport.maxX, viewport.maxY),
        VectorSet(viewport.minX, viewport.minY, viewport.minX, viewport.minY)
    );
    const SIMDVectorDouble dimensions = ConvertVectorSingleToDouble(extent);
    const SIMDVectorDouble maxDimensions = VectorDoubleSet(
        static_cast<f64>(limits.maxViewportDimensions[0u]),
        static_cast<f64>(limits.maxViewportDimensions[1u]),
        static_cast<f64>(limits.maxViewportDimensions[0u]),
        static_cast<f64>(limits.maxViewportDimensions[1u])
    );
    return
        (VectorDoubleMoveMask(VectorDoubleLessOrEqual(dimensions, maxDimensions)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY
        && viewport.minX >= limits.viewportBoundsRange[0u]
        && viewport.maxX <= limits.viewportBoundsRange[1u]
        && viewport.minY >= limits.viewportBoundsRange[0u]
        && viewport.maxY <= limits.viewportBoundsRange[1u]
    ;
}

b8 VulkanViewportPolicy::IsImplicitScissorValid(const Viewport& viewport)noexcept{
    const SIMDVectorSingle viewportBounds = VectorSet(viewport.minX, viewport.maxX, viewport.minY, viewport.maxY);
    if(!VectorIsFinite(viewportBounds, VectorComponentMask::s_XYZW))
        return false;

    const SIMDVectorSingle flooredBounds = VectorFloor(viewportBounds);
    const SIMDVectorSingle ceiledBounds = VectorCeiling(viewportBounds);
    const SIMDVectorDouble clampedBounds = ConvertVectorSingleToDouble(VectorSelect(flooredBounds, ceiledBounds, s_SIMDSelect0101));
    const SIMDVectorDouble minBounds = VectorDoubleSwizzle<0u, 2u, 0u, 2u>(clampedBounds);
    const SIMDVectorDouble maxBounds = VectorDoubleSwizzle<1u, 3u, 1u, 3u>(clampedBounds);
    const SIMDVectorDouble maxLimit = VectorDoubleReplicate(static_cast<f64>(Limit<i32>::s_Max));
    return
        VectorDouble4GreaterOrEqual(minBounds, VectorDoubleZero())
        && VectorDouble4GreaterOrEqual(maxBounds, minBounds)
        && VectorDouble4LessOrEqual(maxBounds, maxLimit)
    ;
}

Expected<VkRect2D> VulkanViewportPolicy::BuildImplicitScissor(const Viewport& viewport)noexcept{
    if(!VulkanViewportPolicy::IsImplicitScissorValid(viewport))
        return MakeUnexpected(Failure{});

    const Rect rect = Rect::FromViewport(viewport);
    VkRect2D scissor{};
    scissor.offset = { rect.minX, rect.minY };
    scissor.extent = { static_cast<u32>(rect.maxX - rect.minX), static_cast<u32>(rect.maxY - rect.minY) };
    return scissor;
}



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

