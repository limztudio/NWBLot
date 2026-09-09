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


TEST(SwapChainPresentation, SwapchainImageUsageMatchesPresentationAndOptionalReadbackConsumers){
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

    const usize requiredUsageOffset = createFunction.find(
        "constexpr VkImageUsageFlags s_RequiredSwapChainImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;"
    );
    const usize requiredUsageValidationOffset = createFunction.find(
        "(surfaceCaps.supportedUsageFlags & s_RequiredSwapChainImageUsage) != s_RequiredSwapChainImageUsage",
        requiredUsageOffset
    );
    const usize optionalReadbackOffset = createFunction.find(
        "const bool swapChainReadbackAvailable = m_deviceParams.enableSwapChainReadback",
        requiredUsageValidationOffset
    );
    const usize readbackSupportOffset = createFunction.find(
        "(surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u",
        optionalReadbackOffset
    );
    const usize imageUsageOffset = createFunction.find(
        "desc.imageUsage = s_RequiredSwapChainImageUsage;",
        readbackSupportOffset
    );
    const usize optionalReadbackConditionOffset = createFunction.find(
        "if(swapChainReadbackAvailable)",
        imageUsageOffset
    );
    const usize optionalReadbackUsageOffset = createFunction.find(
        "desc.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;",
        optionalReadbackConditionOffset
    );
    const usize logicalNonSampledOffset = createFunction.find(
        "textureDesc.isShaderResource = false;",
        optionalReadbackUsageOffset
    );
    const usize logicalRenderTargetOffset = createFunction.find(
        "textureDesc.isRenderTarget = true;",
        logicalNonSampledOffset
    );
    const usize nativeUsageOffset = createFunction.find(".usage = desc.imageUsage", logicalRenderTargetOffset);
    ASSERT_NE(requiredUsageOffset, AStringView::npos);
    ASSERT_NE(requiredUsageValidationOffset, AStringView::npos);
    ASSERT_NE(optionalReadbackOffset, AStringView::npos);
    ASSERT_NE(readbackSupportOffset, AStringView::npos);
    ASSERT_NE(imageUsageOffset, AStringView::npos);
    ASSERT_NE(optionalReadbackConditionOffset, AStringView::npos);
    ASSERT_NE(optionalReadbackUsageOffset, AStringView::npos);
    ASSERT_NE(logicalNonSampledOffset, AStringView::npos);
    ASSERT_NE(logicalRenderTargetOffset, AStringView::npos);
    ASSERT_NE(nativeUsageOffset, AStringView::npos);
    EXPECT_LT(requiredUsageOffset, requiredUsageValidationOffset);
    EXPECT_LT(requiredUsageValidationOffset, optionalReadbackOffset);
    EXPECT_LT(optionalReadbackOffset, readbackSupportOffset);
    EXPECT_LT(readbackSupportOffset, imageUsageOffset);
    EXPECT_LT(imageUsageOffset, optionalReadbackConditionOffset);
    EXPECT_LT(optionalReadbackConditionOffset, optionalReadbackUsageOffset);
    EXPECT_LT(optionalReadbackUsageOffset, logicalNonSampledOffset);
    EXPECT_LT(logicalNonSampledOffset, logicalRenderTargetOffset);
    EXPECT_LT(logicalRenderTargetOffset, nativeUsageOffset);
    EXPECT_EQ(createFunction.find("VK_IMAGE_USAGE_TRANSFER_DST_BIT"), AStringView::npos);
    EXPECT_EQ(createFunction.find("VK_IMAGE_USAGE_SAMPLED_BIT"), AStringView::npos);
}


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


