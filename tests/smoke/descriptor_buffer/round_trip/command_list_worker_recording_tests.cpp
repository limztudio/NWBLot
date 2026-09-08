// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Fan-in only accepts branch deltas that agree on every shared resource. A scheduler must split or serialize
// conflicting packets instead of selecting one final layout arbitrarily.
TEST_F(DescriptorBufferRoundTripTest, StateFanInRejectsConflictingBranchFinalStates){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);

    CommandListResourceStateHandoff baseState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff firstBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff secondBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff fanInState(DescriptorBufferRoundTripTest::arena());
    auto base = device.createCommandList();
    auto firstBranch = device.createCommandList();
    auto secondBranch = device.createCommandList();
    ASSERT_NE(base.get(), nullptr);
    ASSERT_NE(firstBranch.get(), nullptr);
    ASSERT_NE(secondBranch.get(), nullptr);

    base->open();
    base->setTextureState(texture.get(), s_AllSubresources, ResourceStates::RenderTarget);
    base->close(&baseState);
    ASSERT_TRUE(baseState.valid());

    firstBranch->open(&baseState);
    firstBranch->setTextureState(texture.get(), s_AllSubresources, ResourceStates::ShaderResource);
    firstBranch->close(&firstBranchState);
    ASSERT_TRUE(firstBranchState.valid());

    secondBranch->open(&baseState);
    secondBranch->setTextureState(texture.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    secondBranch->close(&secondBranchState);
    ASSERT_TRUE(secondBranchState.valid());

    const CommandListResourceStateHandoff* branchStates[] = { &firstBranchState, &secondBranchState };
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/conflicting_fan_in"));
    EXPECT_FALSE(fanInState.buildFanIn(baseState, branchStates, 2u, fanInScratchArena));
    EXPECT_FALSE(fanInState.valid());
}


// Marker depth is logical state even when vendor crash-marker extensions are unavailable. Close recovers an
// unterminated native label before vkEndCommandBuffer, while clear/reopen require a clean logical boundary.
TEST_F(DescriptorBufferRoundTripTest, CommandListMarkerStateBalancesBeforeReuseAndEnforcesCleanBoundaries){
    auto& device = DescriptorBufferRoundTripTest::device();
    CommandListHandle commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    commandList->beginMarker("tests/marker_recovery/close");
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    EXPECT_FALSE(commandList->isRecording());

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    commandList->beginMarker("tests/marker_recovery/clear");
    commandList->endMarker();
    commandList->clearState();
    EXPECT_TRUE(commandList->isRecording());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    commandList->beginMarker("tests/marker_recovery/reused");
    commandList->endMarker();
    commandList->close();

    CommandList* commandLists[] = { commandList.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());

    CommandListHandle abandonedRecording = device.createCommandList();
    ASSERT_NE(abandonedRecording.get(), nullptr);
    abandonedRecording->open();
    abandonedRecording->beginMarker("tests/marker_recovery/abandoned");
    abandonedRecording->endMarker();
    abandonedRecording->open();
    ASSERT_TRUE(abandonedRecording->isRecording());
    abandonedRecording->close();

#if defined(NWB_DEBUG)
    CommandListHandle clearInvariant = device.createCommandList();
    ASSERT_NE(clearInvariant.get(), nullptr);
    EXPECT_DEATH_IF_SUPPORTED({
        clearInvariant->open();
        clearInvariant->beginMarker("tests/marker_invariant/clear");
        clearInvariant->clearState();
    }, "");

    CommandListHandle openInvariant = device.createCommandList();
    ASSERT_NE(openInvariant.get(), nullptr);
    EXPECT_DEATH_IF_SUPPORTED({
        openInvariant->open();
        openInvariant->beginMarker("tests/marker_invariant/open");
        openInvariant->open();
    }, "");
#endif
}


