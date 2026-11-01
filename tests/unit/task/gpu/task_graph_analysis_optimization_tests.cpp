// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_analysis_optimization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BuildVersionChain(Core::GpuTaskGraph& graph, const Core::GpuGraphResourceVersionOrigin::Enum origin, const usize taskCount, const b8 disjointClobber, const b8 overlappingClobber){
    ASSERT_GE(taskCount, overlappingClobber ? 3u : 2u);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/analysis_optimization/version_buffer"), "Analysis Version Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    const Core::GpuTaskResourceRange versionRange{ .bufferRange = Core::BufferRange(0u, 64u) };
    const auto versionResult = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(buffer).setRange(versionRange).setOrigin(origin));
    ASSERT_TRUE(versionResult);
    const Core::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(version.valid());
    const b8 produced = origin == Core::GpuGraphResourceVersionOrigin::TaskProduced;
    Core::GpuTaskId previous;
    for(usize index = 0u; index < taskCount; ++index){
        Core::GpuTaskResourceUse physicalUse;
        Core::GpuTaskResourceVersionUse versionUse;
        const b8 usesVersion = index == 0u || (produced && index == 1u);
        const b8 writesVersion = produced && index == 0u;
        if(usesVersion){
            physicalUse.resource = buffer;
            physicalUse.range = versionRange;
            physicalUse.requiredState = writesVersion ? Core::ResourceStates::UnorderedAccess : Core::ResourceStates::ShaderResource;
            physicalUse.access = writesVersion ? Core::GpuTaskResourceAccess::Write : Core::GpuTaskResourceAccess::Read;
            versionUse.version = version;
            versionUse.role = writesVersion ? Core::GpuTaskResourceVersionRole::Produce : Core::GpuTaskResourceVersionRole::Consume;
        }
        else if((disjointClobber || overlappingClobber) && index + 1u == taskCount){
            physicalUse.resource = buffer;
            physicalUse.range.bufferRange = Core::BufferRange(disjointClobber ? 128u : 0u, 64u);
            physicalUse.requiredState = Core::ResourceStates::UnorderedAccess;
            physicalUse.access = Core::GpuTaskResourceAccess::Write;
        }
        Core::GpuTaskDesc desc;
        const auto derivedNameResult = IndexedName(Name("tests/analysis_optimization/version_task"), index);
        ASSERT_TRUE(derivedNameResult);
        desc.setIdentity(*derivedNameResult).setMarkerLabel("Analysis Version Task");
        if(previous.valid())
            desc.setDependencies(Span(&previous, 1u));
        if(physicalUse.resource.valid())
            desc.setResourceUses(Span(&physicalUse, 1u));
        if(usesVersion)
            desc.setResourceVersionUses(Span(&versionUse, 1u));
        const Core::GpuTaskCommandRequirements commands{ Core::GpuQueueCapability::Compute };
        const auto previouTaskAdmission = graph.addTask(desc, commands);
        ASSERT_TRUE(previouTaskAdmission);
        previous = *previouTaskAdmission;
        ASSERT_TRUE(previous.valid());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphAnalysis, ResourceVersionsWithoutOverlappingClobbersPreserveStableOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const Array<Core::GpuGraphResourceVersionOrigin::Enum, 2> origins = { Core::GpuGraphResourceVersionOrigin::ImportedRoot, Core::GpuGraphResourceVersionOrigin::TaskProduced, };
    for(const Core::GpuGraphResourceVersionOrigin::Enum origin : origins){
        SCOPED_TRACE(origin);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        ASSERT_NO_FATAL_FAILURE(BuildVersionChain(graph, origin, 8u, true, false));
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        ASSERT_EQ(analysis.topologicalOrder().size(), 8u);
        EXPECT_EQ(analysis.resourceVersionEdgeCount(), origin == Core::GpuGraphResourceVersionOrigin::TaskProduced ? 1u : 0u);
        for(usize index = 0u; index < analysis.topologicalOrder().size(); ++index)
            EXPECT_EQ(analysis.topologicalOrder()[index].index, index);
        for(const Core::GpuTaskDependencyEdge& edge : analysis.inferredEdges())
            EXPECT_NE(edge.hazard, Core::GpuTaskHazardType::VersionLifetime);
    }
}

