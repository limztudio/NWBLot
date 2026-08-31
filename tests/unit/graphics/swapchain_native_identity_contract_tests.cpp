// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_swapchain_native_identity_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct SwapchainNativeIdentityContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<SwapchainNativeIdentityContractTestArenaTag>;


static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SwapChainPresentation, NativeTextureImportReceivesExactSwapchainProvenanceBeforePublication){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfaceSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_surface.cpp",
        surfaceSource
    ));
    const AStringView fullSurfaceSource(surfaceSource.data(), surfaceSource.size());
    const usize createFunctionBegin = fullSurfaceSource.find("bool BackendContext::createVulkanSwapChain(){");
    const usize createFunctionEnd = fullSurfaceSource.find("NWB_VULKAN_END", createFunctionBegin);
    ASSERT_NE(createFunctionBegin, AStringView::npos);
    ASSERT_NE(createFunctionEnd, AStringView::npos);
    ASSERT_LT(createFunctionBegin, createFunctionEnd);
    const AStringView createFunction = fullSurfaceSource.substr(
        createFunctionBegin,
        createFunctionEnd - createFunctionBegin
    );
    const usize topologyOffset = createFunction.find(
        "const GpuPhysicalQueueTopology topology = m_rhiDevice->getPhysicalQueueTopology();"
    );
    const usize familyVectorOffset = createFunction.find(
        "Vector<u32, Alloc::ScratchArena> queueFamilyIndices(scratchArena);",
        topologyOffset
    );
    const usize familyReserveOffset = createFunction.find(
        "queueFamilyIndices.reserve(topology.queueCount + 1u);",
        familyVectorOffset
    );
    const usize appendLambdaOffset = createFunction.find(
        "const auto appendQueueFamily = [&queueFamilyIndices](const u32 familyIndex){",
        familyReserveOffset
    );
    const usize primaryGraphicsOffset = createFunction.find(
        "appendQueueFamily(static_cast<u32>(m_graphicsQueueFamily));",
        appendLambdaOffset
    );
    const usize topologyGraphicsLoopOffset = createFunction.find(
        "for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){",
        primaryGraphicsOffset
    );
    const usize topologyQueueOffset = createFunction.find(
        "const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];",
        topologyGraphicsLoopOffset
    );
    const usize topologyGraphicsClassOffset = createFunction.find(
        "if(queue.queueClass == CommandQueue::Graphics)",
        topologyQueueOffset
    );
    const usize topologyGraphicsFamilyOffset = createFunction.find(
        "appendQueueFamily(queue.familyIndex);",
        topologyGraphicsClassOffset
    );
    const usize presentFamilyOffset = createFunction.find(
        "appendQueueFamily(static_cast<u32>(m_presentQueueFamily));",
        topologyGraphicsFamilyOffset
    );
    const usize sharingEnableOffset = createFunction.find(
        "const bool enableSwapChainSharing = queueFamilyIndices.size() > 1u;",
        presentFamilyOffset
    );
    const usize swapchainDescOffset = createFunction.find("VkSwapchainCreateInfoKHR desc = {};", sharingEnableOffset);
    const usize sharingModeOffset = createFunction.find(
        "desc.imageSharingMode = enableSwapChainSharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;",
        swapchainDescOffset
    );
    const usize familyCountOffset = createFunction.find(
        "desc.queueFamilyIndexCount = enableSwapChainSharing ? static_cast<u32>(queueFamilyIndices.size()) : 0u;",
        sharingModeOffset
    );
    const usize familyPointerOffset = createFunction.find(
        "desc.pQueueFamilyIndices = enableSwapChainSharing ? queueFamilyIndices.data() : nullptr;",
        familyCountOffset
    );
    const usize swapchainCreateOffset = createFunction.find(
        "vkCreateSwapchainKHR(m_vulkanDevice, &desc, nullptr, &m_swapChain)",
        familyPointerOffset
    );
    ASSERT_NE(topologyOffset, AStringView::npos);
    ASSERT_NE(familyVectorOffset, AStringView::npos);
    ASSERT_NE(familyReserveOffset, AStringView::npos);
    ASSERT_NE(appendLambdaOffset, AStringView::npos);
    ASSERT_NE(primaryGraphicsOffset, AStringView::npos);
    ASSERT_NE(topologyGraphicsLoopOffset, AStringView::npos);
    ASSERT_NE(topologyQueueOffset, AStringView::npos);
    ASSERT_NE(topologyGraphicsClassOffset, AStringView::npos);
    ASSERT_NE(topologyGraphicsFamilyOffset, AStringView::npos);
    ASSERT_NE(presentFamilyOffset, AStringView::npos);
    ASSERT_NE(sharingEnableOffset, AStringView::npos);
    ASSERT_NE(swapchainDescOffset, AStringView::npos);
    ASSERT_NE(sharingModeOffset, AStringView::npos);
    ASSERT_NE(familyCountOffset, AStringView::npos);
    ASSERT_NE(familyPointerOffset, AStringView::npos);
    ASSERT_NE(swapchainCreateOffset, AStringView::npos);
    EXPECT_LT(topologyOffset, familyVectorOffset);
    EXPECT_LT(familyVectorOffset, familyReserveOffset);
    EXPECT_LT(familyReserveOffset, appendLambdaOffset);
    EXPECT_LT(appendLambdaOffset, primaryGraphicsOffset);
    EXPECT_LT(primaryGraphicsOffset, topologyGraphicsLoopOffset);
    EXPECT_LT(topologyGraphicsLoopOffset, topologyQueueOffset);
    EXPECT_LT(topologyQueueOffset, topologyGraphicsClassOffset);
    EXPECT_LT(topologyGraphicsClassOffset, topologyGraphicsFamilyOffset);
    EXPECT_LT(topologyGraphicsFamilyOffset, presentFamilyOffset);
    EXPECT_LT(presentFamilyOffset, sharingEnableOffset);
    EXPECT_LT(sharingEnableOffset, swapchainDescOffset);
    EXPECT_LT(swapchainDescOffset, sharingModeOffset);
    EXPECT_LT(sharingModeOffset, familyCountOffset);
    EXPECT_LT(familyCountOffset, familyPointerOffset);
    EXPECT_LT(familyPointerOffset, swapchainCreateOffset);

    const AStringView appendQueueFamily = createFunction.substr(
        appendLambdaOffset,
        primaryGraphicsOffset - appendLambdaOffset
    );
    const usize dedupeLoopOffset = appendQueueFamily.find("for(const u32 existingFamilyIndex : queueFamilyIndices){");
    const usize duplicateFamilyOffset = appendQueueFamily.find(
        "if(existingFamilyIndex == familyIndex)",
        dedupeLoopOffset
    );
    const usize duplicateReturnOffset = appendQueueFamily.find("return;", duplicateFamilyOffset);
    const usize familyPushOffset = appendQueueFamily.find("queueFamilyIndices.push_back(familyIndex);", duplicateReturnOffset);
    ASSERT_NE(dedupeLoopOffset, AStringView::npos);
    ASSERT_NE(duplicateFamilyOffset, AStringView::npos);
    ASSERT_NE(duplicateReturnOffset, AStringView::npos);
    ASSERT_NE(familyPushOffset, AStringView::npos);
    EXPECT_LT(dedupeLoopOffset, duplicateFamilyOffset);
    EXPECT_LT(duplicateFamilyOffset, duplicateReturnOffset);
    EXPECT_LT(duplicateReturnOffset, familyPushOffset);

    const usize imageFlagsOffset = createFunction.find("const VkImageCreateFlags swapChainImageFlags =");
    const usize imageFlagsEndOffset = createFunction.find(";", imageFlagsOffset);
    ASSERT_NE(imageFlagsOffset, AStringView::npos);
    ASSERT_NE(imageFlagsEndOffset, AStringView::npos);
    const AStringView imageFlags = createFunction.substr(
        imageFlagsOffset,
        imageFlagsEndOffset - imageFlagsOffset
    );
    EXPECT_NE(imageFlags.find("desc.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR"), AStringView::npos);
    EXPECT_NE(
        imageFlags.find("? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT"),
        AStringView::npos
    );
    EXPECT_NE(imageFlags.find(": 0u"), AStringView::npos);
    const usize provenanceOffset = createFunction.find("const NativeTextureProvenance nativeProvenance{");
    const usize importOffset = createFunction.find("m_rhiDevice->createHandleForNativeTexture(");
    const usize publicationOffset = createFunction.find("m_swapChainImages.push_back(Move(sci));", importOffset);
    const usize logicalSharingOffset = createFunction.find(
        "textureDesc.queueSharing = ResourceQueueSharing::Graphics"
    );
    ASSERT_NE(provenanceOffset, AStringView::npos);
    ASSERT_NE(importOffset, AStringView::npos);
    ASSERT_NE(publicationOffset, AStringView::npos);
    ASSERT_NE(logicalSharingOffset, AStringView::npos);
    EXPECT_LT(swapchainCreateOffset, provenanceOffset);
    ASSERT_LT(logicalSharingOffset, provenanceOffset);
    ASSERT_LT(provenanceOffset, importOffset);
    EXPECT_LT(importOffset, publicationOffset);
    const usize importEndOffset = createFunction.find(");", importOffset);
    const usize provenanceArgumentOffset = createFunction.find("nativeProvenance", importOffset);
    ASSERT_NE(importEndOffset, AStringView::npos);
    ASSERT_NE(provenanceArgumentOffset, AStringView::npos);
    EXPECT_LT(provenanceArgumentOffset, importEndOffset);
    const AStringView provenance = createFunction.substr(provenanceOffset, importOffset - provenanceOffset);
    EXPECT_NE(provenance.find(".usage = desc.imageUsage"), AStringView::npos);
    EXPECT_NE(provenance.find(".flags = swapChainImageFlags"), AStringView::npos);
    EXPECT_NE(provenance.find(".sharingMode = desc.imageSharingMode"), AStringView::npos);
    EXPECT_NE(provenance.find(".queueFamilyIndexCount = desc.queueFamilyIndexCount"), AStringView::npos);
    EXPECT_NE(provenance.find(".queueFamilyIndices = desc.pQueueFamilyIndices"), AStringView::npos);
    EXPECT_NE(provenance.find(".initialStateKnown = false"), AStringView::npos);
    EXPECT_EQ(createFunction.find("sci.rhiHandle->m_imageInfo"), AStringView::npos);
    EXPECT_EQ(createFunction.find("sci.rhiHandle->initializeRetainedSubresourceStates"), AStringView::npos);

    AString textureDeviceSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "texture_device.cpp",
        textureDeviceSource
    ));
    const AStringView fullTextureDeviceSource(textureDeviceSource.data(), textureDeviceSource.size());
    const usize importFunctionBegin = fullTextureDeviceSource.find(
        "TextureHandle Device::createHandleForNativeTexture("
    );
    const usize importFunctionEnd = fullTextureDeviceSource.find(
        "SamplerHandle Device::createSampler(",
        importFunctionBegin
    );
    ASSERT_NE(importFunctionBegin, AStringView::npos);
    ASSERT_NE(importFunctionEnd, AStringView::npos);
    ASSERT_LT(importFunctionBegin, importFunctionEnd);
    const AStringView importFunction = fullTextureDeviceSource.substr(
        importFunctionBegin,
        importFunctionEnd - importFunctionBegin
    );
    const usize usageValidationOffset = importFunction.find("if(nativeProvenance.usage == 0u){");
    const usize sharingValidationOffset = importFunction.find(
        "__hidden_texture_device::ValidateNativeTextureSharing("
    );
    const usize consistencyValidationOffset = importFunction.find(
        "VulkanTextureDetail::IsTextureImageInfoConsistent(desc, imageInfo)"
    );
    const usize allocationOffset = importFunction.find("auto* texture = NewArenaObject<Texture>(");
    const usize identityPublicationOffset = importFunction.find(
        "m_allocator.tryRegisterTextureNativeIdentity(*texture)"
    );
    ASSERT_NE(usageValidationOffset, AStringView::npos);
    ASSERT_NE(sharingValidationOffset, AStringView::npos);
    ASSERT_NE(consistencyValidationOffset, AStringView::npos);
    ASSERT_NE(allocationOffset, AStringView::npos);
    ASSERT_NE(identityPublicationOffset, AStringView::npos);
    EXPECT_LT(usageValidationOffset, sharingValidationOffset);
    EXPECT_LT(sharingValidationOffset, consistencyValidationOffset);
    EXPECT_LT(consistencyValidationOffset, allocationOffset);
    EXPECT_LT(allocationOffset, identityPublicationOffset);
}