// Independent primary command lists use distinct Vulkan command-pool shards. Start both recording jobs at the same
// latch, then record a second batch after timeline retirement to cover worker-affine recycler reuse.
TEST_F(DescriptorBufferRoundTripTest, IndependentPrimaryCommandListsRecordConcurrentlyOnGraphicsWorkers){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(device.waitForIdle());
    device.runGarbageCollection();
    const GpuCommandArenaStatistics beforeStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(beforeStatistics.valid());
    EXPECT_EQ(beforeStatistics.queue, graphicsQueue);
    EXPECT_EQ(
        beforeStatistics.currentCommandBufferCount,
        beforeStatistics.reusableCommandBufferCount
            + beforeStatistics.leasedCommandBufferCount
            + beforeStatistics.pendingCommandBufferCount
    );
    auto firstBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto secondBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(firstBuffer.get(), nullptr);
    ASSERT_NE(secondBuffer.get(), nullptr);

    // Reserve worker identities at the far end of the index domain for this smoke. Existing process-wide queue
    // storage may be populated by earlier tests, while these two shards still have deterministic first-growth and
    // second-recording reuse transitions.
    constexpr u32 s_FirstTelemetryWorkerIndex = Limit<u32>::s_Max - 1u;
    constexpr u32 s_SecondTelemetryWorkerIndex = Limit<u32>::s_Max - 2u;
    CommandListParameters firstParameters;
    firstParameters
        .setPhysicalQueue(graphicsQueue)
        .setRecordingWorkerIndex(s_FirstTelemetryWorkerIndex)
    ;
    CommandListParameters secondParameters;
    secondParameters
        .setPhysicalQueue(graphicsQueue)
        .setRecordingWorkerIndex(s_SecondTelemetryWorkerIndex)
    ;
    auto firstCommandList = device.createCommandList(firstParameters);
    auto secondCommandList = device.createCommandList(secondParameters);
    ASSERT_NE(firstCommandList.get(), nullptr);
    ASSERT_NE(secondCommandList.get(), nullptr);
    EXPECT_EQ(firstCommandList->getDescription().recordingWorkerIndex, s_FirstTelemetryWorkerIndex);
    EXPECT_EQ(secondCommandList->getDescription().recordingWorkerIndex, s_SecondTelemetryWorkerIndex);

    Latch recordingStarted(2);
    bool firstRecorded = false;
    bool secondRecorded = false;
    const Graphics::TaskHandle firstJob = graphics.scheduleGraphicsTask([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        firstCommandList->open();
        firstCommandList->setBufferState(firstBuffer.get(), ResourceStates::CopyDest);
        firstCommandList->close();
        firstRecorded = true;
    });
    const Graphics::TaskHandle secondJob = graphics.scheduleGraphicsTask([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        secondCommandList->open();
        secondCommandList->setBufferState(secondBuffer.get(), ResourceStates::CopyDest);
        secondCommandList->close();
        secondRecorded = true;
    });
    ASSERT_TRUE(firstJob.valid());
    ASSERT_TRUE(secondJob.valid());

    graphics.waitTask(firstJob);
    graphics.waitTask(secondJob);
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);

    const GpuCommandArenaStatistics growthStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(growthStatistics.valid());
    EXPECT_EQ(growthStatistics.growthEventCount, beforeStatistics.growthEventCount + 2u);
    EXPECT_EQ(growthStatistics.currentCommandBufferCount, beforeStatistics.currentCommandBufferCount + 2u);
    EXPECT_GE(growthStatistics.highWaterCommandBufferCount, growthStatistics.currentCommandBufferCount);
    EXPECT_GE(growthStatistics.highWaterCommandBufferCount, beforeStatistics.highWaterCommandBufferCount);
    EXPECT_EQ(growthStatistics.workerArenaCount, beforeStatistics.workerArenaCount + 2u);
    EXPECT_EQ(growthStatistics.commandPoolEpochCount, beforeStatistics.commandPoolEpochCount + 2u);
    EXPECT_EQ(growthStatistics.leasedCommandBufferCount, beforeStatistics.leasedCommandBufferCount + 2u);
    EXPECT_GE(growthStatistics.commandPoolEpochCount, growthStatistics.workerArenaCount);
    EXPECT_GT(
        growthStatistics.nativeHandleStorageLowerBoundBytes,
        beforeStatistics.nativeHandleStorageLowerBoundBytes
    );
    const GpuCommandArenaWorkerStatistics firstGrowthWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_FirstTelemetryWorkerIndex)
    ;
    const GpuCommandArenaWorkerStatistics secondGrowthWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_SecondTelemetryWorkerIndex)
    ;
    ASSERT_TRUE(firstGrowthWorkerStatistics.valid());
    ASSERT_TRUE(secondGrowthWorkerStatistics.valid());
    EXPECT_EQ(firstGrowthWorkerStatistics.commandPoolEpochCount, 1u);
    EXPECT_EQ(secondGrowthWorkerStatistics.commandPoolEpochCount, 1u);
    EXPECT_EQ(firstGrowthWorkerStatistics.currentCommandBufferCount, 1u);
    EXPECT_EQ(secondGrowthWorkerStatistics.currentCommandBufferCount, 1u);
    EXPECT_EQ(firstGrowthWorkerStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(secondGrowthWorkerStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(firstGrowthWorkerStatistics.growthEventCount, 1u);
    EXPECT_EQ(secondGrowthWorkerStatistics.growthEventCount, 1u);
    EXPECT_GE(firstGrowthWorkerStatistics.highWaterCommandBufferCount, 1u);
    EXPECT_GE(secondGrowthWorkerStatistics.highWaterCommandBufferCount, 1u);

    CommandList* commandLists[] = { firstCommandList.get(), secondCommandList.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 2u, graphicsQueue, &submitted), 0u);
    EXPECT_TRUE(submitted);
    const GpuCommandArenaStatistics pendingStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(pendingStatistics.valid());
    EXPECT_EQ(pendingStatistics.pendingCommandBufferCount, beforeStatistics.pendingCommandBufferCount + 2u);
    EXPECT_EQ(pendingStatistics.pendingCommandPoolEpochCount, beforeStatistics.pendingCommandPoolEpochCount + 2u);
    const GpuCommandArenaWorkerStatistics firstPendingWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_FirstTelemetryWorkerIndex)
    ;
    const GpuCommandArenaWorkerStatistics secondPendingWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_SecondTelemetryWorkerIndex)
    ;
    ASSERT_TRUE(firstPendingWorkerStatistics.valid());
    ASSERT_TRUE(secondPendingWorkerStatistics.valid());
    EXPECT_EQ(firstPendingWorkerStatistics.pendingCommandBufferCount, 1u);
    EXPECT_EQ(secondPendingWorkerStatistics.pendingCommandBufferCount, 1u);
    EXPECT_EQ(firstPendingWorkerStatistics.pendingCommandPoolEpochCount, 1u);
    EXPECT_EQ(secondPendingWorkerStatistics.pendingCommandPoolEpochCount, 1u);
    EXPECT_TRUE(device.waitForIdle());
    device.runGarbageCollection();
    const GpuCommandArenaStatistics retiredStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(retiredStatistics.valid());
    EXPECT_LT(retiredStatistics.pendingCommandBufferCount, pendingStatistics.pendingCommandBufferCount);
    EXPECT_GE(
        retiredStatistics.reusableCommandBufferCount,
        pendingStatistics.reusableCommandBufferCount + 2u
    );
    const GpuCommandArenaWorkerStatistics firstRetiredWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_FirstTelemetryWorkerIndex)
    ;
    const GpuCommandArenaWorkerStatistics secondRetiredWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_SecondTelemetryWorkerIndex)
    ;
    ASSERT_TRUE(firstRetiredWorkerStatistics.valid());
    ASSERT_TRUE(secondRetiredWorkerStatistics.valid());
    EXPECT_EQ(firstRetiredWorkerStatistics.pendingCommandBufferCount, 0u);
    EXPECT_EQ(secondRetiredWorkerStatistics.pendingCommandBufferCount, 0u);
    EXPECT_EQ(firstRetiredWorkerStatistics.pendingCommandPoolEpochCount, 0u);
    EXPECT_EQ(secondRetiredWorkerStatistics.pendingCommandPoolEpochCount, 0u);
    EXPECT_EQ(firstRetiredWorkerStatistics.reusableCommandBufferCount, 1u);
    EXPECT_EQ(secondRetiredWorkerStatistics.reusableCommandBufferCount, 1u);

    Latch reusedRecordingStarted(2);
    firstRecorded = false;
    secondRecorded = false;
    const Graphics::TaskHandle reusedFirstJob = graphics.scheduleGraphicsTask([&](){
        reusedRecordingStarted.count_down();
        reusedRecordingStarted.wait();
        firstCommandList->open();
        firstCommandList->setBufferState(firstBuffer.get(), ResourceStates::CopyDest);
        firstCommandList->close();
        firstRecorded = true;
    });
    const Graphics::TaskHandle reusedSecondJob = graphics.scheduleGraphicsTask([&](){
        reusedRecordingStarted.count_down();
        reusedRecordingStarted.wait();
        secondCommandList->open();
        secondCommandList->setBufferState(secondBuffer.get(), ResourceStates::CopyDest);
        secondCommandList->close();
        secondRecorded = true;
    });
    ASSERT_TRUE(reusedFirstJob.valid());
    ASSERT_TRUE(reusedSecondJob.valid());

    graphics.waitTask(reusedFirstJob);
    graphics.waitTask(reusedSecondJob);
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);
    const GpuCommandArenaStatistics reusedStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(reusedStatistics.valid());
    EXPECT_GE(reusedStatistics.resetEventCount, retiredStatistics.resetEventCount + 2u);
    EXPECT_GE(reusedStatistics.leasedCommandBufferCount, retiredStatistics.leasedCommandBufferCount + 2u);
    EXPECT_GE(
        retiredStatistics.reusableCommandBufferCount,
        reusedStatistics.reusableCommandBufferCount + 2u
    );
    const GpuCommandArenaWorkerStatistics firstReusedWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_FirstTelemetryWorkerIndex)
    ;
    const GpuCommandArenaWorkerStatistics secondReusedWorkerStatistics =
        device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, s_SecondTelemetryWorkerIndex)
    ;
    ASSERT_TRUE(firstReusedWorkerStatistics.valid());
    ASSERT_TRUE(secondReusedWorkerStatistics.valid());
    EXPECT_EQ(firstReusedWorkerStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(secondReusedWorkerStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(firstReusedWorkerStatistics.reusableCommandBufferCount, 0u);
    EXPECT_EQ(secondReusedWorkerStatistics.reusableCommandBufferCount, 0u);
    EXPECT_GE(firstReusedWorkerStatistics.resetEventCount, firstRetiredWorkerStatistics.resetEventCount + 1u);
    EXPECT_GE(secondReusedWorkerStatistics.resetEventCount, secondRetiredWorkerStatistics.resetEventCount + 1u);

    CommandList* reusedCommandLists[] = { firstCommandList.get(), secondCommandList.get() };
    bool reusedSubmitted = false;
    EXPECT_GT(device.executeCommandLists(reusedCommandLists, 2u, graphicsQueue, &reusedSubmitted), 0u);
    EXPECT_TRUE(reusedSubmitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Raw aggregate field writes can bypass CommandListParameters setters. The Vulkan boundary must still canonicalize
// direct recording to {0,0}, so the retained description names the direct telemetry shard that owns the lease.
TEST_F(DescriptorBufferRoundTripTest, CanonicalizesRawDirectWorkerIdentityAndKeepsDirectTelemetryQueryable){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(device.waitForIdle());
    device.runGarbageCollection();

    constexpr u64 s_NoncanonicalDirectDomain = 17u;
    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    parameters.recordingWorkerDomain = s_NoncanonicalDirectDomain;
    parameters.recordingWorkerIndex = 0u;

    CommandListHandle commandList = device.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    const CommandListParameters& description = commandList->getDescription();
    EXPECT_EQ(description.recordingWorkerDomain, 0u);
    EXPECT_EQ(description.recordingWorkerIndex, 0u);

    commandList->open();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    commandList->close();

    const GpuCommandArenaWorkerStatistics directStatistics = device.getCommandArenaWorkerStatistics(
        graphicsQueue,
        description.recordingWorkerDomain,
        description.recordingWorkerIndex
    );
    ASSERT_TRUE(directStatistics.valid());
    EXPECT_EQ(directStatistics.queue, graphicsQueue);
    EXPECT_EQ(directStatistics.recordingWorkerDomain, 0u);
    EXPECT_EQ(directStatistics.recordingWorkerIndex, 0u);
    EXPECT_GE(directStatistics.currentCommandBufferCount, 1u);
    EXPECT_GE(directStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(directStatistics.commandPoolEpochCount, directStatistics.currentCommandBufferCount);
    EXPECT_EQ(directStatistics.pendingCommandPoolEpochCount, directStatistics.pendingCommandBufferCount);
    EXPECT_EQ(
        directStatistics.currentCommandBufferCount,
        directStatistics.reusableCommandBufferCount
            + directStatistics.leasedCommandBufferCount
            + directStatistics.pendingCommandBufferCount
    );
    EXPECT_FALSE(device.getCommandArenaWorkerStatistics(graphicsQueue, s_NoncanonicalDirectDomain, 0u).valid());
}


// CpuTaskScheduler worker indices are local to each pool. Distinct pool domains must therefore produce distinct native
// command arenas even when their local indices collide. A second phase deliberately shares one arena so concurrent
// destruction also exercises externally synchronized vkFreeCommandBuffers on abandoned, never-submitted buffers.
TEST_F(DescriptorBufferRoundTripTest, CpuTaskSchedulerDomainsIsolateCollidingWorkersAndSerializeAbandonedBufferFree){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(device.waitForIdle());
    device.runGarbageCollection();
    const GpuCommandArenaStatistics beforeStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(beforeStatistics.valid());
    const GpuCommandArenaWorkerStatistics directStatistics = device.getCommandArenaWorkerStatistics(graphicsQueue, 0u, 0u);
    ASSERT_TRUE(directStatistics.valid());
    EXPECT_EQ(directStatistics.queue, graphicsQueue);
    EXPECT_EQ(directStatistics.recordingWorkerDomain, 0u);
    EXPECT_EQ(directStatistics.recordingWorkerIndex, 0u);
    EXPECT_EQ(directStatistics.commandPoolEpochCount, directStatistics.currentCommandBufferCount);
    EXPECT_EQ(directStatistics.pendingCommandPoolEpochCount, directStatistics.pendingCommandBufferCount);
    EXPECT_EQ(
        directStatistics.currentCommandBufferCount,
        directStatistics.reusableCommandBufferCount
            + directStatistics.leasedCommandBufferCount
            + directStatistics.pendingCommandBufferCount
    );

    auto firstBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto secondBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(firstBuffer.get(), nullptr);
    ASSERT_NE(secondBuffer.get(), nullptr);

    CpuTaskScheduler firstWorkers(1u);
    CpuTaskScheduler secondWorkers(1u);
    CpuTaskScope firstTasks(firstWorkers);
    CpuTaskScope secondTasks(secondWorkers);
    ASSERT_NE(firstWorkers.domainIdentity(), 0u);
    ASSERT_NE(secondWorkers.domainIdentity(), 0u);
    ASSERT_NE(firstWorkers.domainIdentity(), secondWorkers.domainIdentity());
    constexpr u32 s_CollidingWorkerIndex = 2u;

    CommandListParameters legacyParameters;
    legacyParameters
        .setRecordingWorker(firstWorkers.domainIdentity(), s_CollidingWorkerIndex)
        .setRecordingWorkerIndex(s_CollidingWorkerIndex)
    ;
    EXPECT_EQ(legacyParameters.recordingWorkerDomain, 0u);

    Latch distinctCommandListsReady(2);
    Latch releaseDistinctCommandLists(1);
    bool firstRecorded = false;
    bool secondRecorded = false;
    u64 firstObservedDomain = 0u;
    u64 secondObservedDomain = 0u;
    u32 firstObservedWorkerIndex = 0u;
    u32 secondObservedWorkerIndex = 0u;
    const auto firstRecordingTask = firstTasks.submit([&](){
        const u32 workerIndex = static_cast<u32>(firstWorkers.currentWorkerIndex() + 1u);
        CommandListParameters parameters;
        parameters.setPhysicalQueue(graphicsQueue).setRecordingWorker(firstWorkers.domainIdentity(), workerIndex);
        CommandListHandle commandList = device.createCommandList(parameters);
        if(commandList){
            firstObservedDomain = commandList->getDescription().recordingWorkerDomain;
            firstObservedWorkerIndex = commandList->getDescription().recordingWorkerIndex;
            commandList->open();
            commandList->setBufferState(firstBuffer.get(), ResourceStates::CopyDest);
            commandList->close();
            firstRecorded = true;
        }
        distinctCommandListsReady.count_down();
        releaseDistinctCommandLists.wait();
    });
    const auto secondRecordingTask = secondTasks.submit([&](){
        const u32 workerIndex = static_cast<u32>(secondWorkers.currentWorkerIndex() + 1u);
        CommandListParameters parameters;
        parameters.setPhysicalQueue(graphicsQueue).setRecordingWorker(secondWorkers.domainIdentity(), workerIndex);
        CommandListHandle commandList = device.createCommandList(parameters);
        if(commandList){
            secondObservedDomain = commandList->getDescription().recordingWorkerDomain;
            secondObservedWorkerIndex = commandList->getDescription().recordingWorkerIndex;
            commandList->open();
            commandList->setBufferState(secondBuffer.get(), ResourceStates::CopyDest);
            commandList->close();
            secondRecorded = true;
        }
        distinctCommandListsReady.count_down();
        releaseDistinctCommandLists.wait();
    });

    EXPECT_TRUE(firstRecordingTask.valid());
    EXPECT_TRUE(secondRecordingTask.valid());
    distinctCommandListsReady.wait();
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);
    EXPECT_EQ(firstObservedDomain, firstWorkers.domainIdentity());
    EXPECT_EQ(secondObservedDomain, secondWorkers.domainIdentity());
    EXPECT_NE(firstObservedDomain, secondObservedDomain);
    EXPECT_EQ(firstObservedWorkerIndex, s_CollidingWorkerIndex);
    EXPECT_EQ(secondObservedWorkerIndex, s_CollidingWorkerIndex);
    const GpuCommandArenaStatistics distinctStatistics = device.getCommandArenaStatistics(graphicsQueue);
    EXPECT_TRUE(distinctStatistics.valid());
    EXPECT_EQ(distinctStatistics.workerArenaCount, beforeStatistics.workerArenaCount + 2u);
    EXPECT_EQ(distinctStatistics.commandPoolEpochCount, beforeStatistics.commandPoolEpochCount + 2u);
    EXPECT_EQ(distinctStatistics.growthEventCount, beforeStatistics.growthEventCount + 2u);
    EXPECT_EQ(distinctStatistics.currentCommandBufferCount, beforeStatistics.currentCommandBufferCount + 2u);
    EXPECT_EQ(distinctStatistics.leasedCommandBufferCount, beforeStatistics.leasedCommandBufferCount + 2u);
    EXPECT_EQ(distinctStatistics.reusableCommandBufferCount, beforeStatistics.reusableCommandBufferCount);
    EXPECT_EQ(distinctStatistics.pendingCommandBufferCount, beforeStatistics.pendingCommandBufferCount);
    EXPECT_GE(distinctStatistics.highWaterCommandBufferCount, distinctStatistics.currentCommandBufferCount);
    EXPECT_GT(
        distinctStatistics.nativeHandleStorageLowerBoundBytes,
        beforeStatistics.nativeHandleStorageLowerBoundBytes
    );
    const GpuCommandArenaWorkerStatistics firstDistinctWorkerStatistics = device.getCommandArenaWorkerStatistics(
        graphicsQueue,
        firstWorkers.domainIdentity(),
        s_CollidingWorkerIndex
    );
    const GpuCommandArenaWorkerStatistics secondDistinctWorkerStatistics = device.getCommandArenaWorkerStatistics(
        graphicsQueue,
        secondWorkers.domainIdentity(),
        s_CollidingWorkerIndex
    );
    EXPECT_TRUE(firstDistinctWorkerStatistics.valid());
    EXPECT_TRUE(secondDistinctWorkerStatistics.valid());
    EXPECT_EQ(firstDistinctWorkerStatistics.recordingWorkerDomain, firstWorkers.domainIdentity());
    EXPECT_EQ(secondDistinctWorkerStatistics.recordingWorkerDomain, secondWorkers.domainIdentity());
    EXPECT_EQ(firstDistinctWorkerStatistics.recordingWorkerIndex, s_CollidingWorkerIndex);
    EXPECT_EQ(secondDistinctWorkerStatistics.recordingWorkerIndex, s_CollidingWorkerIndex);
    EXPECT_EQ(firstDistinctWorkerStatistics.commandPoolEpochCount, 1u);
    EXPECT_EQ(secondDistinctWorkerStatistics.commandPoolEpochCount, 1u);
    EXPECT_EQ(firstDistinctWorkerStatistics.currentCommandBufferCount, 1u);
    EXPECT_EQ(secondDistinctWorkerStatistics.currentCommandBufferCount, 1u);
    EXPECT_EQ(firstDistinctWorkerStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(secondDistinctWorkerStatistics.leasedCommandBufferCount, 1u);
    EXPECT_EQ(firstDistinctWorkerStatistics.growthEventCount, 1u);
    EXPECT_EQ(secondDistinctWorkerStatistics.growthEventCount, 1u);
    EXPECT_GE(firstDistinctWorkerStatistics.highWaterCommandBufferCount, 1u);
    EXPECT_GE(secondDistinctWorkerStatistics.highWaterCommandBufferCount, 1u);
    EXPECT_GT(firstDistinctWorkerStatistics.nativeHandleStorageLowerBoundBytes, 0u);
    EXPECT_GT(secondDistinctWorkerStatistics.nativeHandleStorageLowerBoundBytes, 0u);

    releaseDistinctCommandLists.count_down();
    firstTasks.wait();
    secondTasks.wait();
    const GpuCommandArenaStatistics afterDistinctStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(afterDistinctStatistics.valid());
    EXPECT_EQ(afterDistinctStatistics.currentCommandBufferCount, beforeStatistics.currentCommandBufferCount);
    EXPECT_EQ(afterDistinctStatistics.leasedCommandBufferCount, beforeStatistics.leasedCommandBufferCount);
    EXPECT_EQ(afterDistinctStatistics.reusableCommandBufferCount, beforeStatistics.reusableCommandBufferCount);
    EXPECT_EQ(afterDistinctStatistics.pendingCommandBufferCount, beforeStatistics.pendingCommandBufferCount);
    const GpuCommandArenaWorkerStatistics firstAbandonedWorkerStatistics = device.getCommandArenaWorkerStatistics(
        graphicsQueue,
        firstWorkers.domainIdentity(),
        s_CollidingWorkerIndex
    );
    const GpuCommandArenaWorkerStatistics secondAbandonedWorkerStatistics = device.getCommandArenaWorkerStatistics(
        graphicsQueue,
        secondWorkers.domainIdentity(),
        s_CollidingWorkerIndex
    );
    ASSERT_TRUE(firstAbandonedWorkerStatistics.valid());
    ASSERT_TRUE(secondAbandonedWorkerStatistics.valid());
    EXPECT_EQ(firstAbandonedWorkerStatistics.currentCommandBufferCount, 0u);
    EXPECT_EQ(secondAbandonedWorkerStatistics.currentCommandBufferCount, 0u);
    EXPECT_EQ(firstAbandonedWorkerStatistics.leasedCommandBufferCount, 0u);
    EXPECT_EQ(secondAbandonedWorkerStatistics.leasedCommandBufferCount, 0u);
    EXPECT_EQ(firstAbandonedWorkerStatistics.growthEventCount, 1u);
    EXPECT_EQ(secondAbandonedWorkerStatistics.growthEventCount, 1u);

    Latch firstSharedCommandListRecorded(1);
    Latch sharedCommandListsReady(2);
    Latch releaseSharedCommandLists(1);
    bool firstSharedRecorded = false;
    bool secondSharedRecorded = false;
    const auto firstSharedRecordingTask = firstTasks.submit([&](){
        CommandListParameters parameters;
        parameters
            .setPhysicalQueue(graphicsQueue)
            .setRecordingWorker(firstWorkers.domainIdentity(), s_CollidingWorkerIndex)
        ;
        CommandListHandle commandList = device.createCommandList(parameters);
        if(commandList){
            commandList->open();
            commandList->setBufferState(firstBuffer.get(), ResourceStates::CopyDest);
            commandList->close();
            firstSharedRecorded = true;
        }
        firstSharedCommandListRecorded.count_down();
        sharedCommandListsReady.count_down();
        releaseSharedCommandLists.wait();
    });
    const auto secondSharedRecordingTask = secondTasks.submit([&](){
        firstSharedCommandListRecorded.wait();

        CommandListParameters parameters;
        parameters
            .setPhysicalQueue(graphicsQueue)
            .setRecordingWorker(firstWorkers.domainIdentity(), s_CollidingWorkerIndex)
        ;
        CommandListHandle commandList = device.createCommandList(parameters);
        if(commandList){
            commandList->open();
            commandList->setBufferState(secondBuffer.get(), ResourceStates::CopyDest);
            commandList->close();
            secondSharedRecorded = true;
        }
        sharedCommandListsReady.count_down();
        releaseSharedCommandLists.wait();
    });

    EXPECT_TRUE(firstSharedRecordingTask.valid());
    EXPECT_TRUE(secondSharedRecordingTask.valid());
    sharedCommandListsReady.wait();
    EXPECT_TRUE(firstSharedRecorded);
    EXPECT_TRUE(secondSharedRecorded);
    const GpuCommandArenaStatistics sharedStatistics = device.getCommandArenaStatistics(graphicsQueue);
    EXPECT_TRUE(sharedStatistics.valid());
    EXPECT_EQ(sharedStatistics.workerArenaCount, distinctStatistics.workerArenaCount);
    EXPECT_EQ(sharedStatistics.commandPoolEpochCount, distinctStatistics.commandPoolEpochCount);
    EXPECT_EQ(sharedStatistics.growthEventCount, distinctStatistics.growthEventCount + 2u);
    EXPECT_EQ(sharedStatistics.currentCommandBufferCount, beforeStatistics.currentCommandBufferCount + 2u);
    EXPECT_EQ(sharedStatistics.leasedCommandBufferCount, beforeStatistics.leasedCommandBufferCount + 2u);
    EXPECT_GE(sharedStatistics.highWaterCommandBufferCount, distinctStatistics.highWaterCommandBufferCount);
    const GpuCommandArenaWorkerStatistics sharedWorkerStatistics = device.getCommandArenaWorkerStatistics(
        graphicsQueue,
        firstWorkers.domainIdentity(),
        s_CollidingWorkerIndex
    );
    EXPECT_TRUE(sharedWorkerStatistics.valid());
    EXPECT_EQ(sharedWorkerStatistics.currentCommandBufferCount, 2u);
    EXPECT_EQ(sharedWorkerStatistics.leasedCommandBufferCount, 2u);
    EXPECT_EQ(sharedWorkerStatistics.growthEventCount, 3u);
    EXPECT_GE(sharedWorkerStatistics.highWaterCommandBufferCount, 2u);

    releaseSharedCommandLists.count_down();
    firstTasks.wait();
    secondTasks.wait();
    const GpuCommandArenaStatistics afterSharedStatistics = device.getCommandArenaStatistics(graphicsQueue);
    ASSERT_TRUE(afterSharedStatistics.valid());
    EXPECT_EQ(afterSharedStatistics.currentCommandBufferCount, beforeStatistics.currentCommandBufferCount);
    EXPECT_EQ(afterSharedStatistics.leasedCommandBufferCount, beforeStatistics.leasedCommandBufferCount);
    EXPECT_EQ(afterSharedStatistics.reusableCommandBufferCount, beforeStatistics.reusableCommandBufferCount);
    EXPECT_EQ(afterSharedStatistics.pendingCommandBufferCount, beforeStatistics.pendingCommandBufferCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

