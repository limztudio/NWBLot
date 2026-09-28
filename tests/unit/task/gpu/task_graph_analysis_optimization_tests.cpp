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

constexpr usize s_BenchmarkTaskCount = 4096u;
constexpr usize s_BenchmarkRepetitions = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Name IndexedName(const Name& prefix, const usize index){
    char text[32u];
    return DeriveName(prefix, FormatDecimal(index, text));
}

static void BuildVersionChain(
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuGraphResourceVersionOrigin::Enum origin,
    const usize taskCount,
    const bool disjointClobber,
    const bool overlappingClobber = false){
    ASSERT_GE(taskCount, overlappingClobber ? 3u : 2u);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/analysis_optimization/version_buffer"),
        "Analysis Version Buffer"
    );
    ASSERT_TRUE(buffer.valid());
    const Graphics::GpuTaskResourceRange versionRange{ .bufferRange = Graphics::BufferRange(0u, 64u) };
    const Graphics::GpuGraphResourceVersionId version = graph.declareResourceVersion(
        Graphics::GpuGraphResourceVersionDesc{}
            .setResource(buffer)
            .setRange(versionRange)
            .setOrigin(origin)
    );
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
        desc
            .setIdentity(IndexedName(Name("tests/analysis_optimization/version_task"), index))
            .setMarkerLabel("Analysis Version Task")
        ;
        if(previous.valid())
            desc.setDependencies(&previous, 1u);
        if(physicalUse.resource.valid())
            desc.setResourceUses(&physicalUse, 1u);
        if(usesVersion)
            desc.setResourceVersionUses(&versionUse, 1u);
        const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
        previous = graph.addTask(desc, commands);
        ASSERT_TRUE(previous.valid());
    }
}

static void BuildDistinctVersions(Graphics::GpuTaskGraph& graph, const usize versionCount){
    Graphics::GpuTaskId previous;
    for(usize index = 0u; index < versionCount; ++index){
        const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
            graph,
            IndexedName(Name("tests/analysis_optimization/distinct_buffer"), index),
            "Distinct Version Buffer"
        );
        ASSERT_TRUE(buffer.valid());
        const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
        const Graphics::GpuGraphResourceVersionId version = graph.declareResourceVersion(
            Graphics::GpuGraphResourceVersionDesc{}
                .setResource(buffer)
                .setRange(range)
                .setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced)
        );
        ASSERT_TRUE(version.valid());
        for(usize roleIndex = 0u; roleIndex < 2u; ++roleIndex){
            const bool producer = roleIndex == 0u;
            const Graphics::GpuTaskResourceUse physicalUse{
                .resource = buffer,
                .range = range,
                .requiredState = producer ? Graphics::ResourceStates::UnorderedAccess : Graphics::ResourceStates::ShaderResource,
                .access = producer ? Graphics::GpuTaskResourceAccess::Write : Graphics::GpuTaskResourceAccess::Read,
            };
            const Graphics::GpuTaskResourceVersionUse versionUse{
                .version = version,
                .role = producer ? Graphics::GpuTaskResourceVersionRole::Produce : Graphics::GpuTaskResourceVersionRole::Consume,
            };
            Graphics::GpuTaskDesc desc;
            desc
                .setIdentity(IndexedName(Name("tests/analysis_optimization/distinct_task"), index * 2u + roleIndex))
                .setMarkerLabel("Distinct Version Task")
                .setResourceUses(&physicalUse, 1u)
                .setResourceVersionUses(&versionUse, 1u)
            ;
            if(previous.valid())
                desc.setDependencies(&previous, 1u);
            const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
            previous = graph.addTask(desc, commands);
            ASSERT_TRUE(previous.valid());
        }
    }
}

static void AppendPresentationChain(
    Graphics::GpuTaskGraph& graph,
    const Graphics::GpuGraphResourceId backBuffer,
    const usize taskCount,
    Graphics::GpuTaskId& outWriter,
    Graphics::GpuTaskId& outProducer){
    ASSERT_GE(taskCount, 2u);
    outWriter = {};
    outProducer = {};
    for(usize index = 0u; index < taskCount; ++index){
        const Graphics::GpuTaskId dependencies[] = { outProducer, outWriter };
        const Graphics::GpuTaskResourceUse use{
            .resource = backBuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(IndexedName(Name("tests/analysis_optimization/presentation_task"), index))
            .setMarkerLabel("Analysis Presentation Task")
        ;
        if(index != 0u)
            desc.setDependencies(dependencies, index == 1u ? 1u : LengthOf(dependencies));
        else
            desc.setResourceUses(&use, 1u);
        const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Graphics };
        outProducer = graph.addTask(desc, commands);
        ASSERT_TRUE(outProducer.valid());
        if(index == 0u)
            outWriter = outProducer;
    }
}

