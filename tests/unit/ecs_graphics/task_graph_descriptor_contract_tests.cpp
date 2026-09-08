// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_descriptor_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Descriptor heap lifetime is owned by the Device rather than any deferred graph attempt or physical queue. Keep
// one by-value current snapshot on the persistent renderer label so no-graph frames retain this diagnostic context.
TEST(EcsGraphics, FrameGraphExportsDeviceWideDescriptorHeapLifecycle){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", frameGraphSource));
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics =\n"
        "        device.getDescriptorHeap().lifecycleStatistics()\n"
        "    ;"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "\"\\nDescriptor heap lifecycle (device-wide current): initialized={} \"\n"
        "        \"resource live/capacity={}/{} sampler live/capacity={}/{} \"\n"
        "        \"acceleration structure live/capacity={}/{} pending retired slots={} \"\n"
        "        \"accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}\""
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "StringAppendFormat(\n"
        "        m_frameGraphRendererLabel,\n"
        "        \"\\nDescriptor heap lifecycle (device-wide current): initialized={} \"\n"
        "        \"resource live/capacity={}/{} sampler live/capacity={}/{} \"\n"
        "        \"acceleration structure live/capacity={}/{} pending retired slots={} \"\n"
        "        \"accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}\""
    ));

    const usize snapshotOffset = frameGraph.find("const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics");
    const usize runtimeValidOffset = frameGraph.find("if(deferredRuntimeStatistics.valid()){");
    const usize queueLoopOffset = frameGraph.find(
        "for(usize queueIndex = 0u; queueIndex < physicalQueueRuntimeStatistics.size(); ++queueIndex){"
    );
    const usize fallbackOffset = frameGraph.find("m_frameGraphRendererLabel += \"Renderer Frame\";");
    const usize lifecycleLabelOffset = frameGraph.find("Descriptor heap lifecycle (device-wide current):");
    const usize rendererFrameOffset = frameGraph.find("const Handle rendererFrame = builder.addPass(");
    ASSERT_NE(snapshotOffset, AStringView::npos);
    ASSERT_NE(runtimeValidOffset, AStringView::npos);
    ASSERT_NE(queueLoopOffset, AStringView::npos);
    ASSERT_NE(fallbackOffset, AStringView::npos);
    ASSERT_NE(lifecycleLabelOffset, AStringView::npos);
    ASSERT_NE(rendererFrameOffset, AStringView::npos);
    EXPECT_LT(runtimeValidOffset, snapshotOffset);
    EXPECT_LT(queueLoopOffset, snapshotOffset);
    EXPECT_LT(fallbackOffset, snapshotOffset);
    EXPECT_LT(snapshotOffset, lifecycleLabelOffset);
    EXPECT_LT(fallbackOffset, lifecycleLabelOffset);
    EXPECT_LT(lifecycleLabelOffset, rendererFrameOffset);

    const AStringView lifecycleLabel = frameGraph.substr(lifecycleLabelOffset, rendererFrameOffset - lifecycleLabelOffset);
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.initialized,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.resourceLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.resourceCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.samplerLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.samplerCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.accelStructLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.accelStructCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.pendingRetiredSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.acceptedHeapUseCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.unsubmittedHeapUseCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.abandonedHeapUseCount"));
    EXPECT_FALSE(ContainsText(lifecycleLabel, "queueStatistics."));
    EXPECT_FALSE(ContainsText(lifecycleLabel, "queueInfo."));
}