TEST(GpuTaskGraphAnalysis, VersionLifetimeConstraintsDoNotInventProducedVersionOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const Array<b8, 2> reversedDeclarations = { false, true };
    for(const b8 reversed : reversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto bufferResult = AddBufferMetadata(graph, Name("tests/analysis_optimization/ambiguous_version_buffer"), "Ambiguous Version Buffer");
        ASSERT_TRUE(bufferResult);
        const Core::GpuGraphResourceId buffer = *bufferResult;
        ASSERT_TRUE(buffer.valid());
        const Core::GpuTaskResourceRange range{ .bufferRange = Core::BufferRange(0u, 64u) };
        Array<Core::GpuGraphResourceVersionId, 2u> versions{};
        for(usize index = 0u; index < LengthOf(versions); ++index){
            const auto versionTaskAdmission = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(buffer).setRange(range).setOrigin(Core::GpuGraphResourceVersionOrigin::TaskProduced));
            ASSERT_TRUE(versionTaskAdmission);
            versions[reversed ? 1u - index : index] = *versionTaskAdmission;
            ASSERT_TRUE(versions[reversed ? 1u - index : index].valid());
        }
        Array<Core::GpuTaskId, 4u> tasks{};
        for(usize index = 0u; index < LengthOf(tasks); ++index){
            const b8 producer = index < LengthOf(versions);
            const Core::GpuTaskResourceUse physicalUse{
                .resource = buffer,
                .range = range,
                .requiredState = producer ? Core::ResourceStates::UnorderedAccess : Core::ResourceStates::ShaderResource,
                .access = producer ? Core::GpuTaskResourceAccess::Write : Core::GpuTaskResourceAccess::Read,
            };
            const Core::GpuTaskResourceVersionUse versionUse{ .version = versions[index % LengthOf(versions)], .role = producer ? Core::GpuTaskResourceVersionRole::Produce : Core::GpuTaskResourceVersionRole::Consume, };
            Core::GpuTaskDesc desc;
            const auto resourceNameResult = IndexedName(Name("tests/analysis_optimization/ambiguous_version_task"), index);
            ASSERT_TRUE(resourceNameResult);
            desc.setIdentity(*resourceNameResult).setMarkerLabel("Ambiguous Version Task").setResourceUses(Span(&physicalUse, 1u)).setResourceVersionUses(Span(&versionUse, 1u));
            const Core::GpuTaskCommandRequirements commands{ Core::GpuQueueCapability::Compute };
            const auto taskAdmission = graph.addTask(desc, commands);
            ASSERT_TRUE(taskAdmission);
            tasks[index] = *taskAdmission;
            ASSERT_TRUE(tasks[index].valid());
        }
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
        usize lifetimeEdges = 0u;
        b8 protectsFirst = false;
        b8 protectsSecond = false;
        for(const Core::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
            if(edge.hazard != Core::GpuTaskHazardType::VersionLifetime)
                continue;
            ++lifetimeEdges;
            if(edge.resourceVersion == versions[0u]){
                EXPECT_EQ(edge.producer, tasks[2u]);
                EXPECT_EQ(edge.consumer, tasks[1u]);
                protectsFirst = true;
            }
            else if(edge.resourceVersion == versions[1u]){
                EXPECT_EQ(edge.producer, tasks[3u]);
                EXPECT_EQ(edge.consumer, tasks[0u]);
                protectsSecond = true;
            }
        }
        EXPECT_EQ(lifetimeEdges, 2u);
        EXPECT_TRUE(protectsFirst);
        EXPECT_TRUE(protectsSecond);
    }
}