static void MeasureAnalysis(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    Core::Alloc::ScratchArena& scratchArena){
    ASSERT_TRUE(Analyze(graph, analysis));
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < s_BenchmarkRepetitions; ++iteration)
        ASSERT_TRUE(Analyze(graph, analysis));
    const u64 elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    char text[32u];
    const AStringView elapsed = FormatDecimal(elapsedNanoseconds, text);
    text[elapsed.size()] = '\0';
    testing::Test::RecordProperty("elapsed_ns", text);
    testing::Test::RecordProperty("repetitions", static_cast<i32>(s_BenchmarkRepetitions));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.analyze(declarations, analysis, scratchArena));
    testing::Test::RecordProperty("task_count", static_cast<i32>(declarations.taskCount()));
    testing::Test::RecordProperty("scratch_peak_bytes", static_cast<i32>(scratchArena.memoryStats().peakUsedBytes));
    testing::Test::RecordProperty("scratch_reserved_bytes", static_cast<i32>(scratchArena.memoryStats().reservedBytes));
}

static void BenchmarkVersionChain(
    const Graphics::GpuGraphResourceVersionOrigin::Enum origin,
    const bool overlappingClobber = false){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    BuildVersionChain(graph, origin, s_BenchmarkTaskCount, false, overlappingClobber);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/version_scratch"));
    MeasureAnalysis(graph, analysis, scratchArena);
    ASSERT_EQ(analysis.topologicalOrder().size(), s_BenchmarkTaskCount);
    if(!overlappingClobber)
        EXPECT_EQ(analysis.edges().size(), s_BenchmarkTaskCount - 1u);
    EXPECT_EQ(analysis.schedulingEdges().size(), s_BenchmarkTaskCount - 1u);
    const usize expectedVersionEdges = origin == Graphics::GpuGraphResourceVersionOrigin::TaskProduced ? 1u : 0u;
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), expectedVersionEdges + (overlappingClobber ? 1u : 0u));
    for(usize index = 0u; index < s_BenchmarkTaskCount; ++index)
        EXPECT_EQ(analysis.topologicalOrder()[index].index, index);
}

static void BenchmarkDistinctVersions(const usize versionCount){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    BuildDistinctVersions(graph, versionCount);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/distinct_version_scratch"));
    MeasureAnalysis(graph, analysis, scratchArena);
    ASSERT_EQ(analysis.topologicalOrder().size(), versionCount * 2u);
    EXPECT_EQ(analysis.edges().size(), versionCount * 2u - 1u);
    EXPECT_EQ(analysis.resourceVersionEdgeCount(), versionCount);
    testing::Test::RecordProperty("version_count", static_cast<i32>(versionCount));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphAnalysis, ResourceVersionsWithoutOverlappingClobbersPreserveStableOrder){
    const Graphics::GpuGraphResourceVersionOrigin::Enum origins[] = {
        Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
        Graphics::GpuGraphResourceVersionOrigin::TaskProduced,
    };
    for(const Graphics::GpuGraphResourceVersionOrigin::Enum origin : origins){
        SCOPED_TRACE(origin);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        BuildVersionChain(graph, origin, 8u, true);
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        ASSERT_EQ(analysis.topologicalOrder().size(), 8u);
        EXPECT_EQ(analysis.resourceVersionEdgeCount(), origin == Graphics::GpuGraphResourceVersionOrigin::TaskProduced ? 1u : 0u);
        for(usize index = 0u; index < analysis.topologicalOrder().size(); ++index)
            EXPECT_EQ(analysis.topologicalOrder()[index].index, index);
        for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges())
            EXPECT_NE(edge.hazard, Graphics::GpuTaskHazardType::VersionLifetime);
    }
}

