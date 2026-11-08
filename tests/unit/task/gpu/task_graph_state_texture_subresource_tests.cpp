// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_state_texture_subresource_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, FansInTerminalTextureStateFragmentsForBroadCrossQueueConsumer){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto textureResult = AddTextureMetadata(graph, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFragmentFanInTexture), "Fragment Fan-In Texture");
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    const Core::GpuTaskCommandRequirements graphicsCommands{ Core::GpuQueueCapability::Graphics };
    const Core::GpuTaskCommandRequirements computeCommands{ Core::GpuQueueCapability::Compute };
    const Core::GpuTaskCommandRequirements transferCommands{ Core::GpuQueueCapability::Transfer };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const Core::GpuTaskResourceRange transferRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Core::GpuTaskResourceRange computeRange{ .textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Core::GpuTaskResourceRange broadRange{ .textureSubresources = Core::s_AllSubresources, };
    const Core::GpuTaskResourceRange initialTailRange{ .textureSubresources = Core::TextureSubresourceSet(2u, Core::TextureSubresourceSet::s_AllMipLevels, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Core::GpuTaskResourceUse transferUse{ .resource = texture, .range = transferRange, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceUse computeUse{ .resource = texture, .range = computeRange, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceUse consumerUse{ .resource = texture, .range = broadRange, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };

    Core::GpuTaskDesc transferDesc;
    transferDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFragmentFanInTransfer)).setMarkerLabel("Fragment Fan-In Transfer").setScheduling(scheduling).setResourceUses(Span(&transferUse, 1u));
    Core::GpuTaskDesc computeDesc;
    computeDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFragmentFanInCompute)).setMarkerLabel("Fragment Fan-In Compute").setScheduling(scheduling).setResourceUses(Span(&computeUse, 1u));
    Core::GpuTaskDesc consumerDesc;
    consumerDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphFragmentFanInConsumer)).setMarkerLabel("Fragment Fan-In Consumer").setScheduling(scheduling).setResourceUses(Span(&consumerUse, 1u));
    const auto transferTaskResult = graph.addTask(transferDesc, transferCommands);
    ASSERT_TRUE(transferTaskResult);
    const Core::GpuTaskId transferTask = *transferTaskResult;
    const auto computeTaskResult = graph.addTask(computeDesc, computeCommands);
    ASSERT_TRUE(computeTaskResult);
    const Core::GpuTaskId computeTask = *computeTaskResult;
    const auto consumerTaskResult = graph.addTask(consumerDesc, graphicsCommands);
    ASSERT_TRUE(consumerTaskResult);
    const Core::GpuTaskId consumerTask = *consumerTaskResult;
    ASSERT_TRUE(transferTask.valid());
    ASSERT_TRUE(computeTask.valid());
    ASSERT_TRUE(consumerTask.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuCompiledTask* const compiledTransfer = compiledPlan.findTask(transferTask).plan;
    const Core::GpuCompiledTask* const compiledCompute = compiledPlan.findTask(computeTask).plan;
    const Core::GpuCompiledTask* const compiledConsumer = compiledPlan.findTask(consumerTask).plan;
    ASSERT_NE(compiledTransfer, nullptr);
    ASSERT_NE(compiledCompute, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledTransfer->queue, queues[2u].id);
    EXPECT_EQ(compiledCompute->queue, queues[1u].id);
    EXPECT_EQ(compiledConsumer->queue, queues[0u].id);

    const Core::GpuSubmissionPacketId transferPacket = compiledTransfer->packet;
    const Core::GpuSubmissionPacketId computePacket = compiledCompute->packet;
    const Core::GpuSubmissionPacketId consumerPacket = compiledConsumer->packet;
    ASSERT_TRUE(transferPacket.valid());
    ASSERT_TRUE(computePacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 2u);
    const Core::GpuPacketStateSeed* const seeds = compiledPlan.findTask(consumerTask).prologueStateSeeds;
    ASSERT_NE(seeds, nullptr);
    b8 hasTransferSeed = false;
    b8 hasComputeSeed = false;
    for(u32 seedIndex = 0u; seedIndex < compiledConsumer->prologueStateSeedCount; ++seedIndex){
        const Core::GpuPacketStateSeed& seed = seeds[seedIndex];
        hasTransferSeed = hasTransferSeed || (seed.resource == texture && seed.range.textureSubresources == transferRange.textureSubresources && seed.sourcePacket == transferPacket);
        hasComputeSeed = hasComputeSeed || (seed.resource == texture && seed.range.textureSubresources == computeRange.textureSubresources && seed.sourcePacket == computePacket);
        EXPECT_NE(seed.range.textureSubresources, broadRange.textureSubresources);
    }
    EXPECT_TRUE(hasTransferSeed);
    EXPECT_TRUE(hasComputeSeed);

    ASSERT_EQ(compiledPlan.packet(consumerPacket).plan->dependencyCount, 2u);
    const Core::GpuPacketDependency* const dependencies = compiledPlan.packet(consumerPacket).dependencies;
    ASSERT_NE(dependencies, nullptr);
    b8 waitsForTransfer = false;
    b8 waitsForCompute = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < compiledPlan.packet(consumerPacket).plan->dependencyCount; ++dependencyIndex){
        waitsForTransfer = waitsForTransfer || dependencies[dependencyIndex].producer == transferPacket;
        waitsForCompute = waitsForCompute || dependencies[dependencyIndex].producer == computePacket;
    }
    EXPECT_TRUE(waitsForTransfer);
    EXPECT_TRUE(waitsForCompute);

    ASSERT_EQ(compiledTransfer->epilogueBarrierCount, 1u);
    ASSERT_EQ(compiledCompute->epilogueBarrierCount, 1u);
    const Core::GpuCompiledBarrier* const transferRelease = compiledPlan.findTask(transferTask).epilogueBarriers;
    const Core::GpuCompiledBarrier* const computeRelease = compiledPlan.findTask(computeTask).epilogueBarriers;
    ASSERT_NE(transferRelease, nullptr);
    ASSERT_NE(computeRelease, nullptr);
    EXPECT_EQ(transferRelease[0u].type, Core::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(transferRelease[0u].range.textureSubresources, transferRange.textureSubresources);
    EXPECT_EQ(transferRelease[0u].sourceQueue, queues[2u].id);
    EXPECT_EQ(transferRelease[0u].destinationQueue, queues[0u].id);
    EXPECT_EQ(computeRelease[0u].type, Core::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(computeRelease[0u].range.textureSubresources, computeRange.textureSubresources);
    EXPECT_EQ(computeRelease[0u].sourceQueue, queues[1u].id);
    EXPECT_EQ(computeRelease[0u].destinationQueue, queues[0u].id);

    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 5u);
    const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(consumerTask).prologueBarriers;
    ASSERT_NE(barriers, nullptr);
    b8 hasTransferAcquire = false;
    b8 hasComputeAcquire = false;
    b8 hasTransferTransition = false;
    b8 hasComputeTransition = false;
    b8 hasInitialTailTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledConsumer->prologueBarrierCount; ++barrierIndex){
        const Core::GpuCompiledBarrier& barrier = barriers[barrierIndex];
        hasTransferAcquire = hasTransferAcquire || (
            barrier.type == Core::GpuCompiledBarrierType::TextureOwnershipAcquire
            && barrier.range.textureSubresources == transferRange.textureSubresources
            && barrier.before == Core::ResourceStates::CopyDest
            && barrier.sourceQueue == queues[2u].id
            && barrier.destinationQueue == queues[0u].id
        );
        hasComputeAcquire = hasComputeAcquire || (
            barrier.type == Core::GpuCompiledBarrierType::TextureOwnershipAcquire
            && barrier.range.textureSubresources == computeRange.textureSubresources
            && barrier.before == Core::ResourceStates::UnorderedAccess
            && barrier.sourceQueue == queues[1u].id
            && barrier.destinationQueue == queues[0u].id
        );
        hasTransferTransition = hasTransferTransition || (
            barrier.type == Core::GpuCompiledBarrierType::TextureTransition
            && barrier.range.textureSubresources == transferRange.textureSubresources
            && barrier.before == Core::ResourceStates::CopyDest
            && barrier.after == Core::ResourceStates::ShaderResource
            && !barrier.isGraphInitialState
        );
        hasComputeTransition = hasComputeTransition || (
            barrier.type == Core::GpuCompiledBarrierType::TextureTransition
            && barrier.range.textureSubresources == computeRange.textureSubresources
            && barrier.before == Core::ResourceStates::UnorderedAccess
            && barrier.after == Core::ResourceStates::ShaderResource
            && !barrier.isGraphInitialState
        );
        hasInitialTailTransition = hasInitialTailTransition || (
            barrier.type == Core::GpuCompiledBarrierType::TextureTransition
            && barrier.range.textureSubresources == initialTailRange.textureSubresources
            && barrier.before == Core::ResourceStates::Common
            && barrier.after == Core::ResourceStates::ShaderResource
            && barrier.isGraphInitialState
        );
    }
    EXPECT_TRUE(hasTransferAcquire);
    EXPECT_TRUE(hasComputeAcquire);
    EXPECT_TRUE(hasTransferTransition);
    EXPECT_TRUE(hasComputeTransition);
    EXPECT_TRUE(hasInitialTailTransition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