TEST(GpuTaskGraphAnalysis, ResourceVersionPreBirthReachabilityHandlesFrozenSemanticCycles){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto buffersAdmission0 = AddBufferMetadata(graph, Name("tests/analysis_optimization/semantic_cycle_buffer_a"), "Semantic Cycle Buffer A");
    const auto buffersAdmission1 = AddBufferMetadata(graph, Name("tests/analysis_optimization/semantic_cycle_buffer_b"), "Semantic Cycle Buffer B");
    ASSERT_TRUE(buffersAdmission0);
    ASSERT_TRUE(buffersAdmission1);
    const Array<Core::GpuGraphResourceId, 2> buffers = {
        *buffersAdmission0,
        *buffersAdmission1,
    };
    const Core::GpuTaskResourceRange range{ .bufferRange = Core::BufferRange(0u, 64u) };
    Array<Core::GpuGraphResourceVersionId, 2u> versions{};
    for(usize index = 0u; index < LengthOf(versions); ++index){
        ASSERT_TRUE(buffers[index].valid());
        const auto versionTaskAdmission = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(buffers[index]).setRange(range).setOrigin(Core::GpuGraphResourceVersionOrigin::TaskProduced));
        ASSERT_TRUE(versionTaskAdmission);
        versions[index] = *versionTaskAdmission;
        ASSERT_TRUE(versions[index].valid());
    }
    Array<Core::GpuTaskId, 5u> tasks{};
    for(usize index = 0u; index < LengthOf(tasks); ++index){
        Array<Core::GpuTaskResourceUse, 2u> physicalUses = {};
        Core::GpuTaskResourceVersionUse versionUse;
        usize physicalUseCount = 0u;
        if(index != 1u){
            const b8 consumer = index == 4u;
            const usize resourceIndex = index == 2u ? 1u : 0u;
            physicalUses[0u] = Core::GpuTaskResourceUse{
                .resource = buffers[resourceIndex],
                .range = range,
                .requiredState = consumer ? Core::ResourceStates::ShaderResource : Core::ResourceStates::UnorderedAccess,
                .access = consumer ? Core::GpuTaskResourceAccess::Read : Core::GpuTaskResourceAccess::Write,
            };
            physicalUseCount = 1u;
            versionUse.version = versions[index == 0u ? 1u : resourceIndex];
            versionUse.role = consumer || index == 0u ? Core::GpuTaskResourceVersionRole::Consume : Core::GpuTaskResourceVersionRole::Produce;
            if(index == 0u){
                physicalUses[1u] = Core::GpuTaskResourceUse{ .resource = buffers[1u], .range = range, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
                physicalUseCount = LengthOf(physicalUses);
            }
        }
        Core::GpuTaskDesc desc;
        const auto derivedNameResult = IndexedName(Name("tests/analysis_optimization/semantic_cycle_task"), index);
        ASSERT_TRUE(derivedNameResult);
        desc.setIdentity(*derivedNameResult).setMarkerLabel("Semantic Cycle Task");
        if(index != 0u)
            desc.setDependencies(Span(&tasks[index - 1u], 1u));
        if(physicalUseCount != 0u){
            desc.setResourceUses(Span(physicalUses.data(), physicalUseCount));
            desc.setResourceVersionUses(Span(&versionUse, 1u));
        }
        const Core::GpuTaskCommandRequirements commands{ Core::GpuQueueCapability::Compute };
        const auto taskAdmission = graph.addTask(desc, commands);
        ASSERT_TRUE(taskAdmission);
        tasks[index] = *taskAdmission;
        ASSERT_TRUE(tasks[index].valid());
    }
    Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/semantic_cycle_scratch"));
    Vector<Core::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> edges(scratchArena);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildResourceVersionDependencyEdges(declarations, scratchArena, edges));
    }
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges[0u].producer, tasks[2u]);
    EXPECT_EQ(edges[0u].consumer, tasks[0u]);
    EXPECT_EQ(edges[1u].producer, tasks[3u]);
    EXPECT_EQ(edges[1u].consumer, tasks[4u]);
    for(const Core::GpuTaskDependencyEdge& edge : edges)
        EXPECT_EQ(edge.hazard, Core::GpuTaskHazardType::VersionDependency);
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, scratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Cycle);
}

