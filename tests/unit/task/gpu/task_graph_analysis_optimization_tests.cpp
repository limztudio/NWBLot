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


[[nodiscard]] static Expected<Name> IndexedName(const Name& prefix, const usize index){
    char text[32u];
    return DeriveName(prefix, FormatDecimal(index, text));
}

static void BuildVersionChain(Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceVersionOrigin::Enum origin, const usize taskCount, const bool disjointClobber, const bool overlappingClobber = false){
    ASSERT_GE(taskCount, overlappingClobber ? 3u : 2u);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/analysis_optimization/version_buffer"), "Analysis Version Buffer");
    ASSERT_TRUE(bufferResult);
    const Graphics::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    const Graphics::GpuTaskResourceRange versionRange{ .bufferRange = Graphics::BufferRange(0u, 64u) };
    const auto versionResult = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(buffer).setRange(versionRange).setOrigin(origin));
    ASSERT_TRUE(versionResult);
    const Graphics::GpuGraphResourceVersionId version = *versionResult;
    ASSERT_TRUE(version.valid());
    const bool produced = origin == Graphics::GpuGraphResourceVersionOrigin::TaskProduced;
    Graphics::GpuTaskId previous;
    for(usize index = 0u; index < taskCount; ++index){
        Graphics::GpuTaskResourceUse physicalUse;
        Graphics::GpuTaskResourceVersionUse versionUse;
        const bool usesVersion = index == 0u || (produced && index == 1u);
        const bool writesVersion = produced && index == 0u;
        if(usesVersion){
            physicalUse.resource = buffer;
            physicalUse.range = versionRange;
            physicalUse.requiredState = writesVersion ? Graphics::ResourceStates::UnorderedAccess : Graphics::ResourceStates::ShaderResource;
            physicalUse.access = writesVersion ? Graphics::GpuTaskResourceAccess::Write : Graphics::GpuTaskResourceAccess::Read;
            versionUse.version = version;
            versionUse.role = writesVersion ? Graphics::GpuTaskResourceVersionRole::Produce : Graphics::GpuTaskResourceVersionRole::Consume;
        }
        else if((disjointClobber || overlappingClobber) && index + 1u == taskCount){
            physicalUse.resource = buffer;
            physicalUse.range.bufferRange = Graphics::BufferRange(disjointClobber ? 128u : 0u, 64u);
            physicalUse.requiredState = Graphics::ResourceStates::UnorderedAccess;
            physicalUse.access = Graphics::GpuTaskResourceAccess::Write;
        }
        Graphics::GpuTaskDesc desc;
        const auto derivedNameAt74Col25 = IndexedName(Name("tests/analysis_optimization/version_task"), index);
        ASSERT_TRUE(derivedNameAt74Col25);
        desc.setIdentity(*derivedNameAt74Col25).setMarkerLabel("Analysis Version Task");
        if(previous.valid())
            desc.setDependencies(&previous, 1u);
        if(physicalUse.resource.valid())
            desc.setResourceUses(&physicalUse, 1u);
        if(usesVersion)
            desc.setResourceVersionUses(&versionUse, 1u);
        const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
        const auto gpuAdmissionAt78 = graph.addTask(desc, commands);
        ASSERT_TRUE(gpuAdmissionAt78);
        previous = *gpuAdmissionAt78;
        ASSERT_TRUE(previous.valid());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphAnalysis, ResourceVersionsWithoutOverlappingClobbersPreserveStableOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const Graphics::GpuGraphResourceVersionOrigin::Enum origins[] = { Graphics::GpuGraphResourceVersionOrigin::ImportedRoot, Graphics::GpuGraphResourceVersionOrigin::TaskProduced, };
    for(const Graphics::GpuGraphResourceVersionOrigin::Enum origin : origins){
        SCOPED_TRACE(origin);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        ASSERT_NO_FATAL_FAILURE(BuildVersionChain(graph, origin, 8u, true));
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
        ASSERT_EQ(analysis.topologicalOrder().size(), 8u);
        EXPECT_EQ(analysis.resourceVersionEdgeCount(), origin == Graphics::GpuGraphResourceVersionOrigin::TaskProduced ? 1u : 0u);
        for(usize index = 0u; index < analysis.topologicalOrder().size(); ++index)
            EXPECT_EQ(analysis.topologicalOrder()[index].index, index);
        for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges())
            EXPECT_NE(edge.hazard, Graphics::GpuTaskHazardType::VersionLifetime);
    }
}

