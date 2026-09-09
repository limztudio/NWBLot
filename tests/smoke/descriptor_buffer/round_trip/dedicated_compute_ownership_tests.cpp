// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A dedicated compute family is optional in CI, but when one exists this is the phase-zero ownership proof:
// exclusive storage moves Compute -> Graphics -> Compute with paired release/acquire barriers and submission-local
// timeline tokens. No rendering job has moved yet; this specifically validates the resource-lifecycle round trip
// that a reused shadow-visibility frame slot will need.
TEST_F(DescriptorBufferRoundTripTest, DedicatedComputeQueueTransfersExclusiveBufferOwnershipRoundTrip){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";

    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto sharedInput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(sharedInput.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);

    CommandListResourceStateHandoff computeToGraphics(asyncScope.arena());
    auto computeProducer = device.createCommandList(computeParams);
    ASSERT_NE(computeProducer.get(), nullptr);
    computeProducer->open();
    computeProducer->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    computeProducer->setBufferState(sharedInput.get(), ResourceStates::UnorderedAccess);
    computeProducer->releaseBufferOwnership(buffer.get(), CommandQueue::Graphics);
    computeProducer->close(&computeToGraphics);
    ASSERT_TRUE(computeToGraphics.valid());

    CommandList* computeProducerLists[] = { computeProducer.get() };
    const QueueSubmissionToken computeToken = device.executeCommandLists(
        computeProducerLists,
        1u,
        CommandQueue::Compute,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(computeToken.valid());
    ASSERT_EQ(computeToken.queue, CommandQueue::Compute);

    CommandListResourceStateHandoff graphicsToCompute(asyncScope.arena());
    auto graphicsConsumer = device.createCommandList();
    ASSERT_NE(graphicsConsumer.get(), nullptr);
    graphicsConsumer->open(&computeToGraphics);
    EXPECT_EQ(graphicsConsumer->getBufferState(buffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(graphicsConsumer->getBufferState(sharedInput.get()), ResourceStates::UnorderedAccess);
    graphicsConsumer->setBufferState(buffer.get(), ResourceStates::ShaderResource);
    graphicsConsumer->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    graphicsConsumer->releaseBufferOwnership(buffer.get(), CommandQueue::Compute);
    graphicsConsumer->close(&graphicsToCompute);
    ASSERT_TRUE(graphicsToCompute.valid());

    // Passing both producer tokens from one physical queue is legal API use. The submission folds them into the
    // single greatest timeline wait Vulkan permits for that semaphore.
    const QueueSubmissionToken computeWaits[] = { computeToken, computeToken };
    const QueueSubmissionDesc graphicsSubmissionDesc = QueueSubmissionDesc().setWaitTokens(computeWaits, 2u);
    CommandList* graphicsConsumerLists[] = { graphicsConsumer.get() };
    const QueueSubmissionToken graphicsToken = device.executeCommandLists(
        graphicsConsumerLists,
        1u,
        CommandQueue::Graphics,
        graphicsSubmissionDesc
    );
    ASSERT_TRUE(graphicsToken.valid());
    ASSERT_EQ(graphicsToken.queue, CommandQueue::Graphics);

    auto computeReuse = device.createCommandList(computeParams);
    ASSERT_NE(computeReuse.get(), nullptr);
    computeReuse->open(&graphicsToCompute);
    EXPECT_EQ(computeReuse->getBufferState(buffer.get()), ResourceStates::ShaderResource);
    computeReuse->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    computeReuse->close();

    const QueueSubmissionToken graphicsWaits[] = { graphicsToken };
    const QueueSubmissionDesc computeSubmissionDesc = QueueSubmissionDesc().setWaitTokens(graphicsWaits, 1u);
    CommandList* computeReuseLists[] = { computeReuse.get() };
    const QueueSubmissionToken reuseToken = device.executeCommandLists(
        computeReuseLists,
        1u,
        CommandQueue::Compute,
        computeSubmissionDesc
    );
    EXPECT_TRUE(reuseToken.valid());
    EXPECT_EQ(reuseToken.queue, CommandQueue::Compute);
    EXPECT_TRUE(device.waitForIdle());
}


// AVBOIT alternates raster and compute phases. Its shared work resources use concurrent queue sharing, while timeline
// waits order the exact Graphics pre -> Compute warp -> Graphics extinction -> Compute integration -> Graphics
// accumulation chain. Exercise the state subsets/fan-ins and every queue crossing on a real dedicated family.
TEST_F(DescriptorBufferRoundTripTest, DedicatedComputeQueueChainsConcurrentAvboitWorkStates){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";

    const auto makeSharedWorkBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
    };
    auto coverage = makeSharedWorkBuffer();
    auto depthWarp = makeSharedWorkBuffer();
    auto control = makeSharedWorkBuffer();
    auto extinction = makeSharedWorkBuffer();
    auto extinctionOverflow = makeSharedWorkBuffer();
    auto transmittance = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDepth(4u)
            .setDimension(TextureDimension::Texture3D)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(coverage.get(), nullptr);
    ASSERT_NE(depthWarp.get(), nullptr);
    ASSERT_NE(control.get(), nullptr);
    ASSERT_NE(extinction.get(), nullptr);
    ASSERT_NE(extinctionOverflow.get(), nullptr);
    ASSERT_NE(transmittance.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);
    auto graphicsPre = device.createCommandList();
    auto computeWarp = device.createCommandList(computeParams);
    auto graphicsExtinction = device.createCommandList();
    auto computeIntegration = device.createCommandList(computeParams);
    auto graphicsAccumulate = device.createCommandList();
    ASSERT_NE(graphicsPre.get(), nullptr);
    ASSERT_NE(computeWarp.get(), nullptr);
    ASSERT_NE(graphicsExtinction.get(), nullptr);
    ASSERT_NE(computeIntegration.get(), nullptr);
    ASSERT_NE(graphicsAccumulate.get(), nullptr);

    CommandListResourceStateHandoff preState(asyncScope.arena());
    CommandListResourceStateHandoff warpInputState(asyncScope.arena());
    CommandListResourceStateHandoff warpState(asyncScope.arena());
    CommandListResourceStateHandoff extinctionInputState(asyncScope.arena());
    CommandListResourceStateHandoff extinctionState(asyncScope.arena());
    CommandListResourceStateHandoff integrationInputState(asyncScope.arena());
    CommandListResourceStateHandoff integrationState(asyncScope.arena());
    CommandListResourceStateHandoff accumulateInputState(asyncScope.arena());
    CommandListResourceStateHandoff finalState(asyncScope.arena());

    graphicsPre->open();
    graphicsPre->setBufferState(coverage.get(), ResourceStates::UnorderedAccess);
    graphicsPre->setBufferState(depthWarp.get(), ResourceStates::CopyDest);
    graphicsPre->setBufferState(control.get(), ResourceStates::CopyDest);
    graphicsPre->setBufferState(extinction.get(), ResourceStates::CopyDest);
    graphicsPre->setBufferState(extinctionOverflow.get(), ResourceStates::CopyDest);
    graphicsPre->setTextureState(transmittance.get(), s_AllSubresources, ResourceStates::CopyDest);
    graphicsPre->close(&preState);
    ASSERT_TRUE(preState.valid());

    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/avboit_chain_fan_in"));
    Core::Buffer* const warpBuffers[] = { coverage.get(), depthWarp.get(), control.get() };
    ASSERT_TRUE(warpInputState.buildResourceSubset(preState, nullptr, 0u, warpBuffers, 3u, fanInScratchArena));
    computeWarp->open(&warpInputState);
    EXPECT_EQ(computeWarp->getBufferState(coverage.get()), ResourceStates::UnorderedAccess);
    computeWarp->setBufferState(coverage.get(), ResourceStates::ShaderResource);
    computeWarp->setBufferState(depthWarp.get(), ResourceStates::UnorderedAccess);
    computeWarp->setBufferState(control.get(), ResourceStates::UnorderedAccess);
    computeWarp->close(&warpState);
    ASSERT_TRUE(warpState.valid());

    const CommandListResourceStateHandoff* const extinctionBranches[] = { &warpState };
    ASSERT_TRUE(extinctionInputState.buildFanIn(preState, extinctionBranches, 1u, fanInScratchArena));
    graphicsExtinction->open(&extinctionInputState);
    EXPECT_EQ(graphicsExtinction->getBufferState(depthWarp.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(graphicsExtinction->getBufferState(control.get()), ResourceStates::UnorderedAccess);
    graphicsExtinction->setBufferState(depthWarp.get(), ResourceStates::ShaderResource);
    graphicsExtinction->setBufferState(control.get(), ResourceStates::ShaderResource);
    graphicsExtinction->setBufferState(extinction.get(), ResourceStates::UnorderedAccess);
    graphicsExtinction->setBufferState(extinctionOverflow.get(), ResourceStates::UnorderedAccess);
    graphicsExtinction->close(&extinctionState);
    ASSERT_TRUE(extinctionState.valid());

    Core::Texture* const integrationTextures[] = { transmittance.get() };
    Core::Buffer* const integrationBuffers[] = { extinction.get(), control.get(), extinctionOverflow.get() };
    ASSERT_TRUE(integrationInputState.buildResourceSubset(
        extinctionState,
        integrationTextures,
        1u,
        integrationBuffers,
        3u,
        fanInScratchArena
    ));
    computeIntegration->open(&integrationInputState);
    EXPECT_EQ(computeIntegration->getBufferState(extinction.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(computeIntegration->getTextureSubresourceState(transmittance.get(), 0u, 0u), ResourceStates::CopyDest);
    computeIntegration->setBufferState(extinction.get(), ResourceStates::ShaderResource);
    computeIntegration->setBufferState(control.get(), ResourceStates::ShaderResource);
    computeIntegration->setBufferState(extinctionOverflow.get(), ResourceStates::ShaderResource);
    computeIntegration->setTextureState(transmittance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeIntegration->close(&integrationState);
    ASSERT_TRUE(integrationState.valid());

    const CommandListResourceStateHandoff* const accumulateBranches[] = { &integrationState };
    ASSERT_TRUE(accumulateInputState.buildFanIn(extinctionState, accumulateBranches, 1u, fanInScratchArena));
    graphicsAccumulate->open(&accumulateInputState);
    EXPECT_EQ(graphicsAccumulate->getBufferState(depthWarp.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(graphicsAccumulate->getTextureSubresourceState(transmittance.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    graphicsAccumulate->setTextureState(transmittance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    graphicsAccumulate->close(&finalState);
    ASSERT_TRUE(finalState.valid());

    CommandList* graphicsPreLists[] = { graphicsPre.get() };
    const QueueSubmissionToken graphicsPreToken = device.executeCommandLists(
        graphicsPreLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsPreToken.valid());

    const QueueSubmissionDesc warpSubmitDesc = QueueSubmissionDesc().setWaitTokens(&graphicsPreToken, 1u);
    CommandList* computeWarpLists[] = { computeWarp.get() };
    const QueueSubmissionToken warpToken = device.executeCommandLists(
        computeWarpLists,
        1u,
        CommandQueue::Compute,
        warpSubmitDesc
    );
    ASSERT_TRUE(warpToken.valid());

    const QueueSubmissionDesc extinctionSubmitDesc = QueueSubmissionDesc().setWaitTokens(&warpToken, 1u);
    CommandList* graphicsExtinctionLists[] = { graphicsExtinction.get() };
    const QueueSubmissionToken extinctionToken = device.executeCommandLists(
        graphicsExtinctionLists,
        1u,
        CommandQueue::Graphics,
        extinctionSubmitDesc
    );
    ASSERT_TRUE(extinctionToken.valid());

    const QueueSubmissionDesc integrationSubmitDesc = QueueSubmissionDesc().setWaitTokens(&extinctionToken, 1u);
    CommandList* computeIntegrationLists[] = { computeIntegration.get() };
    const QueueSubmissionToken integrationToken = device.executeCommandLists(
        computeIntegrationLists,
        1u,
        CommandQueue::Compute,
        integrationSubmitDesc
    );
    ASSERT_TRUE(integrationToken.valid());

    const QueueSubmissionDesc accumulationSubmitDesc = QueueSubmissionDesc().setWaitTokens(&integrationToken, 1u);
    CommandList* graphicsAccumulateLists[] = { graphicsAccumulate.get() };
    const QueueSubmissionToken accumulationToken = device.executeCommandLists(
        graphicsAccumulateLists,
        1u,
        CommandQueue::Graphics,
        accumulationSubmitDesc
    );
    EXPECT_TRUE(accumulationToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Hardware caustics can produce the resolved irradiance on Graphics while deferred lighting and the optional history
// stash run on AsyncCompute. The output is intentionally concurrent, so timeline dependencies are sufficient and no
// exclusive ownership release is required. Exercise both the bootstrap (lighting supplies the stash source) and the
// active lagged path (the current Graphics producer supplies it directly).
TEST_F(DescriptorBufferRoundTripTest, DedicatedComputeQueueLetsGraphicsCausticsFeedLightingAndLaggedStashThroughConcurrentIrradiance){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";

    auto causticIrradiance = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(causticIrradiance.get(), nullptr);
    auto causticHistory = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(causticHistory.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);
    auto graphicsCaustics = device.createCommandList();
    auto asyncLighting = device.createCommandList(computeParams);
    auto bootstrapFinal = device.createCommandList();
    auto bootstrapStash = device.createCommandList(computeParams);
    auto activeGraphicsCaustics = device.createCommandList();
    auto activeStash = device.createCommandList(computeParams);
    ASSERT_NE(graphicsCaustics.get(), nullptr);
    ASSERT_NE(asyncLighting.get(), nullptr);
    ASSERT_NE(bootstrapFinal.get(), nullptr);
    ASSERT_NE(bootstrapStash.get(), nullptr);
    ASSERT_NE(activeGraphicsCaustics.get(), nullptr);
    ASSERT_NE(activeStash.get(), nullptr);

    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/caustic_lagged_subset"));
    CommandListResourceStateHandoff causticsState(asyncScope.arena());
    CommandListResourceStateHandoff lightingState(asyncScope.arena());
    CommandListResourceStateHandoff bootstrapCausticReturnState(asyncScope.arena());
    CommandListResourceStateHandoff bootstrapStashState(asyncScope.arena());
    CommandListResourceStateHandoff activeCausticsState(asyncScope.arena());
    CommandListResourceStateHandoff activeStashState(asyncScope.arena());
    graphicsCaustics->open();
    graphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::UnorderedAccess
    );
    graphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::ShaderResource
    );
    graphicsCaustics->close(&causticsState);
    ASSERT_TRUE(causticsState.valid());

    asyncLighting->open(&causticsState);
    ASSERT_TRUE(asyncLighting->hasCommandBuffer());
    EXPECT_EQ(
        asyncLighting->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    asyncLighting->close(&lightingState);
    ASSERT_TRUE(lightingState.valid());
    ASSERT_TRUE(bootstrapCausticReturnState.buildTextureSubset(
        lightingState,
        causticIrradiance.get(),
        fanInScratchArena
    ));

    // Bootstrap consumes the live image in Async lighting, so the stash imports the post-lighting state.
    bootstrapFinal->open();
    bootstrapFinal->close();
    ASSERT_TRUE(bootstrapFinal->hasCommandBuffer());
    bootstrapStash->open(&bootstrapCausticReturnState);
    EXPECT_EQ(
        bootstrapStash->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    bootstrapStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    bootstrapStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    bootstrapStash->commitBarriers();
    const TextureSlice slice;
    bootstrapStash->copyTexture(*causticHistory, slice, *causticIrradiance, slice);
    bootstrapStash->close(&bootstrapStashState);
    ASSERT_TRUE(bootstrapStashState.valid());

    // Once history is accepted, Graphics lighting uses the immutable prior-frame image. The live caustic result now
    // comes from the current Graphics producer, so the next stash must import that state rather than the old
    // Async-lighting return state.
    activeGraphicsCaustics->open();
    activeGraphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::UnorderedAccess
    );
    activeGraphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::ShaderResource
    );
    activeGraphicsCaustics->close(&activeCausticsState);
    ASSERT_TRUE(activeCausticsState.valid());

    activeStash->open(&activeCausticsState);
    EXPECT_EQ(
        activeStash->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    activeStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    activeStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    activeStash->commitBarriers();
    activeStash->copyTexture(*causticHistory, slice, *causticIrradiance, slice);
    activeStash->close(&activeStashState);
    ASSERT_TRUE(activeStashState.valid());

    CommandList* graphicsLists[] = { graphicsCaustics.get() };
    const QueueSubmissionToken graphicsToken = device.executeCommandLists(
        graphicsLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsToken.valid());

    const QueueSubmissionDesc lightingSubmitDesc = QueueSubmissionDesc().setWaitTokens(&graphicsToken, 1u);
    CommandList* lightingLists[] = { asyncLighting.get() };
    const QueueSubmissionToken lightingToken = device.executeCommandLists(
        lightingLists,
        1u,
        CommandQueue::Compute,
        lightingSubmitDesc
    );
    EXPECT_TRUE(lightingToken.valid());

    const QueueSubmissionDesc bootstrapFinalSubmitDesc = QueueSubmissionDesc().setWaitTokens(&lightingToken, 1u);
    CommandList* bootstrapFinalLists[] = { bootstrapFinal.get() };
    const QueueSubmissionToken bootstrapFinalToken = device.executeCommandLists(
        bootstrapFinalLists,
        1u,
        CommandQueue::Graphics,
        bootstrapFinalSubmitDesc
    );
    ASSERT_TRUE(bootstrapFinalToken.valid());

    const QueueSubmissionDesc bootstrapStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&bootstrapFinalToken, 1u);
    CommandList* bootstrapStashLists[] = { bootstrapStash.get() };
    const QueueSubmissionToken bootstrapStashToken = device.executeCommandLists(
        bootstrapStashLists,
        1u,
        CommandQueue::Compute,
        bootstrapStashSubmitDesc
    );
    ASSERT_TRUE(bootstrapStashToken.valid());

    // This wait is the active-lagged plan's explicit cross-frame protection: do not overwrite the live image until
    // the previous Async history copy has stopped reading it.
    const QueueSubmissionDesc activeCausticsSubmitDesc = QueueSubmissionDesc().setWaitTokens(&bootstrapStashToken, 1u);
    CommandList* activeCausticsLists[] = { activeGraphicsCaustics.get() };
    const QueueSubmissionToken activeCausticsToken = device.executeCommandLists(
        activeCausticsLists,
        1u,
        CommandQueue::Graphics,
        activeCausticsSubmitDesc
    );
    ASSERT_TRUE(activeCausticsToken.valid());

    const QueueSubmissionDesc activeStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&activeCausticsToken, 1u);
    CommandList* activeStashLists[] = { activeStash.get() };
    const QueueSubmissionToken activeStashToken = device.executeCommandLists(
        activeStashLists,
        1u,
        CommandQueue::Compute,
        activeStashSubmitDesc
    );
    EXPECT_TRUE(activeStashToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

