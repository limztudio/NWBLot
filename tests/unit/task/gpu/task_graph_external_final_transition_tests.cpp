// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_final_transition_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsSameStateExternalExportConcurrentReadersIndependent){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Core::GpuGraphResourceType::Texture,
        range,
        range,
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::ShaderResource,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        false,
        {}
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Core::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Core::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Core::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    EXPECT_EQ(compiledPlan.packet(earlierPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(compiledPlan.packet(finalizingPacket).plan->dependencyCount, 0u);

    const Core::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
    const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].before, Core::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Core::ResourceStates::ShaderResource);
}

TEST(GpuTaskGraph, KeepsDisjointExternalFinalTextureFragmentsIndependent){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Core::GpuGraphResourceType::Texture,
        Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), },
        Core::GpuTaskResourceRange{ .textureSubresources = Core::TextureSubresourceSet(1u, 1u, 0u, 1u), },
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::CopySource,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        false,
        {}
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Core::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Core::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Core::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    EXPECT_EQ(compiledPlan.packet(earlierPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(compiledPlan.packet(finalizingPacket).plan->dependencyCount, 0u);

    const Core::GpuCompiledTask* const earlierTask = compiledPlan.findTask(pair.earlierReader).plan;
    const Core::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(earlierTask, nullptr);
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(earlierTask->epilogueBarrierCount, 1u);
    EXPECT_EQ(finalizingTask->epilogueBarrierCount, 1u);
}

TEST(GpuTaskGraph, ElidesSamePacketExternalFinalTransitionSelfDependency){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Core::GpuGraphResourceType::Texture,
        range,
        range,
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::CopySource,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        true,
        false,
        {}
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Core::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Core::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pair.earlierReader);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(pair.finalizingReader), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->dependencyCount, 0u);
    const Core::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(finalizingTask->epilogueBarrierCount, 1u);
}

TEST(GpuTaskGraph, OrdersExternalFinalTransitionsForWholeAllocationResources){
    struct WholeAllocationCase{
        Core::GpuGraphResourceType::Enum type;
        Core::ResourceStates::Mask readState;
        Core::ResourceStates::Mask finalState;
    };
    const Array<WholeAllocationCase, 2> testCases = {
        WholeAllocationCase{ Core::GpuGraphResourceType::Buffer, Core::ResourceStates::ShaderResource, Core::ResourceStates::CopySource, },
        WholeAllocationCase{ Core::GpuGraphResourceType::AccelStruct, Core::ResourceStates::AccelStructRead, Core::ResourceStates::Common, },
    };

    for(const WholeAllocationCase& testCase : testCases){
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto pairResult = AddExternalFinalReadPair(graph, testCase.type, {}, {}, testCase.readState, testCase.finalState, Core::ResourceQueueSharing::GraphicsAndAsyncCompute, false, false, {});
        ASSERT_TRUE(pairResult);
        const ExternalFinalReadPair pair = *pairResult;
        ASSERT_TRUE(pair.resource.valid());
        ASSERT_TRUE(pair.earlierReader.valid());
        ASSERT_TRUE(pair.finalizingReader.valid());

        TwoQueueCompile twoQueueCompile(testArena);
        ASSERT_TRUE(twoQueueCompile.compile(graph));
        const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
        Core::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

        EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

        const Core::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
        const Core::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
        ASSERT_TRUE(earlierPacket.valid());
        ASSERT_TRUE(finalizingPacket.valid());
        ASSERT_NE(earlierPacket, finalizingPacket);
        const Core::GpuSubmissionPacket& finalizingPacketInfo = *compiledPlan.packet(finalizingPacket).plan;
        ASSERT_EQ(finalizingPacketInfo.dependencyCount, 1u);
        const Core::GpuPacketDependency* const dependencies = compiledPlan.packet(finalizingPacket).dependencies;
        ASSERT_NE(dependencies, nullptr);
        EXPECT_EQ(dependencies[0u].producer, earlierPacket);
        EXPECT_EQ(dependencies[0u].consumer, finalizingPacket);

        const Core::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
        ASSERT_NE(finalizingTask, nullptr);
        ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
        const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
        ASSERT_NE(barriers, nullptr);
        EXPECT_EQ(barriers[0u].before, testCase.readState);
        EXPECT_EQ(barriers[0u].after, testCase.finalState);
    }
}

TEST(GpuTaskGraph, ElidesSamePacketReleaseOnlyExternalFinalizationSelfDependency){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Core::GpuGraphResourceType::Texture,
        range,
        range,
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::ShaderResource,
        Core::ResourceQueueSharing::Exclusive,
        true,
        false,
        queues[1u].id
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pair.earlierReader);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(pair.finalizingReader), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->dependencyCount, 0u);
    const Core::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 2u);
    const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Core::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(barriers[0u].before, Core::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Core::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[1u].type, Core::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(barriers[1u].destinationQueue, queues[1u].id);
}

TEST(GpuTaskGraph, AvoidsTransitiveExternalFinalizationPacketDependencies){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Core::GpuGraphResourceType::Texture,
        range,
        range,
        Core::ResourceStates::ShaderResource,
        Core::ResourceStates::CopySource,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        true,
        {}
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Core::GpuTaskResourceUse terminalUse{ .resource = pair.resource, .range = range, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, .hasIndependentStateSource = true, };
    Core::GpuTaskSchedulingHint terminalScheduling;
    terminalScheduling.forceSubmissionBoundary = true;
    terminalScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc terminalDesc;
    terminalDesc
        .setIdentity(Name("tests/task_graph/external_final_transitive_terminal_reader"))
        .setMarkerLabel("External Final Transitive Terminal Reader")
        .setScheduling(terminalScheduling)
        .setDependencies(Span(&pair.finalizingReader, 1u))
        .setResourceUses(Span(&terminalUse, 1u))
    ;
    const auto terminalReaderResult = graph.addTask(terminalDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics });
    ASSERT_TRUE(terminalReaderResult);
    const Core::GpuTaskId terminalReader = *terminalReaderResult;
    ASSERT_TRUE(terminalReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Core::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_NE(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);
    EXPECT_NE(FindEdge(analysis, pair.finalizingReader, terminalReader), nullptr);
    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, terminalReader), nullptr);

    const Core::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Core::GpuSubmissionPacketId middlePacket = compiledPlan.packetForTask(pair.finalizingReader);
    const Core::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(middlePacket.valid());
    ASSERT_TRUE(terminalPacket.valid());
    ASSERT_EQ(compiledPlan.packet(middlePacket).plan->dependencyCount, 1u);
    ASSERT_EQ(compiledPlan.packet(terminalPacket).plan->dependencyCount, 1u);
    const Core::GpuPacketDependency* const middleDependencies = compiledPlan.packet(middlePacket).dependencies;
    const Core::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(terminalPacket).dependencies;
    ASSERT_NE(middleDependencies, nullptr);
    ASSERT_NE(terminalDependencies, nullptr);
    EXPECT_EQ(middleDependencies[0u].producer, earlierPacket);
    EXPECT_EQ(terminalDependencies[0u].producer, middlePacket);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