TEST(GpuTaskGraphAnalysis, VersionLifetimeConstraintsDoNotInventProducedVersionOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const bool reversedDeclarations[] = { false, true };
    for(const bool reversed : reversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto bufferResult = AddBufferMetadata(graph, Name("tests/analysis_optimization/ambiguous_version_buffer"), "Ambiguous Version Buffer");
        ASSERT_TRUE(bufferResult);
        const Graphics::GpuGraphResourceId buffer = *bufferResult;
        ASSERT_TRUE(buffer.valid());
        const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
        Graphics::GpuGraphResourceVersionId versions[2u];
        for(usize index = 0u; index < LengthOf(versions); ++index){
            const auto gpuAdmissionAt116 = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(buffer).setRange(range).setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced));
            ASSERT_TRUE(gpuAdmissionAt116);
            versions[reversed ? 1u - index : index] = *gpuAdmissionAt116;
            ASSERT_TRUE(versions[reversed ? 1u - index : index].valid());
        }
        Graphics::GpuTaskId tasks[4u];
        for(usize index = 0u; index < LengthOf(tasks); ++index){
            const bool producer = index < LengthOf(versions);
            const Graphics::GpuTaskResourceUse physicalUse{
                .resource = buffer,
                .range = range,
                .requiredState = producer ? Graphics::ResourceStates::UnorderedAccess : Graphics::ResourceStates::ShaderResource,
                .access = producer ? Graphics::GpuTaskResourceAccess::Write : Graphics::GpuTaskResourceAccess::Read,
            };
            const Graphics::GpuTaskResourceVersionUse versionUse{ .version = versions[index % LengthOf(versions)], .role = producer ? Graphics::GpuTaskResourceVersionRole::Produce : Graphics::GpuTaskResourceVersionRole::Consume, };
            Graphics::GpuTaskDesc desc;
            const auto derivedNameAt140Col29 = IndexedName(Name("tests/analysis_optimization/ambiguous_version_task"), index);
            ASSERT_TRUE(derivedNameAt140Col29);
            desc.setIdentity(*derivedNameAt140Col29).setMarkerLabel("Ambiguous Version Task").setResourceUses(&physicalUse, 1u).setResourceVersionUses(&versionUse, 1u);
            const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
            const auto gpuAdmissionAt132 = graph.addTask(desc, commands);
            ASSERT_TRUE(gpuAdmissionAt132);
            tasks[index] = *gpuAdmissionAt132;
            ASSERT_TRUE(tasks[index].valid());
        }
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
        usize lifetimeEdges = 0u;
        bool protectsFirst = false;
        bool protectsSecond = false;
        for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
            if(edge.hazard != Graphics::GpuTaskHazardType::VersionLifetime)
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
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto buffersAdmission0 = AddBufferMetadata(graph, Name("tests/analysis_optimization/semantic_cycle_buffer_a"), "Semantic Cycle Buffer A");
    const auto buffersAdmission1 = AddBufferMetadata(graph, Name("tests/analysis_optimization/semantic_cycle_buffer_b"), "Semantic Cycle Buffer B");
    ASSERT_TRUE(buffersAdmission0);
    ASSERT_TRUE(buffersAdmission1);
    const Graphics::GpuGraphResourceId buffers[] = {
        *buffersAdmission0,
        *buffersAdmission1,
    };
    const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
    Graphics::GpuGraphResourceVersionId versions[2u];
    for(usize index = 0u; index < LengthOf(versions); ++index){
        ASSERT_TRUE(buffers[index].valid());
        const auto gpuAdmissionAt173 = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(buffers[index]).setRange(range).setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced));
        ASSERT_TRUE(gpuAdmissionAt173);
        versions[index] = *gpuAdmissionAt173;
        ASSERT_TRUE(versions[index].valid());
    }
    Graphics::GpuTaskId tasks[5u];
    for(usize index = 0u; index < LengthOf(tasks); ++index){
        Graphics::GpuTaskResourceUse physicalUses[2u] = {};
        Graphics::GpuTaskResourceVersionUse versionUse;
        usize physicalUseCount = 0u;
        if(index != 1u){
            const bool consumer = index == 4u;
            const usize resourceIndex = index == 2u ? 1u : 0u;
            physicalUses[0u] = Graphics::GpuTaskResourceUse{
                .resource = buffers[resourceIndex],
                .range = range,
                .requiredState = consumer ? Graphics::ResourceStates::ShaderResource : Graphics::ResourceStates::UnorderedAccess,
                .access = consumer ? Graphics::GpuTaskResourceAccess::Read : Graphics::GpuTaskResourceAccess::Write,
            };
            physicalUseCount = 1u;
            versionUse.version = versions[index == 0u ? 1u : resourceIndex];
            versionUse.role = consumer || index == 0u ? Graphics::GpuTaskResourceVersionRole::Consume : Graphics::GpuTaskResourceVersionRole::Produce;
            if(index == 0u){
                physicalUses[1u] = Graphics::GpuTaskResourceUse{ .resource = buffers[1u], .range = range, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, };
                physicalUseCount = LengthOf(physicalUses);
            }
        }
        Graphics::GpuTaskDesc desc;
        const auto derivedNameAt217Col25 = IndexedName(Name("tests/analysis_optimization/semantic_cycle_task"), index);
        ASSERT_TRUE(derivedNameAt217Col25);
        desc.setIdentity(*derivedNameAt217Col25).setMarkerLabel("Semantic Cycle Task");
        if(index != 0u)
            desc.setDependencies(&tasks[index - 1u], 1u);
        if(physicalUseCount != 0u){
            desc.setResourceUses(physicalUses, physicalUseCount);
            desc.setResourceVersionUses(&versionUse, 1u);
        }
        const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
        const auto gpuAdmissionAt207 = graph.addTask(desc, commands);
        ASSERT_TRUE(gpuAdmissionAt207);
        tasks[index] = *gpuAdmissionAt207;
        ASSERT_TRUE(tasks[index].valid());
    }
    Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/semantic_cycle_scratch"));
    Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> edges(scratchArena);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildResourceVersionDependencyEdges(declarations, edges, scratchArena));
    }
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges[0u].producer, tasks[2u]);
    EXPECT_EQ(edges[0u].consumer, tasks[0u]);
    EXPECT_EQ(edges[1u].producer, tasks[3u]);
    EXPECT_EQ(edges[1u].consumer, tasks[4u]);
    for(const Graphics::GpuTaskDependencyEdge& edge : edges)
        EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::VersionDependency);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(scratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
}

