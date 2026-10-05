// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_descriptor_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_CORE = "core";
static constexpr AStringView s_GRAPHICS = "graphics";
static constexpr AStringView s_VULKAN = "vulkan";
static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// A frame graph captures raw bindless slots before native recording. The heap-wide lease is the lifetime bridge:
// frees become an exact pending-recording state, the final overlapping release promotes that batch against the
// latest recorded heap use, and a later lease cannot make an already-retired TLAS generation recordable again.
TEST(EcsGraphics, DescriptorHeapPendingRecordingLeaseBridgesFrameSnapshotsToNativeRecording){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString heapHeaderSource;
    AString heapSource;
    AString heapRetirementSource;
    AString descriptorWriteSource;
    AString slotAllocatorSource;
    AString nativeBindingSource;
    AString rendererExecutionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / s_VULKAN / "backend_pipeline_state.h", heapHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / s_VULKAN / "gpu_descriptor_heap.cpp", heapSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap_retirement.cpp",
        heapRetirementSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap_descriptor_buffer.cpp",
        descriptorWriteSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "resource_bindings_commands.cpp",
        nativeBindingSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline_execute.cpp",
        rendererExecutionSource
    ));
    const AStringView heapHeader(heapHeaderSource.data(), heapHeaderSource.size());
    const AStringView heap(heapSource.data(), heapSource.size());
    const AStringView heapRetirement(heapRetirementSource.data(), heapRetirementSource.size());
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap_slot_allocator.cpp",
        slotAllocatorSource
    ));
    const AStringView slotAllocator(slotAllocatorSource.data(), slotAllocatorSource.size());
    const AStringView descriptorWrite(descriptorWriteSource.data(), descriptorWriteSource.size());
    const AStringView nativeBinding(nativeBindingSource.data(), nativeBindingSource.size());
    const AStringView rendererExecution(rendererExecutionSource.data(), rendererExecutionSource.size());

    EXPECT_TRUE(ContainsText(heapHeader, "enum class SlotState : u8{"));
    EXPECT_TRUE(ContainsText(heapHeader, "PendingRecording,"));
    EXPECT_TRUE(ContainsText(heapHeader, "class PendingRecordingLease final{"));
    EXPECT_TRUE(ContainsText(heapHeader, "PendingRecordingLease(const PendingRecordingLease&) = delete;"));
    EXPECT_TRUE(ContainsText(heapHeader, "PendingRecordingLease(PendingRecordingLease&&) = delete;"));
    EXPECT_TRUE(ContainsText(heapHeader, "u64 m_descriptorBufferGeneration = 0u;"));
    EXPECT_TRUE(ContainsText(heapHeader, "FixedTable<GpuDescriptorHandle> m_pendingRecording;"));
    EXPECT_TRUE(ContainsText(heapHeader, "usize m_pendingRecordingCount = 0u;"));
    EXPECT_TRUE(ContainsText(heapHeader, "usize m_retiredCount = 0u;"));
    EXPECT_TRUE(ContainsText(heapHeader, "usize freeCount = 0u;"));

    EXPECT_TRUE(ContainsText(heap, "if(m_activePendingRecordingLeaseCount != 0u){"));
    EXPECT_TRUE(ContainsText(heap, "allocator.slotStates[handle.slot()] = SlotState::PendingRecording;"));
    EXPECT_TRUE(ContainsText(heap, "m_pendingRecording[m_pendingRecordingCount] = handle;"));
    EXPECT_TRUE(ContainsText(heap, "++m_pendingRecordingCount;"));
    EXPECT_TRUE(ContainsText(heap, "allocator.slotStates[handle.slot()] = SlotState::Retired;"));
    EXPECT_TRUE(ContainsText(heap, "m_retired[m_retiredCount] = RetiredSlot{ m_lastHeapUseID, handle };"));
    EXPECT_TRUE(ContainsText(
        heap,
        "statistics.pendingRetiredSlotCount = m_pendingRecordingCount + m_retiredCount;"
    ));
    EXPECT_TRUE(ContainsText(heap, "!m_resourceSlots.initialize(arena, resourceCapacity)"));
    EXPECT_TRUE(ContainsText(heap, "!m_samplerSlots.initialize(arena, samplerCapacity)"));
    EXPECT_TRUE(ContainsText(heap, "!m_accelStructSlots.initialize(arena, accelStructCapacity)"));
    EXPECT_TRUE(ContainsText(slotAllocator, "bool GpuDescriptorHeap::SlotAllocator::initialize("));
    EXPECT_TRUE(ContainsText(slotAllocator, "!freeList.initialize(arena, newCapacity)"));
    EXPECT_TRUE(ContainsText(slotAllocator, "|| !slotStates.initialize(arena, newCapacity)"));
    EXPECT_TRUE(ContainsText(slotAllocator, "|| !allocatedClasses.initialize(arena, newCapacity)"));
    EXPECT_TRUE(ContainsText(heapRetirement, "allocator.freeList[allocator.freeCount] = retired.handle.slot();")
        || ContainsText(heap, "allocator.freeList[allocator.freeCount] = retired.handle.slot();"));

    const usize releasePendingBegin = heapRetirement.find("void GpuDescriptorHeap::releasePendingRecordingLease(");
    const usize collectRetiredBegin = heapRetirement.find("void GpuDescriptorHeap::collectRetired(){", releasePendingBegin);
    ASSERT_NE(releasePendingBegin, AStringView::npos);
    ASSERT_NE(collectRetiredBegin, AStringView::npos);
    const AStringView releasePending = heapRetirement.substr(releasePendingBegin, collectRetiredBegin - releasePendingBegin);
    EXPECT_TRUE(ContainsText(releasePending, "NothrowScopedLock lock(m_mutex);"));
    EXPECT_TRUE(ContainsText(releasePending, "TerminateInvariant();"));
    EXPECT_FALSE(ContainsText(releasePending, "AbortInvariant"));
    EXPECT_TRUE(ContainsText(releasePending, "--m_activePendingRecordingLeaseCount;"));
    EXPECT_TRUE(ContainsText(releasePending, "m_pendingRecordingCount = 0u;"));
    EXPECT_FALSE(ContainsText(releasePending, "collectRetired()"));
    EXPECT_FALSE(ContainsText(releasePending, "NWB_LOGGER_"));
    EXPECT_FALSE(ContainsText(releasePending, ".push_back("));
    EXPECT_TRUE(ContainsText(
        heap,
        "if(m_activePendingRecordingLeaseCount != 0u){\n"
        "        NWB_LOGGER_ERROR(GLB_TEXT(\"Vulkan: GpuDescriptorHeap initialization rejected while pending-recording leases are active.\"));"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "if(m_activePendingRecordingLeaseCount != 0u || !m_heapUses.empty())"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "m_accelStructSlots.slotStates[handle.slot()] != SlotState::Live"
    ));
    EXPECT_FALSE(ContainsText(
        descriptorWrite,
        "allocator.slotStates[handle.slot()] == SlotState::PendingRecording"
    ));

    EXPECT_TRUE(ContainsText(
        nativeBinding,
        "slotState != GpuDescriptorHeap::SlotState::Live\n"
        "                && slotState != GpuDescriptorHeap::SlotState::PendingRecording"
    ));
    EXPECT_FALSE(ContainsText(nativeBinding, "m_activePendingRecordingLeaseCount != 0u"));

    const usize deviceCheckOffset = rendererExecution.find(
        "if(m_graphics.isDeviceRecreationRequested() || device.requiresRecreation())"
    );
    const usize recoveryCheckOffset = rendererExecution.find("if(m_frameRenderRecoveryFailed)", deviceCheckOffset);
    const usize leaseOffset = rendererExecution.find(
        "Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease",
        recoveryCheckOffset
    );
    const usize snapshotOffset = rendererExecution.find(
        "const RayTracingFrameCpuStateSnapshot rayTracingCpuState",
        leaseOffset
    );
    const usize graphBuildOffset = rendererExecution.find("buildDeferredLightingTaskGraph(", snapshotOffset);
    ASSERT_NE(deviceCheckOffset, AStringView::npos);
    ASSERT_NE(recoveryCheckOffset, AStringView::npos);
    ASSERT_NE(leaseOffset, AStringView::npos);
    ASSERT_NE(snapshotOffset, AStringView::npos);
    ASSERT_NE(graphBuildOffset, AStringView::npos);
    EXPECT_LT(deviceCheckOffset, recoveryCheckOffset);
    EXPECT_LT(recoveryCheckOffset, leaseOffset);
    EXPECT_LT(leaseOffset, snapshotOffset);
    EXPECT_LT(snapshotOffset, graphBuildOffset);
    EXPECT_EQ(CountText(
        rendererExecution,
        "Core::GpuDescriptorHeap::PendingRecordingLease descriptorHeapPendingRecordingLease"
    ), 1u);

    // Check the lease lifetime bridge on the owning heap sources.
    EXPECT_TRUE(ContainsText(heap, "GpuDescriptorHeap::PendingRecordingLease GpuDescriptorHeap::acquirePendingRecordingLease(){"));
    EXPECT_TRUE(ContainsText(heapRetirement, "void GpuDescriptorHeap::releasePendingRecordingLease(const u64 descriptorBufferGeneration)noexcept{"));
    EXPECT_TRUE(ContainsText(
        nativeBinding,
        "managerSnapshot.generation != heap.m_descriptorBufferGeneration"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "allocator.slotStates[handle.slot()] = SlotState::PendingRecording;"
    ));
}


