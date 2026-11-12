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
    const f64 minX = static_cast<f64>(VectorGetX(flooredBounds));
    const f64 maxX = static_cast<f64>(VectorGetY(ceiledBounds));
    const f64 minY = static_cast<f64>(VectorGetZ(flooredBounds));
    const f64 maxY = static_cast<f64>(VectorGetW(ceiledBounds));
    return minX >= 0.0 && minY >= 0.0 && maxX >= minX && maxY >= minY && maxX <= static_cast<f64>(Limit<i32>::s_Max) && maxY <= static_cast<f64>(Limit<i32>::s_Max);
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