// Scheduler submission, WSI presentation, queue waits, event-query submits, and device idle must share the one
// canonical host lock for each VkQueue. Device idle additionally freezes every scheduler-semantic queue first, then
// covers present-only native states before entering Vulkan.
TEST(SwapChainPresentation, CanonicalNativeQueueStateSerializesEveryInternalHostAccess){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString nativeStateHeader;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "native_queue_state.h",
        nativeStateHeader
    ));
    const AStringView fullNativeStateHeader(nativeStateHeader.data(), nativeStateHeader.size());
    EXPECT_NE(fullNativeStateHeader.find("VkQueue queue = VK_NULL_HANDLE;"), AStringView::npos);
    EXPECT_NE(fullNativeStateHeader.find("Futex hostMutex;"), AStringView::npos);

    AString backendHeader;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", backendHeader));
    const AStringView fullBackendHeader(backendHeader.data(), backendHeader.size());
    const usize queueClassOffset = fullBackendHeader.find("class Queue final : NoCopy{");
    const usize eventQueryClassOffset = fullBackendHeader.find("class EventQuery final", queueClassOffset);
    ASSERT_NE(queueClassOffset, AStringView::npos);
    ASSERT_NE(eventQueryClassOffset, AStringView::npos);
    const AStringView queueClass = fullBackendHeader.substr(
        queueClassOffset,
        eventQueryClassOffset - queueClassOffset
    );
    EXPECT_NE(queueClass.find("NativeQueueState& m_nativeQueue;"), AStringView::npos);
    EXPECT_NE(queueClass.find("Futex m_mutex;"), AStringView::npos);
    EXPECT_EQ(queueClass.find("VkQueue m_queue;"), AStringView::npos);
    EXPECT_NE(fullBackendHeader.find("GraphicsVector<NativeQueueState*> m_nativeQueueStates;"), AStringView::npos);
    EXPECT_NE(fullBackendHeader.find("[[nodiscard]] bool setEventQuery(EventQuery& query"), AStringView::npos);
    EXPECT_NE(fullBackendHeader.find("[[nodiscard]] bool waitEventQuery(EventQuery& query"), AStringView::npos);

    AString queueSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "queue_submission.cpp", queueSource));
    const AStringView fullQueueSource(queueSource.data(), queueSource.size());
    const usize submitHostLockOffset = fullQueueSource.find("ScopedLock hostLock(m_nativeQueue.hostMutex);");
    const usize nativeSubmitOffset = fullQueueSource.find("vkQueueSubmit2(m_nativeQueue.queue", submitHostLockOffset);
    const usize waitHostLockOffset = fullQueueSource.find(
        "ScopedLock hostLock(m_nativeQueue.hostMutex);",
        nativeSubmitOffset
    );
    const usize nativeWaitOffset = fullQueueSource.find("vkQueueWaitIdle(m_nativeQueue.queue)", waitHostLockOffset);
    ASSERT_NE(submitHostLockOffset, AStringView::npos);
    ASSERT_NE(nativeSubmitOffset, AStringView::npos);
    ASSERT_NE(waitHostLockOffset, AStringView::npos);
    ASSERT_NE(nativeWaitOffset, AStringView::npos);
    EXPECT_LT(submitHostLockOffset, nativeSubmitOffset);
    EXPECT_LT(waitHostLockOffset, nativeWaitOffset);

    AString deviceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device.cpp", deviceSource));
    const AStringView fullDeviceSource(deviceSource.data(), deviceSource.size());
    const usize semanticLockOffset = fullDeviceSource.find("queue->m_mutex.lock();");
    const usize hostLockOffset = fullDeviceSource.find("nativeQueueState->hostMutex.lock();", semanticLockOffset);
    const usize deviceIdleOffset = fullDeviceSource.find("vkDeviceWaitIdle(m_context.device);", hostLockOffset);
    const usize hostUnlockOffset = fullDeviceSource.find("nativeQueueState->hostMutex.unlock();", deviceIdleOffset);
    const usize semanticUnlockOffset = fullDeviceSource.find("queue->m_mutex.unlock();", hostUnlockOffset);
    ASSERT_NE(semanticLockOffset, AStringView::npos);
    ASSERT_NE(hostLockOffset, AStringView::npos);
    ASSERT_NE(deviceIdleOffset, AStringView::npos);
    ASSERT_NE(hostUnlockOffset, AStringView::npos);
    ASSERT_NE(semanticUnlockOffset, AStringView::npos);
    EXPECT_LT(semanticLockOffset, hostLockOffset);
    EXPECT_LT(hostLockOffset, deviceIdleOffset);
    EXPECT_LT(deviceIdleOffset, hostUnlockOffset);
    EXPECT_LT(hostUnlockOffset, semanticUnlockOffset);

    AString submissionLifecycleSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "device_submission_lifecycle.cpp",
        submissionLifecycleSource
    ));
    const AStringView fullSubmissionLifecycleSource(submissionLifecycleSource.data(), submissionLifecycleSource.size());
    const usize presentFunctionOffset = fullSubmissionLifecycleSource.find("bool Device::presentNativeQueue(");
    const usize presentFunctionEnd = fullSubmissionLifecycleSource.find(
        "NWB_VULKAN_END",
        presentFunctionOffset
    );
    ASSERT_NE(presentFunctionOffset, AStringView::npos);
    ASSERT_NE(presentFunctionEnd, AStringView::npos);
    const AStringView presentFunction = fullSubmissionLifecycleSource.substr(
        presentFunctionOffset,
        presentFunctionEnd - presentFunctionOffset
    );
    const usize presentHostLockOffset = presentFunction.find("ScopedLock hostLock(nativeQueue.hostMutex);");
    const usize nativePresentOffset = presentFunction.find("vkQueuePresentKHR(nativeQueue.queue", presentHostLockOffset);
    ASSERT_NE(presentHostLockOffset, AStringView::npos);
    ASSERT_NE(nativePresentOffset, AStringView::npos);
    EXPECT_LT(presentHostLockOffset, nativePresentOffset);

    AString orchestrationSource;
    AString presentationSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp",
        orchestrationSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_presentation.cpp",
        presentationSource
    ));
    const AStringView fullOrchestrationSource(orchestrationSource.data(), orchestrationSource.size());
    const AStringView fullPresentationSource(presentationSource.data(), presentationSource.size());
    EXPECT_NE(
        fullPresentationSource.find("m_rhiDevice->presentNativeQueue(m_presentNativeQueueIndex, presentInfo, res)"),
        AStringView::npos
    );
    EXPECT_EQ(fullPresentationSource.find("vkQueuePresentKHR"), AStringView::npos);

    AString surfaceSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_surface.cpp",
        surfaceSource
    ));
    const AStringView fullSurfaceSource(surfaceSource.data(), surfaceSource.size());
    EXPECT_EQ(fullSurfaceSource.find("vkDeviceWaitIdle"), AStringView::npos);
    EXPECT_EQ(fullSurfaceSource.find("waitForIdle()"), AStringView::npos);
    const usize lifecycleDrainOffset = fullOrchestrationSource.find("m_rhiDevice->beginLifecycleDrain()");
    const usize acquireProofOffset = fullOrchestrationSource.find(
        "waitAcquireSyncSlotsForLifecycle()",
        lifecycleDrainOffset
    );
    const usize transitionIdleOffset = fullOrchestrationSource.find(
        "const VkResult idleResult = m_rhiDevice->waitForNativeIdle();",
        acquireProofOffset
    );
    const usize preparedStateOffset = fullOrchestrationSource.find(
        "? SwapChainLifecycleState::PreparedResize",
        transitionIdleOffset
    );
    ASSERT_NE(lifecycleDrainOffset, AStringView::npos);
    ASSERT_NE(acquireProofOffset, AStringView::npos);
    ASSERT_NE(transitionIdleOffset, AStringView::npos);
    ASSERT_NE(preparedStateOffset, AStringView::npos);
    EXPECT_LT(lifecycleDrainOffset, acquireProofOffset);
    EXPECT_LT(acquireProofOffset, transitionIdleOffset);
    EXPECT_LT(transitionIdleOffset, preparedStateOffset);

    AString querySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "event_query.cpp", querySource));
    const AStringView fullQuerySource(querySource.data(), querySource.size());
    const usize querySemanticLockOffset = fullQuerySource.find("ScopedLock lock(q->m_mutex);");
    const usize queryHostLockOffset = fullQuerySource.find(
        "ScopedLock hostLock(q->m_nativeQueue.hostMutex);",
        querySemanticLockOffset
    );
    const usize querySubmitOffset = fullQuerySource.find("vkQueueSubmit(q->m_nativeQueue.queue", queryHostLockOffset);
    ASSERT_NE(querySemanticLockOffset, AStringView::npos);
    ASSERT_NE(queryHostLockOffset, AStringView::npos);
    ASSERT_NE(querySubmitOffset, AStringView::npos);
    EXPECT_LT(querySemanticLockOffset, queryHostLockOffset);
    EXPECT_LT(queryHostLockOffset, querySubmitOffset);
    EXPECT_NE(fullQuerySource.find("deviceLossContext = \"event query submit\";", querySubmitOffset), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("ScopedLock queryLock(query.m_mutex);"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("if(query.m_started){"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("deviceLossContext = \"event query reset\";"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(deviceLossContext);", querySubmitOffset), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(\"event query poll\")"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(\"event query wait\")"), AStringView::npos);

    const usize frameSignalResetOffset = fullPresentationSource.find("resetFramePresentationSignal();");
    const usize framePresentationUnlockOffset = fullPresentationSource.find(
        "presentationLock.unlock();",
        frameSignalResetOffset
    );
    const usize frameQueryWaitOffset = fullPresentationSource.find(
        "if(!m_rhiDevice->waitEventQueryInternal(",
        framePresentationUnlockOffset
    );
    const usize frameQueryPopOffset = fullPresentationSource.find("m_framesInFlight.pop();", frameQueryWaitOffset);
    const usize frameQuerySubmitOffset = fullPresentationSource.find(
        "if(!m_rhiDevice->setEventQueryInternal(",
        frameQueryPopOffset
    );
    const usize frameQueryPublishOffset = fullPresentationSource.find("m_framesInFlight.push(query);", frameQuerySubmitOffset);
    ASSERT_NE(frameSignalResetOffset, AStringView::npos);
    ASSERT_NE(framePresentationUnlockOffset, AStringView::npos);
    ASSERT_NE(frameQueryWaitOffset, AStringView::npos);
    ASSERT_NE(frameQueryPopOffset, AStringView::npos);
    ASSERT_NE(frameQuerySubmitOffset, AStringView::npos);
    ASSERT_NE(frameQueryPublishOffset, AStringView::npos);
    EXPECT_LT(frameSignalResetOffset, framePresentationUnlockOffset);
    EXPECT_LT(framePresentationUnlockOffset, frameQueryWaitOffset);
    EXPECT_LT(frameQueryWaitOffset, frameQueryPopOffset);
    EXPECT_LT(frameQueryPopOffset, frameQuerySubmitOffset);
    EXPECT_LT(frameQuerySubmitOffset, frameQueryPublishOffset);
}


// Presentation-signal retirement must publish a quarantined lifecycle before dropping the presentation and lifecycle
// locks for the canonical queue join. Exact identity is revalidated before a semaphore can be replaced.
TEST(SwapChainPresentation, PresentationSignalRetirementJoinsWithoutQueuePresentationLockInversion){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString contextHeaderSource;
    AString frameSource;
    AString presentationSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context.h",
        contextHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_frame.cpp",
        frameSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_presentation.cpp",
        presentationSource
    ));
    const AStringView contextHeader(contextHeaderSource.data(), contextHeaderSource.size());
    const AStringView frame(frameSource.data(), frameSource.size());
    const AStringView presentation(presentationSource.data(), presentationSource.size());

    EXPECT_NE(contextHeader.find("Retiring,"), AStringView::npos);
    EXPECT_NE(contextHeader.find("RetiringPresentation,"), AStringView::npos);
    const usize retirementBegin = frame.find("bool BackendContext::cancelFramePresentationSignalDeferred(");
    const usize retirementEnd = frame.find("void BackendContext::clearSemaphores(", retirementBegin);
    ASSERT_NE(retirementBegin, AStringView::npos);
    ASSERT_NE(retirementEnd, AStringView::npos);
    const AStringView retirement = frame.substr(retirementBegin, retirementEnd - retirementBegin);
    const usize signalRetiringOffset = retirement.find(
        "m_framePresentationSignalState = FramePresentationSignalState::Retiring;"
    );
    const usize lifecycleRetiringOffset = retirement.find(
        "m_swapChainLifecycleState = SwapChainLifecycleState::RetiringPresentation;",
        signalRetiringOffset
    );
    const usize presentationUnlockOffset = retirement.find("presentationLock.unlock();", lifecycleRetiringOffset);
    const usize lifecycleUnlockOffset = retirement.find("lifecycleLock.unlock();", presentationUnlockOffset);
    const usize nativeIdleOffset = retirement.find("device->waitForNativeIdle();", lifecycleUnlockOffset);
    const usize lifecycleRelockOffset = retirement.find("lifecycleLock.lock();", nativeIdleOffset);
    const usize presentationRelockOffset = retirement.find("presentationLock.lock();", lifecycleRelockOffset);
    const usize identityRevalidationOffset = retirement.find("const bool retirementIdentityMatches", presentationRelockOffset);
    const usize replacementOffset = retirement.find("replaceFramePresentationSemaphoreAfterIdle()", identityRevalidationOffset);
    ASSERT_NE(signalRetiringOffset, AStringView::npos);
    ASSERT_NE(lifecycleRetiringOffset, AStringView::npos);
    ASSERT_NE(presentationUnlockOffset, AStringView::npos);
    ASSERT_NE(lifecycleUnlockOffset, AStringView::npos);
    ASSERT_NE(nativeIdleOffset, AStringView::npos);
    ASSERT_NE(lifecycleRelockOffset, AStringView::npos);
    ASSERT_NE(presentationRelockOffset, AStringView::npos);
    ASSERT_NE(identityRevalidationOffset, AStringView::npos);
    ASSERT_NE(replacementOffset, AStringView::npos);
    EXPECT_LT(signalRetiringOffset, lifecycleRetiringOffset);
    EXPECT_LT(lifecycleRetiringOffset, presentationUnlockOffset);
    EXPECT_LT(presentationUnlockOffset, lifecycleUnlockOffset);
    EXPECT_LT(lifecycleUnlockOffset, nativeIdleOffset);
    EXPECT_LT(nativeIdleOffset, lifecycleRelockOffset);
    EXPECT_LT(lifecycleRelockOffset, presentationRelockOffset);
    EXPECT_LT(presentationRelockOffset, identityRevalidationOffset);
    EXPECT_LT(identityRevalidationOffset, replacementOffset);
    EXPECT_EQ(retirement.find("captureDeviceLoss("), AStringView::npos);

    const usize abandonBegin = presentation.find("bool BackendContext::abandonAcquiredFrame(){");
    const usize abandonEnd = presentation.find("bool BackendContext::present(){", abandonBegin);
    ASSERT_NE(abandonBegin, AStringView::npos);
    ASSERT_NE(abandonEnd, AStringView::npos);
    const AStringView abandon = presentation.substr(abandonBegin, abandonEnd - abandonBegin);
    const usize quarantineIdentityOffset = abandon.find("m_swapChainIndex = Limit<u32>::s_Max;");
    const usize abandonPresentationUnlockOffset = abandon.find("presentationLock.unlock();", quarantineIdentityOffset);
    const usize deferredRetirementOffset = abandon.find(
        "cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)",
        abandonPresentationUnlockOffset
    );
    ASSERT_NE(quarantineIdentityOffset, AStringView::npos);
    ASSERT_NE(abandonPresentationUnlockOffset, AStringView::npos);
    ASSERT_NE(deferredRetirementOffset, AStringView::npos);
    EXPECT_LT(quarantineIdentityOffset, abandonPresentationUnlockOffset);
    EXPECT_LT(abandonPresentationUnlockOffset, deferredRetirementOffset);
    EXPECT_EQ(abandon.find("waitForNativeIdle()"), AStringView::npos);
}


