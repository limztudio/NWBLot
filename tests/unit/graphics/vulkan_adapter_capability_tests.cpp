// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <core/graphics/vulkan/backend_context_capabilities.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_adapter_capability_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;
namespace GraphicsBackend = NWB::Core::GraphicsBackend;
namespace VulkanDetail = NWB::Core::GraphicsBackend::VulkanDetail;

struct VulkanAdapterCapabilityTestArenaTag{};
using TestArena = NWB::Tests::TestArena<VulkanAdapterCapabilityTestArenaTag>;


static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}

static VulkanDetail::PhysicalDeviceFeatureSupport FullySupportedFeatures(){
    VulkanDetail::PhysicalDeviceFeatureSupport support;
    VkPhysicalDeviceFeatures& core = support.features.features;
    core.shaderImageGatherExtended = VK_TRUE;
    core.samplerAnisotropy = VK_TRUE;
    core.tessellationShader = VK_TRUE;
    core.geometryShader = VK_TRUE;
    core.imageCubeArray = VK_TRUE;
    core.shaderInt16 = VK_TRUE;
    core.depthClamp = VK_TRUE;
    core.fillModeNonSolid = VK_TRUE;
    core.fragmentStoresAndAtomics = VK_TRUE;
    core.dualSrcBlend = VK_TRUE;
    core.vertexPipelineStoresAndAtomics = VK_TRUE;
    core.shaderInt64 = VK_TRUE;
    core.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    core.shaderStorageImageReadWithoutFormat = VK_TRUE;
    core.independentBlend = VK_TRUE;
    core.fullDrawIndexUint32 = VK_TRUE;
    core.multiDrawIndirect = VK_TRUE;
    core.drawIndirectFirstInstance = VK_TRUE;
    support.vulkan11.storageBuffer16BitAccess = VK_TRUE;
    support.vulkan11.shaderDrawParameters = VK_TRUE;
    support.vulkan12.bufferDeviceAddress = VK_TRUE;
    support.vulkan12.descriptorIndexing = VK_TRUE;
    support.vulkan12.runtimeDescriptorArray = VK_TRUE;
    support.vulkan12.timelineSemaphore = VK_TRUE;
    support.vulkan12.shaderFloat16 = VK_TRUE;
    support.vulkan12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    support.vulkan12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    support.vulkan12.shaderSubgroupExtendedTypes = VK_TRUE;
    support.vulkan12.scalarBlockLayout = VK_TRUE;
    support.dynamicRendering.dynamicRendering = VK_TRUE;
    support.synchronization2.synchronization2 = VK_TRUE;
    support.descriptorBuffer.descriptorBuffer = VK_TRUE;
    return support;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(VulkanQueueFamilySelection, AcceptsRequiredSplitFamiliesWithoutOptionalAsyncCompute){
    VkQueueFamilyProperties families[2] = {};
    families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT;
    families[0].queueCount = 1u;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1u;

    const auto selection = VulkanDetail::SelectRequiredQueueFamilies(families, 2u, false, false);
    EXPECT_EQ(selection.graphicsFamily, 0);
    EXPECT_EQ(selection.computeFamily, 1);
    EXPECT_EQ(selection.asyncComputeFamily, GraphicsBackend::s_InvalidQueueFamilyIndex);
}

TEST(VulkanQueueFamilySelection, AliasesRequiredRolesOnUniversalFamily){
    VkQueueFamilyProperties families[2] = {};
    families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    families[0].queueCount = 1u;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1u;

    const auto selection = VulkanDetail::SelectRequiredQueueFamilies(families, 2u, false, false);
    EXPECT_EQ(selection.graphicsFamily, 0);
    EXPECT_EQ(selection.computeFamily, 0);
    EXPECT_EQ(selection.asyncComputeFamily, GraphicsBackend::s_InvalidQueueFamilyIndex);
}

TEST(VulkanQueueFamilySelection, KeepsRequiredAliasWhileSelectingOptionalAsyncOffload){
    VkQueueFamilyProperties families[2] = {};
    families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    families[0].queueCount = 1u;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1u;

    const auto selection = VulkanDetail::SelectRequiredQueueFamilies(families, 2u, true, false);
    EXPECT_EQ(selection.graphicsFamily, 0);
    EXPECT_EQ(selection.computeFamily, 0);
    EXPECT_EQ(selection.asyncComputeFamily, 1);
}

TEST(VulkanQueueFamilySelection, RejectsMissingRequiredRolesAndIgnoresEmptyFamilies){
    VkQueueFamilyProperties missingCompute[2] = {};
    missingCompute[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    missingCompute[0].queueCount = 0u;
    missingCompute[1].queueFlags = VK_QUEUE_GRAPHICS_BIT;
    missingCompute[1].queueCount = 1u;
    const auto noCompute = VulkanDetail::SelectRequiredQueueFamilies(missingCompute, 2u, false, false);
    EXPECT_EQ(noCompute.graphicsFamily, 1);
    EXPECT_EQ(noCompute.computeFamily, GraphicsBackend::s_InvalidQueueFamilyIndex);

    VkQueueFamilyProperties missingGraphics[1] = {};
    missingGraphics[0].queueFlags = VK_QUEUE_COMPUTE_BIT;
    missingGraphics[0].queueCount = 1u;
    const auto noGraphics = VulkanDetail::SelectRequiredQueueFamilies(missingGraphics, 1u, false, false);
    EXPECT_EQ(noGraphics.graphicsFamily, GraphicsBackend::s_InvalidQueueFamilyIndex);
    EXPECT_EQ(noGraphics.computeFamily, 0);
}

TEST(VulkanQueueFamilySelection, SelectsDedicatedTransferOnlyWhenRequested){
    VkQueueFamilyProperties families[2] = {};
    families[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    families[0].queueCount = 1u;
    families[1].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1u;

    const auto disabled = VulkanDetail::SelectRequiredQueueFamilies(families, 2u, false, false);
    EXPECT_EQ(disabled.dedicatedTransferFamily, GraphicsBackend::s_InvalidQueueFamilyIndex);
    const auto enabled = VulkanDetail::SelectRequiredQueueFamilies(families, 2u, false, true);
    EXPECT_EQ(enabled.dedicatedTransferFamily, 1);
}

TEST(VulkanAdapterSelection, MandatoryFeatureContractIncludesCoreVersionedAndExtensionFeatures){
    auto support = FullySupportedFeatures();
    EXPECT_EQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), nullptr);

    support.vulkan12.shaderFloat16 = VK_FALSE;
    EXPECT_STREQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), "shaderFloat16");
    support = FullySupportedFeatures();
    support.dynamicRendering.dynamicRendering = VK_FALSE;
    EXPECT_STREQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), "dynamicRendering");
    support = FullySupportedFeatures();
    support.descriptorBuffer.descriptorBuffer = VK_FALSE;
    EXPECT_STREQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), "descriptorBuffer");
}

