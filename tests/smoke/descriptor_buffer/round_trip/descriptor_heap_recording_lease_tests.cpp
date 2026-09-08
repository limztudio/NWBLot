// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Heap slots are raw ABI values, so lifecycle bookkeeping must prevent a duplicate free from recycling the same raw
// slot twice and must reject a write through a handle that is already in its deferred-free quarantine.
TEST_F(DescriptorBufferAllocationTest, DescriptorHeapRejectsRetiredAndDoubleFreedHandles){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(2u)
        .setSamplerCapacity(1u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    const GpuDescriptorHeapLifecycleStatistics initialStatistics = heap.lifecycleStatistics();
    EXPECT_TRUE(initialStatistics.initialized);
    EXPECT_EQ(initialStatistics.resourceCapacity, 2u);
    EXPECT_EQ(initialStatistics.samplerCapacity, 1u);
    EXPECT_EQ(initialStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(initialStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(initialStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(initialStatistics.abandonedHeapUseCount, 0u);

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle retired = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(retired.valid());
    ASSERT_TRUE(heap.write(retired, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    const GpuDescriptorHeapLifecycleStatistics allocatedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(allocatedStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(allocatedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(allocatedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(allocatedStatistics.abandonedHeapUseCount, 0u);

    heap.free(retired);
    const GpuDescriptorHeapLifecycleStatistics immediatelyRetiredStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(immediatelyRetiredStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(immediatelyRetiredStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(immediatelyRetiredStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(immediatelyRetiredStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(immediatelyRetiredStatistics.abandonedHeapUseCount, 0u);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(heap.write(retired, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        heap.free(retired);
    }, "");
#else
    EXPECT_FALSE(heap.write(retired, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    heap.free(retired);
#endif

    const GpuDescriptorHandle first = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle second = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_NE(first, second);
}


// CPU graph snapshots can outlive the registry generation they captured until native recording consumes their raw
// descriptor slots. Overlapping leases therefore keep every freed slot quarantined until the final lease releases.
TEST_F(DescriptorBufferAllocationTest, DescriptorHeapPendingRecordingLeaseProtectsCapturedSlots){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(2u)
        .setSamplerCapacity(1u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle capturedHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(capturedHandle.valid());
    ASSERT_TRUE(heap.write(
        capturedHandle,
        DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())
    ));
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

    GpuDescriptorHandle secondPendingHandle = GpuDescriptorHandle::invalid();
    ArenaMemoryStats beforeFinalLeaseRelease;
    {
        auto outerLease = heap.acquirePendingRecordingLease();
        ASSERT_TRUE(outerLease.valid());
        {
            auto innerLease = heap.acquirePendingRecordingLease();
            ASSERT_TRUE(innerLease.valid());

            heap.free(capturedHandle);
            const GpuDescriptorHeapLifecycleStatistics firstPendingStatistics = heap.lifecycleStatistics();
            EXPECT_EQ(firstPendingStatistics.resourceLiveSlotCount, 0u);
            EXPECT_EQ(firstPendingStatistics.pendingRetiredSlotCount, 1u);
            EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

            secondPendingHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
            ASSERT_TRUE(secondPendingHandle.valid());
            EXPECT_NE(secondPendingHandle.slot(), capturedHandle.slot());
            heap.free(secondPendingHandle);
            heap.collectRetired();
            EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 2u);

            heap.shutdown();
            EXPECT_TRUE(heap.isInitialized()) << "an active recording lease allowed heap shutdown";
        }
        EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 2u)
            << "an overlapping lease release promoted slots too early";
        beforeFinalLeaseRelease = DescriptorBufferRoundTripTest::arena().memoryStats();
    }

    const ArenaMemoryStats afterFinalLeaseRelease = DescriptorBufferRoundTripTest::arena().memoryStats();
    EXPECT_EQ(afterFinalLeaseRelease.allocationCount, beforeFinalLeaseRelease.allocationCount);
    EXPECT_EQ(afterFinalLeaseRelease.reallocationCount, beforeFinalLeaseRelease.reallocationCount);
    EXPECT_EQ(afterFinalLeaseRelease.deallocationCount, beforeFinalLeaseRelease.deallocationCount);
    const GpuDescriptorHeapLifecycleStatistics promotedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(promotedStatistics.pendingRetiredSlotCount, 2u);
    EXPECT_EQ(promotedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(promotedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics completedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(completedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 1u);

    const GpuDescriptorHandle recycledFirst = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle recycledSecond = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(recycledFirst.valid());
    ASSERT_TRUE(recycledSecond.valid());
    EXPECT_TRUE(recycledFirst.slot() == capturedHandle.slot() || recycledSecond.slot() == capturedHandle.slot());
    EXPECT_TRUE(
        recycledFirst.slot() == secondPendingHandle.slot()
        || recycledSecond.slot() == secondPendingHandle.slot()
    );
    heap.free(recycledFirst);
    heap.free(recycledSecond);
    heap.collectRetired();
}


// A pending snapshot with no native recording has no GPU boundary to await. Reinitialization must preserve its heap
// generation, and final lease release must publish retirement without allocation before explicit maintenance reuses it.
TEST_F(DescriptorBufferAllocationTest, DescriptorHeapPendingRecordingLeaseRejectsReinitializeAndRecyclesWithoutRecording){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(1u)
        .setSamplerCapacity(1u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    const GpuDescriptorHandle capturedHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(capturedHandle.valid());
    {
        auto pendingRecordingLease = heap.acquirePendingRecordingLease();
        ASSERT_TRUE(pendingRecordingLease.valid());
        heap.free(capturedHandle);
        EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 1u);

#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(heap.initialize(heapDesc));
        }, "");
#else
        EXPECT_FALSE(heap.initialize(heapDesc));
#endif
        EXPECT_TRUE(heap.isInitialized());
        EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 1u);
    }

    EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 1u);
    heap.collectRetired();
    EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 0u);
    const GpuDescriptorHandle recycledHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(recycledHandle.valid());
    EXPECT_EQ(recycledHandle.slot(), capturedHandle.slot());
    heap.free(recycledHandle);
    heap.collectRetired();
}


// The lease destructor runs during ordinary stack unwinding. It publishes into the pre-sized retirement journal and
// must not allocate, deallocate, log, query a queue, or invoke an explicit maintenance operation.
TEST_F(DescriptorBufferAllocationTest, DescriptorHeapPendingRecordingLeaseUnwindPublishesWithoutArenaTraffic){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(1u)
        .setSamplerCapacity(1u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    const GpuDescriptorHandle capturedHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(capturedHandle.valid());
    ArenaMemoryStats beforeUnwind;
    EXPECT_THROW({
        auto pendingRecordingLease = heap.acquirePendingRecordingLease();
        ASSERT_TRUE(pendingRecordingLease.valid());
        heap.free(capturedHandle);
        beforeUnwind = DescriptorBufferRoundTripTest::arena().memoryStats();
        throw 7u;
    }, u32);

    const ArenaMemoryStats afterUnwind = DescriptorBufferRoundTripTest::arena().memoryStats();
    EXPECT_EQ(afterUnwind.allocationCount, beforeUnwind.allocationCount);
    EXPECT_EQ(afterUnwind.reallocationCount, beforeUnwind.reallocationCount);
    EXPECT_EQ(afterUnwind.deallocationCount, beforeUnwind.deallocationCount);
    EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 1u);

    heap.collectRetired();
    EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 0u);
    const GpuDescriptorHandle recycledHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(recycledHandle.valid());
    EXPECT_EQ(recycledHandle.slot(), capturedHandle.slot());
    heap.free(recycledHandle);
    heap.collectRetired();
}


// Only the device-owned heap can cross the native binding ingress. Record after freeing a captured slot, then prove
// final lease release ties its retirement to that still-unsubmitted command buffer instead of recycling it early.
TEST_F(DescriptorBufferRoundTripTest, DeviceDescriptorHeapPendingRecordingLeaseTracksRecordedUse){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());
    ASSERT_TRUE(device.waitForIdle());
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics baselineStatistics = heap.lifecycleStatistics();

    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/pending_recording_lease"})
    ;
    auto shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapRetirementComputeSpirv,
        sizeof(s_DescriptorHeapRetirementComputeSpirv)
    );
    ASSERT_TRUE(shader);

    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    auto pipeline = device.createComputePipeline(pipelineDesc);
    ASSERT_TRUE(pipeline);

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle capturedHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(capturedHandle.valid());
    ASSERT_TRUE(heap.write(
        capturedHandle,
        DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())
    ));
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    {
        auto pendingRecordingLease = heap.acquirePendingRecordingLease();
        ASSERT_TRUE(pendingRecordingLease.valid());
        heap.free(capturedHandle);
        const GpuDescriptorHeapLifecycleStatistics pendingStatistics = heap.lifecycleStatistics();
        EXPECT_EQ(pendingStatistics.resourceLiveSlotCount, baselineStatistics.resourceLiveSlotCount);
        EXPECT_EQ(pendingStatistics.pendingRetiredSlotCount, baselineStatistics.pendingRetiredSlotCount + 1u);

        commandList->open();
        ComputeState computeState;
        computeState.setPipeline(pipeline.get());
        commandList->setComputeState(computeState);
        heap.bindCompute(*commandList, *pipeline);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        ASSERT_FALSE(commandList->commandRecordingFailed());
        ASSERT_TRUE(commandList->hasCommandBuffer());
    }

    const GpuDescriptorHeapLifecycleStatistics recordedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(recordedStatistics.pendingRetiredSlotCount, baselineStatistics.pendingRetiredSlotCount + 1u);
    EXPECT_EQ(recordedStatistics.acceptedHeapUseCount, baselineStatistics.acceptedHeapUseCount);
    EXPECT_EQ(recordedStatistics.unsubmittedHeapUseCount, baselineStatistics.unsubmittedHeapUseCount + 1u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken submissionToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(submissionToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    heap.collectRetired();

    const GpuDescriptorHeapLifecycleStatistics completedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(completedStatistics.pendingRetiredSlotCount, baselineStatistics.pendingRetiredSlotCount);
    EXPECT_EQ(completedStatistics.acceptedHeapUseCount, baselineStatistics.acceptedHeapUseCount);
    EXPECT_EQ(completedStatistics.unsubmittedHeapUseCount, baselineStatistics.unsubmittedHeapUseCount);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 1u);

    const GpuDescriptorHandle recycledHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(recycledHandle.valid());
    EXPECT_EQ(recycledHandle.slot(), capturedHandle.slot());
    heap.free(recycledHandle);
    heap.collectRetired();
}


// Heap bookkeeping has fixed bounds at initialize(). Exercise its public lifecycle at every bound: shutdown must
// release retained resources, capacity changes must start from a fully reset generation, and the pre-sized slot and
// retirement storage must recycle a complete leased generation without touching the shared object arena.
TEST_F(DescriptorBufferAllocationTest, DescriptorHeapFixedMetadataResizesAndRecyclesLeasedFullCapacity){
    auto& device = DescriptorBufferRoundTripTest::device();
    GraphicsBackend::GpuDescriptorHeap heap(device);
    const auto makeHeapDesc = [](const u32 resourceCapacity, const u32 samplerCapacity){
        GpuDescriptorHeapDesc desc;
        desc
            .setResourceCapacity(resourceCapacity)
            .setSamplerCapacity(samplerCapacity)
            .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
        ;
        return desc;
    };
    const auto expectShutdownLifecycle = [&heap](){
        const GpuDescriptorHeapLifecycleStatistics statistics = heap.lifecycleStatistics();
        EXPECT_FALSE(statistics.initialized);
        EXPECT_EQ(statistics.resourceCapacity, 0u);
        EXPECT_EQ(statistics.samplerCapacity, 0u);
        EXPECT_EQ(statistics.accelStructCapacity, 0u);
        EXPECT_EQ(statistics.resourceLiveSlotCount, 0u);
        EXPECT_EQ(statistics.samplerLiveSlotCount, 0u);
        EXPECT_EQ(statistics.accelStructLiveSlotCount, 0u);
        EXPECT_EQ(statistics.pendingRetiredSlotCount, 0u);
        EXPECT_EQ(statistics.acceptedHeapUseCount, 0u);
        EXPECT_EQ(statistics.unsubmittedHeapUseCount, 0u);
        EXPECT_EQ(statistics.abandonedHeapUseCount, 0u);
    };

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    auto sampler = device.createSampler(SamplerDesc().setAllFilters(true));
    ASSERT_TRUE(storageBuffer);
    ASSERT_TRUE(sampler);
    const usize resourceReferencesBeforeHeap = storageBuffer->getReferenceCount();
    const usize samplerReferencesBeforeHeap = sampler->getReferenceCount();

    constexpr u32 s_InitialResourceCapacity = 2u;
    constexpr u32 s_InitialSamplerCapacity = 2u;
    ASSERT_TRUE(heap.initialize(makeHeapDesc(s_InitialResourceCapacity, s_InitialSamplerCapacity)));
    EXPECT_EQ(heap.lifecycleStatistics().resourceCapacity, s_InitialResourceCapacity);
    EXPECT_EQ(heap.lifecycleStatistics().samplerCapacity, s_InitialSamplerCapacity);

    // Leave both descriptors live deliberately. resetStateForShutdownLocked() must discard these retainers even
    // though they never flowed through the normal deferred-free path.
    const GpuDescriptorHandle retainedResource = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle retainedSampler = heap.allocate(GpuDescriptorClass::Sampler);
    ASSERT_TRUE(retainedResource.valid());
    ASSERT_TRUE(retainedSampler.valid());
    ASSERT_TRUE(heap.write(retainedResource, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    ASSERT_TRUE(heap.write(retainedSampler, DescriptorWriteItem::Sampler(0u, sampler.get())));
    EXPECT_EQ(storageBuffer->getReferenceCount(), resourceReferencesBeforeHeap + 1u);
    EXPECT_EQ(sampler->getReferenceCount(), samplerReferencesBeforeHeap + 1u);

    heap.shutdown();
    expectShutdownLifecycle();
    EXPECT_EQ(storageBuffer->getReferenceCount(), resourceReferencesBeforeHeap);
    EXPECT_EQ(sampler->getReferenceCount(), samplerReferencesBeforeHeap);

    constexpr u32 s_LargeResourceCapacity = 5u;
    constexpr u32 s_LargeSamplerCapacity = 4u;
    ASSERT_TRUE(heap.initialize(makeHeapDesc(s_LargeResourceCapacity, s_LargeSamplerCapacity)));
    const GpuDescriptorHeapLifecycleStatistics largeStatistics = heap.lifecycleStatistics();
    ASSERT_TRUE(largeStatistics.initialized);
    EXPECT_EQ(largeStatistics.resourceCapacity, s_LargeResourceCapacity);
    EXPECT_EQ(largeStatistics.samplerCapacity, s_LargeSamplerCapacity);

    GpuDescriptorHandle resourceHandles[s_LargeResourceCapacity] = {};
    GpuDescriptorHandle samplerHandles[s_LargeSamplerCapacity] = {};
    const ArenaMemoryStats beforeLeasedRecycle = DescriptorBufferRoundTripTest::arena().memoryStats();
    {
        auto pendingRecordingLease = heap.acquirePendingRecordingLease();
        ASSERT_TRUE(pendingRecordingLease.valid());

        for(u32 slot = 0u; slot < s_LargeResourceCapacity; ++slot){
            resourceHandles[slot] = heap.allocate(GpuDescriptorClass::StorageBuffer);
            ASSERT_TRUE(resourceHandles[slot].valid());
            ASSERT_TRUE(heap.write(
                resourceHandles[slot],
                DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())
            ));
        }
        for(u32 slot = 0u; slot < s_LargeSamplerCapacity; ++slot){
            samplerHandles[slot] = heap.allocate(GpuDescriptorClass::Sampler);
            ASSERT_TRUE(samplerHandles[slot].valid());
            ASSERT_TRUE(heap.write(samplerHandles[slot], DescriptorWriteItem::Sampler(0u, sampler.get())));
        }
        const GpuDescriptorHeapLifecycleStatistics fullStatistics = heap.lifecycleStatistics();
        EXPECT_EQ(fullStatistics.resourceLiveSlotCount, s_LargeResourceCapacity);
        EXPECT_EQ(fullStatistics.samplerLiveSlotCount, s_LargeSamplerCapacity);
        EXPECT_EQ(fullStatistics.pendingRetiredSlotCount, 0u);
        EXPECT_EQ(storageBuffer->getReferenceCount(), resourceReferencesBeforeHeap + s_LargeResourceCapacity);
        EXPECT_EQ(sampler->getReferenceCount(), samplerReferencesBeforeHeap + s_LargeSamplerCapacity);

        for(const GpuDescriptorHandle handle : resourceHandles)
            heap.free(handle);
        for(const GpuDescriptorHandle handle : samplerHandles)
            heap.free(handle);
        const GpuDescriptorHeapLifecycleStatistics pendingStatistics = heap.lifecycleStatistics();
        EXPECT_EQ(pendingStatistics.resourceLiveSlotCount, 0u);
        EXPECT_EQ(pendingStatistics.samplerLiveSlotCount, 0u);
        EXPECT_EQ(
            pendingStatistics.pendingRetiredSlotCount,
            s_LargeResourceCapacity + s_LargeSamplerCapacity
        );
    }

    const GpuDescriptorHeapLifecycleStatistics releasedLeaseStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(
        releasedLeaseStatistics.pendingRetiredSlotCount,
        s_LargeResourceCapacity + s_LargeSamplerCapacity
    );
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics collectedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(collectedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(collectedStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(collectedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), resourceReferencesBeforeHeap);
    EXPECT_EQ(sampler->getReferenceCount(), samplerReferencesBeforeHeap);

    // Refill both namespaces from the free lists. This must stay entirely inside the initialized heap's fixed
    // metadata; no GlobalArena allocation, reallocation, or deallocation is allowed on this hot lifecycle path.
    for(GpuDescriptorHandle& handle : resourceHandles){
        handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(handle.valid());
    }
    for(GpuDescriptorHandle& handle : samplerHandles){
        handle = heap.allocate(GpuDescriptorClass::Sampler);
        ASSERT_TRUE(handle.valid());
    }
    const GpuDescriptorHeapLifecycleStatistics recycledStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(recycledStatistics.resourceLiveSlotCount, s_LargeResourceCapacity);
    EXPECT_EQ(recycledStatistics.samplerLiveSlotCount, s_LargeSamplerCapacity);
    EXPECT_EQ(recycledStatistics.pendingRetiredSlotCount, 0u);
    const ArenaMemoryStats afterLeasedRecycle = DescriptorBufferRoundTripTest::arena().memoryStats();
    EXPECT_EQ(afterLeasedRecycle.usedBytes, beforeLeasedRecycle.usedBytes);
    EXPECT_EQ(afterLeasedRecycle.allocationCount, beforeLeasedRecycle.allocationCount);
    EXPECT_EQ(afterLeasedRecycle.reallocationCount, beforeLeasedRecycle.reallocationCount);
    EXPECT_EQ(afterLeasedRecycle.deallocationCount, beforeLeasedRecycle.deallocationCount);

    // A new descriptor size must never overwrite the active generation. Tear it down first, then create a smaller
    // fixed arena/table set and prove both namespaces begin usable again.
    heap.shutdown();
    expectShutdownLifecycle();

    constexpr u32 s_SmallResourceCapacity = 1u;
    constexpr u32 s_SmallSamplerCapacity = 1u;
    ASSERT_TRUE(heap.initialize(makeHeapDesc(s_SmallResourceCapacity, s_SmallSamplerCapacity)));
    const GpuDescriptorHeapLifecycleStatistics smallStatistics = heap.lifecycleStatistics();
    ASSERT_TRUE(smallStatistics.initialized);
    EXPECT_EQ(smallStatistics.resourceCapacity, s_SmallResourceCapacity);
    EXPECT_EQ(smallStatistics.samplerCapacity, s_SmallSamplerCapacity);
    const GpuDescriptorHandle smallResource = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle smallSampler = heap.allocate(GpuDescriptorClass::Sampler);
    ASSERT_TRUE(smallResource.valid());
    ASSERT_TRUE(smallSampler.valid());
    heap.free(smallResource);
    heap.free(smallSampler);
    heap.collectRetired();
    heap.shutdown();
    expectShutdownLifecycle();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

