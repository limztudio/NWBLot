// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_external_final_transition_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, KeepsSameStateExternalExportConcurrentReadersIndependent){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Graphics::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    EXPECT_EQ(compiledPlan.packet(earlierPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(compiledPlan.packet(finalizingPacket).plan->dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::ShaderResource);
}

TEST(GpuTaskGraph, KeepsDisjointExternalFinalTextureFragmentsIndependent){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuTaskResourceRange{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), },
        Graphics::GpuTaskResourceRange{ .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u), },
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Graphics::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(finalizingPacket.valid());
    ASSERT_NE(earlierPacket, finalizingPacket);
    EXPECT_EQ(compiledPlan.packet(earlierPacket).plan->dependencyCount, 0u);
    EXPECT_EQ(compiledPlan.packet(finalizingPacket).plan->dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const earlierTask = compiledPlan.findTask(pair.earlierReader).plan;
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(earlierTask, nullptr);
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(earlierTask->epilogueBarrierCount, 1u);
    EXPECT_EQ(finalizingTask->epilogueBarrierCount, 1u);
}

TEST(GpuTaskGraph, ElidesSamePacketExternalFinalTransitionSelfDependency){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Graphics::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pair.earlierReader);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(pair.finalizingReader), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->dependencyCount, 0u);
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    EXPECT_EQ(finalizingTask->epilogueBarrierCount, 1u);
}

TEST(GpuTaskGraph, OrdersExternalFinalTransitionsForWholeAllocationResources){
    struct WholeAllocationCase{
        Graphics::GpuGraphResourceType::Enum type;
        Graphics::ResourceStates::Mask readState;
        Graphics::ResourceStates::Mask finalState;
    };
    const WholeAllocationCase testCases[] = {
        { Graphics::GpuGraphResourceType::Buffer, Graphics::ResourceStates::ShaderResource, Graphics::ResourceStates::CopySource, },
        { Graphics::GpuGraphResourceType::AccelStruct, Graphics::ResourceStates::AccelStructRead, Graphics::ResourceStates::Common, },
    };

    for(const WholeAllocationCase& testCase : testCases){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto pairResult = AddExternalFinalReadPair(graph, testCase.type, {}, {}, testCase.readState, testCase.finalState, Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute, false, false, {});
        ASSERT_TRUE(pairResult);
        const ExternalFinalReadPair pair = *pairResult;
        ASSERT_TRUE(pair.resource.valid());
        ASSERT_TRUE(pair.earlierReader.valid());
        ASSERT_TRUE(pair.finalizingReader.valid());

        TwoQueueCompile twoQueueCompile(testArena);
        ASSERT_TRUE(twoQueueCompile.compile(graph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
        Graphics::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

        EXPECT_EQ(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);

        const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
        const Graphics::GpuSubmissionPacketId finalizingPacket = compiledPlan.packetForTask(pair.finalizingReader);
        ASSERT_TRUE(earlierPacket.valid());
        ASSERT_TRUE(finalizingPacket.valid());
        ASSERT_NE(earlierPacket, finalizingPacket);
        const Graphics::GpuSubmissionPacket& finalizingPacketInfo = *compiledPlan.packet(finalizingPacket).plan;
        ASSERT_EQ(finalizingPacketInfo.dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(finalizingPacket).dependencies;
        ASSERT_NE(dependencies, nullptr);
        EXPECT_EQ(dependencies[0u].producer, earlierPacket);
        EXPECT_EQ(dependencies[0u].consumer, finalizingPacket);

        const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
        ASSERT_NE(finalizingTask, nullptr);
        ASSERT_EQ(finalizingTask->epilogueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
        ASSERT_NE(barriers, nullptr);
        EXPECT_EQ(barriers[0u].before, testCase.readState);
        EXPECT_EQ(barriers[0u].after, testCase.finalState);
    }
}

TEST(GpuTaskGraph, ElidesSamePacketReleaseOnlyExternalFinalizationSelfDependency){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::Exclusive,
        true,
        false,
        queues[1u].id
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(pair.earlierReader);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(pair.finalizingReader), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->dependencyCount, 0u);
    const Graphics::GpuCompiledTask* const finalizingTask = compiledPlan.findTask(pair.finalizingReader).plan;
    ASSERT_NE(finalizingTask, nullptr);
    ASSERT_EQ(finalizingTask->epilogueBarrierCount, s_ExpectedDualCount);
    const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(pair.finalizingReader).epilogueBarriers;
    ASSERT_NE(barriers, nullptr);
    EXPECT_EQ(barriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(barriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[0u].after, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(barriers[1u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(barriers[1u].destinationQueue, queues[1u].id);
}

TEST(GpuTaskGraph, AvoidsTransitiveExternalFinalizationPacketDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const auto pairResult = AddExternalFinalReadPair(
        graph,
        Graphics::GpuGraphResourceType::Texture,
        range,
        range,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        true,
        {}
    );
    ASSERT_TRUE(pairResult);
    const ExternalFinalReadPair pair = *pairResult;
    ASSERT_TRUE(pair.resource.valid());
    ASSERT_TRUE(pair.earlierReader.valid());
    ASSERT_TRUE(pair.finalizingReader.valid());

    const Graphics::GpuTaskResourceUse terminalUse{ .resource = pair.resource, .range = range, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, .hasIndependentStateSource = true, };
    Graphics::GpuTaskSchedulingHint terminalScheduling;
    terminalScheduling.forceSubmissionBoundary = true;
    terminalScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc terminalDesc;
    terminalDesc
        .setIdentity(Name("tests/task_graph/external_final_transitive_terminal_reader"))
        .setMarkerLabel("External Final Transitive Terminal Reader")
        .setScheduling(terminalScheduling)
        .setDependencies(&pair.finalizingReader, 1u)
        .setResourceUses(&terminalUse, 1u)
    ;
    const auto terminalReaderResult = graph.addTask(terminalDesc, Graphics::GpuTaskCommandRequirements{ Graphics::GpuQueueCapability::Graphics });
    ASSERT_TRUE(terminalReaderResult);
    const Graphics::GpuTaskId terminalReader = *terminalReaderResult;
    ASSERT_TRUE(terminalReader.valid());

    TwoQueueCompile twoQueueCompile(testArena);
    ASSERT_TRUE(twoQueueCompile.compile(graph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);
    Graphics::GpuTaskGraphAnalysis& analysis = twoQueueCompile.analysis;

    EXPECT_NE(FindEdge(analysis, pair.earlierReader, pair.finalizingReader), nullptr);
    EXPECT_NE(FindEdge(analysis, pair.finalizingReader, terminalReader), nullptr);
    EXPECT_EQ(FindEdge(analysis, pair.earlierReader, terminalReader), nullptr);

    const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
    const Graphics::GpuSubmissionPacketId middlePacket = compiledPlan.packetForTask(pair.finalizingReader);
    const Graphics::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalReader);
    ASSERT_TRUE(earlierPacket.valid());
    ASSERT_TRUE(middlePacket.valid());
    ASSERT_TRUE(terminalPacket.valid());
    ASSERT_EQ(compiledPlan.packet(middlePacket).plan->dependencyCount, 1u);
    ASSERT_EQ(compiledPlan.packet(terminalPacket).plan->dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const middleDependencies = compiledPlan.packet(middlePacket).dependencies;
    const Graphics::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(terminalPacket).dependencies;
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

