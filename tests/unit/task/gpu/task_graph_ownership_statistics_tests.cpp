// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


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


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void CheckOwnershipStatistics(const usize resourceCount, const bool benchmark){
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
    if(benchmark){
        u64 minimumAnalysisNanoseconds = Limit<u64>::s_Max;
        for(usize iteration = 0u; iteration < 4u; ++iteration){
            const Timer begin = TimerNow();
            ASSERT_TRUE(Analyze(graph, compilation.analysis));
            if(iteration != 0u)
                minimumAnalysisNanoseconds = Min(minimumAnalysisNanoseconds, DurationInNS<u64>(TimerNow(), begin));
        }
        RecordUnsignedProperty(NotNull<const char*>{ "ownership_analysis_ns" }, minimumAnalysisNanoseconds);
        Core::Alloc::ScratchArena analysisScratch(s_TaskGraphScratchArena);
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphCompiler compiler;
        ASSERT_TRUE(compiler.analyze(declarations, compilation.analysis, analysisScratch));
        RecordUnsignedProperty(
            NotNull<const char*>{ "ownership_analysis_scratch_bytes" }, analysisScratch.memoryStats().peakUsedBytes
        );
    }
    u64 minimumCompileNanoseconds = Limit<u64>::s_Max;
    u64 minimumQueryNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < (benchmark ? 4u : 1u); ++iteration){
        const Timer compileBegin = benchmark ? TimerNow() : Timer{};
        ASSERT_TRUE(compilation.compile(graph));
        if(benchmark && iteration != 0u)
            minimumCompileNanoseconds = Min(minimumCompileNanoseconds, DurationInNS<u64>(TimerNow(), compileBegin));
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
        const Timer queryBegin = benchmark ? TimerNow() : Timer{};
        const auto graphics = plan.physicalQueueCompileStatistics(compilation.graphicsQueue.id);
        const auto compute = plan.physicalQueueCompileStatistics(compilation.computeQueue.id);
        const auto idle = plan.physicalQueueCompileStatistics(compilation.transferQueue.id);
        if(benchmark && iteration != 0u)
            minimumQueryNanoseconds = Min(minimumQueryNanoseconds, DurationInNS<u64>(TimerNow(), queryBegin));
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
    if(benchmark){
        RecordUnsignedProperty(NotNull<const char*>{ "ownership_resource_count" }, resourceCount);
        RecordUnsignedProperty(NotNull<const char*>{ "ownership_compile_ns" }, minimumCompileNanoseconds);
        RecordUnsignedProperty(NotNull<const char*>{ "ownership_queue_queries_ns" }, minimumQueryNanoseconds);
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
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(3u, false);
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(17u, false);
}

TEST(GpuTaskGraphOwnershipStatistics, DISABLED_Benchmark8Resources){
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(8u, true);
}

TEST(GpuTaskGraphOwnershipStatistics, DISABLED_Benchmark256Resources){
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(256u, true);
}

TEST(GpuTaskGraphOwnershipStatistics, DISABLED_Benchmark1024Resources){
    __hidden_task_graph_ownership_statistics_tests::CheckOwnershipStatistics(1024u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