TEST(GpuTaskGraphAnalysis, VersionLifetimeConstraintsDoNotInventProducedVersionOrder){
    const bool reversedDeclarations[] = { false, true };
    for(const bool reversed : reversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
            graph,
            Name("tests/analysis_optimization/ambiguous_version_buffer"),
            "Ambiguous Version Buffer"
        );
        ASSERT_TRUE(buffer.valid());
        const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
        Graphics::GpuGraphResourceVersionId versions[2u];
        for(usize index = 0u; index < LengthOf(versions); ++index){
            versions[reversed ? 1u - index : index] = graph.declareResourceVersion(
                Graphics::GpuGraphResourceVersionDesc{}
                    .setResource(buffer)
                    .setRange(range)
                    .setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced)
            );
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
            const Graphics::GpuTaskResourceVersionUse versionUse{
                .version = versions[index % LengthOf(versions)],
                .role = producer ? Graphics::GpuTaskResourceVersionRole::Produce : Graphics::GpuTaskResourceVersionRole::Consume,
            };
            Graphics::GpuTaskDesc desc;
            desc
                .setIdentity(IndexedName(Name("tests/analysis_optimization/ambiguous_version_task"), index))
                .setMarkerLabel("Ambiguous Version Task")
                .setResourceUses(&physicalUse, 1u)
                .setResourceVersionUses(&versionUse, 1u)
            ;
            const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
            tasks[index] = graph.addTask(desc, commands);
            ASSERT_TRUE(tasks[index].valid());
        }
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
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
    const Graphics::GpuGraphResourceId buffers[] = {
        AddBufferMetadata(graph, Name("tests/analysis_optimization/semantic_cycle_buffer_a"), "Semantic Cycle Buffer A"),
        AddBufferMetadata(graph, Name("tests/analysis_optimization/semantic_cycle_buffer_b"), "Semantic Cycle Buffer B"),
    };
    const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
    Graphics::GpuGraphResourceVersionId versions[2u];
    for(usize index = 0u; index < LengthOf(versions); ++index){
        ASSERT_TRUE(buffers[index].valid());
        versions[index] = graph.declareResourceVersion(
            Graphics::GpuGraphResourceVersionDesc{}
                .setResource(buffers[index])
                .setRange(range)
                .setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced)
        );
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
            versionUse.role = consumer || index == 0u
                ? Graphics::GpuTaskResourceVersionRole::Consume : Graphics::GpuTaskResourceVersionRole::Produce;
            if(index == 0u){
                physicalUses[1u] = Graphics::GpuTaskResourceUse{
                    .resource = buffers[1u],
                    .range = range,
                    .requiredState = Graphics::ResourceStates::ShaderResource,
                    .access = Graphics::GpuTaskResourceAccess::Read,
                };
                physicalUseCount = LengthOf(physicalUses);
            }
        }
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(IndexedName(Name("tests/analysis_optimization/semantic_cycle_task"), index))
            .setMarkerLabel("Semantic Cycle Task")
        ;
        if(index != 0u)
            desc.setDependencies(&tasks[index - 1u], 1u);
        if(physicalUseCount != 0u){
            desc.setResourceUses(physicalUses, physicalUseCount);
            desc.setResourceVersionUses(&versionUse, 1u);
        }
        const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
        tasks[index] = graph.addTask(desc, commands);
        ASSERT_TRUE(tasks[index].valid());
    }
    Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/semantic_cycle_scratch"));
    Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> edges(scratchArena);
    Graphics::GpuTaskGraphAnalysisDiagnostic diagnostic;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildResourceVersionDependencyEdges(
            declarations,
            edges,
            diagnostic,
            scratchArena
        ));
    }
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges[0u].producer, tasks[2u]);
    EXPECT_EQ(edges[0u].consumer, tasks[0u]);
    EXPECT_EQ(edges[1u].producer, tasks[3u]);
    EXPECT_EQ(edges[1u].consumer, tasks[4u]);
    for(const Graphics::GpuTaskDependencyEdge& edge : edges)
        EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::VersionDependency);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
}

