// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/task/gpu/compiler_internal.h>

#include <global/math/vector_double.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_range_planning_benchmark_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Workload{
    enum Enum : u8{
        WholeOverwrites,
        PartialOverwrites,
        InternalWholeUses,
        FragmentedFanIn,
    };
};

constexpr usize s_Repetitions = 64u;
constexpr u64 s_BufferBytes = 4096u;
constexpr u64 s_PartialBytes = s_BufferBytes / 2u;
constexpr u64 s_FragmentBytes = 8u;
inline constexpr Name s_InputArena("tests/task/gpu/range_planning_inputs");
inline constexpr Name s_CompileArena("tests/task/gpu/range_planning_compile");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuTaskResourceUse BufferUse(
    const Core::GpuGraphResourceId resource,
    const u64 offset,
    const u64 size,
    const Core::ResourceStates::Mask state,
    const Core::GpuTaskResourceAccess::Enum access = Core::GpuTaskResourceAccess::Write
)noexcept{
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = { .bufferRange = Core::BufferRange(offset, size) },
        .requiredState = state,
        .access = access,
    };
}

void BenchmarkRangePlanning(const Workload::Enum workload, const usize count){
    TestArena<struct RangePlanningBenchmarkTag> testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Core::GpuPhysicalQueueInfo queue{
        .familyIndex = 0u,
        .queueIndex = 0u,
        .id = { .index = 0u, .deviceGeneration = 1u },
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer,
    };
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    const Core::GpuGraphResourceId buffer = graph.importResource(Core::GpuGraphResourceDesc{}
        .setIdentity(Name("tests/range_planning/buffer"))
        .setMarkerLabel("Range Planning Buffer")
        .setType(Core::GpuGraphResourceType::Buffer)
        .setInitialState(Core::ResourceStates::Common)
        .setExternalFinalState(Core::ResourceStates::ShaderResource)
        .setExternalFinalReleaseDestinationQueue(queue.id)
    );
    ASSERT_TRUE(buffer.valid());

    Core::Alloc::ScratchArena inputArena(s_InputArena);
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> tasks(inputArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> internalUses(inputArena);
    const usize taskCount = workload == Workload::InternalWholeUses ? 1u
        : count + (workload == Workload::FragmentedFanIn ? 1u : 0u);
    tasks.reserve(taskCount);
    if(workload == Workload::InternalWholeUses){
        internalUses.reserve(count);
        for(usize index = 0u; index < count; ++index){
            internalUses.push_back(BufferUse(buffer, 0u, s_BufferBytes,
                index % 2u == 0u ? Core::ResourceStates::CopyDest : Core::ResourceStates::UnorderedAccess));
        }
    }
    const Core::GpuTaskCommandRequirements commands{ .requiredCapabilities = Core::GpuQueueCapability::Graphics };
    for(usize index = 0u; index < taskCount; ++index){
        Core::GpuTaskResourceUse use = BufferUse(buffer, 0u, s_BufferBytes,
            index % 2u == 0u ? Core::ResourceStates::CopyDest : Core::ResourceStates::UnorderedAccess);
        if(workload == Workload::PartialOverwrites && index != 0u)
            use.range.bufferRange = Core::BufferRange(index % 2u == 0u ? s_PartialBytes : 0u, s_PartialBytes);
        else if(workload == Workload::FragmentedFanIn){
            use = index == count
                ? BufferUse(buffer, 0u, count * s_FragmentBytes, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read)
                : BufferUse(buffer, index * s_FragmentBytes, s_FragmentBytes, Core::ResourceStates::CopyDest);
        }
        char text[32u] = {};
        Core::GpuTaskDesc desc;
        desc
            .setIdentity(DeriveName(Name("tests/range_planning/task/"), FormatDecimal(index, text)))
            .setMarkerLabel("Range Planning Task")
            .setResourceUses(workload == Workload::InternalWholeUses ? internalUses.data() : &use,
                workload == Workload::InternalWholeUses ? internalUses.size() : 1u)
        ;
        if(!tasks.empty())
            desc.setDependencies(&tasks.back(), 1u);
        const Core::GpuTaskId task = graph.addTask(desc, commands);
        ASSERT_TRUE(task.valid());
        tasks.push_back(task);
    }

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    const Core::GpuTaskGraphCompiler compiler;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    Core::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    {
        Core::Alloc::ScratchArena scratchArena(s_CompileArena);
        ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options));
    }

    u64 elapsedNanoseconds = 0u;
    f64 compileSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    u64 scratchPeakBytes = 0u;
    u64 scratchRetainedBytes = 0u;
    u64 scratchAllocationCount = 0u;
    for(usize repetition = 0u; repetition < s_Repetitions; ++repetition){
        Core::Alloc::ScratchArena scratchArena(s_CompileArena);
        const Timer begin = TimerNow();
        const bool compiled = compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options);
        elapsedNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(compiled);
        const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
        const Core::GpuTaskGraphCompileStatistics statistics = plan.compileStatistics();
        compileSeconds += statistics.totalSeconds;
        resourceStatePlanningSeconds += statistics.resourceStatePlanningSeconds;
        const ArenaMemoryStats memory = scratchArena.memoryStats();
        scratchPeakBytes = Max(scratchPeakBytes, memory.peakUsedBytes);
        scratchRetainedBytes = Max(scratchRetainedBytes, memory.usedBytes);
        scratchAllocationCount = Max(scratchAllocationCount, memory.allocationCount);
    }

    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_EQ(plan.taskCount(), taskCount);
    const Core::GpuCompiledTaskView terminalTask = plan.findTask(tasks.back());
    ASSERT_TRUE(terminalTask.valid());
    const Core::GpuCompiledExternalResourceExportView exported = plan.externalResourceExport(buffer);
    ASSERT_TRUE(exported.valid());
    ASSERT_EQ(exported.plan->finalState, Core::ResourceStates::ShaderResource);
    if(workload == Workload::PartialOverwrites){
        ASSERT_EQ(exported.plan->sourceCount, 2u);
        EXPECT_EQ(exported.sources[0u].producerTask, tasks[count - 2u]);
        EXPECT_EQ(exported.sources[0u].range.bufferRange, Core::BufferRange(s_PartialBytes, s_PartialBytes));
        EXPECT_EQ(exported.sources[1u].producerTask, tasks.back());
        EXPECT_EQ(exported.sources[1u].range.bufferRange, Core::BufferRange(0u, s_PartialBytes));
    }
    else{
        ASSERT_EQ(exported.plan->sourceCount, 1u);
        EXPECT_EQ(exported.sources[0u].producerTask, tasks.back());
        EXPECT_EQ(exported.sources[0u].range.bufferRange,
            Core::BufferRange(0u, workload == Workload::FragmentedFanIn ? count * s_FragmentBytes : s_BufferBytes));
        ASSERT_EQ(terminalTask.plan->epilogueBarrierCount, 1u);
        EXPECT_EQ(terminalTask.epilogueBarriers[0u].type, Core::GpuCompiledBarrierType::BufferStateExport);
        EXPECT_EQ(terminalTask.epilogueBarriers[0u].after, Core::ResourceStates::ShaderResource);
    }
    if(workload == Workload::InternalWholeUses){
        ASSERT_EQ(terminalTask.plan->prologueBarrierCount, 1u);
        EXPECT_EQ(terminalTask.prologueBarriers[0u].range.bufferRange, Core::BufferRange(0u, s_BufferBytes));
        EXPECT_EQ(terminalTask.epilogueBarriers[0u].before, Core::ResourceStates::UnorderedAccess);
    }
    else if(workload == Workload::FragmentedFanIn){
        ASSERT_EQ(terminalTask.plan->prologueBarrierCount, count);
        for(usize index = 0u; index < count; ++index){
            EXPECT_EQ(terminalTask.prologueBarriers[index].range.bufferRange,
                Core::BufferRange(index * s_FragmentBytes, s_FragmentBytes));
            EXPECT_EQ(terminalTask.prologueBarriers[index].before, Core::ResourceStates::CopyDest);
            EXPECT_EQ(terminalTask.prologueBarriers[index].after, Core::ResourceStates::ShaderResource);
        }
    }

    const SIMDVectorDouble compileNanoseconds = SIMDVectorDouble{ compileSeconds, resourceStatePlanningSeconds } * 1'000'000'000.0;
    RecordUnsignedTestProperty("elapsed_ns", elapsedNanoseconds);
    RecordUnsignedTestProperty("compile_ns", static_cast<u64>(compileNanoseconds.x));
    RecordUnsignedTestProperty("resource_state_planning_ns", static_cast<u64>(compileNanoseconds.y));
    RecordUnsignedTestProperty("scratch_peak_bytes", scratchPeakBytes);
    RecordUnsignedTestProperty("scratch_retained_bytes", scratchRetainedBytes);
    RecordUnsignedTestProperty("scratch_allocations_per_compile", scratchAllocationCount);
    RecordUnsignedTestProperty("repetitions", s_Repetitions);
    RecordUnsignedTestProperty("task_count", taskCount);
    RecordUnsignedTestProperty("resource_use_count", workload == Workload::InternalWholeUses ? count : taskCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphRangePlanning, DISABLED_WholeOverwriteBenchmark128Uses){
    BenchmarkRangePlanning(Workload::WholeOverwrites, 128u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_WholeOverwriteBenchmark512Uses){
    BenchmarkRangePlanning(Workload::WholeOverwrites, 512u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_PartialOverwriteBenchmark128Uses){
    BenchmarkRangePlanning(Workload::PartialOverwrites, 128u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_PartialOverwriteBenchmark512Uses){
    BenchmarkRangePlanning(Workload::PartialOverwrites, 512u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_InternalWholeUseBenchmark128Uses){
    BenchmarkRangePlanning(Workload::InternalWholeUses, 128u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_InternalWholeUseBenchmark512Uses){
    BenchmarkRangePlanning(Workload::InternalWholeUses, 512u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_FragmentedFanInBenchmark128Uses){
    BenchmarkRangePlanning(Workload::FragmentedFanIn, 128u);
}

TEST(GpuTaskGraphRangePlanning, DISABLED_FragmentedFanInBenchmark512Uses){
    BenchmarkRangePlanning(Workload::FragmentedFanIn, 512u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