// Device creation owns one canonical identity for every queue requested from Vulkan, while only scheduler-visible
// roles enter physical topology. A distinct present-only queue therefore remains available for synchronized WSI
// without being mislabeled as a task-graph queue.
TEST(SwapChainPresentation, CanonicalNativeQueueRegistryPrecedesPhysicalSchedulerProjection){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString moduleHeader;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "module.h", moduleHeader));
    const AStringView fullModuleHeader(moduleHeader.data(), moduleHeader.size());
    const usize nativeDescOffset = fullModuleHeader.find("struct VulkanNativeQueueDesc{");
    const usize physicalDescOffset = fullModuleHeader.find("struct VulkanPhysicalQueueDesc{");
    const usize nativeIndexOffset = fullModuleHeader.find(
        "u32 nativeQueueIndex = Limit<u32>::s_Max;",
        physicalDescOffset
    );
    ASSERT_NE(nativeDescOffset, AStringView::npos);
    ASSERT_NE(physicalDescOffset, AStringView::npos);
    ASSERT_NE(nativeIndexOffset, AStringView::npos);
    EXPECT_LT(nativeDescOffset, physicalDescOffset);
    EXPECT_LT(physicalDescOffset, nativeIndexOffset);

    AString deviceSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_device.cpp",
        deviceSource
    ));
    const AStringView fullDeviceSource(deviceSource.data(), deviceSource.size());
    const usize nativeLoopOffset = fullDeviceSource.find("for(const VkDeviceQueueCreateInfo& queueInfo : queueDesc){");
    const usize queueIndexLoopOffset = fullDeviceSource.find(
        "for(u32 nativeQueueOffset = 0u; nativeQueueOffset < queueInfo.queueCount; ++nativeQueueOffset){",
        nativeLoopOffset
    );
    const usize nativeGetOffset = fullDeviceSource.find(
        "vkGetDeviceQueue(m_vulkanDevice, queueInfo.queueFamilyIndex, nativeQueueOffset, &queue);",
        queueIndexLoopOffset
    );
    const usize nativePublishOffset = fullDeviceSource.find(
        "m_nativeQueues.push_back(VulkanNativeQueueDesc{",
        nativeGetOffset
    );
    const usize presentLookupOffset = fullDeviceSource.find(
        "m_presentNativeQueueIndex = findNativeQueueIndex(",
        nativePublishOffset
    );
    const usize presentFamilyAppendOffset = fullDeviceSource.find(
        "appendUniqueQueueFamily(m_presentQueueFamily);"
    );
    ASSERT_NE(nativeLoopOffset, AStringView::npos);
    ASSERT_NE(queueIndexLoopOffset, AStringView::npos);
    ASSERT_NE(nativeGetOffset, AStringView::npos);
    ASSERT_NE(nativePublishOffset, AStringView::npos);
    ASSERT_NE(presentLookupOffset, AStringView::npos);
    ASSERT_NE(presentFamilyAppendOffset, AStringView::npos);
    EXPECT_LT(presentFamilyAppendOffset, nativeLoopOffset);
    EXPECT_LT(nativeLoopOffset, queueIndexLoopOffset);
    EXPECT_LT(queueIndexLoopOffset, nativeGetOffset);
    EXPECT_LT(nativeGetOffset, nativePublishOffset);
    EXPECT_LT(nativePublishOffset, presentLookupOffset);

    AString orchestrationSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp",
        orchestrationSource
    ));
    const AStringView fullOrchestrationSource(orchestrationSource.data(), orchestrationSource.size());
    const usize physicalProjectionOffset = fullOrchestrationSource.find(
        "Vector<VulkanPhysicalQueueDesc, Alloc::ScratchArena> physicalQueues{scratchArena};"
    );
    const usize nativeRegistryOffset = fullOrchestrationSource.find(
        "deviceDesc.nativeQueues = m_nativeQueues.data();"
    );
    const usize nativeCountOffset = fullOrchestrationSource.find(
        "deviceDesc.nativeQueueCount = m_nativeQueues.size();",
        nativeRegistryOffset
    );
    const usize physicalRegistryOffset = fullOrchestrationSource.find(
        "deviceDesc.physicalQueues = physicalQueues.data();",
        nativeCountOffset
    );
    const usize physicalCountOffset = fullOrchestrationSource.find(
        "deviceDesc.physicalQueueCount = physicalQueues.size();",
        physicalRegistryOffset
    );
    ASSERT_NE(physicalProjectionOffset, AStringView::npos);
    ASSERT_NE(nativeRegistryOffset, AStringView::npos);
    ASSERT_NE(nativeCountOffset, AStringView::npos);
    ASSERT_NE(physicalRegistryOffset, AStringView::npos);
    ASSERT_NE(physicalCountOffset, AStringView::npos);
    EXPECT_LT(physicalProjectionOffset, nativeRegistryOffset);
    EXPECT_LT(nativeRegistryOffset, nativeCountOffset);
    EXPECT_LT(nativeCountOffset, physicalRegistryOffset);
    EXPECT_EQ(
        fullOrchestrationSource.substr(physicalProjectionOffset, physicalCountOffset - physicalProjectionOffset).find(
            "m_presentNativeQueueIndex"
        ),
        AStringView::npos
    );
    EXPECT_EQ(fullOrchestrationSource.find(".queue = m_graphicsQueue"), AStringView::npos);
    EXPECT_EQ(fullOrchestrationSource.find(".queue = m_computeQueue"), AStringView::npos);
    EXPECT_EQ(fullOrchestrationSource.find(".queue = m_transferQueue"), AStringView::npos);

    AString rhiDeviceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device.cpp", rhiDeviceSource));
    const AStringView fullRhiDeviceSource(rhiDeviceSource.data(), rhiDeviceSource.size());
    const usize familyBoundsOffset = fullRhiDeviceSource.find(
        "nativeQueue.familyIndex >= physicalQueueFamilyCount"
    );
    const usize queueBoundsOffset = fullRhiDeviceSource.find(
        "nativeQueue.queueIndex >= physicalQueueFamilies[nativeQueue.familyIndex].queueCount",
        familyBoundsOffset
    );
    ASSERT_NE(familyBoundsOffset, AStringView::npos);
    ASSERT_NE(queueBoundsOffset, AStringView::npos);
    EXPECT_LT(familyBoundsOffset, queueBoundsOffset);

    AString queueSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device_queue.cpp", queueSource));
    const AStringView fullQueueSource(queueSource.data(), queueSource.size());
    EXPECT_NE(
        fullQueueSource.find("desc.primaryForClass && m_explicitPrimaryQueues[queueClassIndex]"),
        AStringView::npos
    );
}


