// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Accepted graph state must retain only live typed resources while preserving a later packet's final state across
// generations. This is the shared cache used by runtime skinning instead of renderer-owned raw handoff fan-in.
TEST_F(DescriptorBufferRoundTripTest, PersistentGraphStateCacheFiltersAndMergesAcceptedBufferStates){
    auto& device = DescriptorBufferRoundTripTest::device();
    Alloc::GlobalArena persistentStateArena(Name("tests/descriptor_buffer/persistent_state_cache"));
    Alloc::GlobalArena foreignStateArena(Name("tests/descriptor_buffer/persistent_state_cache_foreign"));
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/persistent_state_fan_in"));
    auto liveBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    auto retiredBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(liveBuffer.get(), nullptr);
    ASSERT_NE(retiredBuffer.get(), nullptr);

    CommandListResourceStateHandoff initialStates(persistentStateArena);
    CommandListResourceStateHandoff finalStates(persistentStateArena);
    auto initialProducer = device.createCommandList();
    auto finalProducer = device.createCommandList();
    auto preAcceptanceConsumer = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(initialProducer.get(), nullptr);
    ASSERT_NE(finalProducer.get(), nullptr);
    ASSERT_NE(preAcceptanceConsumer.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);

    initialProducer->open();
    initialProducer->setBufferState(liveBuffer.get(), ResourceStates::UnorderedAccess);
    initialProducer->setBufferState(retiredBuffer.get(), ResourceStates::UnorderedAccess);
    initialProducer->close(&initialStates);
    ASSERT_TRUE(initialStates.valid());

    CommandListResourceStateHandoff foreignArenaStates(foreignStateArena);
    ASSERT_TRUE(foreignArenaStates.buildResourceSubset(initialStates, nullptr, 0u, nullptr, 0u));
    ASSERT_TRUE(foreignArenaStates.valid());
    ASSERT_TRUE(foreignArenaStates.empty());
    EXPECT_FALSE(initialStates.exchangeSnapshot(foreignArenaStates));
    EXPECT_FALSE(initialStates.empty());
    EXPECT_TRUE(foreignArenaStates.empty());

    GpuPersistentResourceStateCache acceptedState(persistentStateArena);
    {
        const BufferHandle initialLiveBuffers[] = { liveBuffer, retiredBuffer };
        ASSERT_TRUE(acceptedState.replaceBufferSubset(initialStates, initialLiveBuffers, LengthOf(initialLiveBuffers)));
    }
    EXPECT_EQ(acceptedState.retainedBufferCount(), 2u);
    GpuPersistentResourceStateCache::Candidate recordingSource(acceptedState);
    {
        const BufferHandle recordingLiveBuffers[] = { liveBuffer, retiredBuffer };
        ASSERT_TRUE(acceptedState.buildFilteredBufferSubset(
            recordingSource,
            *acceptedState.source(),
            recordingLiveBuffers,
            LengthOf(recordingLiveBuffers)
        ));
    }
    ASSERT_NE(recordingSource.source(), nullptr);
    // Drop the caller's retired handle before opening the next command list from the raw handoff. The cache keeps a
    // retirement-safe typed reference until the current-live filter drops that generation.
    retiredBuffer.reset();

    finalProducer->open(recordingSource.source());
    finalProducer->setBufferState(liveBuffer.get(), ResourceStates::ShaderResource);
    finalProducer->close(&finalStates);
    ASSERT_TRUE(finalStates.valid());

    const BufferHandle currentLiveBuffers[] = { liveBuffer };
    GpuPersistentResourceStateCache::Candidate acceptedCandidate(acceptedState);
    ASSERT_TRUE(acceptedState.buildMergedBufferSubset(
        acceptedCandidate,
        finalStates,
        currentLiveBuffers,
        LengthOf(currentLiveBuffers),
        fanInScratchArena
    ));
    ASSERT_TRUE(acceptedCandidate.valid());
    ASSERT_FALSE(acceptedCandidate.empty());
    GpuPersistentResourceStateCache foreignState(persistentStateArena);
    EXPECT_FALSE(foreignState.commit(acceptedCandidate));
    EXPECT_FALSE(foreignState.valid());
    EXPECT_TRUE(acceptedCandidate.valid());
    // Candidate construction alone must not publish a state from a packet that could still be rejected.
    preAcceptanceConsumer->open(acceptedState.source());
    EXPECT_EQ(preAcceptanceConsumer->getBufferState(liveBuffer.get()), ResourceStates::UnorderedAccess);
    preAcceptanceConsumer->close();
    const ArenaMemoryStats memoryBeforeCommit = persistentStateArena.memoryStats();
    const bool stateCommitted = acceptedState.commit(acceptedCandidate);
    const ArenaMemoryStats memoryAfterCommit = persistentStateArena.memoryStats();
    ASSERT_TRUE(stateCommitted);
    EXPECT_EQ(memoryAfterCommit.allocationCount, memoryBeforeCommit.allocationCount);
    EXPECT_EQ(memoryAfterCommit.reallocationCount, memoryBeforeCommit.reallocationCount);
    EXPECT_EQ(memoryAfterCommit.deallocationCount, memoryBeforeCommit.deallocationCount);
    EXPECT_EQ(memoryAfterCommit.usedBytes, memoryBeforeCommit.usedBytes);
    EXPECT_FALSE(acceptedCandidate.valid());
    EXPECT_TRUE(acceptedCandidate.empty());
    EXPECT_EQ(acceptedCandidate.source(), nullptr);
    ASSERT_NE(acceptedState.source(), nullptr);
    EXPECT_EQ(acceptedState.retainedBufferCount(), 1u);

    consumer->open(acceptedState.source());
    EXPECT_EQ(consumer->getBufferState(liveBuffer.get()), ResourceStates::ShaderResource);
    consumer->close();

    u64 displacedStorageDeallocationCount = 0u;
    u64 displacedStorageUsedBytes = 0u;
    {
        GpuPersistentResourceStateCache::Candidate emptyCandidate(acceptedState);
        ASSERT_TRUE(acceptedState.buildMergedBufferSubset(
            emptyCandidate,
            finalStates,
            nullptr,
            0u,
            fanInScratchArena
        ));
        ASSERT_TRUE(emptyCandidate.valid());
        ASSERT_TRUE(emptyCandidate.empty());
        const ArenaMemoryStats memoryBeforeEmptyCommit = persistentStateArena.memoryStats();
        const bool emptyStateCommitted = acceptedState.commit(emptyCandidate);
        const ArenaMemoryStats memoryAfterEmptyCommit = persistentStateArena.memoryStats();
        ASSERT_TRUE(emptyStateCommitted);
        EXPECT_EQ(memoryAfterEmptyCommit.allocationCount, memoryBeforeEmptyCommit.allocationCount);
        EXPECT_EQ(memoryAfterEmptyCommit.reallocationCount, memoryBeforeEmptyCommit.reallocationCount);
        EXPECT_EQ(memoryAfterEmptyCommit.deallocationCount, memoryBeforeEmptyCommit.deallocationCount);
        EXPECT_EQ(memoryAfterEmptyCommit.usedBytes, memoryBeforeEmptyCommit.usedBytes);
        EXPECT_TRUE(acceptedState.valid());
        EXPECT_TRUE(acceptedState.empty());
        EXPECT_EQ(acceptedState.retainedBufferCount(), 0u);
        EXPECT_FALSE(emptyCandidate.valid());
        EXPECT_TRUE(emptyCandidate.empty());
        EXPECT_EQ(emptyCandidate.source(), nullptr);

        const ArenaMemoryStats memoryBeforeRejectedRecommit = persistentStateArena.memoryStats();
        EXPECT_FALSE(acceptedState.commit(emptyCandidate));
        const ArenaMemoryStats memoryAfterRejectedRecommit = persistentStateArena.memoryStats();
        EXPECT_EQ(memoryAfterRejectedRecommit.allocationCount, memoryBeforeRejectedRecommit.allocationCount);
        EXPECT_EQ(memoryAfterRejectedRecommit.reallocationCount, memoryBeforeRejectedRecommit.reallocationCount);
        EXPECT_EQ(memoryAfterRejectedRecommit.deallocationCount, memoryBeforeRejectedRecommit.deallocationCount);
        EXPECT_EQ(memoryAfterRejectedRecommit.usedBytes, memoryBeforeRejectedRecommit.usedBytes);
        EXPECT_TRUE(acceptedState.valid());
        EXPECT_TRUE(acceptedState.empty());
        displacedStorageDeallocationCount = memoryAfterRejectedRecommit.deallocationCount;
        displacedStorageUsedBytes = memoryAfterRejectedRecommit.usedBytes;
    }
    const ArenaMemoryStats memoryAfterDisplacedStorageRetirement = persistentStateArena.memoryStats();
    EXPECT_GT(memoryAfterDisplacedStorageRetirement.deallocationCount, displacedStorageDeallocationCount);
    EXPECT_LT(memoryAfterDisplacedStorageRetirement.usedBytes, displacedStorageUsedBytes);

    CommandList* commandLists[] = { initialProducer.get(), finalProducer.get(), consumer.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Compute scratch can include descriptor-visible images as well as buffers. The accepted cache must hold both typed
// handles after the caller drops its own references, so the next packet can safely import their native states.
TEST_F(DescriptorBufferRoundTripTest, PersistentGraphStateCacheRetainsAcceptedTextureAndBufferStates){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);
    ASSERT_NE(buffer.get(), nullptr);

    CommandListResourceStateHandoff acceptedStates(DescriptorBufferRoundTripTest::arena());
    auto producer = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);

    producer->open();
    producer->setTextureState(texture.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    producer->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    producer->close(&acceptedStates);
    ASSERT_TRUE(acceptedStates.valid());

    GpuPersistentResourceStateCache cache(DescriptorBufferRoundTripTest::arena());
    {
        const TextureHandle textures[] = { texture };
        const BufferHandle buffers[] = { buffer };
        ASSERT_TRUE(cache.replaceResourceSubset(
            acceptedStates,
            textures,
            LengthOf(textures),
            buffers,
            LengthOf(buffers)
        ));
    }
    EXPECT_EQ(cache.retainedTextureCount(), 1u);
    EXPECT_EQ(cache.retainedBufferCount(), 1u);
    Texture* const retainedTexture = texture.get();
    Buffer* const retainedBuffer = buffer.get();
    texture.reset();
    buffer.reset();

    consumer->open(cache.source());
    EXPECT_EQ(consumer->getTextureSubresourceState(retainedTexture, 0u, 0u), ResourceStates::UnorderedAccess);
    EXPECT_EQ(consumer->getBufferState(retainedBuffer), ResourceStates::UnorderedAccess);
    consumer->close();

    CommandList* commandLists[] = { producer.get(), consumer.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Cross-frame Compute scratch must not carry the preceding frame's state for resources that the current Graphics
// prefix has already prepared. Select the private scratch state before fan-in so the current prefix remains authoritative
// for shared inputs while the Compute-only resource retains its prior layout.
TEST_F(DescriptorBufferRoundTripTest, CommandListStateHandoffSeparatesCurrentInputsFromPersistentScratch){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto sharedInput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    auto computeScratch = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(sharedInput.get(), nullptr);
    ASSERT_NE(computeScratch.get(), nullptr);

    CommandListResourceStateHandoff previousComputeState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff currentPrefixState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff persistentScratchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff nextComputeState(DescriptorBufferRoundTripTest::arena());
    auto previousCompute = device.createCommandList();
    auto currentPrefix = device.createCommandList();
    auto nextCompute = device.createCommandList();
    ASSERT_NE(previousCompute.get(), nullptr);
    ASSERT_NE(currentPrefix.get(), nullptr);
    ASSERT_NE(nextCompute.get(), nullptr);

    previousCompute->open();
    previousCompute->setBufferState(sharedInput.get(), ResourceStates::UnorderedAccess);
    previousCompute->setBufferState(computeScratch.get(), ResourceStates::UnorderedAccess);
    previousCompute->close(&previousComputeState);
    ASSERT_TRUE(previousComputeState.valid());

    currentPrefix->open();
    currentPrefix->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    currentPrefix->close(&currentPrefixState);
    ASSERT_TRUE(currentPrefixState.valid());

    Core::Buffer* const scratchBuffers[] = { computeScratch.get() };
    ASSERT_TRUE(persistentScratchState.buildResourceSubset(
        previousComputeState,
        nullptr,
        0u,
        scratchBuffers,
        1u
    ));

    const CommandListResourceStateHandoff* branches[] = { &persistentScratchState };
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/current_input_fan_in"));
    ASSERT_TRUE(nextComputeState.buildFanIn(currentPrefixState, branches, 1u, fanInScratchArena));

    nextCompute->open(&nextComputeState);
    EXPECT_EQ(nextCompute->getBufferState(sharedInput.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(nextCompute->getBufferState(computeScratch.get()), ResourceStates::UnorderedAccess);
    nextCompute->close();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

