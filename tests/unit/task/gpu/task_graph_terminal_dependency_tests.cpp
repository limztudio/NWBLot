// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_terminal_dependency_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

namespace TerminalDependencyScenario{
    enum Enum : u8{
        ReadPairs,
        RootedStar,
        Chain,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CheckTerminalDependencies(
    const usize readerCount,
    const TerminalDependencyScenario::Enum scenario,
    const bool lateFallback
){
    ASSERT_GT(readerCount, 0u);
    if(lateFallback){
        ASSERT_EQ(scenario, TerminalDependencyScenario::ReadPairs);
        ASSERT_GE(readerCount, 3u);
    }
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena declarationScratch(Name("tests/terminal_dependency/declarations"));
    Vector<Graphics::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(declarationScratch);
    Vector<Graphics::GpuTaskId, Core::Alloc::ScratchArena> readers(declarationScratch);
    Vector<Graphics::GpuTaskId, Core::Alloc::ScratchArena> finalizers(declarationScratch);
    Vector<Graphics::GpuTaskDiagnosticQueueOverride, Core::Alloc::ScratchArena> overrides(declarationScratch);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const usize pairCount = scenario == TerminalDependencyScenario::ReadPairs ? readerCount : 1u;
    resources.reserve(pairCount * 2u);
    readers.reserve(readerCount);
    finalizers.reserve(pairCount);
    overrides.reserve(readerCount + pairCount);
    for(usize resourceIndex = 0u; resourceIndex < pairCount * 2u; ++resourceIndex){
        char identityText[32u] = {};
        const Name identity = DeriveName(Name("tests/terminal_dependency/resource/"), FormatDecimal(resourceIndex, identityText));
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Terminal Dependency Buffer")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setInitialState(Graphics::ResourceStates::ShaderResource)
                .setExternalFinalState(Graphics::ResourceStates::CopySource)
        );
        ASSERT_TRUE(resource.valid());
        resources.push_back(resource);
    }
    const auto resourceUse = [&](const usize resourceIndex){
        return Graphics::GpuTaskResourceUse{
            .resource = resources[resourceIndex],
            .range = { .bufferRange = Graphics::BufferRange(0u, 64u) },
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        };
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    for(usize readerIndex = 0u; readerIndex < readerCount; ++readerIndex){
        const usize pairIndex = scenario == TerminalDependencyScenario::ReadPairs ? readerIndex : 0u;
        Array<Graphics::GpuTaskResourceUse, 4u> uses{
            resourceUse(pairIndex * 2u), resourceUse(pairIndex * 2u + 1u), {}, {}
        };
        usize useCount = 2u;
        if(lateFallback && readerIndex == 1u){
            uses[2u] = resourceUse((pairCount - 1u) * 2u);
            uses[3u] = resourceUse((pairCount - 1u) * 2u + 1u);
            useCount = uses.size();
        }
        char identityText[32u] = {};
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(DeriveName(Name("tests/terminal_dependency/reader/"), FormatDecimal(readerIndex, identityText)))
            .setMarkerLabel("Independent Terminal Reader")
            .setScheduling(scheduling)
            .setResourceUses(uses.data(), useCount)
        ;
        if(scenario == TerminalDependencyScenario::Chain && !readers.empty())
            desc.setDependencies(&readers.back(), 1u);
        const usize queueIndex = scenario == TerminalDependencyScenario::ReadPairs && !lateFallback ? 0u : readerIndex % 2u;
        const Graphics::GpuTaskId reader = graph.addTask(desc, queueIndex == 0u ? GraphicsCommands() : ComputeCommands());
        ASSERT_TRUE(reader.valid());
        readers.push_back(reader);
        overrides.push_back({ .task = reader, .queue = queues[queueIndex].id });
    }
    for(usize pairIndex = 0u; pairIndex < pairCount; ++pairIndex){
        const Graphics::GpuTaskResourceUse uses[] = { resourceUse(pairIndex * 2u), resourceUse(pairIndex * 2u + 1u) };
        char identityText[32u] = {};
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(DeriveName(Name("tests/terminal_dependency/finalizer/"), FormatDecimal(pairIndex, identityText)))
            .setMarkerLabel("Terminal Finalizing Reader")
            .setScheduling(scheduling)
            .setResourceUses(uses, LengthOf(uses))
        ;
        if(scenario == TerminalDependencyScenario::Chain)
            desc.setDependencies(&readers.back(), 1u);
        else if(lateFallback && pairIndex + 1u == pairCount)
            desc.setDependencies(&finalizers[1u], 1u);
        const usize queueIndex = scenario == TerminalDependencyScenario::ReadPairs
            ? (lateFallback ? (pairIndex + 1u) % 2u : 1u)
            : readerCount % 2u
        ;
        const Graphics::GpuTaskId finalizer = graph.addTask(desc, queueIndex == 0u ? GraphicsCommands() : ComputeCommands());
        ASSERT_TRUE(finalizer.valid());
        finalizers.push_back(finalizer);
        overrides.push_back({ .task = finalizer, .queue = queues[queueIndex].id });
    }

    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(Name("tests/terminal_dependency/compilation"));
    const Graphics::GpuTaskGraphCompiler compiler;
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    Graphics::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    options.queueAssignmentOptions.diagnosticQueueOverrides = overrides.data();
    options.queueAssignmentOptions.diagnosticQueueOverrideCount = overrides.size();
    ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options));

    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_EQ(plan.taskCount(), readers.size() + finalizers.size());
    ASSERT_EQ(plan.packetCount(), readers.size() + finalizers.size());
    const auto& statistics = plan.compileStatistics();
    EXPECT_EQ(statistics.prologueStateSeedCount, 0u);
    EXPECT_EQ(statistics.epilogueBarrierCount, resources.size());
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 0u);
    EXPECT_EQ(statistics.packetDependencyCount, readerCount + (lateFallback ? 1u : 0u));
    for(usize readerIndex = 0u; readerIndex < readers.size(); ++readerIndex){
        const Graphics::GpuSubmissionPacketId packet = plan.packetForTask(readers[readerIndex]);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(packet.index, readerIndex);
        const Graphics::GpuCompiledPacketView view = plan.packet(packet);
        ASSERT_TRUE(view.valid());
        const bool chained = scenario == TerminalDependencyScenario::Chain && readerIndex != 0u;
        ASSERT_EQ(view.plan->dependencyCount, chained ? 1u : 0u);
        if(chained)
            EXPECT_EQ(view.dependencies[0u].producer, plan.packetForTask(readers[readerIndex - 1u]));
    }
    for(usize pairIndex = 0u; pairIndex < finalizers.size(); ++pairIndex){
        const Graphics::GpuSubmissionPacketId packet = plan.packetForTask(finalizers[pairIndex]);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(packet.index, readers.size() + pairIndex);
        const Graphics::GpuCompiledPacketView view = plan.packet(packet);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.plan->recordingFrontier, 0u);
        const bool delayedFallback = lateFallback && pairIndex + 1u == pairCount;
        usize dependencyCount = scenario == TerminalDependencyScenario::RootedStar ? readerCount : 1u;
        if(delayedFallback)
            dependencyCount = 2u;
        ASSERT_EQ(view.plan->dependencyCount, dependencyCount);
        if(delayedFallback){
            EXPECT_EQ(view.dependencies[0u].producer, plan.packetForTask(finalizers[1u]));
            EXPECT_EQ(view.dependencies[1u].producer, plan.packetForTask(readers[pairIndex]));
        }
        else if(scenario == TerminalDependencyScenario::ReadPairs)
            EXPECT_EQ(view.dependencies[0u].producer, plan.packetForTask(readers[pairIndex]));
        else{
            for(usize dependencyIndex = 0u; dependencyIndex < dependencyCount; ++dependencyIndex){
                const Graphics::GpuSubmissionPacketId producer = plan.packetForTask(readers[readerCount - dependencyIndex - 1u]);
                EXPECT_EQ(view.dependencies[dependencyIndex].producer, producer);
            }
        }
        for(usize dependencyIndex = 0u; dependencyIndex < dependencyCount; ++dependencyIndex)
            EXPECT_EQ(view.dependencies[dependencyIndex].consumer, packet);
        const Graphics::GpuCompiledTaskView task = plan.findTask(finalizers[pairIndex]);
        ASSERT_TRUE(task.valid());
        ASSERT_EQ(task.plan->epilogueBarrierCount, 2u);
        for(usize barrierIndex = 0u; barrierIndex < 2u; ++barrierIndex){
            EXPECT_EQ(task.epilogueBarriers[barrierIndex].type, Graphics::GpuCompiledBarrierType::BufferStateExport);
            EXPECT_EQ(task.epilogueBarriers[barrierIndex].resource, resources[pairIndex * 2u + barrierIndex]);
        }
    }

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphTerminalDependencies, PreservesDuplicatePairOrderAndLateIndirectReachability){
    using namespace __hidden_task_graph_terminal_dependency_tests;
    for(const usize readerCount : { 3u, 17u }){
        CheckTerminalDependencies(readerCount, TerminalDependencyScenario::ReadPairs, true);
        CheckTerminalDependencies(readerCount, TerminalDependencyScenario::RootedStar, false);
        CheckTerminalDependencies(readerCount, TerminalDependencyScenario::Chain, false);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