TEST(GpuTaskGraphAnalysis, VersionLifetimeEdgesPreserveDistinctTaskAndVersionOrder){
    const bool reversedDeclarations[] = { false, true };
    for(const bool reversed : reversedDeclarations){
        SCOPED_TRACE(reversed);
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId buffers[] = {
            AddBufferMetadata(graph, Name("tests/analysis_optimization/lifetime_buffer_a"), "Lifetime Buffer A"),
            AddBufferMetadata(graph, Name("tests/analysis_optimization/lifetime_buffer_b"), "Lifetime Buffer B"),
        };
        const Graphics::GpuTaskResourceRange range{ .bufferRange = Graphics::BufferRange(0u, 64u) };
        Graphics::GpuGraphResourceVersionId versions[2u];
        for(usize index = 0u; index < LengthOf(versions); ++index){
            const usize versionIndex = reversed ? 1u - index : index;
            ASSERT_TRUE(buffers[versionIndex].valid());
            versions[versionIndex] = graph.declareResourceVersion(
                Graphics::GpuGraphResourceVersionDesc{}
                    .setResource(buffers[versionIndex])
                    .setRange(range)
                    .setOrigin(Graphics::GpuGraphResourceVersionOrigin::TaskProduced)
            );
            ASSERT_TRUE(versions[versionIndex].valid());
        }
        Graphics::GpuTaskId tasks[3u];
        for(usize index = 0u; index < LengthOf(tasks); ++index){
            const bool writer = index != 1u;
            const Graphics::ResourceStates::Mask state = writer
                ? Graphics::ResourceStates::UnorderedAccess : Graphics::ResourceStates::ShaderResource;
            const Graphics::GpuTaskResourceAccess::Enum access = writer
                ? Graphics::GpuTaskResourceAccess::Write : Graphics::GpuTaskResourceAccess::Read;
            const Graphics::GpuTaskResourceUse physicalUses[] = {
                Graphics::GpuTaskResourceUse{ .resource = buffers[0u], .range = range, .requiredState = state, .access = access },
                Graphics::GpuTaskResourceUse{
                    .resource = buffers[0u],
                    .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(16u, 32u) },
                    .requiredState = state,
                    .access = access,
                },
                Graphics::GpuTaskResourceUse{ .resource = buffers[1u], .range = range, .requiredState = state, .access = access },
            };
            const Graphics::GpuTaskResourceVersionRole::Enum role = index == 0u
                ? Graphics::GpuTaskResourceVersionRole::Produce : Graphics::GpuTaskResourceVersionRole::Consume;
            const Graphics::GpuTaskResourceVersionUse versionUses[] = {
                Graphics::GpuTaskResourceVersionUse{ .version = versions[0u], .role = role },
                Graphics::GpuTaskResourceVersionUse{ .version = versions[1u], .role = role },
            };
            Graphics::GpuTaskDesc desc;
            desc
                .setIdentity(IndexedName(Name("tests/analysis_optimization/lifetime_task"), index))
                .setMarkerLabel("Lifetime Version Task")
                .setResourceUses(physicalUses, LengthOf(physicalUses))
            ;
            if(index != 2u)
                desc.setResourceVersionUses(versionUses, LengthOf(versionUses));
            const Graphics::GpuTaskCommandRequirements commands{ Graphics::GpuQueueCapability::Compute };
            tasks[index] = graph.addTask(desc, commands);
            ASSERT_TRUE(tasks[index].valid());
        }
        Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/lifetime_edge_scratch"));
        Vector<Graphics::GpuTaskDependencyEdge, Core::Alloc::ScratchArena> edges(scratchArena);
        Graphics::GpuTaskGraphAnalysisDiagnostic diagnostic;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildResourceVersionDependencyEdges(
                declarations,
                edges,
                diagnostic,
                scratchArena
            ));
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
        ASSERT_TRUE(Analyze(graph, analysis));
        ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
        for(usize index = 0u; index < LengthOf(tasks); ++index)
            EXPECT_EQ(analysis.topologicalOrder()[index], tasks[index]);
    }
}

TEST(GpuTaskGraphAnalysis, DISABLED_ResourceVersionImportedRootBenchmark4096Tasks){
    BenchmarkVersionChain(Graphics::GpuGraphResourceVersionOrigin::ImportedRoot);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ResourceVersionProducedNoClobberBenchmark4096Tasks){
    BenchmarkVersionChain(Graphics::GpuGraphResourceVersionOrigin::TaskProduced);
}

TEST(GpuTaskGraphAnalysis, DISABLED_ResourceVersionProducedOneClobberBenchmark4096Tasks){
    BenchmarkVersionChain(Graphics::GpuGraphResourceVersionOrigin::TaskProduced, true);
}

TEST(GpuTaskGraphAnalysis, DISABLED_DistinctProducedVersionBenchmark1024Versions){
    BenchmarkDistinctVersions(1024u);
}

TEST(GpuTaskGraphAnalysis, DISABLED_DistinctProducedVersionBenchmark2048Versions){
    BenchmarkDistinctVersions(2048u);
}

TEST(GpuTaskGraphAnalysis, DISABLED_PresentationAncestryBenchmark4096Tasks){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backBuffer = AddPresentationTexture(
        testArena,
        context,
        allocator,
        graph,
        Name("tests/analysis_optimization/presentation_benchmark_buffer"),
        "Presentation Benchmark Buffer",
        Graphics::ResourceStates::Unknown
    );
    ASSERT_TRUE(backBuffer.valid());
    Graphics::GpuTaskId writer;
    Graphics::GpuTaskId producer;
    AppendPresentationChain(graph, backBuffer, s_BenchmarkTaskCount, writer, producer);
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(graph.declarePresentEndpoint(Graphics::GpuPresentEndpoint{ .producer = producer, .backBuffer = backBuffer }));
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(Name("tests/analysis_optimization/presentation_scratch"));
    MeasureAnalysis(graph, analysis, scratchArena);
    EXPECT_EQ(analysis.edges().size(), s_BenchmarkTaskCount * 2u - 3u);
    EXPECT_EQ(analysis.schedulingEdges().size(), s_BenchmarkTaskCount - 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

