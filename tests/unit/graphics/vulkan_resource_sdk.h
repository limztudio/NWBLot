// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>


#include <core/graphics/runtime/runtime_standalone_graph.h>
#include <core/graphics/vulkan/blas_opacity_micromap_policy.h>
#include <core/graphics/vulkan/texture_image_policy.h>

#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && NWB_GRAPHICS_BACKEND_VULKAN
#include <core/graphics/vulkan/calibrated_timestamp_probe.h>
#include <core/graphics/vulkan/cluster_operation_input.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::VulkanSdk{

namespace VulkanDetail = Core::GraphicsBackend::VulkanDetail;
namespace TextureDetail = Core::GraphicsBackend::VulkanTextureDetail;

[[nodiscard]] inline usize WorkspaceAddress(const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease& lease)noexcept{
    return reinterpret_cast<usize>(lease.workspace());
}

inline void SetImageQueueFamilies(VkImageCreateInfo& image, Span<const u32> families)noexcept{
    image.queueFamilyIndexCount = static_cast<u32>(families.size());
    image.pQueueFamilyIndices = families.data();
}

[[nodiscard]] inline auto ValidateTextureCreateDesc(const Core::TextureDesc& desc){
    return TextureDetail::TextureImagePolicy::ValidateTextureCreateDesc(desc, {}, false);
}

[[nodiscard]] inline auto ResolveMicromapIndexFormat(const Core::Format::Enum format){
    return VulkanDetail::BlasOpacityMicromapPolicy::ResolveOpacityMicromapIndexFormat(format, {});
}

inline void SetGeometryUsageCount(Core::RayTracingGeometryDesc& geometry, const u32 count)noexcept{
    geometry.geometryData.triangles.numOmmUsageCounts = count;
}

[[nodiscard]] inline usize GeometryChainAddress(const VulkanDetail::BlasGeometryScratch& scratch, const usize index)noexcept{
    return reinterpret_cast<usize>(scratch.geometries[index].geometry.triangles.pNext);
}

[[nodiscard]] inline usize MicromapUsagesAddress(const VkAccelerationStructureTrianglesOpacityMicromapEXT& micromap)noexcept{
    return reinterpret_cast<usize>(micromap.pUsageCounts);
}

inline void AttachMicromaps(
    const Span<const Core::RayTracingGeometryDesc> geometries,
    const VulkanDetail::OpacityMicromapUsageOffsetVector& offsets,
    const VulkanDetail::OpacityMicromapUsageVector& usages,
    VulkanDetail::OpacityMicromapGeometryVector& micromaps,
    VulkanDetail::BlasGeometryScratch& scratch
)noexcept{
    VulkanDetail::BlasOpacityMicromapPolicy::AttachBlasOpacityMicromapGeometries(geometries.data(), geometries.size(), offsets, usages, micromaps, scratch);
}

#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && NWB_GRAPHICS_BACKEND_VULKAN
[[nodiscard]] inline auto BuildClusterInput(const Core::RayTracingClusterOperationParams& params, const AStringView operation, Core::Alloc::ScratchArena& scratch){
    const auto platformName = AppendPlatformText(scratch, {}, operation);
    return VulkanDetail::ClusterOperationInput::Build(params, platformName);
}

[[nodiscard]] inline usize MoveInputAddress(const VkClusterAccelerationStructureInputInfoNV& info)noexcept{ return reinterpret_cast<usize>(info.opInput.pMoveObjects); }
[[nodiscard]] inline usize BlasInputAddress(const VkClusterAccelerationStructureInputInfoNV& info)noexcept{ return reinterpret_cast<usize>(info.opInput.pClustersBottomLevel); }
[[nodiscard]] inline usize TriangleInputAddress(const VkClusterAccelerationStructureInputInfoNV& info)noexcept{ return reinterpret_cast<usize>(info.opInput.pTriangleClusters); }

[[nodiscard]] inline usize MoveFieldAddress(const VulkanDetail::ClusterOperationInput& input)noexcept{ return reinterpret_cast<usize>(&input.move); }
[[nodiscard]] inline usize BlasFieldAddress(const VulkanDetail::ClusterOperationInput& input)noexcept{ return reinterpret_cast<usize>(&input.blas); }
[[nodiscard]] inline usize TriangleFieldAddress(const VulkanDetail::ClusterOperationInput& input)noexcept{ return reinterpret_cast<usize>(&input.cluster); }

template<class Driver>
VKAPI_ATTR VkResult VKAPI_CALL EnumerateTimeDomains(VkPhysicalDevice device, u32* count, VkTimeDomainKHR* domains){
    auto& driver = *reinterpret_cast<Driver*>(device);
    if(!domains)
        return driver.enumerateCount(*count) ? VK_SUCCESS : VK_INCOMPLETE;
    domains[0] = driver.enumerateDomain(*count) ? VK_TIME_DOMAIN_DEVICE_KHR : VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
    return VK_SUCCESS;
}

template<class Driver>
VKAPI_ATTR VkResult VKAPI_CALL GetCalibratedTimestamps(VkDevice device, u32, const VkCalibratedTimestampInfoKHR*, u64* timestamps, u64* maxDeviation){
    reinterpret_cast<Driver*>(device)->timestamp(*timestamps, *maxDeviation);
    return VK_SUCCESS;
}

template<class Driver>
[[nodiscard]] b8 QueryCalibratedTimestamps(Driver& driver, Core::Alloc::ScratchArena& scratch){
    return VulkanDetail::CalibratedTimestampProbe::Query(reinterpret_cast<VkPhysicalDevice>(&driver), reinterpret_cast<VkDevice>(&driver),
        &EnumerateTimeDomains<Driver>, &GetCalibratedTimestamps<Driver>, scratch);
}
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

