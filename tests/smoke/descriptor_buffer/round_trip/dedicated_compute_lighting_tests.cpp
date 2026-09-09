// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static TextureHandle CreateExclusiveRayOutputTestTexture(GraphicsBackend::Device& device){
    return device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
}


// Deferred lighting and the logical composite now join the dedicated Compute queue after AVBOIT. The exclusive
// shadow, caustic, and surfel outputs therefore remain Compute-local through their only consumer; Graphics imports only the linear
// composite image for presentation. This exercises the narrow handoffs and verifies that no Graphics acquire/release
// is needed before the next Compute reuse of shadow/caustic/surfel outputs.
TEST_F(DescriptorBufferRoundTripTest, DedicatedComputeQueueKeepsDeferredLightingAndCompositeOnComputeUntilPresent){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";

    auto gbuffer = CreateConcurrentTestTexture(device);
    auto opaqueColor = CreateConcurrentTestTexture(device, true);
    auto compositeColor = CreateConcurrentTestTexture(device, true);
    auto avboitColor = CreateConcurrentTestTexture(device);
    auto avboitExtinction = CreateConcurrentTestTexture(device);
    auto shadowVisibility = CreateExclusiveRayOutputTestTexture(device);
    auto causticIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto surfelIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto slotsBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(compositeColor.get(), nullptr);
    ASSERT_NE(avboitColor.get(), nullptr);
    ASSERT_NE(avboitExtinction.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(slotsBuffer.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);
    auto prefix = device.createCommandList();
    auto rayEffects = device.createCommandList(computeParams);
    auto avboit = device.createCommandList();
    auto lighting = device.createCommandList(computeParams);
    auto composite = device.createCommandList(computeParams);
    auto present = device.createCommandList();
    auto computeReuse = device.createCommandList(computeParams);
    ASSERT_NE(prefix.get(), nullptr);
    ASSERT_NE(rayEffects.get(), nullptr);
    ASSERT_NE(avboit.get(), nullptr);
    ASSERT_NE(lighting.get(), nullptr);
    ASSERT_NE(composite.get(), nullptr);
    ASSERT_NE(present.get(), nullptr);
    ASSERT_NE(computeReuse.get(), nullptr);

    CommandListResourceStateHandoff prefixState(asyncScope.arena());
    CommandListResourceStateHandoff rayEffectsState(asyncScope.arena());
    CommandListResourceStateHandoff shadowLightingState(asyncScope.arena());
    CommandListResourceStateHandoff causticLightingState(asyncScope.arena());
    CommandListResourceStateHandoff surfelLightingState(asyncScope.arena());
    CommandListResourceStateHandoff avboitState(asyncScope.arena());
    CommandListResourceStateHandoff lightingBaseState(asyncScope.arena());
    CommandListResourceStateHandoff avboitLightingState(asyncScope.arena());
    CommandListResourceStateHandoff lightingInputState(asyncScope.arena());
    CommandListResourceStateHandoff lightingState(asyncScope.arena());
    CommandListResourceStateHandoff opaqueCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff avboitCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff compositeBaseState(asyncScope.arena());
    CommandListResourceStateHandoff compositeInputState(asyncScope.arena());
    CommandListResourceStateHandoff compositeState(asyncScope.arena());
    CommandListResourceStateHandoff compositePresentState(asyncScope.arena());
    CommandListResourceStateHandoff presentBaseState(asyncScope.arena());
    CommandListResourceStateHandoff presentInputState(asyncScope.arena());
    CommandListResourceStateHandoff presentState(asyncScope.arena());
    CommandListResourceStateHandoff shadowReturnState(asyncScope.arena());
    CommandListResourceStateHandoff causticReturnState(asyncScope.arena());
    CommandListResourceStateHandoff surfelReturnState(asyncScope.arena());
    CommandListResourceStateHandoff computeReuseInputState(asyncScope.arena());
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/deferred_compute_fan_in"));

    prefix->open();
    prefix->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prefix->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
    prefix->setBufferState(slotsBuffer.get(), ResourceStates::ConstantBuffer);
    prefix->close(&prefixState);
    ASSERT_TRUE(prefixState.valid());

    rayEffects->open(&prefixState);
    rayEffects->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->setBufferState(slotsBuffer.get(), ResourceStates::ConstantBuffer);
    rayEffects->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    rayEffects->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    rayEffects->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    rayEffects->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->close(&rayEffectsState);
    ASSERT_TRUE(rayEffectsState.valid());
    ASSERT_TRUE(shadowLightingState.buildTextureSubset(rayEffectsState, shadowVisibility.get(), fanInScratchArena));
    ASSERT_TRUE(causticLightingState.buildTextureSubset(rayEffectsState, causticIrradiance.get(), fanInScratchArena));
    ASSERT_TRUE(surfelLightingState.buildTextureSubset(rayEffectsState, surfelIrradiance.get(), fanInScratchArena));

    avboit->open(&prefixState);
    avboit->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    avboit->setTextureState(avboitColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    avboit->setTextureState(avboitExtinction.get(), s_AllSubresources, ResourceStates::ShaderResource);
    avboit->close(&avboitState);
    ASSERT_TRUE(avboitState.valid());

    Texture* const lightingBaseTextures[] = { gbuffer.get(), opaqueColor.get() };
    Buffer* const lightingBaseBuffers[] = { slotsBuffer.get() };
    ASSERT_TRUE(lightingBaseState.buildResourceSubset(
        prefixState,
        lightingBaseTextures,
        2u,
        lightingBaseBuffers,
        1u,
        fanInScratchArena
    ));
    Texture* const avboitLightingTextures[] = { gbuffer.get() };
    ASSERT_TRUE(avboitLightingState.buildResourceSubset(
        avboitState,
        avboitLightingTextures,
        1u,
        nullptr,
        0u,
        fanInScratchArena
    ));
    const CommandListResourceStateHandoff* const lightingBranches[] = {
        &shadowLightingState,
        &causticLightingState,
        &surfelLightingState,
        &avboitLightingState,
    };
    ASSERT_TRUE(lightingInputState.buildFanIn(lightingBaseState, lightingBranches, 4u, fanInScratchArena));

    lighting->open(&lightingInputState);
    EXPECT_EQ(lighting->getTextureSubresourceState(gbuffer.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Common);
    lighting->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    lighting->close(&lightingState);
    ASSERT_TRUE(lightingState.valid());
    ASSERT_TRUE(opaqueCompositeState.buildTextureSubset(lightingState, opaqueColor.get(), fanInScratchArena));

    Texture* const compositeBaseTextures[] = { avboitColor.get(), avboitExtinction.get() };
    ASSERT_TRUE(avboitCompositeState.buildResourceSubset(
        avboitState,
        compositeBaseTextures,
        2u,
        nullptr,
        0u,
        fanInScratchArena
    ));
    Buffer* const compositeBaseBuffers[] = { slotsBuffer.get() };
    ASSERT_TRUE(compositeBaseState.buildResourceSubset(
        lightingBaseState,
        nullptr,
        0u,
        compositeBaseBuffers,
        1u,
        fanInScratchArena
    ));
    const CommandListResourceStateHandoff* const compositeBranches[] = {
        &avboitCompositeState,
        &opaqueCompositeState,
    };
    ASSERT_TRUE(compositeInputState.buildFanIn(compositeBaseState, compositeBranches, 2u, fanInScratchArena));

    composite->open(&compositeInputState);
    EXPECT_EQ(composite->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(composite->getTextureSubresourceState(avboitColor.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::Unknown);
    composite->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(avboitColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(avboitExtinction.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    composite->close(&compositeState);
    ASSERT_TRUE(compositeState.valid());

    ASSERT_TRUE(compositePresentState.buildTextureSubset(compositeState, compositeColor.get(), fanInScratchArena));
    Buffer* const presentBaseBuffers[] = { slotsBuffer.get() };
    ASSERT_TRUE(presentBaseState.buildResourceSubset(
        compositeBaseState,
        nullptr,
        0u,
        presentBaseBuffers,
        1u,
        fanInScratchArena
    ));
    const CommandListResourceStateHandoff* const presentBranches[] = { &compositePresentState };
    ASSERT_TRUE(presentInputState.buildFanIn(presentBaseState, presentBranches, 1u, fanInScratchArena));
    present->open(&presentInputState);
    EXPECT_EQ(present->getTextureSubresourceState(compositeColor.get(), 0u, 0u), ResourceStates::Common);
    // opaqueColor is deliberately absent from present's handoff and no producer has been accepted yet. A separate
    // list must not observe the speculative retained Common restoration recorded by prefix or lighting.
    EXPECT_EQ(present->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Unknown);
    present->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    present->close(&presentState);
    ASSERT_TRUE(presentState.valid());

    ASSERT_TRUE(shadowReturnState.buildTextureSubset(lightingState, shadowVisibility.get(), fanInScratchArena));
    ASSERT_TRUE(causticReturnState.buildTextureSubset(lightingState, causticIrradiance.get(), fanInScratchArena));
    ASSERT_TRUE(surfelReturnState.buildTextureSubset(lightingState, surfelIrradiance.get(), fanInScratchArena));
    const CommandListResourceStateHandoff* const computeReuseBranches[] = {
        &shadowReturnState,
        &causticReturnState,
        &surfelReturnState,
    };
    ASSERT_TRUE(computeReuseInputState.buildFanIn(prefixState, computeReuseBranches, 3u, fanInScratchArena));
    computeReuse->open(&computeReuseInputState);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    computeReuse->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->close();

    CommandList* prefixLists[] = { prefix.get() };
    const QueueSubmissionToken prefixToken = device.executeCommandLists(
        prefixLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(prefixToken.valid());

    // Once prefix is accepted, its retained terminal restoration is globally available to a later raw list.
    const CommandListHandle acceptedPrefixStateProbe = device.createCommandList();
    ASSERT_NE(acceptedPrefixStateProbe.get(), nullptr);
    acceptedPrefixStateProbe->open();
    EXPECT_EQ(acceptedPrefixStateProbe->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Common);
    acceptedPrefixStateProbe->close();

    const QueueSubmissionDesc rayEffectsSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
    CommandList* rayEffectsLists[] = { rayEffects.get() };
    const QueueSubmissionToken rayEffectsToken = device.executeCommandLists(
        rayEffectsLists,
        1u,
        CommandQueue::Compute,
        rayEffectsSubmitDesc
    );
    ASSERT_TRUE(rayEffectsToken.valid());

    const QueueSubmissionDesc avboitSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
    CommandList* avboitLists[] = { avboit.get() };
    const QueueSubmissionToken avboitToken = device.executeCommandLists(
        avboitLists,
        1u,
        CommandQueue::Graphics,
        avboitSubmitDesc
    );
    ASSERT_TRUE(avboitToken.valid());

    const QueueSubmissionDesc lightingSubmitDesc = QueueSubmissionDesc().setWaitTokens(&avboitToken, 1u);
    CommandList* lightingLists[] = { lighting.get() };
    const QueueSubmissionToken lightingToken = device.executeCommandLists(
        lightingLists,
        1u,
        CommandQueue::Compute,
        lightingSubmitDesc
    );
    ASSERT_TRUE(lightingToken.valid());

    CommandList* compositeLists[] = { composite.get() };
    const QueueSubmissionToken compositeToken = device.executeCommandLists(
        compositeLists,
        1u,
        CommandQueue::Compute,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(compositeToken.valid());

    const QueueSubmissionDesc presentSubmitDesc = QueueSubmissionDesc().setWaitTokens(&compositeToken, 1u);
    CommandList* presentLists[] = { present.get() };
    const QueueSubmissionToken presentToken = device.executeCommandLists(
        presentLists,
        1u,
        CommandQueue::Graphics,
        presentSubmitDesc
    );
    ASSERT_TRUE(presentToken.valid());

    // This reuse is ordered after composite by the same AsyncCompute queue. It deliberately does not wait for the
    // Graphics present packet because that packet imports only the concurrent composite presentation image.
    CommandList* reuseLists[] = { computeReuse.get() };
    const QueueSubmissionToken reuseToken = device.executeCommandLists(
        reuseLists,
        1u,
        CommandQueue::Compute,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(reuseToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// The optional latency trade-off keeps the three exclusive live effect outputs on AsyncCompute, but copies an
// accepted snapshot into concurrent, keep-initial-state history images. The next frame's Graphics lighting samples
// only those history images while AsyncCompute writes a new live triple; final still joins the current producer before
// the next snapshot. This verifies the precise queue and state topology without relying on a renderer pipeline.
TEST_F(DescriptorBufferRoundTripTest, DedicatedComputeQueueUsesAcceptedLaggedLightingHistory){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";

    auto gbuffer = CreateConcurrentTestTexture(device);
    auto opaqueColor = CreateConcurrentTestTexture(device, true);
    auto compositeColor = CreateConcurrentTestTexture(device, true);
    auto shadowVisibility = CreateExclusiveRayOutputTestTexture(device);
    auto causticIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto surfelIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto shadowHistory = CreateConcurrentTestTexture(device, true);
    auto causticHistory = CreateConcurrentTestTexture(device, true);
    auto surfelHistory = CreateConcurrentTestTexture(device, true);
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(compositeColor.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(shadowHistory.get(), nullptr);
    ASSERT_NE(causticHistory.get(), nullptr);
    ASSERT_NE(surfelHistory.get(), nullptr);

    CommandListParameters asyncParams;
    asyncParams.setQueueType(CommandQueue::Compute);
    auto seedPrefix = device.createCommandList();
    auto seedProducer = device.createCommandList(asyncParams);
    auto seedFinal = device.createCommandList();
    auto seedStash = device.createCommandList(asyncParams);
    auto nextPrefix = device.createCommandList();
    auto nextProducer = device.createCommandList(asyncParams);
    auto laggedLighting = device.createCommandList();
    auto laggedComposite = device.createCommandList();
    auto laggedFinal = device.createCommandList();
    auto nextStash = device.createCommandList(asyncParams);
    ASSERT_NE(seedPrefix.get(), nullptr);
    ASSERT_NE(seedProducer.get(), nullptr);
    ASSERT_NE(seedFinal.get(), nullptr);
    ASSERT_NE(seedStash.get(), nullptr);
    ASSERT_NE(nextPrefix.get(), nullptr);
    ASSERT_NE(nextProducer.get(), nullptr);
    ASSERT_NE(laggedLighting.get(), nullptr);
    ASSERT_NE(laggedComposite.get(), nullptr);
    ASSERT_NE(laggedFinal.get(), nullptr);
    ASSERT_NE(nextStash.get(), nullptr);

    CommandListResourceStateHandoff seedPrefixState(asyncScope.arena());
    CommandListResourceStateHandoff seedProducerState(asyncScope.arena());
    CommandListResourceStateHandoff seedShadowSourceState(asyncScope.arena());
    CommandListResourceStateHandoff seedCausticSourceState(asyncScope.arena());
    CommandListResourceStateHandoff seedSurfelSourceState(asyncScope.arena());
    CommandListResourceStateHandoff seedStashInputState(asyncScope.arena());
    CommandListResourceStateHandoff seedStashState(asyncScope.arena());
    CommandListResourceStateHandoff seedHistoryState(asyncScope.arena());
    CommandListResourceStateHandoff seedShadowReturnState(asyncScope.arena());
    CommandListResourceStateHandoff seedCausticReturnState(asyncScope.arena());
    CommandListResourceStateHandoff seedSurfelReturnState(asyncScope.arena());
    CommandListResourceStateHandoff nextPrefixState(asyncScope.arena());
    CommandListResourceStateHandoff nextProducerInputState(asyncScope.arena());
    CommandListResourceStateHandoff nextProducerState(asyncScope.arena());
    CommandListResourceStateHandoff nextShadowSourceState(asyncScope.arena());
    CommandListResourceStateHandoff nextCausticSourceState(asyncScope.arena());
    CommandListResourceStateHandoff nextSurfelSourceState(asyncScope.arena());
    CommandListResourceStateHandoff laggedLightingBaseState(asyncScope.arena());
    CommandListResourceStateHandoff laggedLightingInputState(asyncScope.arena());
    CommandListResourceStateHandoff laggedLightingState(asyncScope.arena());
    CommandListResourceStateHandoff laggedOpaqueCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff laggedCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff laggedCompositeFinalState(asyncScope.arena());
    CommandListResourceStateHandoff laggedFinalState(asyncScope.arena());
    CommandListResourceStateHandoff nextStashInputState(asyncScope.arena());
    CommandListResourceStateHandoff nextStashState(asyncScope.arena());
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/lagged_lighting_fan_in"));

    seedPrefix->open();
    seedPrefix->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedPrefix->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedPrefix->close(&seedPrefixState);
    ASSERT_TRUE(seedPrefixState.valid());

    seedProducer->open(&seedPrefixState);
    seedProducer->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    seedProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    seedProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    seedProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->close(&seedProducerState);
    ASSERT_TRUE(seedProducerState.valid());
    ASSERT_TRUE(seedShadowSourceState.buildTextureSubset(seedProducerState, shadowVisibility.get(), fanInScratchArena));
    ASSERT_TRUE(seedCausticSourceState.buildTextureSubset(seedProducerState, causticIrradiance.get(), fanInScratchArena));
    ASSERT_TRUE(seedSurfelSourceState.buildTextureSubset(seedProducerState, surfelIrradiance.get(), fanInScratchArena));

    seedFinal->open();
    seedFinal->close();
    ASSERT_TRUE(seedFinal->hasCommandBuffer());

    const CommandListResourceStateHandoff* const seedStashBranches[] = {
        &seedCausticSourceState,
        &seedSurfelSourceState,
    };
    ASSERT_TRUE(seedStashInputState.buildFanIn(
        seedShadowSourceState,
        seedStashBranches,
        LengthOf(seedStashBranches),
        fanInScratchArena
    ));
    seedStash->open(&seedStashInputState);
    EXPECT_EQ(seedStash->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(seedStash->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(seedStash->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    seedStash->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::CopySource);
    seedStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    seedStash->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    seedStash->setTextureState(shadowHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedStash->setTextureState(surfelHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedStash->commitBarriers();
    const TextureSlice slice;
    seedStash->copyTexture(*shadowHistory, slice, *shadowVisibility, slice);
    seedStash->copyTexture(*causticHistory, slice, *causticIrradiance, slice);
    seedStash->copyTexture(*surfelHistory, slice, *surfelIrradiance, slice);
    seedStash->close(&seedStashState);
    ASSERT_TRUE(seedStashState.valid());
    ASSERT_TRUE(seedShadowReturnState.buildTextureSubset(seedStashState, shadowVisibility.get(), fanInScratchArena));
    ASSERT_TRUE(seedCausticReturnState.buildTextureSubset(seedStashState, causticIrradiance.get(), fanInScratchArena));
    ASSERT_TRUE(seedSurfelReturnState.buildTextureSubset(seedStashState, surfelIrradiance.get(), fanInScratchArena));
    Texture* const seedHistoryTextures[] = {
        shadowHistory.get(),
        causticHistory.get(),
        surfelHistory.get(),
    };
    ASSERT_TRUE(seedHistoryState.buildResourceSubset(
        seedStashState,
        seedHistoryTextures,
        LengthOf(seedHistoryTextures),
        nullptr,
        0u,
        fanInScratchArena
    ));

    nextPrefix->open();
    nextPrefix->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextPrefix->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextPrefix->close(&nextPrefixState);
    ASSERT_TRUE(nextPrefixState.valid());

    const CommandListResourceStateHandoff* const nextProducerBranches[] = {
        &seedShadowReturnState,
        &seedCausticReturnState,
        &seedSurfelReturnState,
    };
    ASSERT_TRUE(nextProducerInputState.buildFanIn(
        nextPrefixState,
        nextProducerBranches,
        LengthOf(nextProducerBranches),
        fanInScratchArena
    ));
    nextProducer->open(&nextProducerInputState);
    EXPECT_EQ(nextProducer->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::CopySource);
    EXPECT_EQ(nextProducer->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::CopySource);
    EXPECT_EQ(nextProducer->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::CopySource);
    nextProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    nextProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    nextProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    nextProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextProducer->close(&nextProducerState);
    ASSERT_TRUE(nextProducerState.valid());
    ASSERT_TRUE(nextShadowSourceState.buildTextureSubset(nextProducerState, shadowVisibility.get(), fanInScratchArena));
    ASSERT_TRUE(nextCausticSourceState.buildTextureSubset(nextProducerState, causticIrradiance.get(), fanInScratchArena));
    ASSERT_TRUE(nextSurfelSourceState.buildTextureSubset(nextProducerState, surfelIrradiance.get(), fanInScratchArena));

    Texture* const laggedLightingBaseTextures[] = {
        gbuffer.get(),
        opaqueColor.get(),
    };
    ASSERT_TRUE(laggedLightingBaseState.buildResourceSubset(
        nextPrefixState,
        laggedLightingBaseTextures,
        LengthOf(laggedLightingBaseTextures),
        nullptr,
        0u,
        fanInScratchArena
    ));
    const CommandListResourceStateHandoff* const laggedLightingBranches[] = { &seedHistoryState };
    ASSERT_TRUE(laggedLightingInputState.buildFanIn(
        laggedLightingBaseState,
        laggedLightingBranches,
        LengthOf(laggedLightingBranches),
        fanInScratchArena
    ));
    laggedLighting->open(&laggedLightingInputState);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(gbuffer.get(), 0u, 0u), ResourceStates::ShaderResource);
    // The future Graphics submission waits for seedStashToken, while this explicit subset supplies its terminal
    // Common state during pre-submit recording. A raw retained-state lookup cannot represent this dependency.
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(shadowHistory.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(causticHistory.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(surfelHistory.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::Unknown);
    laggedLighting->setTextureState(shadowHistory.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedLighting->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedLighting->setTextureState(surfelHistory.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedLighting->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    laggedLighting->close(&laggedLightingState);
    ASSERT_TRUE(laggedLightingState.valid());
    ASSERT_TRUE(laggedOpaqueCompositeState.buildTextureSubset(laggedLightingState, opaqueColor.get(), fanInScratchArena));

    laggedComposite->open(&laggedOpaqueCompositeState);
    EXPECT_EQ(laggedComposite->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Common);
    laggedComposite->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedComposite->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    laggedComposite->close(&laggedCompositeState);
    ASSERT_TRUE(laggedCompositeState.valid());
    ASSERT_TRUE(laggedCompositeFinalState.buildTextureSubset(laggedCompositeState, compositeColor.get(), fanInScratchArena));

    laggedFinal->open(&laggedCompositeFinalState);
    EXPECT_EQ(laggedFinal->getTextureSubresourceState(compositeColor.get(), 0u, 0u), ResourceStates::Common);
    laggedFinal->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedFinal->close(&laggedFinalState);
    ASSERT_TRUE(laggedFinalState.valid());

    const CommandListResourceStateHandoff* const nextStashBranches[] = {
        &nextCausticSourceState,
        &nextSurfelSourceState,
    };
    ASSERT_TRUE(nextStashInputState.buildFanIn(
        nextShadowSourceState,
        nextStashBranches,
        LengthOf(nextStashBranches),
        fanInScratchArena
    ));
    nextStash->open(&nextStashInputState);
    nextStash->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::CopySource);
    nextStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    nextStash->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    nextStash->setTextureState(shadowHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextStash->setTextureState(surfelHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextStash->commitBarriers();
    nextStash->copyTexture(*shadowHistory, slice, *shadowVisibility, slice);
    nextStash->copyTexture(*causticHistory, slice, *causticIrradiance, slice);
    nextStash->copyTexture(*surfelHistory, slice, *surfelIrradiance, slice);
    nextStash->close(&nextStashState);
    ASSERT_TRUE(nextStashState.valid());

    CommandList* seedPrefixLists[] = { seedPrefix.get() };
    const QueueSubmissionToken seedPrefixToken = device.executeCommandLists(
        seedPrefixLists,
        LengthOf(seedPrefixLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(seedPrefixToken.valid());

    const QueueSubmissionDesc seedProducerSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedPrefixToken, 1u);
    CommandList* seedProducerLists[] = { seedProducer.get() };
    const QueueSubmissionToken seedProducerToken = device.executeCommandLists(
        seedProducerLists,
        LengthOf(seedProducerLists),
        CommandQueue::Compute,
        seedProducerSubmitDesc
    );
    ASSERT_TRUE(seedProducerToken.valid());

    const QueueSubmissionDesc seedFinalSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedProducerToken, 1u);
    CommandList* seedFinalLists[] = { seedFinal.get() };
    const QueueSubmissionToken seedFinalToken = device.executeCommandLists(
        seedFinalLists,
        LengthOf(seedFinalLists),
        CommandQueue::Graphics,
        seedFinalSubmitDesc
    );
    ASSERT_TRUE(seedFinalToken.valid());

    const QueueSubmissionDesc seedStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedFinalToken, 1u);
    CommandList* seedStashLists[] = { seedStash.get() };
    const QueueSubmissionToken seedStashToken = device.executeCommandLists(
        seedStashLists,
        LengthOf(seedStashLists),
        CommandQueue::Compute,
        seedStashSubmitDesc
    );
    ASSERT_TRUE(seedStashToken.valid());

    CommandList* nextPrefixLists[] = { nextPrefix.get() };
    const QueueSubmissionToken nextPrefixToken = device.executeCommandLists(
        nextPrefixLists,
        LengthOf(nextPrefixLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(nextPrefixToken.valid());

    const QueueSubmissionDesc nextProducerSubmitDesc = QueueSubmissionDesc().setWaitTokens(&nextPrefixToken, 1u);
    CommandList* nextProducerLists[] = { nextProducer.get() };
    const QueueSubmissionToken nextProducerToken = device.executeCommandLists(
        nextProducerLists,
        LengthOf(nextProducerLists),
        CommandQueue::Compute,
        nextProducerSubmitDesc
    );
    ASSERT_TRUE(nextProducerToken.valid());

    const QueueSubmissionDesc laggedLightingSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedStashToken, 1u);
    CommandList* laggedLightingLists[] = { laggedLighting.get() };
    const QueueSubmissionToken laggedLightingToken = device.executeCommandLists(
        laggedLightingLists,
        LengthOf(laggedLightingLists),
        CommandQueue::Graphics,
        laggedLightingSubmitDesc
    );
    ASSERT_TRUE(laggedLightingToken.valid());

    CommandList* laggedCompositeLists[] = { laggedComposite.get() };
    const QueueSubmissionToken laggedCompositeToken = device.executeCommandLists(
        laggedCompositeLists,
        LengthOf(laggedCompositeLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(laggedCompositeToken.valid());

    const QueueSubmissionToken laggedFinalWaitTokens[] = { laggedCompositeToken, nextProducerToken };
    const QueueSubmissionDesc laggedFinalSubmitDesc = QueueSubmissionDesc().setWaitTokens(
        laggedFinalWaitTokens,
        LengthOf(laggedFinalWaitTokens)
    );
    CommandList* laggedFinalLists[] = { laggedFinal.get() };
    const QueueSubmissionToken laggedFinalToken = device.executeCommandLists(
        laggedFinalLists,
        LengthOf(laggedFinalLists),
        CommandQueue::Graphics,
        laggedFinalSubmitDesc
    );
    ASSERT_TRUE(laggedFinalToken.valid());

    const QueueSubmissionDesc nextStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&laggedFinalToken, 1u);
    CommandList* nextStashLists[] = { nextStash.get() };
    const QueueSubmissionToken nextStashToken = device.executeCommandLists(
        nextStashLists,
        LengthOf(nextStashLists),
        CommandQueue::Compute,
        nextStashSubmitDesc
    );
    EXPECT_TRUE(nextStashToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