// Descriptor storage may be freed only after host access and native execution are joined. A failed non-loss wait
// quarantines the manager storage for a later retry, while an actual device loss is the only teardown exception.
TEST(EcsGraphics, DescriptorStorageTeardownRequiresCompletedDeviceJoinOrActualLoss){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString managerHeaderSource;
    AString managerSource;
    AString heapSource;
    AString deviceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / s_VULKAN / "backend_pipeline_state.h", managerHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "resource_bindings_descriptor_buffer.cpp",
        managerSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / s_VULKAN / "gpu_descriptor_heap.cpp", heapSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_CORE / s_GRAPHICS / s_VULKAN / "device.cpp", deviceSource));
    const AStringView managerHeader(managerHeaderSource.data(), managerHeaderSource.size());
    const AStringView manager(managerSource.data(), managerSource.size());
    const AStringView heap(heapSource.data(), heapSource.size());
    const AStringView device(deviceSource.data(), deviceSource.size());

    EXPECT_TRUE(ContainsText(managerHeader, "~DescriptorBufferManager()noexcept;"));
    EXPECT_TRUE(ContainsText(managerHeader, "[[nodiscard]] bool shutdownForLifecycleOperation(VkResult& outIdleResult);"));
    EXPECT_TRUE(ContainsText(managerHeader, "void shutdownForDeviceTeardown()noexcept;"));
    EXPECT_TRUE(ContainsText(managerHeader, "[[nodiscard]] bool shutdown();"));
    EXPECT_FALSE(ContainsText(managerHeader, "shutdownAfterDeviceIdleOrLoss"));
    EXPECT_TRUE(ContainsText(
        manager,
        "DescriptorBufferManager::~DescriptorBufferManager()noexcept{\n"
        "    shutdownForDeviceTeardown();"
    ));
    EXPECT_TRUE(ContainsText(
        manager,
        "if(!shutdownForLifecycleOperation(idleResult))\n"
        "        return false;"
    ));
    EXPECT_TRUE(ContainsText(
        manager,
        "outIdleResult != VK_SUCCESS && outIdleResult != VK_ERROR_DEVICE_LOST"
    ));
    EXPECT_TRUE(ContainsText(
        manager,
        "Descriptor-buffer shutdown is refusing to destroy storage after device-idle wait failed."
    ));

    const usize managerWaitOffset = manager.find(
        "outIdleResult = m_device.waitForNativeIdle();"
    );
    const usize managerLossOffset = manager.find("if(outIdleResult == VK_ERROR_DEVICE_LOST)", managerWaitOffset);
    const usize managerRefusalOffset = manager.find(
        "if(outIdleResult != VK_SUCCESS && outIdleResult != VK_ERROR_DEVICE_LOST){",
        managerLossOffset
    );
    const usize managerDestroyOffset = manager.find("shutdownSegment(m_resourceSegment);", managerRefusalOffset);
    ASSERT_NE(managerWaitOffset, AStringView::npos);
    ASSERT_NE(managerLossOffset, AStringView::npos);
    ASSERT_NE(managerRefusalOffset, AStringView::npos);
    ASSERT_NE(managerDestroyOffset, AStringView::npos);
    EXPECT_LT(managerWaitOffset, managerLossOffset);
    EXPECT_LT(managerLossOffset, managerRefusalOffset);
    EXPECT_LT(managerRefusalOffset, managerDestroyOffset);
    EXPECT_TRUE(ContainsText(
        manager,
        "operationLock.unlock();\n"
        "    if(idleResult == VK_ERROR_DEVICE_LOST)\n"
        "        m_device.captureDeviceLoss(\"descriptor-buffer shutdown idle\");"
    ));
    EXPECT_TRUE(ContainsText(
        manager,
        "void DescriptorBufferManager::shutdownForDeviceTeardown()noexcept{\n"
        "    NothrowScopedLock operationLock(m_lifecycleOperationMutex);"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "GpuDescriptorHeap::~GpuDescriptorHeap()noexcept{\n"
        "    shutdownForDeviceTeardown();"
    ));
    EXPECT_TRUE(ContainsText(
        heap,
        "void GpuDescriptorHeap::shutdownForDeviceTeardown()noexcept{\n"
        "    NothrowScopedLock lock(m_mutex);\n"
        "    resetStateForShutdownLocked();"
    ));
    EXPECT_FALSE(ContainsText(heap, "waitForIdle()"));

    const usize deviceWaitOffset = device.find(
        "const VkResult nativeIdleResult = lifecycleDestructionPrepared ? VK_SUCCESS : waitForNativeIdle();"
    );
    const usize deviceSafetyOffset = device.find(
        "const bool nativeTeardownSafe = nativeIdleResult == VK_SUCCESS || nativeIdleResult == VK_ERROR_DEVICE_LOST;",
        deviceWaitOffset
    );
    const usize deviceTerminationOffset = device.find("TerminateInvariant();", deviceSafetyOffset);
    const usize heapDestroyOffset = device.find("m_gpuDescriptorHeap.shutdownForDeviceTeardown();", deviceTerminationOffset);
    const usize deviceManagerDestroyOffset = device.find(
        "m_descriptorBufferManager.shutdownForDeviceTeardown();",
        heapDestroyOffset
    );
    ASSERT_NE(deviceWaitOffset, AStringView::npos);
    ASSERT_NE(deviceSafetyOffset, AStringView::npos);
    ASSERT_NE(deviceTerminationOffset, AStringView::npos);
    ASSERT_NE(heapDestroyOffset, AStringView::npos);
    ASSERT_NE(deviceManagerDestroyOffset, AStringView::npos);
    EXPECT_LT(deviceWaitOffset, deviceSafetyOffset);
    EXPECT_LT(deviceSafetyOffset, deviceTerminationOffset);
    EXPECT_LT(deviceTerminationOffset, heapDestroyOffset);
    EXPECT_LT(heapDestroyOffset, deviceManagerDestroyOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

