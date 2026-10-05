// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <core/graphics/backend_selection/test/backend_context_capabilities.h>
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

TEST(VulkanAdapterSelection, ExtensionLookupHonorsNonTerminatedViewBounds){
    VkExtensionProperties extensions[1] = {};
    GLB_STRCPY(extensions[0].extensionName, VK_MAX_EXTENSION_NAME_SIZE, "VK_EXT_sample");
    constexpr char s_NameWithSuffix[] = "VK_EXT_sample_suffix";
    constexpr usize s_ExtensionNameLength = sizeof("VK_EXT_sample") - 1u;
    EXPECT_TRUE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView(s_NameWithSuffix, s_ExtensionNameLength)));
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView(s_NameWithSuffix)));
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView(s_NameWithSuffix, s_ExtensionNameLength - 1u)));
}


TEST(VulkanAdapterSelection, ExtensionLookupBoundsNativeNamesAndRejectsEmbeddedNullRequests){
    VkExtensionProperties extensions[1] = {};
    char requestedName[VK_MAX_EXTENSION_NAME_SIZE];
    for(usize characterIndex = 0u; characterIndex < VK_MAX_EXTENSION_NAME_SIZE; ++characterIndex){
        requestedName[characterIndex] = 'a';
        if(characterIndex + 1u < VK_MAX_EXTENSION_NAME_SIZE)
            extensions[0].extensionName[characterIndex] = 'a';
    }
    const AStringView maximumFittingName(requestedName, VK_MAX_EXTENSION_NAME_SIZE - 1u);
    EXPECT_TRUE(VulkanDetail::HasDeviceExtension(extensions, 1u, maximumFittingName));
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView(requestedName, VK_MAX_EXTENSION_NAME_SIZE)));

    extensions[0].extensionName[VK_MAX_EXTENSION_NAME_SIZE - 1u] = 'a';
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(extensions, 1u, maximumFittingName));

    extensions[0].extensionName[1u] = '\0';
    constexpr char s_EmbeddedNullName[] = { 'a', '\0', 'a' };
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView(s_EmbeddedNullName, sizeof(s_EmbeddedNullName))));
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView{}));

    extensions[0].extensionName[0u] = '\0';
    EXPECT_TRUE(VulkanDetail::HasDeviceExtension(extensions, 1u, AStringView{}));
    EXPECT_FALSE(VulkanDetail::HasDeviceExtension(nullptr, 0u, AStringView{}));
}


TEST(VulkanAdapterSelection, MandatoryFeatureContractIncludesCoreVersionedAndExtensionFeatures){
    auto support = FullySupportedFeatures();
    EXPECT_TRUE(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support).empty());

    support.vulkan12.shaderFloat16 = VK_FALSE;
    EXPECT_EQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), AStringView("shaderFloat16"));
    support = FullySupportedFeatures();
    support.dynamicRendering.dynamicRendering = VK_FALSE;
    EXPECT_EQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), AStringView("dynamicRendering"));
    support = FullySupportedFeatures();
    support.descriptorBuffer.descriptorBuffer = VK_FALSE;
    EXPECT_EQ(VulkanDetail::FindMissingMandatoryPhysicalDeviceFeature(support), AStringView("descriptorBuffer"));
}

TEST(VulkanAdapterSelection, PreflightsFullFeatureContractBeforeDiscretePreference){
    TestArena testArena;
    AString adapterSource;
    ASSERT_TRUE(ReadTextFile(
        NWB::Tests::RepoRootOf(testArena.arena, __FILE__) / "core" / "graphics" / "vulkan" / "backend_context_adapter.cpp",
        adapterSource
    ));
    const AStringView source(adapterSource.data(), adapterSource.size());
    const usize pickBegin = source.find("bool BackendContext::pickPhysicalDevice(){");
    ASSERT_NE(pickBegin, AStringView::npos);
    const usize pickEnd = source.find(
        "bool BackendContext::enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters){",
        pickBegin
    );
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