// The submission gate is one atomic state machine so its noexcept drain cannot allocate or throw. All fallible
// validation precedes native acceptance, and the accepted-submit commit path is invariant-only and logger-free.
TEST(SwapChainPresentation, SubmissionDrainAndAcceptedCommitRemainNoThrowAfterPublication){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendHeaderSource;
    AString lifecycleSource;
    AString queueSource;
    AString submissionSource;
    AString descriptorHeapSource;
    AString trackedCommandBufferSource;
    AString stateTrackingSource;
    AString commandMarkersSource;
    AString rayTracingBuildSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", backendHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "device_submission_lifecycle.cpp",
        lifecycleSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "queue.cpp", queueSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "queue_submission.cpp", submissionSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap.cpp",
        descriptorHeapSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "tracked_command_buffer.cpp",
        trackedCommandBufferSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "state_tracking.cpp", stateTrackingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "command_markers.cpp", commandMarkersSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "raytracing_commands_build.cpp",
        rayTracingBuildSource
    ));
    const AStringView backendHeader(backendHeaderSource.data(), backendHeaderSource.size());
    const AStringView lifecycle(lifecycleSource.data(), lifecycleSource.size());
    const AStringView queue(queueSource.data(), queueSource.size());
    const AStringView submission(submissionSource.data(), submissionSource.size());
    const AStringView descriptorHeap(descriptorHeapSource.data(), descriptorHeapSource.size());
    const AStringView trackedCommandBuffer(trackedCommandBufferSource.data(), trackedCommandBufferSource.size());
    const AStringView stateTracking(stateTrackingSource.data(), stateTrackingSource.size());
    const AStringView commandMarkers(commandMarkersSource.data(), commandMarkersSource.size());
    const AStringView rayTracingBuild(rayTracingBuildSource.data(), rayTracingBuildSource.size());

    EXPECT_NE(
        backendHeader.find("static constexpr u64 s_SubmissionDrainBit = static_cast<u64>(1u) << 63u;"),
        AStringView::npos
    );
    EXPECT_NE(backendHeader.find("Atomic<u64> m_submissionOperationState = 0u;"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("m_submissionOperationMutex"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("m_submissionOperationCondition"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("m_activeSubmissionOperationCount"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("m_submissionSuspended"), AStringView::npos);

    const usize beginOperationOffset = lifecycle.find("bool Device::beginSubmissionOperation()noexcept{");
    const usize endOperationOffset = lifecycle.find("void Device::endSubmissionOperation()noexcept{", beginOperationOffset);
    const usize beginDrainOffset = lifecycle.find("bool Device::beginLifecycleDrain()noexcept{", endOperationOffset);
    const usize endDrainOffset = lifecycle.find("void Device::endLifecycleDrain()noexcept{", beginDrainOffset);
    const usize sealDrainOffset = lifecycle.find("bool Device::sealLifecycleDrainForDestruction()noexcept{", endDrainOffset);
    const usize consumeSemaphoreOffset = lifecycle.find("QueueSubmissionToken Device::consumeAcquiredImageSemaphore", sealDrainOffset);
    ASSERT_NE(beginOperationOffset, AStringView::npos);
    ASSERT_NE(endOperationOffset, AStringView::npos);
    ASSERT_NE(beginDrainOffset, AStringView::npos);
    ASSERT_NE(endDrainOffset, AStringView::npos);
    ASSERT_NE(sealDrainOffset, AStringView::npos);
    ASSERT_NE(consumeSemaphoreOffset, AStringView::npos);
    const AStringView beginOperation = lifecycle.substr(beginOperationOffset, endOperationOffset - beginOperationOffset);
    const AStringView endOperation = lifecycle.substr(endOperationOffset, beginDrainOffset - endOperationOffset);
    const AStringView beginDrain = lifecycle.substr(beginDrainOffset, endDrainOffset - beginDrainOffset);
    const AStringView endDrain = lifecycle.substr(endDrainOffset, sealDrainOffset - endDrainOffset);
    const AStringView sealDrain = lifecycle.substr(sealDrainOffset, consumeSemaphoreOffset - sealDrainOffset);
    EXPECT_NE(beginOperation.find("compare_exchange_weak("), AStringView::npos);
    EXPECT_NE(endOperation.find("compare_exchange_weak("), AStringView::npos);
    EXPECT_NE(endOperation.find("if((state & s_SubmissionOperationCountMask) == 0u)"), AStringView::npos);
    EXPECT_NE(endOperation.find("TerminateInvariant();"), AStringView::npos);
    EXPECT_EQ(endOperation.find("fetch_sub("), AStringView::npos);
    EXPECT_NE(endOperation.find("m_submissionOperationState.notify_all();"), AStringView::npos);
    EXPECT_NE(beginDrain.find("state | s_SubmissionDrainBit"), AStringView::npos);
    EXPECT_NE(beginDrain.find("m_submissionOperationState.wait(state, MemoryOrder::acquire);"), AStringView::npos);
    EXPECT_NE(endDrain.find("m_submissionOperationState.store(0u, MemoryOrder::release);"), AStringView::npos);
    EXPECT_EQ(sealDrain.find("store(0u"), AStringView::npos);
    EXPECT_EQ(lifecycle.find("ConditionVariable"), AStringView::npos);
    EXPECT_EQ(lifecycle.find("UniqueLock"), AStringView::npos);
    EXPECT_EQ(lifecycle.find("catch("), AStringView::npos);

    const usize queuePreflightOffset = submission.find("validateCommandBufferSubmissionState(*tracked)");
    const usize accelStructPreflightOffset = submission.find(
        "validatePendingAccelStructBuildCommits()",
        queuePreflightOffset
    );
    const usize heapPreflightOffset = submission.find(
        "validateCommandBufferUseSubmissionLocked(*tracked, submissionToken, heapUseIndex)",
        accelStructPreflightOffset
    );
    const usize nativeSubmitOffset = submission.find("vkQueueSubmit2(m_nativeQueue.queue", heapPreflightOffset);
    const usize ownershipSpliceOffset = submission.find(
        "m_commandBuffersInFlight.splice(m_commandBuffersInFlight.end(), preparedCommandBuffers)",
        heapPreflightOffset
    );
    const usize heapCommitOffset = submission.find(
        "ticket.heap->commitCommandBufferUseSubmissionLocked(*ticket.commandBuffer, submissionToken, ticket.heapUseIndex)",
        nativeSubmitOffset
    );
    const usize queueCommitOffset = submission.find("commitCommandBufferStateTransition(*tracked", heapCommitOffset);
    ASSERT_NE(queuePreflightOffset, AStringView::npos);
    ASSERT_NE(accelStructPreflightOffset, AStringView::npos);
    ASSERT_NE(heapPreflightOffset, AStringView::npos);
    ASSERT_NE(ownershipSpliceOffset, AStringView::npos);
    ASSERT_NE(nativeSubmitOffset, AStringView::npos);
    ASSERT_NE(queueCommitOffset, AStringView::npos);
    ASSERT_NE(heapCommitOffset, AStringView::npos);
    EXPECT_LT(queuePreflightOffset, accelStructPreflightOffset);
    EXPECT_LT(accelStructPreflightOffset, heapPreflightOffset);
    EXPECT_LT(heapPreflightOffset, ownershipSpliceOffset);
    EXPECT_LT(ownershipSpliceOffset, nativeSubmitOffset);
    EXPECT_LT(nativeSubmitOffset, heapCommitOffset);
    EXPECT_LT(heapCommitOffset, queueCommitOffset);
    const usize submitFunctionEnd = submission.find("VkResult Queue::updateLastFinishedID()", nativeSubmitOffset);
    const usize commandBatchGuardOffset = submission.rfind("if(hasCommands){", ownershipSpliceOffset);
    ASSERT_NE(submitFunctionEnd, AStringView::npos);
    ASSERT_NE(commandBatchGuardOffset, AStringView::npos);
    EXPECT_LT(commandBatchGuardOffset, ownershipSpliceOffset);
    EXPECT_EQ(
        submission.substr(nativeSubmitOffset, submitFunctionEnd - nativeSubmitOffset).find(".splice("),
        AStringView::npos
    );
    EXPECT_EQ(submission.find("Alloc::ScratchArena scratchArena"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_submitDescriptorHeapUseCommitTickets"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_submitValidatedTimerQueryCommandBuffers"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_submitWaitInfos"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_submitSignalInfos"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_submitCommandBufferInfos"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_submitPreparedCommandBuffers"), AStringView::npos);
    EXPECT_EQ(submission.find("CommandBufferList preparedCommandBuffers{"), AStringView::npos);
    EXPECT_NE(submission.find("for(usize i = 0u; i < numCmd; ++i)", nativeSubmitOffset), AStringView::npos);

    const usize registerBegin = queue.find("void Queue::registerCommandBuffer(");
    const usize queueValidateBegin = queue.find("bool Queue::validateCommandBufferSubmissionState(", registerBegin);
    const usize transitionBegin = queue.find("void Queue::transitionCommandBufferState(", queueValidateBegin);
    const usize queueCommitBegin = queue.find("void Queue::commitCommandBufferStateTransition(", transitionBegin);
    const usize unregisterBegin = queue.find("void Queue::unregisterCommandBuffer(", queueCommitBegin);
    ASSERT_NE(registerBegin, AStringView::npos);
    ASSERT_NE(queueValidateBegin, AStringView::npos);
    ASSERT_NE(transitionBegin, AStringView::npos);
    ASSERT_NE(queueCommitBegin, AStringView::npos);
    ASSERT_NE(unregisterBegin, AStringView::npos);
    EXPECT_EQ(queue.substr(registerBegin, queueValidateBegin - registerBegin).find("NWB_LOGGER_"), AStringView::npos);
    EXPECT_EQ(queue.substr(transitionBegin, queueCommitBegin - transitionBegin).find(")noexcept{"), AStringView::npos);
    EXPECT_EQ(queue.substr(queueCommitBegin, unregisterBegin - queueCommitBegin).find("NWB_LOGGER_"), AStringView::npos);
    EXPECT_NE(queue.substr(queueCommitBegin, unregisterBegin - queueCommitBegin).find("TerminateInvariant()"), AStringView::npos);
    EXPECT_NE(queue.substr(queueCommitBegin, unregisterBegin - queueCommitBegin).find("DecrementOrAbort("), AStringView::npos);
    EXPECT_EQ(queue.substr(queueCommitBegin, unregisterBegin - queueCommitBegin).find("fetch_sub("), AStringView::npos);
    EXPECT_NE(backendHeader.find("void registerCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept;"), AStringView::npos);

    const usize heapCommitBegin = descriptorHeap.find("void GpuDescriptorHeap::commitCommandBufferUseSubmissionLocked(");
    const usize heapDiscardBegin = descriptorHeap.find("void GpuDescriptorHeap::discardCommandBufferUse(", heapCommitBegin);
    ASSERT_NE(heapCommitBegin, AStringView::npos);
    ASSERT_NE(heapDiscardBegin, AStringView::npos);
    EXPECT_EQ(
        descriptorHeap.substr(heapCommitBegin, heapDiscardBegin - heapCommitBegin).find("NWB_LOGGER_"),
        AStringView::npos
    );

    const usize accelStructValidationBegin = trackedCommandBuffer.find(
        "bool TrackedCommandBuffer::validatePendingAccelStructBuildCommits()const noexcept{"
    );
    const usize accelStructCommitBegin = trackedCommandBuffer.find(
        "void TrackedCommandBuffer::commitPendingAccelStructBuildCommits()noexcept{",
        accelStructValidationBegin
    );
    const usize accelStructDiscardBegin = trackedCommandBuffer.find(
        "void TrackedCommandBuffer::releasePendingAccelStructBuildCommits(){",
        accelStructCommitBegin
    );
    ASSERT_NE(accelStructValidationBegin, AStringView::npos);
    ASSERT_NE(accelStructCommitBegin, AStringView::npos);
    ASSERT_NE(accelStructDiscardBegin, AStringView::npos);
    const AStringView accelStructCommit = trackedCommandBuffer.substr(
        accelStructCommitBegin,
        accelStructDiscardBegin - accelStructCommitBegin
    );
    EXPECT_NE(accelStructCommit.find("NothrowScopedLock lock("), AStringView::npos);
    EXPECT_NE(accelStructCommit.find("TerminateInvariant();"), AStringView::npos);
    EXPECT_NE(accelStructCommit.find("commit.displacedRole = accelStruct.m_acceptedBuildSignatureRole;"), AStringView::npos);
    EXPECT_NE(accelStructCommit.find("accelStruct.m_acceptedBuildSignatureRole = commit.preparedRole;"), AStringView::npos);
    EXPECT_NE(accelStructCommit.find("commit.preparedRole = nullptr;"), AStringView::npos);
    EXPECT_EQ(accelStructCommit.find("NWB_LOGGER_"), AStringView::npos);
    EXPECT_EQ(accelStructCommit.find(".clear()"), AStringView::npos);

    const usize timerCommitBegin = trackedCommandBuffer.find("void TrackedCommandBuffer::commitTimerQueryRecordingClaims(");
    const usize timerDiscardBegin = trackedCommandBuffer.find("void TrackedCommandBuffer::discardTimerQueryRecordingClaims()", timerCommitBegin);
    const usize bufferCommitBegin = trackedCommandBuffer.find("void TrackedCommandBuffer::commitRetainedBufferStateCommits()", timerDiscardBegin);
    const usize textureCommitBegin = trackedCommandBuffer.find("void TrackedCommandBuffer::commitRetainedTextureStateCommits()", bufferCommitBegin);
    const usize opacityCommitBegin = trackedCommandBuffer.find("void TrackedCommandBuffer::commitPendingOpacityMicromapBuildCommits()", accelStructDiscardBegin);
    ASSERT_NE(timerCommitBegin, AStringView::npos);
    ASSERT_NE(timerDiscardBegin, AStringView::npos);
    ASSERT_NE(bufferCommitBegin, AStringView::npos);
    ASSERT_NE(textureCommitBegin, AStringView::npos);
    ASSERT_NE(opacityCommitBegin, AStringView::npos);
    EXPECT_NE(trackedCommandBuffer.substr(timerCommitBegin, timerDiscardBegin - timerCommitBegin).find("TerminateInvariant()"), AStringView::npos);
    EXPECT_NE(trackedCommandBuffer.find("TerminateInvariant()", bufferCommitBegin), AStringView::npos);
    EXPECT_NE(trackedCommandBuffer.find("TerminateInvariant()", textureCommitBegin), AStringView::npos);
    EXPECT_NE(trackedCommandBuffer.find("TerminateInvariant()", opacityCommitBegin), AStringView::npos);

    const usize finalizeDetachedBegin = submission.find("const auto finalizeDetachedRecordingAttempts =");
    const usize finalizeDetachedEnd = submission.find("const auto releaseDescriptorBufferLifecycle =", finalizeDetachedBegin);
    ASSERT_NE(finalizeDetachedBegin, AStringView::npos);
    ASSERT_NE(finalizeDetachedEnd, AStringView::npos);
    const AStringView finalizeDetached = submission.substr(
        finalizeDetachedBegin,
        finalizeDetachedEnd - finalizeDetachedBegin
    );
    EXPECT_NE(finalizeDetached.find("TerminateInvariant();"), AStringView::npos);

    const usize recordingCommitBegin = stateTracking.find("void StateTracker::commitRecordingAttempt()noexcept{");
    const usize recordingRollbackBegin = stateTracking.find("void StateTracker::rollbackRecordingAttempt()noexcept{", recordingCommitBegin);
    ASSERT_NE(recordingCommitBegin, AStringView::npos);
    ASSERT_NE(recordingRollbackBegin, AStringView::npos);
    EXPECT_NE(stateTracking.substr(recordingCommitBegin, recordingRollbackBegin - recordingCommitBegin).find("TerminateInvariant();"), AStringView::npos);

    EXPECT_NE(commandMarkers.find("class CrashMarkerRollback final"), AStringView::npos);
    EXPECT_NE(commandMarkers.find("crashMarkerRollback.disarm();"), AStringView::npos);
    EXPECT_EQ(commandMarkers.find("catch("), AStringView::npos);

    const usize topLevelAppend = rayTracingBuild.find("appendPendingAccelStructBuildCommit(");
    const usize topLevelNativeBuild = rayTracingBuild.find("vkCmdBuildAccelerationStructuresKHR(", topLevelAppend);
    const usize bottomLevelAppend = rayTracingBuild.find("appendPendingAccelStructBuildCommit(", topLevelNativeBuild);
    const usize bottomLevelNativeBuild = rayTracingBuild.find("vkCmdBuildAccelerationStructuresKHR(", bottomLevelAppend);
    ASSERT_NE(topLevelAppend, AStringView::npos);
    ASSERT_NE(topLevelNativeBuild, AStringView::npos);
    ASSERT_NE(bottomLevelAppend, AStringView::npos);
    ASSERT_NE(bottomLevelNativeBuild, AStringView::npos);
    EXPECT_LT(topLevelAppend, topLevelNativeBuild);
    EXPECT_LT(bottomLevelAppend, bottomLevelNativeBuild);
    EXPECT_NE(rayTracingBuild.substr(topLevelAppend, topLevelNativeBuild - topLevelAppend).find("rejectCommandRecording("), AStringView::npos);
    EXPECT_NE(rayTracingBuild.substr(bottomLevelAppend, bottomLevelNativeBuild - bottomLevelAppend).find("rejectCommandRecording("), AStringView::npos);
}


// Upload and scratch chunks remain in one owning ledger, while a stable intrusive chain limits accepted retirement
// to active recordings. Both the chunk commit and the concrete presentation resolution remain allocation-free.
TEST(SwapChainPresentation, UploadChunkRetirementIsBoundedAndNoThrowAfterNativeAcceptance){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendHeaderSource;
    AString ownerLookupSource;
    AString uploadSource;
    AString deviceQueueSource;
    AString frameSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", backendHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "submitted_command_buffer_owner_lookup.h",
        ownerLookupSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "upload.cpp", uploadSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device_queue.cpp", deviceQueueSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_frame.cpp",
        frameSource
    ));
    const AStringView backendHeader(backendHeaderSource.data(), backendHeaderSource.size());
    const AStringView ownerLookup(ownerLookupSource.data(), ownerLookupSource.size());
    const AStringView upload(uploadSource.data(), uploadSource.size());
    const AStringView deviceQueue(deviceQueueSource.data(), deviceQueueSource.size());
    const AStringView frame(frameSource.data(), frameSource.size());

    EXPECT_NE(backendHeader.find("struct QueueChunkLedger{"), AStringView::npos);
    EXPECT_NE(backendHeader.find("BufferChunk* previousActiveChunk;"), AStringView::npos);
    EXPECT_NE(backendHeader.find("BufferChunk* nextActiveChunk;"), AStringView::npos);
    EXPECT_NE(backendHeader.find("BufferChunk* firstActiveChunk = nullptr;"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("ChunkRecyclePredicate"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("m_chunkPool"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("m_activeChunks"), AStringView::npos);

    EXPECT_NE(ownerLookup.find("using OwnerEntries = Vector<OwnerEntry, Alloc::GlobalArena>;"), AStringView::npos);
    EXPECT_NE(ownerLookup.find("OwnerEntry* m_ownerEntriesData = nullptr;"), AStringView::npos);
    EXPECT_NE(ownerLookup.find("m_ownerEntries.resize(tableCapacity);"), AStringView::npos);
    EXPECT_NE(ownerLookup.find("const OwnerEntry& entry = m_ownerEntriesData[index];"), AStringView::npos);
    EXPECT_NE(ownerLookup.find("index = (index + 1u) & m_ownerEntryMask;"), AStringView::npos);
    EXPECT_EQ(ownerLookup.find("HashMap<"), AStringView::npos);
    EXPECT_EQ(ownerLookup.find("Hasher<"), AStringView::npos);
    EXPECT_EQ(ownerLookup.find(".find("), AStringView::npos);
    EXPECT_EQ(ownerLookup.find(".reserve("), AStringView::npos);
    EXPECT_NE(backendHeader.find("Futex m_submissionWorkspaceMutex;"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_executeExpectedCommandLists"), AStringView::npos);
    EXPECT_NE(backendHeader.find("m_executeSubmittedOwners"), AStringView::npos);
    EXPECT_EQ(deviceQueue.find("Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CommandListExecuteArena)"), AStringView::npos);

    const usize preparationBegin = deviceQueue.find("bool Device::prepareSubmissionCommandListWorkspaceLocked(");
    const usize finalizationBegin = deviceQueue.find("void Device::finalizeSubmissionCommandListResourcesLocked(");
    const usize graphSubmissionBegin = deviceQueue.find("QueueSubmissionToken Device::executeGraphCommandLists(", finalizationBegin);
    ASSERT_NE(preparationBegin, AStringView::npos);
    ASSERT_NE(finalizationBegin, AStringView::npos);
    ASSERT_NE(graphSubmissionBegin, AStringView::npos);
    const AStringView preparation = deviceQueue.substr(preparationBegin, finalizationBegin - preparationBegin);
    const AStringView finalization = deviceQueue.substr(finalizationBegin, graphSubmissionBegin - finalizationBegin);
    const usize lookupPrepare = preparation.find("submittedOwners.prepare(expectedCommandLists.size());");
    const usize acceptedBranch = finalization.find("if(submissionAccepted){");
    const usize firstUploadCommit = finalization.find("m_uploadManager.submitChunks(", acceptedBranch);
    const usize firstScratchCommit = finalization.find("m_scratchManager.submitChunks(", firstUploadCommit);
    ASSERT_NE(lookupPrepare, AStringView::npos);
    ASSERT_NE(acceptedBranch, AStringView::npos);
    ASSERT_NE(firstUploadCommit, AStringView::npos);
    ASSERT_NE(firstScratchCommit, AStringView::npos);
    EXPECT_LT(acceptedBranch, firstUploadCommit);
    EXPECT_LT(firstUploadCommit, firstScratchCommit);

    EXPECT_EQ(
        deviceQueue.find("ScopedLock submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);"),
        AStringView::npos
    );
    const usize firstWorkspaceLock = deviceQueue.find(
        "UniqueLock<Futex> submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);"
    );
    const usize firstNativeSubmission = deviceQueue.find("const u64 submittedID = queue->submit(", firstWorkspaceLock);
    const usize firstFinalization = deviceQueue.find("finalizeSubmissionCommandListResourcesLocked(", firstNativeSubmission);
    const usize firstWorkspaceUnlock = deviceQueue.find("submissionWorkspaceLock.unlock();", firstFinalization);
    const usize firstDeviceLossCapture = deviceQueue.find("captureDeviceLoss(\"queue submit\");", firstWorkspaceUnlock);
    const usize secondWorkspaceLock = deviceQueue.find(
        "UniqueLock<Futex> submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);",
        graphSubmissionBegin
    );
    const usize secondNativeSubmission = deviceQueue.find("const u64 submittedID = queue->submit(", secondWorkspaceLock);
    const usize secondFinalization = deviceQueue.find("finalizeSubmissionCommandListResourcesLocked(", secondNativeSubmission);
    const usize hookResolution = deviceQueue.find("hookResolution.resolve(submissionToken);", secondFinalization);
    const usize secondWorkspaceUnlock = deviceQueue.find("submissionWorkspaceLock.unlock();", hookResolution);
    const usize secondDeviceLossCapture = deviceQueue.find("captureDeviceLoss(\"queue submit\");", secondWorkspaceUnlock);
    ASSERT_NE(firstWorkspaceLock, AStringView::npos);
    ASSERT_NE(firstNativeSubmission, AStringView::npos);
    ASSERT_NE(firstFinalization, AStringView::npos);
    ASSERT_NE(firstWorkspaceUnlock, AStringView::npos);
    ASSERT_NE(firstDeviceLossCapture, AStringView::npos);
    ASSERT_NE(secondWorkspaceLock, AStringView::npos);
    ASSERT_NE(secondNativeSubmission, AStringView::npos);
    ASSERT_NE(secondFinalization, AStringView::npos);
    ASSERT_NE(hookResolution, AStringView::npos);
    ASSERT_NE(secondWorkspaceUnlock, AStringView::npos);
    ASSERT_NE(secondDeviceLossCapture, AStringView::npos);
    EXPECT_LT(firstWorkspaceLock, firstNativeSubmission);
    EXPECT_LT(firstNativeSubmission, firstFinalization);
    EXPECT_LT(firstFinalization, firstWorkspaceUnlock);
    EXPECT_LT(firstWorkspaceUnlock, firstDeviceLossCapture);
    EXPECT_LT(secondWorkspaceLock, secondNativeSubmission);
    EXPECT_LT(secondNativeSubmission, secondFinalization);
    EXPECT_LT(secondFinalization, hookResolution);
    EXPECT_LT(hookResolution, secondWorkspaceUnlock);
    EXPECT_LT(secondWorkspaceUnlock, secondDeviceLossCapture);

    const usize linkBegin = upload.find("void UploadManager::linkActiveChunkLocked(");
    const usize retireBegin = upload.find("void UploadManager::retireChunkLocked(", linkBegin);
    const usize submittedBegin = upload.find("void UploadManager::retireSubmittedChunksLocked(", retireBegin);
    const usize ownerBegin = upload.find("void UploadManager::retireOwnerChunksLocked(", submittedBegin);
    const usize suballocateBegin = upload.find("bool UploadManager::suballocateBuffer(", ownerBegin);
    const usize submitBegin = upload.find("void UploadManager::submitChunks(", suballocateBegin);
    const usize discardBegin = upload.find("void UploadManager::discardChunks(", submitBegin);
    const usize abandonBegin = upload.find("void UploadManager::abandonChunks(", discardBegin);
    ASSERT_NE(linkBegin, AStringView::npos);
    ASSERT_NE(retireBegin, AStringView::npos);
    ASSERT_NE(submittedBegin, AStringView::npos);
    ASSERT_NE(ownerBegin, AStringView::npos);
    ASSERT_NE(suballocateBegin, AStringView::npos);
    ASSERT_NE(submitBegin, AStringView::npos);
    ASSERT_NE(discardBegin, AStringView::npos);
    ASSERT_NE(abandonBegin, AStringView::npos);

    const AStringView retire = upload.substr(retireBegin, submittedBegin - retireBegin);
    EXPECT_NE(retire.find("ledger.firstActiveChunk = chunk.nextActiveChunk;"), AStringView::npos);
    EXPECT_NE(retire.find("chunk.owner = nullptr;"), AStringView::npos);
    EXPECT_NE(retire.find("chunk.nativeRecordingID = 0u;"), AStringView::npos);
    EXPECT_EQ(retire.find(".splice("), AStringView::npos);
    EXPECT_EQ(retire.find(".erase("), AStringView::npos);
    EXPECT_EQ(retire.find("NWB_LOGGER_"), AStringView::npos);

    const AStringView submitted = upload.substr(submittedBegin, ownerBegin - submittedBegin);
    EXPECT_NE(submitted.find("BufferChunk* chunk = ledger->firstActiveChunk;"), AStringView::npos);
    EXPECT_NE(submitted.find("BufferChunk* const nextChunk = chunk->nextActiveChunk;"), AStringView::npos);
    EXPECT_NE(submitted.find("submittedOwners.contains("), AStringView::npos);
    EXPECT_EQ(submitted.find("ledger->chunks"), AStringView::npos);
    EXPECT_EQ(submitted.find(".splice("), AStringView::npos);
    EXPECT_EQ(submitted.find(".erase("), AStringView::npos);
    EXPECT_EQ(submitted.find("predicate"), AStringView::npos);
    EXPECT_EQ(submitted.find("NWB_LOGGER_"), AStringView::npos);

    const AStringView submit = upload.substr(submitBegin, discardBegin - submitBegin);
    EXPECT_NE(submit.find("static_assert(noexcept(retireSubmittedChunksLocked("), AStringView::npos);
    EXPECT_NE(submit.find("NothrowScopedLock lock(m_mutex);"), AStringView::npos);
    EXPECT_NE(submit.find("retireSubmittedChunksLocked("), AStringView::npos);
    EXPECT_EQ(submit.find(".splice("), AStringView::npos);
    EXPECT_EQ(submit.find(".erase("), AStringView::npos);
    EXPECT_EQ(submit.find("NWB_LOGGER_"), AStringView::npos);
    EXPECT_EQ(upload.find(".splice("), AStringView::npos);

    const AStringView discard = upload.substr(discardBegin, abandonBegin - discardBegin);
    EXPECT_NE(discard.find("retireOwnerChunksLocked(queue, reusableVersion, true, *owner, nativeRecordingID);"), AStringView::npos);
    EXPECT_NE(upload.substr(abandonBegin).find("NothrowScopedLock lock(m_mutex);"), AStringView::npos);
    EXPECT_NE(
        upload.substr(abandonBegin).find("retireOwnerChunksLocked(queue, 0u, true, *owner, nativeRecordingID);"),
        AStringView::npos
    );

    const usize resolutionBegin = frame.find("bool BackendContext::resolveFramePresentationSignal(");
    const usize resolutionEnd = frame.find("bool BackendContext::confirmFramePresentationSignal(", resolutionBegin);
    ASSERT_NE(resolutionBegin, AStringView::npos);
    ASSERT_NE(resolutionEnd, AStringView::npos);
    const AStringView presentationResolution = frame.substr(resolutionBegin, resolutionEnd - resolutionBegin);
    EXPECT_NE(presentationResolution.find(")noexcept{"), AStringView::npos);
    EXPECT_NE(presentationResolution.find("NothrowScopedLock presentationLock(m_framePresentationMutex);"), AStringView::npos);
    EXPECT_NE(presentationResolution.find("m_framePresentationCondition.notify_all();"), AStringView::npos);
    EXPECT_EQ(presentationResolution.find(".push_back("), AStringView::npos);
    EXPECT_EQ(presentationResolution.find(".emplace"), AStringView::npos);
    EXPECT_EQ(presentationResolution.find(".reserve("), AStringView::npos);
    EXPECT_EQ(presentationResolution.find("NWB_LOGGER_"), AStringView::npos);
    EXPECT_NE(
        backendHeader.find("const GpuPhysicalQueueInfo* getPhysicalQueueInfo(const GpuPhysicalQueueId& queue)const noexcept;"),
        AStringView::npos
    );
}


// Logical submission quarantine must stop a generation without claiming native device loss. Only an observed
// VK_ERROR_DEVICE_LOST may authorize teardown without a successful device-idle join or collect loss diagnostics.
TEST(SwapChainPresentation, LogicalQuarantineRemainsDistinctFromNativeDeviceLoss){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendHeaderSource;
    AString deviceSource;
    AString diagnosticSource;
    AString presentationSource;
    AString submissionLifecycleSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", backendHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device.cpp", deviceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device_diagnostics.cpp", diagnosticSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_presentation.cpp",
        presentationSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "device_submission_lifecycle.cpp",
        submissionLifecycleSource
    ));
    const AStringView backendHeader(backendHeaderSource.data(), backendHeaderSource.size());
    const AStringView device(deviceSource.data(), deviceSource.size());
    const AStringView diagnostics(diagnosticSource.data(), diagnosticSource.size());
    const AStringView presentation(presentationSource.data(), presentationSource.size());
    const AStringView submissionLifecycle(submissionLifecycleSource.data(), submissionLifecycleSource.size());

    EXPECT_NE(backendHeader.find("Atomic<bool> m_deviceLost = false;"), AStringView::npos);
    EXPECT_NE(backendHeader.find("Atomic<bool> m_deviceQuarantined = false;"), AStringView::npos);
    EXPECT_NE(
        backendHeader.find("return isDeviceLost() || m_deviceQuarantined.load(MemoryOrder::acquire);"),
        AStringView::npos
    );
    EXPECT_NE(
        backendHeader.find("void quarantineDevice()noexcept{ m_deviceQuarantined.store(true, MemoryOrder::release); }"),
        AStringView::npos
    );
    EXPECT_NE(backendHeader.find("void markDeviceLost()noexcept{ m_deviceLost.store(true, MemoryOrder::release); }"), AStringView::npos);
    EXPECT_NE(backendHeader.find("void captureDeviceLoss(AStringView context);"), AStringView::npos);
    EXPECT_NE(diagnostics.find("void Device::captureDeviceLoss("), AStringView::npos);
    EXPECT_NE(diagnostics.find("markDeviceLost();"), AStringView::npos);
    EXPECT_EQ(diagnostics.find("catch("), AStringView::npos);
    EXPECT_EQ(diagnostics.find("m_deviceQuarantined.store"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("captureGpuCrash"), AStringView::npos);

    const usize destructorOffset = device.find("Device::~Device()noexcept{");
    const usize waitForIdleOffset = device.find("bool Device::waitForIdle(){", destructorOffset);
    ASSERT_NE(destructorOffset, AStringView::npos);
    ASSERT_NE(waitForIdleOffset, AStringView::npos);
    const AStringView destructor = device.substr(destructorOffset, waitForIdleOffset - destructorOffset);
    EXPECT_NE(destructor.find("waitForNativeIdle()"), AStringView::npos);
    EXPECT_NE(destructor.find("m_gpuDescriptorHeap.shutdownForDeviceTeardown();"), AStringView::npos);
    EXPECT_NE(destructor.find("m_descriptorBufferManager.shutdownForDeviceTeardown();"), AStringView::npos);
    EXPECT_EQ(destructor.find("waitForIdle()"), AStringView::npos);
    EXPECT_EQ(destructor.find("savePipelineCacheData()"), AStringView::npos);
    EXPECT_EQ(destructor.find("captureDeviceLoss("), AStringView::npos);
    EXPECT_EQ(destructor.find("NWB_LOGGER_"), AStringView::npos);

    EXPECT_NE(presentation.find("captureDeviceLossAfterUnlock(\"acquire next image\")"), AStringView::npos);
    EXPECT_NE(presentation.find("captureDeviceLossAfterUnlock(\"present\", &presentationLock)"), AStringView::npos);
    EXPECT_NE(presentation.find("m_rhiDevice->quarantineDevice();"), AStringView::npos);
    EXPECT_EQ(presentation.find("captureDeviceLoss(\"present semaphore idle\")"), AStringView::npos);
    EXPECT_NE(submissionLifecycle.find("if(submissionsBlocked())"), AStringView::npos);
    EXPECT_NE(submissionLifecycle.find("bool Device::beginLifecycleDrain()noexcept{"), AStringView::npos);
}


// A failed host join must leave the public Graphics lifecycle live and retryable. A successful device-loss teardown
// may release WSI objects, but resize must never create a replacement on the lost VkDevice.
TEST(SwapChainPresentation, TeardownFailureDoesNotPublishADeadOrRecreatedInstance){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendHeader;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context.h",
        backendHeader
    ));
    const AStringView fullBackendHeader(backendHeader.data(), backendHeader.size());
    EXPECT_NE(fullBackendHeader.find("[[nodiscard]] bool destroy();"), AStringView::npos);

    AString graphicsHeader;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime.h", graphicsHeader));
    EXPECT_NE(graphicsHeader.find("~GraphicsRuntime()noexcept(false);"), AString::npos);

    AString graphicsSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime.cpp", graphicsSource));
    const AStringView fullGraphicsSource(graphicsSource.data(), graphicsSource.size());
    const usize graphicsDestroyOffset = fullGraphicsSource.find("bool GraphicsRuntime::destroy(){");
    const usize prepareDestroyOffset = fullGraphicsSource.find(
        "prepareSwapChainTransition(SwapChainTransitionKind::Destroy, transitionTicket)",
        graphicsDestroyOffset
    );
    const usize highLevelClearOffset = fullGraphicsSource.find("m_renderPasses.clear();", prepareDestroyOffset);
    const usize commitDestroyOffset = fullGraphicsSource.find(
        "m_backend->commitDestroy(Move(transitionTicket))",
        highLevelClearOffset
    );
    const usize instanceDestroyedOffset = fullGraphicsSource.find(
        "m_instanceCreated = false;",
        commitDestroyOffset
    );
    ASSERT_NE(graphicsDestroyOffset, AStringView::npos);
    ASSERT_NE(prepareDestroyOffset, AStringView::npos);
    ASSERT_NE(highLevelClearOffset, AStringView::npos);
    ASSERT_NE(commitDestroyOffset, AStringView::npos);
    ASSERT_NE(instanceDestroyedOffset, AStringView::npos);
    EXPECT_LT(prepareDestroyOffset, highLevelClearOffset);
    EXPECT_LT(highLevelClearOffset, commitDestroyOffset);
    EXPECT_LT(commitDestroyOffset, instanceDestroyedOffset);
    const usize graphicsDestructorOffset = fullGraphicsSource.find("GraphicsRuntime::~GraphicsRuntime()noexcept(false){");
    const usize retirementScopeOffset = fullGraphicsSource.find("const auto retireTasks = [this]()noexcept{", graphicsDestructorOffset);
    const usize destructorTaskDrainOffset = fullGraphicsSource.find("m_tasks.drain();", retirementScopeOffset);
    const usize schedulerDetachOffset = fullGraphicsSource.find("m_gpuTasks.detachDevice(*device)", destructorTaskDrainOffset);
    const usize activeUnwindOffset = fullGraphicsSource.find("if(UncaughtExceptionCount() > 0){", schedulerDetachOffset);
    const usize unwindRetirementOffset = fullGraphicsSource.find("retireTasks();", activeUnwindOffset);
    const usize activeUnwindReturnOffset = fullGraphicsSource.find("return;", unwindRetirementOffset);
    const usize destructorFailureGuardOffset = fullGraphicsSource.find("ScopeExit drainOnFailure(retireTasks);", activeUnwindReturnOffset);
    const usize destructorAssertionOffset = fullGraphicsSource.find("NWB_FATAL_ASSERT_MSG(", graphicsDestructorOffset);
    const usize destructorDestroyOffset = fullGraphicsSource.find("destroy(),", destructorAssertionOffset);
    const usize destructorGuardReleaseOffset = fullGraphicsSource.find("drainOnFailure.release();", destructorDestroyOffset);
    ASSERT_NE(graphicsDestructorOffset, AStringView::npos);
    ASSERT_NE(retirementScopeOffset, AStringView::npos);
    ASSERT_NE(destructorTaskDrainOffset, AStringView::npos);
    ASSERT_NE(schedulerDetachOffset, AStringView::npos);
    ASSERT_NE(activeUnwindOffset, AStringView::npos);
    ASSERT_NE(unwindRetirementOffset, AStringView::npos);
    ASSERT_NE(activeUnwindReturnOffset, AStringView::npos);
    ASSERT_NE(destructorFailureGuardOffset, AStringView::npos);
    ASSERT_NE(destructorAssertionOffset, AStringView::npos);
    ASSERT_NE(destructorDestroyOffset, AStringView::npos);
    ASSERT_NE(destructorGuardReleaseOffset, AStringView::npos);
    EXPECT_LT(graphicsDestructorOffset, retirementScopeOffset);
    EXPECT_LT(retirementScopeOffset, destructorTaskDrainOffset);
    EXPECT_LT(destructorTaskDrainOffset, schedulerDetachOffset);
    EXPECT_LT(schedulerDetachOffset, activeUnwindOffset);
    EXPECT_LT(activeUnwindOffset, unwindRetirementOffset);
    EXPECT_LT(unwindRetirementOffset, activeUnwindReturnOffset);
    EXPECT_LT(activeUnwindReturnOffset, destructorFailureGuardOffset);
    EXPECT_LT(destructorFailureGuardOffset, destructorAssertionOffset);
    EXPECT_LT(destructorAssertionOffset, destructorDestroyOffset);
    EXPECT_LT(destructorDestroyOffset, destructorGuardReleaseOffset);
    EXPECT_LT(destructorGuardReleaseOffset, graphicsDestroyOffset);

    AString frameHeader;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "frame" / "module.h", frameHeader));
    EXPECT_NE(frameHeader.find("void cleanupPlatform()noexcept;"), AString::npos);

    AString frameSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "frame" / "module.cpp", frameSource));
    const AStringView fullFrameSource(frameSource.data(), frameSource.size());
    const usize frameDestructorOffset = fullFrameSource.find("Frame::~Frame()noexcept(false){");
    const usize frameActiveUnwindOffset = fullFrameSource.find("if(UncaughtExceptionCount() > 0){", frameDestructorOffset);
    const usize schedulerDrainOffset = fullFrameSource.find("m_cpuTasks.drain();", frameActiveUnwindOffset);
    const usize platformDetachOffset = fullFrameSource.find("cleanupPlatform();", schedulerDrainOffset);
    const usize frameActiveUnwindReturnOffset = fullFrameSource.find("return;", platformDetachOffset);
    const usize normalTaskJoinOffset = fullFrameSource.find("m_cpuTasks.wait();", frameActiveUnwindReturnOffset);
    const usize normalCleanupOffset = fullFrameSource.find("cleanup();", frameActiveUnwindReturnOffset);
    ASSERT_NE(frameDestructorOffset, AStringView::npos);
    ASSERT_NE(frameActiveUnwindOffset, AStringView::npos);
    ASSERT_NE(schedulerDrainOffset, AStringView::npos);
    ASSERT_NE(platformDetachOffset, AStringView::npos);
    ASSERT_NE(frameActiveUnwindReturnOffset, AStringView::npos);
    ASSERT_NE(normalTaskJoinOffset, AStringView::npos);
    ASSERT_NE(normalCleanupOffset, AStringView::npos);
    EXPECT_LT(frameDestructorOffset, frameActiveUnwindOffset);
    EXPECT_LT(frameActiveUnwindOffset, schedulerDrainOffset);
    EXPECT_LT(schedulerDrainOffset, platformDetachOffset);
    EXPECT_LT(platformDetachOffset, frameActiveUnwindReturnOffset);
    EXPECT_LT(frameActiveUnwindReturnOffset, normalTaskJoinOffset);
    EXPECT_LT(normalTaskJoinOffset, normalCleanupOffset);

    AString loaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "loader" / "main.cpp", loaderSource));
    const usize callbackGuardOffset = loaderSource.find("~CallbackShutdownGuard()noexcept(false){");
    const usize beforeShutdownJoinOffset = loaderSource.find("m_tasks.wait();", callbackGuardOffset);
    const usize callbackShutdownOffset = loaderSource.find("m_callbacks.onShutdown();", beforeShutdownJoinOffset);
    const usize afterShutdownJoinOffset = loaderSource.find("m_tasks.wait();", callbackShutdownOffset);
    ASSERT_NE(callbackGuardOffset, AString::npos);
    ASSERT_NE(beforeShutdownJoinOffset, AString::npos);
    ASSERT_NE(callbackShutdownOffset, AString::npos);
    ASSERT_NE(afterShutdownJoinOffset, AString::npos);
    EXPECT_LT(beforeShutdownJoinOffset, callbackShutdownOffset);
    EXPECT_LT(callbackShutdownOffset, afterShutdownJoinOffset);

    const usize resizePreparationOffset = fullGraphicsSource.find("if(!backBufferResizing(transitionTicket))");
    const usize resizeBackendOffset = fullGraphicsSource.find(
        "if(!m_backend->commitSwapChainResize(Move(transitionTicket)))",
        resizePreparationOffset
    );
    const usize resizeCompletionOffset = fullGraphicsSource.find("if(!backBufferResized())", resizeBackendOffset);
    ASSERT_NE(resizePreparationOffset, AStringView::npos);
    ASSERT_NE(resizeBackendOffset, AStringView::npos);
    ASSERT_NE(resizeCompletionOffset, AStringView::npos);
    EXPECT_LT(resizePreparationOffset, resizeBackendOffset);
    EXPECT_LT(resizeBackendOffset, resizeCompletionOffset);

    AString orchestrationSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp",
        orchestrationSource
    ));
    const AStringView fullOrchestrationSource(orchestrationSource.data(), orchestrationSource.size());
    const usize resizeCommitOffset = fullOrchestrationSource.find(
        "bool BackendContext::commitSwapChainResize(SwapChainTransitionTicket&& ticket)"
    );
    const usize resizeDestroyOffset = fullOrchestrationSource.find("commitPreparedSwapChainDestruction();", resizeCommitOffset);
    const usize resizeCreateOffset = fullOrchestrationSource.find("if(!createSwapChainResources())", resizeDestroyOffset);
    const usize resizeReadyOffset = fullOrchestrationSource.find(
        "m_swapChainLifecycleState = SwapChainLifecycleState::Ready;",
        resizeCreateOffset
    );
    ASSERT_NE(resizeCommitOffset, AStringView::npos);
    ASSERT_NE(resizeDestroyOffset, AStringView::npos);
    ASSERT_NE(resizeCreateOffset, AStringView::npos);
    ASSERT_NE(resizeReadyOffset, AStringView::npos);
    EXPECT_LT(resizeCommitOffset, resizeDestroyOffset);
    EXPECT_LT(resizeDestroyOffset, resizeCreateOffset);
    EXPECT_LT(resizeCreateOffset, resizeReadyOffset);

    AString rhiHeader;
    AString presentationHeader;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "rhi" / "device.h", rhiHeader));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "rhi" / "presentation.h", presentationHeader));
    const AStringView fullRhiHeader(rhiHeader.data(), rhiHeader.size());
    const AStringView fullPresentationHeader(presentationHeader.data(), presentationHeader.size());
    EXPECT_NE(fullPresentationHeader.find("struct BeginFrameResult{"), AStringView::npos);
    EXPECT_NE(
        fullPresentationHeader.find("BeginFrameStatus::Enum status = BeginFrameStatus::Failed;"),
        AStringView::npos
    );
    EXPECT_EQ(fullRhiHeader.find("BackBufferResizeCallbacks"), AStringView::npos);
    EXPECT_NE(fullGraphicsSource.find("if(beginFrameResult.status == BeginFrameStatus::ResizeRequired)"), AStringView::npos);
    EXPECT_NE(fullGraphicsSource.find("requestDeviceRecreation();", resizeCompletionOffset), AStringView::npos);
    EXPECT_EQ(fullOrchestrationSource.find("callbacks.resizeFailed"), AStringView::npos);
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
    const usize prepareFunctionBegin = fullSurfaceSource.find("bool BackendContext::prepareSwapChainImageRevocation(){");
    const usize commitFunctionBegin = fullSurfaceSource.find(
        "void BackendContext::commitPreparedSwapChainDestruction()noexcept{",
        prepareFunctionBegin
    );
    const usize failureCleanupBegin = fullSurfaceSource.find(
        "bool BackendContext::destroySwapChainAfterCreateFailure(){",
        commitFunctionBegin
    );
    ASSERT_NE(prepareFunctionBegin, AStringView::npos);
    ASSERT_NE(commitFunctionBegin, AStringView::npos);
    ASSERT_NE(failureCleanupBegin, AStringView::npos);
    const AStringView prepareFunction = fullSurfaceSource.substr(
        prepareFunctionBegin,
        commitFunctionBegin - prepareFunctionBegin
    );
    const AStringView commitFunction = fullSurfaceSource.substr(
        commitFunctionBegin,
        failureCleanupBegin - commitFunctionBegin
    );
    const usize canRevokeOffset = prepareFunction.find("canRevokeUnmanagedNativeImage(");
    const usize prepareRevokeOffset = prepareFunction.find("prepareRevokeUnmanagedNativeImage(", canRevokeOffset);
    const usize commitRevokeOffset = commitFunction.find("commitRevokeUnmanagedNativeImage(");
    const usize nativeDestroyOffset = commitFunction.find("vkDestroySwapchainKHR(", commitRevokeOffset);
    const usize releaseIdentityOffset = commitFunction.find(
        "releasePreparedRevokeUnmanagedNativeImageIdentity(",
        nativeDestroyOffset
    );
    const usize wrapperClearOffset = commitFunction.find("m_swapChainImages.clear();", releaseIdentityOffset);
    ASSERT_NE(canRevokeOffset, AStringView::npos);
    ASSERT_NE(prepareRevokeOffset, AStringView::npos);
    ASSERT_NE(commitRevokeOffset, AStringView::npos);
    ASSERT_NE(nativeDestroyOffset, AStringView::npos);
    ASSERT_NE(releaseIdentityOffset, AStringView::npos);
    ASSERT_NE(wrapperClearOffset, AStringView::npos);
    EXPECT_LT(canRevokeOffset, prepareRevokeOffset);
    EXPECT_LT(commitRevokeOffset, nativeDestroyOffset);
    EXPECT_LT(nativeDestroyOffset, releaseIdentityOffset);
    EXPECT_LT(releaseIdentityOffset, wrapperClearOffset);
    EXPECT_EQ(commitFunction.find("waitForIdle()"), AStringView::npos);
    EXPECT_EQ(commitFunction.find("NWB_LOGGER_"), AStringView::npos);
    EXPECT_EQ(commitFunction.find("unregisterTextureNativeIdentity"), AStringView::npos);

    AString textureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "texture.cpp", textureSource));
    const AStringView fullTextureSource(textureSource.data(), textureSource.size());
    const usize prepareRevokeFunctionBegin = fullTextureSource.find("bool Texture::prepareRevokeUnmanagedNativeImage(");
    const usize commitRevokeFunctionBegin = fullTextureSource.find(
        "void Texture::commitRevokeUnmanagedNativeImage(",
        prepareRevokeFunctionBegin
    );
    const usize releaseIdentityFunctionBegin = fullTextureSource.find(
        "void Texture::releasePreparedRevokeUnmanagedNativeImageIdentity(",
        commitRevokeFunctionBegin
    );
    const usize releaseIdentityFunctionEnd = fullTextureSource.find(
        "bool Texture::isRetainedSubresourceStateKnown(",
        releaseIdentityFunctionBegin
    );
    ASSERT_NE(prepareRevokeFunctionBegin, AStringView::npos);
    ASSERT_NE(commitRevokeFunctionBegin, AStringView::npos);
    ASSERT_NE(releaseIdentityFunctionBegin, AStringView::npos);
    ASSERT_NE(releaseIdentityFunctionEnd, AStringView::npos);
    ASSERT_LT(prepareRevokeFunctionBegin, commitRevokeFunctionBegin);
    ASSERT_LT(commitRevokeFunctionBegin, releaseIdentityFunctionBegin);
    ASSERT_LT(releaseIdentityFunctionBegin, releaseIdentityFunctionEnd);

    const AStringView prepareRevokeFunction = fullTextureSource.substr(
        prepareRevokeFunctionBegin,
        commitRevokeFunctionBegin - prepareRevokeFunctionBegin
    );
    const usize prepareIdentityOffset = prepareRevokeFunction.find(
        "m_allocator.isTextureNativeIdentityRegistered(expectedNativeImage, *this)"
    );
    const usize preparedImageOffset = prepareRevokeFunction.find("m_preparedRevokedNativeImage = expectedNativeImage;");
    ASSERT_NE(prepareIdentityOffset, AStringView::npos);
    ASSERT_NE(preparedImageOffset, AStringView::npos);
    EXPECT_LT(prepareIdentityOffset, preparedImageOffset);
    EXPECT_EQ(prepareRevokeFunction.find("vkDestroyImageView("), AStringView::npos);

    const AStringView commitRevokeFunction = fullTextureSource.substr(
        commitRevokeFunctionBegin,
        releaseIdentityFunctionBegin - commitRevokeFunctionBegin
    );
    const usize viewDestroyOffset = commitRevokeFunction.find("vkDestroyImageView(");
    const usize viewClearOffset = commitRevokeFunction.find("m_views.clear();");
    const usize imageClearOffset = commitRevokeFunction.find("m_image = VK_NULL_HANDLE;");
    ASSERT_NE(viewDestroyOffset, AStringView::npos);
    ASSERT_NE(viewClearOffset, AStringView::npos);
    ASSERT_NE(imageClearOffset, AStringView::npos);
    EXPECT_LT(viewDestroyOffset, viewClearOffset);
    EXPECT_LT(viewClearOffset, imageClearOffset);
    EXPECT_NE(commitRevokeFunction.find("TerminateInvariant();"), AStringView::npos);
    EXPECT_EQ(commitRevokeFunction.find("unregisterTextureNativeIdentity"), AStringView::npos);
    EXPECT_EQ(commitRevokeFunction.find("NWB_LOGGER_"), AStringView::npos);

    const AStringView releaseIdentityFunction = fullTextureSource.substr(
        releaseIdentityFunctionBegin,
        releaseIdentityFunctionEnd - releaseIdentityFunctionBegin
    );
    const usize releaseInvariantOffset = releaseIdentityFunction.find("TerminateInvariant();");
    const usize unregisterIdentityOffset = releaseIdentityFunction.find(
        "m_allocator.unregisterTextureNativeIdentity(expectedNativeImage, *this);",
        releaseInvariantOffset
    );
    ASSERT_NE(releaseInvariantOffset, AStringView::npos);
    ASSERT_NE(unregisterIdentityOffset, AStringView::npos);
    EXPECT_LT(releaseInvariantOffset, unregisterIdentityOffset);
    EXPECT_EQ(releaseIdentityFunction.find("NWB_LOGGER_"), AStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