TEST(GpuTaskGraphAnalysis, VersionLifetimeEdgesPreserveDistinctTaskAndVersionOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    const bool reversedDeclarations[] = { false, true };
    for(const bool reversed : reversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto buffersAdmission0 = AddBufferMetadata(graph, Name("tests/analysis_optimization/lifetime_buffer_a"), "Lifetime Buffer A");
        const auto buffersAdmission1 = AddBufferMetadata(graph, Name("tests/analysis_optimization/lifetime_buffer_b"), "Lifetime Buffer B");
        ASSERT_TRUE(buffersAdmission0);
        ASSERT_TRUE(buffersAdmission1);
        const Graphics::GpuGraphResourceId buffers[] = {
            *buffersAdmission0,
            *buffersAdmission1,
        };
        const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
        Graphics::GpuGraphResourceVersionId versions[2u];
        for(usize index = 0u; index < LengthOf(versions); ++index){
            const usize versionIndex = reversed ? 1u - index : index;
            ASSERT_TRUE(buffers[versionIndex].valid());
            const auto gpuAdmissionAt243 = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(buffers[versionIndex]).setRange(range).setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced));
            ASSERT_TRUE(gpuAdmissionAt243);
            versions[versionIndex] = *gpuAdmissionAt243;
            ASSERT_TRUE(versions[versionIndex].valid());
        }
        Graphics::GpuTaskId tasks[3u];
        for(usize index = 0u; index < LengthOf(tasks); ++index){
            const bool writer = index != 1u;
            const Graphics::ResourceStates::Mask state = writer ? Graphics::ResourceStates::UnorderedAccess : Graphics::ResourceStates::ShaderResource;
            const Graphics::GpuTaskResourceAccess::Enum access = writer ? Graphics::GpuTaskResourceAccess::Write : Graphics::GpuTaskResourceAccess::Read;
            const Graphics::GpuTaskResourceUse physicalUses[] = {
                Graphics::GpuTaskResourceUse{ .resource = buffers[0u], .range = range, .requiredState = state, .access = access },
                Graphics::GpuTaskResourceUse{ .resource = buffers[0u], .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(16u, 32u) }, .requiredState = state, .access = access, },
                Graphics::GpuTaskResourceUse{ .resource = buffers[1u], .range = range, .requiredState = state, .access = access },
            };
            const Graphics::GpuTaskResourceVersionRole::Enum role = index == 0u ? Graphics::GpuTaskResourceVersionRole::Produce : Graphics::GpuTaskResourceVersionRole::Consume;
            const Graphics::GpuTaskResourceVersionUse versionUses[] = {
                Graphics::GpuTaskResourceVersionUse{ .version = versions[0u], .role = role },
                Graphics::GpuTaskResourceVersionUse{ .version = versions[1u], .role = role },
            };
            Graphics::GpuTaskDesc desc;
            const auto derivedNameAt288Col29 = IndexedName(Name("tests/analysis_optimization/lifetime_task"), index);
            ASSERT_TRUE(derivedNameAt288Col29);
            desc.setIdentity(*derivedNameAt288Col29).setMarkerLabel("Lifetime Version Task").setResourceUses(physicalUses, LengthOf(physicalUses));
            if(index != 2u)
                desc.setResourceVersionUses(versionUses, LengthOf(versionUses));
            const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
            const auto gpuAdmissionAt266 = graph.addTask(desc, commands);
            ASSERT_TRUE(gpuAdmissionAt266);
            tasks[index] = *gpuAdmissionAt266;
            ASSERT_TRUE(tasks[index].valid());
        }
        Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/lifetime_edge_scratch"));
        Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> edges(scratchArena);
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildResourceVersionDependencyEdges(declarations, edges, scratchArena));
        }
        ASSERT_EQ(edges.size(), 4u);
        for(usize index = 0u; index < LengthOf(versions); ++index){
            EXPECT_EQ(edges[index].producer, tasks[0u]);
            EXPECT_EQ(edges[index].consumer, tasks[1u]);
            EXPECT_EQ(edges[index].resourceVersion, versions[index]);
            EXPECT_EQ(edges[index].hazard, Graphics::GpuTaskHazardType::VersionDependency);
            const usize versionIndex = reversed ? 1u - index : index;
            const Graphics::GpuTaskDependencyEdge& lifetime = edges[index + LengthOf(versions)];
            EXPECT_EQ(lifetime.producer, tasks[1u]);
            EXPECT_EQ(lifetime.consumer, tasks[2u]);
            EXPECT_EQ(lifetime.resource, buffers[versionIndex]);
            EXPECT_EQ(lifetime.resourceVersion, versions[versionIndex]);
            EXPECT_EQ(lifetime.hazard, Graphics::GpuTaskHazardType::VersionLifetime);
        }
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(operationScratchArena, graph, analysis));
        ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
        for(usize index = 0u; index < LengthOf(tasks); ++index)
            EXPECT_EQ(analysis.topologicalOrder()[index], tasks[index]);
    }
}

