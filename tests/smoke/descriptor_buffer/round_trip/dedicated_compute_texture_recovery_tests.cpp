// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustics (including the hardware dispatch-rays producer) and surfel GI add two exclusive Compute results beside
// shadowVisibility. If Graphics effects/final cannot consume them, the recovery packet must acquire and return all
// three outputs together; the next Compute packet
// then imports their shared return handoff alongside its ordinary concurrent prefix input.
TEST_F(DescriptorBufferRoundTripTest, DedicatedComputeQueueRecoversCausticSurfelAndShadowTextureOwnershipTogether){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";

    const auto makeExclusiveOutput = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto shadowVisibility = makeExclusiveOutput();
    auto causticIrradiance = makeExclusiveOutput();
    auto surfelIrradiance = makeExclusiveOutput();
    auto sharedInput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(sharedInput.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);

    CommandListResourceStateHandoff prefixState(asyncScope.arena());
    CommandListResourceStateHandoff computeState(asyncScope.arena());
    CommandListResourceStateHandoff shadowGraphicsState(asyncScope.arena());
    CommandListResourceStateHandoff causticGraphicsState(asyncScope.arena());
    CommandListResourceStateHandoff surfelGraphicsState(asyncScope.arena());
    CommandListResourceStateHandoff recoveryInputState(asyncScope.arena());
    CommandListResourceStateHandoff recoveryState(asyncScope.arena());
    CommandListResourceStateHandoff nextComputeInputState(asyncScope.arena());
    Alloc::ScratchArena fanInScratchArena(Name("tests/descriptor_buffer/recovery_fan_in"));

    auto prefix = device.createCommandList();
    auto compute = device.createCommandList(computeParams);
    auto recovery = device.createCommandList();
    auto computeReuse = device.createCommandList(computeParams);
    ASSERT_NE(prefix.get(), nullptr);
    ASSERT_NE(compute.get(), nullptr);
    ASSERT_NE(recovery.get(), nullptr);
    ASSERT_NE(computeReuse.get(), nullptr);

    prefix->open();
    prefix->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    prefix->close(&prefixState);
    ASSERT_TRUE(prefixState.valid());

    compute->open(&prefixState);
    compute->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    compute->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    compute->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    compute->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    compute->releaseTextureOwnership(shadowVisibility.get(), s_AllSubresources, CommandQueue::Graphics);
    compute->releaseTextureOwnership(causticIrradiance.get(), s_AllSubresources, CommandQueue::Graphics);
    compute->releaseTextureOwnership(surfelIrradiance.get(), s_AllSubresources, CommandQueue::Graphics);
    compute->close(&computeState);
    ASSERT_TRUE(computeState.valid());
    ASSERT_TRUE(shadowGraphicsState.buildTextureSubset(computeState, shadowVisibility.get(), fanInScratchArena));
    ASSERT_TRUE(causticGraphicsState.buildTextureSubset(computeState, causticIrradiance.get(), fanInScratchArena));
    ASSERT_TRUE(surfelGraphicsState.buildTextureSubset(computeState, surfelIrradiance.get(), fanInScratchArena));

    const CommandListResourceStateHandoff* recoveryBranches[] = { &causticGraphicsState, &surfelGraphicsState };
    ASSERT_TRUE(recoveryInputState.buildFanIn(shadowGraphicsState, recoveryBranches, 2u, fanInScratchArena));

    CommandList* prefixLists[] = { prefix.get() };
    const QueueSubmissionToken prefixToken = device.executeCommandLists(
        prefixLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(prefixToken.valid());

    const QueueSubmissionDesc computeSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
    CommandList* computeLists[] = { compute.get() };
    const QueueSubmissionToken computeToken = device.executeCommandLists(
        computeLists,
        1u,
        CommandQueue::Compute,
        computeSubmitDesc
    );
    ASSERT_TRUE(computeToken.valid());

    recovery->open(&recoveryInputState);
    EXPECT_EQ(recovery->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    EXPECT_EQ(recovery->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    EXPECT_EQ(recovery->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    recovery->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    recovery->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    recovery->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    recovery->releaseTextureOwnership(shadowVisibility.get(), s_AllSubresources, CommandQueue::Compute);
    recovery->releaseTextureOwnership(causticIrradiance.get(), s_AllSubresources, CommandQueue::Compute);
    recovery->releaseTextureOwnership(surfelIrradiance.get(), s_AllSubresources, CommandQueue::Compute);
    recovery->close(&recoveryState);
    ASSERT_TRUE(recoveryState.valid());

    const QueueSubmissionDesc recoverySubmitDesc = QueueSubmissionDesc().setWaitTokens(&computeToken, 1u);
    CommandList* recoveryLists[] = { recovery.get() };
    const QueueSubmissionToken recoveryToken = device.executeCommandLists(
        recoveryLists,
        1u,
        CommandQueue::Graphics,
        recoverySubmitDesc
    );
    ASSERT_TRUE(recoveryToken.valid());

    const CommandListResourceStateHandoff* nextComputeBranches[] = { &recoveryState };
    ASSERT_TRUE(nextComputeInputState.buildFanIn(prefixState, nextComputeBranches, 1u, fanInScratchArena));
    computeReuse->open(&nextComputeInputState);
    EXPECT_EQ(computeReuse->getBufferState(sharedInput.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    computeReuse->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->close();

    const QueueSubmissionDesc reuseSubmitDesc = QueueSubmissionDesc().setWaitTokens(&recoveryToken, 1u);
    CommandList* reuseLists[] = { computeReuse.get() };
    const QueueSubmissionToken reuseToken = device.executeCommandLists(
        reuseLists,
        1u,
        CommandQueue::Compute,
        reuseSubmitDesc
    );
    EXPECT_TRUE(reuseToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

