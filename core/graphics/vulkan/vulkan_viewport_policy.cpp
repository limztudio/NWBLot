// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_viewport_policy.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


b8 VulkanViewportPolicy::IsViewportValid(const Viewport& viewport, const VkPhysicalDeviceLimits& limits)noexcept{
    if(!IsFinite(viewport.minX) || !IsFinite(viewport.maxX) || !IsFinite(viewport.minY) || !IsFinite(viewport.maxY) || !IsFinite(viewport.minZ) || !IsFinite(viewport.maxZ))
        return false;
    if(viewport.maxX <= viewport.minX || viewport.maxY < viewport.minY || viewport.minZ < 0.0f || viewport.minZ > 1.0f || viewport.maxZ < 0.0f || viewport.maxZ > 1.0f)
        return false;

    const f32 width = viewport.width();
    const f32 height = viewport.height();
    return
        static_cast<f64>(width) <= static_cast<f64>(limits.maxViewportDimensions[0u])
        && static_cast<f64>(height) <= static_cast<f64>(limits.maxViewportDimensions[1u])
        && viewport.minX >= limits.viewportBoundsRange[0u]
        && viewport.maxX <= limits.viewportBoundsRange[1u]
        && viewport.minY >= limits.viewportBoundsRange[0u]
        && viewport.maxY <= limits.viewportBoundsRange[1u]
    ;
}

b8 VulkanViewportPolicy::IsImplicitScissorValid(const Viewport& viewport)noexcept{
    if(!IsFinite(viewport.minX) || !IsFinite(viewport.maxX) || !IsFinite(viewport.minY) || !IsFinite(viewport.maxY))
        return false;

    const SIMDVectorSingle viewportBounds = VectorSet(viewport.minX, viewport.maxX, viewport.minY, viewport.maxY);
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

