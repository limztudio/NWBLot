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
    EXPECT_NE(fullBackendHeader.find("[[nodiscard]] bool setEventQuery(EventQuery* query"), AStringView::npos);
    EXPECT_NE(fullBackendHeader.find("[[nodiscard]] bool waitEventQuery(EventQuery* query"), AStringView::npos);

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
    const usize semanticLockListOffset = fullDeviceSource.find("waitMutexes.push_back(&queue->m_mutex);");
    const usize hostLockListOffset = fullDeviceSource.find(
        "waitMutexes.push_back(&nativeQueueState->hostMutex);",
        semanticLockListOffset
    );
    const usize lockSetOffset = fullDeviceSource.find("DeviceWaitLockSet lockSet(waitMutexes);", hostLockListOffset);
    const usize deviceIdleOffset = fullDeviceSource.find("vkDeviceWaitIdle(m_context.device);", lockSetOffset);
    ASSERT_NE(semanticLockListOffset, AStringView::npos);
    ASSERT_NE(hostLockListOffset, AStringView::npos);
    ASSERT_NE(lockSetOffset, AStringView::npos);
    ASSERT_NE(deviceIdleOffset, AStringView::npos);
    EXPECT_LT(semanticLockListOffset, hostLockListOffset);
    EXPECT_LT(hostLockListOffset, lockSetOffset);
    EXPECT_LT(lockSetOffset, deviceIdleOffset);

    AString deviceQueueSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "device_queue.cpp",
        deviceQueueSource
    ));
    const AStringView fullDeviceQueueSource(deviceQueueSource.data(), deviceQueueSource.size());
    const usize presentFunctionOffset = fullDeviceQueueSource.find("bool Device::presentNativeQueue(");
    const usize registerFunctionOffset = fullDeviceQueueSource.find(
        "bool Device::registerPhysicalQueue(",
        presentFunctionOffset
    );
    ASSERT_NE(presentFunctionOffset, AStringView::npos);
    ASSERT_NE(registerFunctionOffset, AStringView::npos);
    const AStringView presentFunction = fullDeviceQueueSource.substr(
        presentFunctionOffset,
        registerFunctionOffset - presentFunctionOffset
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
        "const bool deviceIdle = m_rhiDevice->waitForIdle();",
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
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(\"event query submit\")", querySubmitOffset), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("ScopedLock queryLock(query->m_mutex);"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("if(query->m_started){"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(\"event query reset\")"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(\"event query poll\")"), AStringView::npos);
    EXPECT_NE(fullQuerySource.find("captureDeviceLoss(\"event query wait\")"), AStringView::npos);

    const usize frameQueryWaitOffset = fullPresentationSource.find("if(!m_rhiDevice->waitEventQuery(query.get())){");
    const usize frameQueryPopOffset = fullPresentationSource.find("m_framesInFlight.pop();", frameQueryWaitOffset);
    const usize frameQuerySubmitOffset = fullPresentationSource.find("if(!m_rhiDevice->setEventQuery(query.get()", frameQueryPopOffset);
    const usize frameQueryPublishOffset = fullPresentationSource.find("m_framesInFlight.push(query);", frameQuerySubmitOffset);
    ASSERT_NE(frameQueryWaitOffset, AStringView::npos);
    ASSERT_NE(frameQueryPopOffset, AStringView::npos);
    ASSERT_NE(frameQuerySubmitOffset, AStringView::npos);
    ASSERT_NE(frameQueryPublishOffset, AStringView::npos);
    EXPECT_LT(frameQueryWaitOffset, frameQueryPopOffset);
    EXPECT_LT(frameQueryPopOffset, frameQuerySubmitOffset);
    EXPECT_LT(frameQuerySubmitOffset, frameQueryPublishOffset);
}


// Logical submission quarantine must stop a generation without claiming native device loss. Only an observed
// VK_ERROR_DEVICE_LOST may authorize teardown without a successful device-idle join or collect loss diagnostics.
TEST(SwapChainPresentation, LogicalQuarantineRemainsDistinctFromNativeDeviceLoss){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendHeaderSource;
    AString diagnosticSource;
    AString presentationSource;
    AString deviceQueueSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", backendHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device_diagnostics.cpp", diagnosticSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_presentation.cpp",
        presentationSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device_queue.cpp", deviceQueueSource));
    const AStringView backendHeader(backendHeaderSource.data(), backendHeaderSource.size());
    const AStringView diagnostics(diagnosticSource.data(), diagnosticSource.size());
    const AStringView presentation(presentationSource.data(), presentationSource.size());
    const AStringView deviceQueue(deviceQueueSource.data(), deviceQueueSource.size());

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
    EXPECT_NE(diagnostics.find("void Device::captureDeviceLoss("), AStringView::npos);
    EXPECT_NE(diagnostics.find("m_deviceLost.store(true, MemoryOrder::release);"), AStringView::npos);
    EXPECT_EQ(diagnostics.find("m_deviceQuarantined.store"), AStringView::npos);
    EXPECT_EQ(backendHeader.find("captureGpuCrash"), AStringView::npos);

    EXPECT_NE(presentation.find("captureDeviceLoss(\"acquire next image\")"), AStringView::npos);
    EXPECT_NE(presentation.find("captureDeviceLoss(\"present\")"), AStringView::npos);
    EXPECT_NE(presentation.find("m_rhiDevice->quarantineDevice();"), AStringView::npos);
    EXPECT_EQ(presentation.find("captureDeviceLoss(\"present semaphore idle\")"), AStringView::npos);
    EXPECT_NE(deviceQueue.find("if(submissionsBlocked())"), AStringView::npos);
    EXPECT_NE(deviceQueue.find("bool Device::beginLifecycleDrain()noexcept{"), AStringView::npos);
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

    AString graphicsSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    const AStringView fullGraphicsSource(graphicsSource.data(), graphicsSource.size());
    const usize graphicsDestroyOffset = fullGraphicsSource.find("bool Graphics::destroy(){");
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
    const usize graphicsDestructorOffset = fullGraphicsSource.find("Graphics::~Graphics(){");
    const usize destructorAssertionOffset = fullGraphicsSource.find("NWB_FATAL_ASSERT_MSG(", graphicsDestructorOffset);
    const usize destructorDestroyOffset = fullGraphicsSource.find("destroy(),", destructorAssertionOffset);
    ASSERT_NE(graphicsDestructorOffset, AStringView::npos);
    ASSERT_NE(destructorAssertionOffset, AStringView::npos);
    ASSERT_NE(destructorDestroyOffset, AStringView::npos);
    EXPECT_LT(graphicsDestructorOffset, destructorAssertionOffset);
    EXPECT_LT(destructorAssertionOffset, destructorDestroyOffset);
    EXPECT_LT(destructorDestroyOffset, graphicsDestroyOffset);
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
    const usize resizeDestroyOffset = fullOrchestrationSource.find("if(!destroySwapChainPrepared())", resizeCommitOffset);
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
    const usize destroyFunctionBegin = fullSurfaceSource.find("bool BackendContext::destroySwapChainPrepared()noexcept{");
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
    EXPECT_EQ(destroyFunction.find("waitForIdle()"), AStringView::npos);

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

