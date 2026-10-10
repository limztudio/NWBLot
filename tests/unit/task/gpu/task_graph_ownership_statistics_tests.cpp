// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_ownership_statistics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CheckOwnershipStatistics(const usize resourceCount){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena declarationScratch(s_TaskGraphScratchArena);
    Vector<Graphics::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{ declarationScratch };
    uses.reserve(resourceCount * 2u);
    for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/ownership_statistics/resource/"), FormatDecimal(resourceIndex, identityText));
        const Graphics::GpuGraphResourceId resource = AddTextureMetadata(
            graph,
            identity,
            "Ownership Statistics Texture",
            Graphics::ResourceStates::Common,
            Graphics::ResourceQueueSharing::Exclusive
        );
        ASSERT_TRUE(resource.valid());
        for(u32 rangeIndex = 0u; rangeIndex < 2u; ++rangeIndex){
            uses.push_back({
                .resource = resource,
                .range = { .textureSubresources = Graphics::TextureSubresourceSet(rangeIndex * 2u, 1u, 0u, 1u) },
                .requiredState = Graphics::ResourceStates::CopySource,
                .access = Graphics::GpuTaskResourceAccess::Write,
            });
        }
    }
    for(usize taskIndex = 0u; taskIndex < 3u; ++taskIndex){
        char identityText[32u] = {};
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(DeriveName(Name("tests/ownership_statistics/task/"), FormatDecimal(taskIndex, identityText)))
            .setMarkerLabel("Ownership Statistics Task")
            .setResourceUses(uses.data(), uses.size())
        ;
        const auto commands = taskIndex == 1u ? ComputeCommands() : GraphicsCommands();
        ASSERT_TRUE(graph.addTask(desc, commands).valid());
    }
    ThreeQueueCompile compilation(testArena);

    {
        ASSERT_TRUE(compilation.compile(graph));
        ASSERT_EQ(compilation.analysis.inferredEdges().size(), resourceCount * 2u);
        for(usize edgeIndex = 0u; edgeIndex < compilation.analysis.inferredEdges().size(); ++edgeIndex){
            const auto& edge = compilation.analysis.inferredEdges()[edgeIndex];
            EXPECT_EQ(edge.producer.index, edgeIndex / resourceCount);
            EXPECT_EQ(edge.consumer.index, edgeIndex / resourceCount + 1u);
            EXPECT_EQ(edge.resource.index, edgeIndex % resourceCount);
            EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::WriteAfterWrite);
        }
        const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
        const auto& statistics = plan.compileStatistics();
        EXPECT_EQ(statistics.logicalOwnershipTransferCount, resourceCount * 4u);
        EXPECT_EQ(statistics.logicalOwnershipTransferSignatureCount, resourceCount * 2u);
        EXPECT_EQ(statistics.repeatedOwnershipTransferSignatureCount, resourceCount);
        EXPECT_EQ(statistics.concurrentSharingCouldAvoidTransferCount, resourceCount * 4u);
        EXPECT_EQ(statistics.concurrentSharingAdviceResourceCount, resourceCount);
        const auto graphics = plan.physicalQueueCompileStatistics(compilation.graphicsQueue.id);
        const auto compute = plan.physicalQueueCompileStatistics(compilation.computeQueue.id);
        const auto idle = plan.physicalQueueCompileStatistics(compilation.transferQueue.id);
        EXPECT_EQ(graphics.taskCount, 2u);
        EXPECT_EQ(compute.taskCount, 1u);
        EXPECT_TRUE(idle.valid());
        EXPECT_EQ(idle.taskCount, 0u);
        EXPECT_EQ(idle.incomingLogicalOwnershipTransferCount, 0u);
        EXPECT_EQ(idle.outgoingLogicalOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(idle.concurrentSharingAdviceResourceCount, 0u);
        EXPECT_FALSE(plan.physicalQueueCompileStatistics({ .index = 0u, .deviceGeneration = 2u }).valid());
        EXPECT_FALSE(plan.physicalQueueCompileStatistics({ .index = 17u, .deviceGeneration = 1u }).valid());
        for(const auto& queue : { graphics, compute }){
            EXPECT_EQ(queue.incomingLogicalOwnershipTransferCount, resourceCount * 2u);
            EXPECT_EQ(queue.outgoingLogicalOwnershipTransferCount, resourceCount * 2u);
            EXPECT_EQ(queue.incomingLogicalOwnershipTransferSignatureCount, resourceCount);
            EXPECT_EQ(queue.outgoingLogicalOwnershipTransferSignatureCount, resourceCount);
            EXPECT_EQ(queue.ownershipReleaseBarrierCount, resourceCount * 2u);
            EXPECT_EQ(queue.ownershipAcquireBarrierCount, resourceCount * 2u);
            EXPECT_EQ(queue.concurrentSharingAdviceResourceCount, resourceCount);
        }
        EXPECT_EQ(graphics.incomingRepeatedOwnershipTransferSignatureCount, resourceCount);
        EXPECT_EQ(graphics.outgoingRepeatedOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(compute.incomingRepeatedOwnershipTransferSignatureCount, 0u);
        EXPECT_EQ(compute.outgoingRepeatedOwnershipTransferSignatureCount, resourceCount);
    }

    compilation.compiledGraph.reset();
    {
        const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
        EXPECT_FALSE(plan.physicalQueueCompileStatistics(compilation.graphicsQueue.id).valid());
    }
    ASSERT_TRUE(compilation.compile(graph));
    const Graphics::GpuPhysicalQueueTopology invalidTopology{};
    EXPECT_FALSE(Compile(graph, compilation.analysis, invalidTopology, compilation.assignments, compilation.compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView plan(compilation.compiledGraph);
    EXPECT_FALSE(plan.physicalQueueCompileStatistics(compilation.graphicsQueue.id).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, CountsRepeatedOwnershipSignaturesAcrossResourcesAndSubranges){
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(3u);
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(17u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

