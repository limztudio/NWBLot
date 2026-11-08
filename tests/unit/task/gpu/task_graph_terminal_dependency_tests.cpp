// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


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


static void CheckTerminalDependencies(const usize readerCount, const TerminalDependencyScenario::Enum scenario, const b8 lateFallback){
    ASSERT_GT(readerCount, 0u);
    if(lateFallback){
        ASSERT_EQ(scenario, TerminalDependencyScenario::ReadPairs);
        ASSERT_GE(readerCount, 3u);
    }
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena declarationScratch{Name(TaskGraphTestUtils::s_GpuCaseTerminalDependencyDeclarations)};
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(declarationScratch);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> readers(declarationScratch);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> finalizers(declarationScratch);
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue() };
    const usize pairCount = scenario == TerminalDependencyScenario::ReadPairs ? readerCount : 1u;
    resources.reserve(pairCount * 2u);
    readers.reserve(readerCount);
    finalizers.reserve(pairCount);
    for(usize resourceIndex = 0u; resourceIndex < pairCount * 2u; ++resourceIndex){
        const auto identityNameResult = IndexedName(Name(TaskGraphTestUtils::s_GpuCaseTerminalDependencyResource), resourceIndex);
        ASSERT_TRUE(identityNameResult);
        const Name identity = *identityNameResult;
        const auto resourceResult = graph.importResource(
            Core::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Terminal Dependency Buffer")
                .setType(Core::GpuGraphResourceType::Buffer)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setInitialState(Core::ResourceStates::ShaderResource)
                .setExternalFinalState(Core::ResourceStates::CopySource)
        );
        ASSERT_TRUE(resourceResult);
        const Core::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());
        resources.push_back(resource);
    }
    const auto resourceUse = [&](const usize resourceIndex)noexcept{
        return Core::GpuTaskResourceUse{
            .resource = resources[resourceIndex],
            .range = { .bufferRange = Core::BufferRange(0u, 64u) },
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        };
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    for(usize readerIndex = 0u; readerIndex < readerCount; ++readerIndex){
        const usize pairIndex = scenario == TerminalDependencyScenario::ReadPairs ? readerIndex : 0u;
        Array<Core::GpuTaskResourceUse, 4u> uses{ resourceUse(pairIndex * 2u), resourceUse(pairIndex * 2u + 1u), {}, {} };
        usize useCount = 2u;
        if(lateFallback && readerIndex == 1u){
            uses[2u] = resourceUse((pairCount - 1u) * 2u);
            uses[3u] = resourceUse((pairCount - 1u) * 2u + 1u);
            useCount = uses.size();
        }
        Core::GpuTaskDesc desc;
        const auto readerNameResult = IndexedName(Name(TaskGraphTestUtils::s_GpuCaseTerminalDependencyReader), readerIndex);
        ASSERT_TRUE(readerNameResult);
        desc.setIdentity(*readerNameResult).setMarkerLabel("Independent Terminal Reader").setScheduling(scheduling).setResourceUses(Span(uses.data(), useCount));
        if(scenario == TerminalDependencyScenario::Chain && !readers.empty())
            desc.setDependencies(Span(&readers.back(), 1u));
        const usize queueIndex = scenario == TerminalDependencyScenario::ReadPairs && !lateFallback ? 0u : readerIndex % 2u;
        const auto readerResult = graph.addTask(desc, queueIndex == 0u ? GraphicsCommands() : ComputeCommands());
        ASSERT_TRUE(readerResult);
        const Core::GpuTaskId reader = *readerResult;
        ASSERT_TRUE(reader.valid());
        readers.push_back(reader);
    }
    for(usize pairIndex = 0u; pairIndex < pairCount; ++pairIndex){
        const Array<Core::GpuTaskResourceUse, 2u> uses = { resourceUse(pairIndex * 2u), resourceUse(pairIndex * 2u + 1u) };
        Core::GpuTaskDesc desc;
        const auto secondReaderNameResult = IndexedName(Name(TaskGraphTestUtils::s_GpuCaseTerminalDependencyFinalizer), pairIndex);
        ASSERT_TRUE(secondReaderNameResult);
        desc.setIdentity(*secondReaderNameResult).setMarkerLabel("Terminal Finalizing Reader").setScheduling(scheduling).setResourceUses(uses);
        if(scenario == TerminalDependencyScenario::Chain)
            desc.setDependencies(Span(&readers.back(), 1u));
        else if(lateFallback && pairIndex + 1u == pairCount)
            desc.setDependencies(Span(&finalizers[1u], 1u));
        const usize queueIndex = scenario == TerminalDependencyScenario::ReadPairs ? (lateFallback ? (pairIndex + 1u) % 2u : 1u) : readerCount % 2u;
        const auto finalizerResult = graph.addTask(desc, queueIndex == 0u ? GraphicsCommands() : ComputeCommands());
        ASSERT_TRUE(finalizerResult);
        const Core::GpuTaskId finalizer = *finalizerResult;
        ASSERT_TRUE(finalizer.valid());
        finalizers.push_back(finalizer);
    }

    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::Alloc::ScratchArena scratchArena{Name(TaskGraphTestUtils::s_GpuCaseTerminalDependencyCompilation)};
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, scratchArena, graph, topology));

    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_EQ(plan.taskCount(), readers.size() + finalizers.size());
    ASSERT_EQ(plan.packetCount(), readers.size() + finalizers.size());
    const auto& statistics = plan.compileStatistics();
    EXPECT_EQ(statistics.prologueStateSeedCount, 0u);
    EXPECT_EQ(statistics.epilogueBarrierCount, resources.size());
    EXPECT_EQ(statistics.logicalOwnershipTransferCount, 0u);
    EXPECT_EQ(statistics.packetDependencyCount, readerCount + (lateFallback ? 1u : 0u));
    for(usize readerIndex = 0u; readerIndex < readers.size(); ++readerIndex){
        const Core::GpuSubmissionPacketId packet = plan.packetForTask(readers[readerIndex]);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(packet.index, readerIndex);
        const Core::GpuCompiledPacketView view = plan.packet(packet);
        ASSERT_TRUE(view.valid());
        const b8 chained = scenario == TerminalDependencyScenario::Chain && readerIndex != 0u;
        ASSERT_EQ(view.plan->dependencyCount, chained ? 1u : 0u);
        if(chained)
            EXPECT_EQ(view.dependencies[0u].producer, plan.packetForTask(readers[readerIndex - 1u]));
    }
    for(usize pairIndex = 0u; pairIndex < finalizers.size(); ++pairIndex){
        const Core::GpuSubmissionPacketId packet = plan.packetForTask(finalizers[pairIndex]);
        ASSERT_TRUE(packet.valid());
        EXPECT_EQ(packet.index, readers.size() + pairIndex);
        const Core::GpuCompiledPacketView view = plan.packet(packet);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.plan->recordingFrontier, 0u);
        const b8 delayedFallback = lateFallback && pairIndex + 1u == pairCount;
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
                const Core::GpuSubmissionPacketId producer = plan.packetForTask(readers[readerCount - dependencyIndex - 1u]);
                EXPECT_EQ(view.dependencies[dependencyIndex].producer, producer);
            }
        }
        for(usize dependencyIndex = 0u; dependencyIndex < dependencyCount; ++dependencyIndex)
            EXPECT_EQ(view.dependencies[dependencyIndex].consumer, packet);
        const Core::GpuCompiledTaskView task = plan.findTask(finalizers[pairIndex]);
        ASSERT_TRUE(task.valid());
        ASSERT_EQ(task.plan->epilogueBarrierCount, 2u);
        for(usize barrierIndex = 0u; barrierIndex < 2u; ++barrierIndex){
            EXPECT_EQ(task.epilogueBarriers[barrierIndex].type, Core::GpuCompiledBarrierType::BufferStateExport);
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
        ASSERT_NO_FATAL_FAILURE(CheckTerminalDependencies(readerCount, TerminalDependencyScenario::ReadPairs, true));
        ASSERT_NO_FATAL_FAILURE(CheckTerminalDependencies(readerCount, TerminalDependencyScenario::RootedStar, false));
        ASSERT_NO_FATAL_FAILURE(CheckTerminalDependencies(readerCount, TerminalDependencyScenario::Chain, false));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