TEST(VulkanAdapterSelection, PreflightsFullFeatureContractBeforeDiscretePreference){
    TestArena testArena;
    AString adapterSource;
    ASSERT_TRUE(ReadTextFile(
        RepoRoot(testArena) / "core" / "graphics" / "vulkan" / "backend_context_adapter.cpp",
        adapterSource
    ));
    const AStringView source(adapterSource.data(), adapterSource.size());
    const usize pickBegin = source.find("bool BackendContext::pickPhysicalDevice(){");
    const usize pickEnd = source.find("// Adapter enumeration", pickBegin);
    ASSERT_NE(pickBegin, AStringView::npos);
    ASSERT_NE(pickEnd, AStringView::npos);
    const AStringView pick = source.substr(pickBegin, pickEnd - pickBegin);
    const usize queryOffset = pick.find("VulkanDetail::QueryPhysicalDeviceFeatureSupport(");
    const usize validationOffset = pick.find("VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(", queryOffset);
    const usize discretePreferenceOffset = pick.find("prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU", validationOffset);
    ASSERT_NE(queryOffset, AStringView::npos);
    ASSERT_NE(validationOffset, AStringView::npos);
    ASSERT_NE(discretePreferenceOffset, AStringView::npos);
    EXPECT_LT(queryOffset, validationOffset);
    EXPECT_LT(validationOffset, discretePreferenceOffset);
}

TEST(VulkanQueueFamilySelection, UsesExactSurfaceSupportInsteadOfGenericWin32PresentationCapability){
    TestArena testArena;
    AString adapterSource;
    ASSERT_TRUE(ReadTextFile(
        RepoRoot(testArena) / "core" / "graphics" / "vulkan" / "backend_context_adapter.cpp",
        adapterSource
    ));
    const AStringView source(adapterSource.data(), adapterSource.size());
    const usize queueBegin = source.find("bool BackendContext::findQueueFamilies(VkPhysicalDevice physicalDevice){");
    const usize queueEnd = source.find("bool BackendContext::pickPhysicalDevice(){", queueBegin);
    ASSERT_NE(queueBegin, AStringView::npos);
    ASSERT_NE(queueEnd, AStringView::npos);
    const AStringView queueSelection = source.substr(queueBegin, queueEnd - queueBegin);
    EXPECT_NE(queueSelection.find("vkGetPhysicalDeviceSurfaceSupportKHR"), AStringView::npos);
    EXPECT_EQ(queueSelection.find("vkGetPhysicalDeviceWin32PresentationSupportKHR"), AStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