TEST(GpuTaskGraphAnalysis, VersionLifetimeEdgesPreserveDistinctTaskAndVersionOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const Array<b8, 2> reversedDeclarations = { false, true };
    for(const b8 reversed : reversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const auto buffersAdmission0 = AddBufferMetadata(graph, Name("tests/analysis_optimization/lifetime_buffer_a"), "Lifetime Buffer A");
        const auto buffersAdmission1 = AddBufferMetadata(graph, Name("tests/analysis_optimization/lifetime_buffer_b"), "Lifetime Buffer B");
        ASSERT_TRUE(buffersAdmission0);
        ASSERT_TRUE(buffersAdmission1);
        const Array<Core::GpuGraphResourceId, 2> buffers = {
            *buffersAdmission0,
            *buffersAdmission1,
        };
        const Core::GpuTaskResourceRange range{ .bufferRange = Core::BufferRange(0u, 64u) };
        Array<Core::GpuGraphResourceVersionId, 2u> versions{};
        for(usize index = 0u; index < LengthOf(versions); ++index){
            const usize versionIndex = reversed ? 1u - index : index;
            ASSERT_TRUE(buffers[versionIndex].valid());
            const auto versionTaskAdmission = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(buffers[versionIndex]).setRange(range).setOrigin(Core::GpuGraphResourceVersionOrigin::TaskProduced));
            ASSERT_TRUE(versionTaskAdmission);
            versions[versionIndex] = *versionTaskAdmission;
            ASSERT_TRUE(versions[versionIndex].valid());
        }
        Array<Core::GpuTaskId, 3u> tasks{};
        for(usize index = 0u; index < LengthOf(tasks); ++index){
            const b8 writer = index != 1u;
            const Core::ResourceStates::Mask state = writer ? Core::ResourceStates::UnorderedAccess : Core::ResourceStates::ShaderResource;
            const Core::GpuTaskResourceAccess::Enum access = writer ? Core::GpuTaskResourceAccess::Write : Core::GpuTaskResourceAccess::Read;
            const Array<Core::GpuTaskResourceUse, 3> physicalUses = {
                Core::GpuTaskResourceUse{ .resource = buffers[0u], .range = range, .requiredState = state, .access = access },
                Core::GpuTaskResourceUse{ .resource = buffers[0u], .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(16u, 32u) }, .requiredState = state, .access = access, },
                Core::GpuTaskResourceUse{ .resource = buffers[1u], .range = range, .requiredState = state, .access = access },
            };
            const Core::GpuTaskResourceVersionRole::Enum role = index == 0u ? Core::GpuTaskResourceVersionRole::Produce : Core::GpuTaskResourceVersionRole::Consume;
            const Array<Core::GpuTaskResourceVersionUse, 2> versionUses = {
                Core::GpuTaskResourceVersionUse{ .version = versions[0u], .role = role },
                Core::GpuTaskResourceVersionUse{ .version = versions[1u], .role = role },
            };
            Core::GpuTaskDesc desc;
            const auto resourceNameResult = IndexedName(Name("tests/analysis_optimization/lifetime_task"), index);
            ASSERT_TRUE(resourceNameResult);
            desc.setIdentity(*resourceNameResult).setMarkerLabel("Lifetime Version Task").setResourceUses(physicalUses);
            if(index != 2u)
                desc.setResourceVersionUses(versionUses);
            const Core::GpuTaskCommandRequirements commands{ Core::GpuQueueCapability::Compute };
            const auto taskAdmission = graph.addTask(desc, commands);
            ASSERT_TRUE(taskAdmission);
            tasks[index] = *taskAdmission;
            ASSERT_TRUE(tasks[index].valid());
        }
        Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/lifetime_edge_scratch"));
        Vector<Core::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> edges(scratchArena);
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            ASSERT_TRUE(Core::GpuTaskGraphCompilerDetail::BuildResourceVersionDependencyEdges(declarations, scratchArena, edges));
        }
        ASSERT_EQ(edges.size(), 4u);
        for(usize index = 0u; index < LengthOf(versions); ++index){
            EXPECT_EQ(edges[index].producer, tasks[0u]);
            EXPECT_EQ(edges[index].consumer, tasks[1u]);
            EXPECT_EQ(edges[index].resourceVersion, versions[index]);
            EXPECT_EQ(edges[index].hazard, Core::GpuTaskHazardType::VersionDependency);
            const usize versionIndex = reversed ? 1u - index : index;
            const Core::GpuTaskDependencyEdge& lifetime = edges[index + LengthOf(versions)];
            EXPECT_EQ(lifetime.producer, tasks[1u]);
            EXPECT_EQ(lifetime.consumer, tasks[2u]);
            EXPECT_EQ(lifetime.resource, buffers[versionIndex]);
            EXPECT_EQ(lifetime.resourceVersion, versions[versionIndex]);
            EXPECT_EQ(lifetime.hazard, Core::GpuTaskHazardType::VersionLifetime);
        }
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
        for(usize index = 0u; index < LengthOf(tasks); ++index)
            EXPECT_EQ(analysis.topologicalOrder()[index], tasks[index]);
    }
}

TEST(GpuTaskGraphAnalysis, ReportsEarlierUncoveredVersionUseBeforeLaterDuplicate){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/analysis_optimization/binding_failure_buffer"), "Version Binding Failure Buffer");
    ASSERT_TRUE(bufferResult);
    const Core::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    Array<Core::GpuTaskResourceVersionUse, 10u> versionUses{};
    for(usize index = 0u; index + 1u < LengthOf(versionUses); ++index){
        const Core::GpuTaskResourceRange range{ .bufferRange = Core::BufferRange(index == 0u ? 128u : 0u, 64u) };
        const auto versionResult = graph.declareResourceVersion(Core::GpuGraphResourceVersionDesc{}.setResource(buffer).setRange(range).setOrigin(Core::GpuGraphResourceVersionOrigin::ImportedRoot));
        ASSERT_TRUE(versionResult);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        versionUses[index] = Core::GpuTaskResourceVersionUse{ .version = version, .role = Core::GpuTaskResourceVersionRole::Consume, };
    }
    versionUses[LengthOf(versionUses) - 1u] = versionUses[1u];
    const Core::GpuTaskResourceUse physicalUse{
        .resource = buffer,
        .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(0u, 64u) },
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskCommandRequirements commands{ Core::GpuQueueCapability::Compute };
    const auto taskResult = graph.addTask(
        Core::GpuTaskDesc{}
            .setIdentity(Name("tests/analysis_optimization/binding_failure_task"))
            .setMarkerLabel("Version Binding Failure Task")
            .setResourceUses(Span(&physicalUse, 1u))
            .setResourceVersionUses(versionUses),
        commands
    );
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, operationScratchArena, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
    EXPECT_EQ(analysis.diagnostic().task, task);
    EXPECT_FALSE(analysis.diagnostic().relatedTask.valid());
    EXPECT_EQ(analysis.diagnostic().resource, buffer);
    EXPECT_EQ(analysis.diagnostic().resourceVersion, versionUses[0u].version);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