// Swapchain teardown must invalidate retained wrappers before the driver can destroy their images, while reserving
// each native identity until destruction is complete. This source contract covers the WSI-only interval that a
// headless public fixture cannot enter without exposing a production mutation seam.
TEST(SwapChainPresentation, NativeTextureRetirementBracketsNativeDestructionAndClearsViews){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfaceSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_surface.cpp",
        surfaceSource
    ));
    const AStringView fullSurfaceSource(surfaceSource.data(), surfaceSource.size());
    const usize destroyFunctionBegin = fullSurfaceSource.find("void BackendContext::destroySwapChain(){");
    const usize destroyFunctionEnd = fullSurfaceSource.find(
        "bool BackendContext::createVulkanSwapChain(){",
        destroyFunctionBegin
    );
    ASSERT_NE(destroyFunctionBegin, AStringView::npos);
    ASSERT_NE(destroyFunctionEnd, AStringView::npos);
    ASSERT_LT(destroyFunctionBegin, destroyFunctionEnd);
    const AStringView destroyFunction = fullSurfaceSource.substr(
        destroyFunctionBegin,
        destroyFunctionEnd - destroyFunctionBegin
    );
    const usize revokeOffset = destroyFunction.find(
        "swapChainImage.rhiHandle->revokeUnmanagedNativeImage(swapChainImage.image)"
    );
    const usize nativeDestroyOffset = destroyFunction.find(
        "vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr)"
    );
    const usize releaseOffset = destroyFunction.find(
        "swapChainImage.rhiHandle->releaseRevokedNativeImageIdentity(swapChainImage.image)"
    );
    const usize wrapperClearOffset = destroyFunction.find("m_swapChainImages.clear();");
    ASSERT_NE(revokeOffset, AStringView::npos);
    ASSERT_NE(nativeDestroyOffset, AStringView::npos);
    ASSERT_NE(releaseOffset, AStringView::npos);
    ASSERT_NE(wrapperClearOffset, AStringView::npos);
    EXPECT_LT(revokeOffset, nativeDestroyOffset);
    EXPECT_LT(nativeDestroyOffset, releaseOffset);
    EXPECT_LT(releaseOffset, wrapperClearOffset);

    AString textureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "texture.cpp", textureSource));
    const AStringView fullTextureSource(textureSource.data(), textureSource.size());
    const usize revokeFunctionBegin = fullTextureSource.find("bool Texture::revokeUnmanagedNativeImage(");
    const usize releaseFunctionBegin = fullTextureSource.find(
        "void Texture::releaseRevokedNativeImageIdentity(",
        revokeFunctionBegin
    );
    const usize releaseFunctionEnd = fullTextureSource.find(
        "bool Texture::isRetainedSubresourceStateKnown(",
        releaseFunctionBegin
    );
    ASSERT_NE(revokeFunctionBegin, AStringView::npos);
    ASSERT_NE(releaseFunctionBegin, AStringView::npos);
    ASSERT_NE(releaseFunctionEnd, AStringView::npos);
    ASSERT_LT(revokeFunctionBegin, releaseFunctionBegin);
    ASSERT_LT(releaseFunctionBegin, releaseFunctionEnd);

    const AStringView revokeFunction = fullTextureSource.substr(
        revokeFunctionBegin,
        releaseFunctionBegin - revokeFunctionBegin
    );
    const usize viewDestroyOffset = revokeFunction.find("vkDestroyImageView(");
    const usize viewClearOffset = revokeFunction.find("m_views.clear();");
    const usize imageClearOffset = revokeFunction.find("m_image = VK_NULL_HANDLE;");
    ASSERT_NE(viewDestroyOffset, AStringView::npos);
    ASSERT_NE(viewClearOffset, AStringView::npos);
    ASSERT_NE(imageClearOffset, AStringView::npos);
    EXPECT_LT(viewDestroyOffset, viewClearOffset);
    EXPECT_LT(viewClearOffset, imageClearOffset);
    EXPECT_EQ(revokeFunction.find("unregisterTextureNativeIdentity"), AStringView::npos);

    const AStringView releaseFunction = fullTextureSource.substr(
        releaseFunctionBegin,
        releaseFunctionEnd - releaseFunctionBegin
    );
    const usize revokedStateCheckOffset = releaseFunction.find("if(m_managed || m_image != VK_NULL_HANDLE)");
    const usize identityReleaseOffset = releaseFunction.find(
        "m_allocator.unregisterTextureNativeIdentity(expectedNativeImage, *this);"
    );
    ASSERT_NE(revokedStateCheckOffset, AStringView::npos);
    ASSERT_NE(identityReleaseOffset, AStringView::npos);
    EXPECT_LT(revokedStateCheckOffset, identityReleaseOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