// A descriptor-buffer bind validates every retained resource against the exact physical queue, not just descriptors
// referenced by the current shader. Keep every renderer-owned heap resource admissible to Graphics and AsyncCompute.
TEST(EcsGraphics, GlobalHeapRetainedResourcesAdmitAsyncCompute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rendererSource;
    AString skinningCacheSource;
    AString skinningResourcesSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/material_pass_resources.cpp",
            "csg/csg_peel_targets.cpp",
            "csg/csg_resources.cpp",
            "csg/csg_interval_resources.cpp",
            "mesh/mesh_bindings.cpp",
            "raytrace/rt_shadow.cpp",
            "raytrace/rt_caustics.cpp",
            "raytrace/rt_surfel_gi.cpp",
            "raytrace/rt_swbvh.cpp",
        },
        rendererSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_mesh" / "skinning" / "runtime_cache_resources.cpp",
        skinningCacheSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_mesh" / "skinning" / "resources.cpp",
        skinningResourcesSource
    ));

    const AStringView renderer(rendererSource.data(), rendererSource.size());
    const AStringView skinningCache(skinningCacheSource.data(), skinningCacheSource.size());
    const AStringView skinningResources(skinningResourcesSource.data(), skinningResourcesSource.size());
    constexpr AStringView s_Sharing = "Core::ResourceQueueSharing::GraphicsAndAsyncCompute";
    const auto expectSharedBlock = [&](const AStringView source, const AStringView beginMarker, const AStringView endMarker){
        const usize beginOffset = source.find(beginMarker);
        const usize endOffset = source.find(endMarker, beginOffset);
        ASSERT_NE(beginOffset, AStringView::npos);
        ASSERT_NE(endOffset, AStringView::npos);
        ASSERT_LT(beginOffset, endOffset);
        EXPECT_TRUE(ContainsText(source.substr(beginOffset, endOffset - beginOffset), s_Sharing));
    };

    expectSharedBlock(renderer, "Core::BufferDesc instanceBufferDesc;", "Core::BufferHandle instanceBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc materialTypedBufferDesc;", "Core::BufferHandle materialTypedBuffer =");
    expectSharedBlock(renderer, "auto createCsgTexture =", "auto createPeelTexture =");
    expectSharedBlock(renderer, "[[nodiscard]] static bool ReserveCsgStructuredBuffer(", "[[nodiscard]] static CsgClipCutterResolveResult::Enum");
    expectSharedBlock(renderer, "if(!m_csgState.m_clipContextSlotsBuffer){", "EnsureCsgBufferHeapHandle(");
    expectSharedBlock(renderer, "bool RendererCsgSystem::createCsgIntervalSampleStateBuffer(){", "m_csgState.m_intervalSampleStateBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc emulationVertexBufferDesc;", "mesh.emulationVertexBuffer =");
    expectSharedBlock(renderer, "Core::TextureDesc coarseDesc;", "targets.shadowCoarseTransmittance =");
    expectSharedBlock(renderer, "Core::TextureDesc softHalfADesc;", "targets.shadowSoftHalfA =");
    expectSharedBlock(renderer, "Core::TextureDesc softGeometryDesc;", "targets.shadowSoftGeometry =");
    expectSharedBlock(renderer, "Core::BufferDesc edgeListDesc;", "Core::BufferHandle edgeListBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc edgeStatsDesc;", "m_rayTracingState.m_swShadowEdgeStatsBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc edgeCounterDesc;", "m_rayTracingState.m_swShadowEdgeCounterBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc indirectArgsDesc;", "m_rayTracingState.m_swShadowIndirectArgsBuffer =");
    expectSharedBlock(renderer, "Core::TextureDesc surfelIrradianceHalfDesc;", "targets.surfelIrradianceHalf =");
    expectSharedBlock(renderer, "Core::TextureDesc accumulatorDesc;", "targets.causticAccumulator =");
    expectSharedBlock(renderer, "Core::TextureDesc historyDesc;", "targets.causticHistory =");
    expectSharedBlock(renderer, "Core::TextureDesc halfBDesc;", "targets.causticResolveHalf =");
    expectSharedBlock(renderer, "Core::TextureDesc geometryDesc;", "targets.causticResolveGeometry =");
    expectSharedBlock(renderer, "if(!m_rayTracingState.m_surfelTraceIndirectArgsBuffer){", "m_rayTracingState.m_surfelTraceIndirectArgsBuffer =");
    expectSharedBlock(renderer, "if(!m_rayTracingState.m_surfelFreeListBuffer){", "m_rayTracingState.m_surfelFreeListBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc keysBufferDesc;", "Core::BufferHandle keysBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc payloadBufferDesc;", "Core::BufferHandle payloadBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc counterBufferDesc;", "Core::BufferHandle counterBuffer =");
    expectSharedBlock(renderer, "Core::BufferDesc parentBufferDesc;", "Core::BufferHandle newParentBuffer =");

    EXPECT_EQ(
        CountText(
            skinningCache,
            "const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute"
        ),
        3u
    );
    expectSharedBlock(skinningResources, "return RuntimeMeshBufferUpload::SetupBuffer<PayloadT>(", "static bool RegisterStorageBuffer(");
    expectSharedBlock(skinningResources, "Core::BufferDesc bindlessSlotsBufferDesc;", "rebuilt.bindlessResourceSlotsBuffer =");
}