TEST(GpuTaskGraphAnalysis, ReportsEarlierUncoveredVersionUseBeforeLaterDuplicate){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/analysis_optimization/binding_failure_buffer"), "Version Binding Failure Buffer");
    ASSERT_TRUE(bufferResult);
    const Graphics::GpuGraphResourceId buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    Graphics::GpuTaskResourceVersionUse versionUses[10u];
    for(usize index = 0u; index + 1u < LengthOf(versionUses); ++index){
        const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(index == 0u ? 128u : 0u, 64u) };
        const auto versionResult = graph.declareResourceVersion(Graphics::GpuGraphResourceVersionDesc{}.setResource(buffer).setRange(range).setOrigin(Graphics::GpuGraphResourceVersionOrigin::ImportedRoot));
        ASSERT_TRUE(versionResult);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        ASSERT_TRUE(version.valid());
        versionUses[index] = Graphics::GpuTaskResourceVersionUse{ .version = version, .role = Graphics::GpuTaskResourceVersionRole::Consume, };
    }
    versionUses[LengthOf(versionUses) - 1u] = versionUses[1u];
    const Graphics::GpuTaskResourceUse physicalUse{
        .resource = buffer,
        .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(0u, 64u) },
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
    const auto taskResult = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/analysis_optimization/binding_failure_task"))
            .setMarkerLabel("Version Binding Failure Task")
            .setResourceUses(&physicalUse, 1u)
            .setResourceVersionUses(versionUses, LengthOf(versionUses)),
        commands
    );
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(operationScratchArena, graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse);
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

