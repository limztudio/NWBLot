// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Exercise the four-submission shadow topology's rejection boundaries with a test-owned vkQueueSubmit2 observer.
// Prefix and shadow failures leave no ownership handoff; effects/final failures repair the accepted Compute release
// through a Graphics acquire/release; a failed recovery deliberately stops before reuse, matching the renderer's
// device-recreation/suspension policy. Injected failures return before the observer forwards to the real driver.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputePacketFailureInjectionPreservesOrSuspendsExclusiveOwnership){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Dedicated Compute queue: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Dedicated Compute queue: adapter has no dedicated compute-only queue family.";
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId computeQueue = device.getPrimaryPhysicalQueue(CommandQueue::Compute);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(computeQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    const VkQueue nativeComputeQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, computeQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    ASSERT_NE(nativeComputeQueue, VK_NULL_HANDLE);

    enum class FailurePoint : u8{
        Prefix,
        Shadow,
        Effects,
        Final,
        Recovery,
    };

    const auto makeExclusiveBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeSharedBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
    };

    CommandListParameters computeParams;
    computeParams.setQueueType(CommandQueue::Compute);

    // Executes the next valid Compute -> Graphics -> Compute cycle. `initialState` is present only after the
    // ownership-recovery acquire/release; `initialWait` makes that handoff's accepted token explicit.
    const auto executeNextValidCycle = [&](
        Buffer* output,
        Buffer* sharedInput,
        const CommandListResourceStateHandoff* initialState,
        const QueueSubmissionToken initialWait
    ){
        CommandListResourceStateHandoff computeToGraphics(asyncScope.arena());
        CommandListResourceStateHandoff graphicsToCompute(asyncScope.arena());
        auto compute = device.createCommandList(computeParams);
        auto graphics = device.createCommandList();
        auto computeReuse = device.createCommandList(computeParams);
        ASSERT_NE(compute.get(), nullptr);
        ASSERT_NE(graphics.get(), nullptr);
        ASSERT_NE(computeReuse.get(), nullptr);

        compute->open(initialState);
        if(initialState)
            EXPECT_EQ(compute->getBufferState(output), ResourceStates::ShaderResource);
        compute->setBufferState(sharedInput, ResourceStates::ShaderResource);
        compute->setBufferState(output, ResourceStates::UnorderedAccess);
        compute->releaseBufferOwnership(output, CommandQueue::Graphics);
        compute->close(&computeToGraphics);
        ASSERT_TRUE(computeToGraphics.valid());

        QueueSubmissionDesc computeSubmitDesc;
        if(initialWait.valid())
            computeSubmitDesc.setWaitTokens(&initialWait, 1u);
        CommandList* computeCommandLists[] = { compute.get() };
        const QueueSubmissionToken computeToken = device.executeCommandLists(
            computeCommandLists,
            1u,
            CommandQueue::Compute,
            computeSubmitDesc
        );
        ASSERT_TRUE(computeToken.valid());

        graphics->open(&computeToGraphics);
        EXPECT_EQ(graphics->getBufferState(output), ResourceStates::UnorderedAccess);
        graphics->setBufferState(output, ResourceStates::ShaderResource);
        graphics->releaseBufferOwnership(output, CommandQueue::Compute);
        graphics->close(&graphicsToCompute);
        ASSERT_TRUE(graphicsToCompute.valid());

        const QueueSubmissionDesc graphicsSubmitDesc = QueueSubmissionDesc().setWaitTokens(&computeToken, 1u);
        CommandList* graphicsCommandLists[] = { graphics.get() };
        const QueueSubmissionToken graphicsToken = device.executeCommandLists(
            graphicsCommandLists,
            1u,
            CommandQueue::Graphics,
            graphicsSubmitDesc
        );
        ASSERT_TRUE(graphicsToken.valid());

        computeReuse->open(&graphicsToCompute);
        EXPECT_EQ(computeReuse->getBufferState(output), ResourceStates::ShaderResource);
        computeReuse->setBufferState(output, ResourceStates::UnorderedAccess);
        computeReuse->close();
        const QueueSubmissionDesc reuseSubmitDesc = QueueSubmissionDesc().setWaitTokens(&graphicsToken, 1u);
        CommandList* reuseCommandLists[] = { computeReuse.get() };
        const QueueSubmissionToken reuseToken = device.executeCommandLists(
            reuseCommandLists,
            1u,
            CommandQueue::Compute,
            reuseSubmitDesc
        );
        ASSERT_TRUE(reuseToken.valid());
        ASSERT_TRUE(device.waitForIdle());
    };

    const FailurePoint failurePoints[] = {
        FailurePoint::Prefix,
        FailurePoint::Shadow,
        FailurePoint::Effects,
        FailurePoint::Final,
        FailurePoint::Recovery,
    };
    for(const FailurePoint failurePoint : failurePoints){
        SCOPED_TRACE(static_cast<u32>(failurePoint));
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());

        auto output = makeExclusiveBuffer();
        auto sharedInput = makeSharedBuffer();
        ASSERT_NE(output.get(), nullptr);
        ASSERT_NE(sharedInput.get(), nullptr);

        CommandListResourceStateHandoff computeToGraphics(asyncScope.arena());
        CommandListResourceStateHandoff graphicsToCompute(asyncScope.arena());
        auto prefix = device.createCommandList();
        auto shadow = device.createCommandList(computeParams);
        auto effects = device.createCommandList();
        auto final = device.createCommandList();
        ASSERT_NE(prefix.get(), nullptr);
        ASSERT_NE(shadow.get(), nullptr);
        ASSERT_NE(effects.get(), nullptr);
        ASSERT_NE(final.get(), nullptr);

        prefix->open();
        prefix->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
        prefix->close();
        if(failurePoint == FailurePoint::Prefix)
            ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        CommandList* prefixCommandLists[] = { prefix.get() };
        const QueueSubmissionToken prefixToken = device.executeCommandLists(
            prefixCommandLists,
            1u,
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        if(failurePoint == FailurePoint::Prefix){
            EXPECT_FALSE(prefixToken.valid());
            executeNextValidCycle(output.get(), sharedInput.get(), nullptr, {});
            EXPECT_FALSE(submissionObserver.overflowed());
            EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
            EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
            continue;
        }
        ASSERT_TRUE(prefixToken.valid());

        shadow->open();
        shadow->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
        shadow->setBufferState(output.get(), ResourceStates::UnorderedAccess);
        shadow->releaseBufferOwnership(output.get(), CommandQueue::Graphics);
        shadow->close(&computeToGraphics);
        ASSERT_TRUE(computeToGraphics.valid());
        if(failurePoint == FailurePoint::Shadow)
            ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeComputeQueue));
        const QueueSubmissionDesc shadowSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
        CommandList* shadowCommandLists[] = { shadow.get() };
        const QueueSubmissionToken shadowToken = device.executeCommandLists(
            shadowCommandLists,
            1u,
            CommandQueue::Compute,
            shadowSubmitDesc
        );
        if(failurePoint == FailurePoint::Shadow){
            EXPECT_FALSE(shadowToken.valid());
            executeNextValidCycle(output.get(), sharedInput.get(), nullptr, prefixToken);
            EXPECT_FALSE(submissionObserver.overflowed());
            EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
            EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
            continue;
        }
        ASSERT_TRUE(shadowToken.valid());

        effects->open();
        effects->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
        effects->close();
        if(failurePoint == FailurePoint::Effects || failurePoint == FailurePoint::Recovery)
            ASSERT_TRUE(submissionObserver.armSubmissionFailures(
                nativeGraphicsQueue,
                failurePoint == FailurePoint::Recovery ? 2u : 1u
            ));
        CommandList* effectsCommandLists[] = { effects.get() };
        const QueueSubmissionToken effectsToken = device.executeCommandLists(
            effectsCommandLists,
            1u,
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        if(failurePoint == FailurePoint::Effects || failurePoint == FailurePoint::Recovery){
            EXPECT_FALSE(effectsToken.valid());
            EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
            EXPECT_EQ(
                submissionObserver.pendingSubmissionFailureCount(),
                failurePoint == FailurePoint::Recovery ? 1u : 0u
            );
        }
        else
            ASSERT_TRUE(effectsToken.valid());

        bool finalRejected = false;
        if(failurePoint != FailurePoint::Effects && failurePoint != FailurePoint::Recovery){
            final->open(&computeToGraphics);
            EXPECT_EQ(final->getBufferState(output.get()), ResourceStates::UnorderedAccess);
            final->setBufferState(output.get(), ResourceStates::ShaderResource);
            final->releaseBufferOwnership(output.get(), CommandQueue::Compute);
            final->close(&graphicsToCompute);
            ASSERT_TRUE(graphicsToCompute.valid());
            if(failurePoint == FailurePoint::Final)
                ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
            const QueueSubmissionToken finalWaitTokens[] = { shadowToken, effectsToken };
            const QueueSubmissionDesc finalSubmitDesc = QueueSubmissionDesc().setWaitTokens(finalWaitTokens, 2u);
            CommandList* finalCommandLists[] = { final.get() };
            const QueueSubmissionToken finalToken = device.executeCommandLists(
                finalCommandLists,
                1u,
                CommandQueue::Graphics,
                finalSubmitDesc
            );
            finalRejected = failurePoint == FailurePoint::Final;
            if(finalRejected){
                EXPECT_FALSE(finalToken.valid());
                EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
                EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
            }
            else
                ASSERT_TRUE(finalToken.valid());

            if(!finalRejected){
                executeNextValidCycle(output.get(), sharedInput.get(), &graphicsToCompute, finalToken);
                continue;
            }
        }

        // Effects/final did not leave an accepted Graphics acquire, so return the accepted Compute release to its
        // documented Compute owner. The second injected rejection covers the renderer's terminal recovery failure.
        auto recovery = device.createCommandList();
        ASSERT_NE(recovery.get(), nullptr);
        recovery->open(&computeToGraphics);
        EXPECT_EQ(recovery->getBufferState(output.get()), ResourceStates::UnorderedAccess);
        recovery->setBufferState(output.get(), ResourceStates::ShaderResource);
        recovery->releaseBufferOwnership(output.get(), CommandQueue::Compute);
        recovery->close(&graphicsToCompute);
        ASSERT_TRUE(graphicsToCompute.valid());
        const QueueSubmissionDesc recoverySubmitDesc = QueueSubmissionDesc().setWaitTokens(&shadowToken, 1u);
        CommandList* recoveryCommandLists[] = { recovery.get() };
        const QueueSubmissionToken recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            CommandQueue::Graphics,
            recoverySubmitDesc
        );
        if(failurePoint == FailurePoint::Recovery){
            EXPECT_FALSE(recoveryToken.valid());
            EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 2u);
            EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
            EXPECT_FALSE(submissionObserver.overflowed());
            EXPECT_TRUE(device.waitForIdle());
            continue;
        }
        ASSERT_TRUE(recoveryToken.valid());
        executeNextValidCycle(output.get(), sharedInput.get(), &graphicsToCompute, recoveryToken);
        EXPECT_FALSE(submissionObserver.overflowed());
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