// A frame graph captures raw bindless slots before native recording. The heap-wide lease is the lifetime bridge:
// frees become an exact pending-recording state, the final overlapping release promotes that batch against the
// latest recorded heap use, and a later lease cannot make an already-retired TLAS generation recordable again.
TEST(EcsGraphics, DescriptorHeapPendingRecordingLeaseBridgesFrameSnapshotsToNativeRecording){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString heapHeaderSource;
    AString heapSource;
    AString descriptorWriteSource;
    AString nativeBindingSource;
    AString rendererExecutionSource;
    AString smokeSource;
    AString rayTracingSmokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", heapHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap.cpp", heapSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap_descriptor_buffer.cpp",
        descriptorWriteSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "resource_bindings_commands.cpp",
        nativeBindingSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp",
        rendererExecutionSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "tests" / "smoke" / "descriptor_buffer" / "round_trip" / "descriptor_heap_recording_lease_tests.cpp",
        smokeSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "tests" / "smoke" / "descriptor_buffer" / "round_trip" / "ray_tracing_descriptor_layout_tests.cpp",
        rayTracingSmokeSource
    ));
    const AStringView heapHeader(heapHeaderSource.data(), heapHeaderSource.size());
    const AStringView heap(heapSource.data(), heapSource.size());
    const AStringView descriptorWrite(descriptorWriteSource.data(), descriptorWriteSource.size());
    const AStringView nativeBinding(nativeBindingSource.data(), nativeBindingSource.size());
    const AStringView rendererExecution(rendererExecutionSource.data(), rendererExecutionSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const AStringView rayTracingSmoke(rayTracingSmokeSource.data(), rayTracingSmokeSource.size());

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
    EXPECT_TRUE(ContainsText(heap, "m_retired[m_retiredCount] = RetiredSlot{ handle, m_lastHeapUseID };"));
    EXPECT_TRUE(ContainsText(
        heap,
        "statistics.pendingRetiredSlotCount = m_pendingRecordingCount + m_retiredCount;"
    ));
    EXPECT_TRUE(ContainsText(heap, "!m_resourceSlots.initialize(arena, resourceCapacity)"));
    EXPECT_TRUE(ContainsText(heap, "!m_samplerSlots.initialize(arena, samplerCapacity)"));
    EXPECT_TRUE(ContainsText(heap, "!m_accelStructSlots.initialize(arena, accelStructCapacity)"));
    EXPECT_TRUE(ContainsText(heap, "!freeList.initialize(arena, newCapacity)"));
    EXPECT_TRUE(ContainsText(heap, "allocator.freeList[allocator.freeCount] = retired.handle.slot();"));

    const usize releasePendingBegin = heap.find("void GpuDescriptorHeap::releasePendingRecordingLease(");
    const usize collectRetiredBegin = heap.find("void GpuDescriptorHeap::collectRetired(){", releasePendingBegin);
    ASSERT_NE(releasePendingBegin, AStringView::npos);
    ASSERT_NE(collectRetiredBegin, AStringView::npos);
    const AStringView releasePending = heap.substr(releasePendingBegin, collectRetiredBegin - releasePendingBegin);
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
        "        NWB_LOGGER_ERROR(NWB_TEXT(\"Vulkan: GpuDescriptorHeap initialization rejected while pending-recording leases are active.\"));"
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

    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferAllocationTest, DescriptorHeapPendingRecordingLeaseProtectsCapturedSlots)"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferAllocationTest, "
        "DescriptorHeapPendingRecordingLeaseRejectsReinitializeAndRecyclesWithoutRecording)"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferAllocationTest, DescriptorHeapPendingRecordingLeaseUnwindPublishesWithoutArenaTraffic)"
    ));
    EXPECT_TRUE(ContainsText(
        smoke,
        "TEST_F(DescriptorBufferRoundTripTest, DeviceDescriptorHeapPendingRecordingLeaseTracksRecordedUse)"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracingSmoke,
        "heap.bindCompute(*commandList, *pipeline, handle);\n"
        "        ASSERT_FALSE(commandList->commandRecordingFailed())"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracingSmoke,
        "native TLAS binding rejected the exact pending-recording generation"
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", managerHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "resource_bindings_descriptor_buffer.cpp",
        managerSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "gpu_descriptor_heap.cpp", heapSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "device.cpp", deviceSource));
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

