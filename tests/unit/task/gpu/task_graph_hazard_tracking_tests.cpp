// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_hazard_tracking_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

inline constexpr Name s_HazardScratchOwner("tests/hazard_tracking/analysis_scratch");


[[nodiscard]] static ArenaMemoryStats HazardScratchOwnerStats(){
    const ArenaMemoryOwnerRecord* record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot snapshot;
        record = ReadArenaMemoryOwnerRecord(*record, snapshot);
        if(snapshot.source == ArenaMemorySource::Arena && snapshot.ownerName == s_HazardScratchOwner)
            return snapshot.stats;
    }
    return {};
}

[[nodiscard]] static Name IndexedName(const Name& prefix, const usize index){
    char text[32u];
    return DeriveName(prefix, FormatDecimal(index, text));
}

[[nodiscard]] static Graphics::GpuTaskResourceUse BufferUse(
    const Graphics::GpuGraphResourceId resource,
    const u64 offset,
    const u64 size,
    const Graphics::GpuTaskResourceAccess::Enum access){
    return Graphics::GpuTaskResourceUse{
        .resource = resource,
        .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(offset, size) },
        .requiredState = access == Graphics::GpuTaskResourceAccess::Read
            ? Graphics::ResourceStates::ShaderResource
            : Graphics::ResourceStates::UnorderedAccess,
        .access = access,
    };
}

void BenchmarkHazards(
    const usize resourceCount,
    const usize taskCount,
    const usize repetitions,
    const usize importedResourceCount = 0u){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Core::Alloc::ScratchArena inputArena(s_TaskGraphScratchArena);
    Vector<Graphics::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(inputArena);
    const usize declaredResourceCount = importedResourceCount > resourceCount ? importedResourceCount : resourceCount;
    resources.reserve(declaredResourceCount);
    for(usize index = 0u; index < declaredResourceCount; ++index){
        const auto resource = AddHazardDomain(
            graph,
            IndexedName(Name("tests/hazard_tracking/resource"), index),
            "Hazard Resource"
        );
        ASSERT_TRUE(resource.valid());
        resources.push_back(resource);
    }
    for(usize index = 0u; index < taskCount; ++index){
        const Graphics::GpuTaskResourceUse use{
            .resource = resources[index % resourceCount],
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        ASSERT_TRUE(AddTask(
            graph,
            IndexedName(Name("tests/hazard_tracking/task"), index),
            "Hazard Task",
            nullptr,
            0u,
            &use,
            1u
        ).valid());
    }
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < repetitions; ++iteration)
        ASSERT_TRUE(Analyze(graph, analysis));
    const u64 elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    ASSERT_EQ(analysis.inferredEdges().size(), taskCount - resourceCount);
    for(usize index = 0u; index < analysis.inferredEdges().size(); ++index){
        const auto& edge = analysis.inferredEdges()[index];
        EXPECT_EQ(edge.producer.index, index);
        EXPECT_EQ(edge.consumer.index, index + resourceCount);
        EXPECT_EQ(edge.resource, resources[index % resourceCount]);
        EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::WriteAfterWrite);
    }
    char text[32u];
    const AStringView elapsed = FormatDecimal(elapsedNanoseconds, text);
    text[elapsed.size()] = '\0';
    testing::Test::RecordProperty("elapsed_ns", text);
    testing::Test::RecordProperty("resource_count", static_cast<int>(resourceCount));
    testing::Test::RecordProperty("declared_resource_count", static_cast<int>(declaredResourceCount));
    testing::Test::RecordProperty("task_count", static_cast<int>(taskCount));
    testing::Test::RecordProperty("repetitions", static_cast<int>(repetitions));

    const ArenaMemoryStats beforeScratch = HazardScratchOwnerStats();
    {
        Core::Alloc::ScratchArena analysisArena(s_HazardScratchOwner);
        const Graphics::GpuTaskGraphCompiler compiler;
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(compiler.analyze(declarations, analysis, analysisArena));
        const ArenaMemoryStats scratchStats = analysisArena.memoryStats();
        testing::Test::RecordProperty("scratch_peak_bytes", static_cast<int>(scratchStats.peakUsedBytes));
        testing::Test::RecordProperty("scratch_reserved_bytes", static_cast<int>(scratchStats.reservedBytes));
        testing::Test::RecordProperty("scratch_retained_bytes", static_cast<int>(scratchStats.usedBytes));
    }
    const ArenaMemoryStats afterScratch = HazardScratchOwnerStats();
    EXPECT_EQ(afterScratch.usedBytes, beforeScratch.usedBytes);
    EXPECT_EQ(afterScratch.reservedBytes, beforeScratch.reservedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, HazardTrackingPreservesResourceAndSurvivingWriterOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto bufferA = AddBufferMetadata(graph, Name("tests/hazard_tracking/a"), "A");
    const auto bufferB = AddBufferMetadata(graph, Name("tests/hazard_tracking/b"), "B");
    ASSERT_TRUE(bufferA.valid());
    ASSERT_TRUE(bufferB.valid());
    const Graphics::GpuTaskResourceUse uses[] = {
        BufferUse(bufferA, 0u, 16u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferB, 0u, 64u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 16u, 16u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 32u, 16u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 16u, 16u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 0u, 48u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(bufferA, 0u, 48u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 0u, 48u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(bufferB, 0u, 64u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(bufferA, 16u, 16u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 0u, 48u, Graphics::GpuTaskResourceAccess::Read),
    };
    Graphics::GpuTaskId tasks[LengthOf(uses)];
    for(usize index = 0u; index < LengthOf(uses); ++index){
        tasks[index] = AddTask(
            graph,
            IndexedName(Name("tests/hazard_tracking/task"), index),
            "Task",
            nullptr,
            0u,
            &uses[index],
            1u
        );
        ASSERT_TRUE(tasks[index].valid());
    }
    struct ExpectedHazard{
        usize producer;
        usize consumer;
        Graphics::GpuTaskHazardType::Enum hazard;
    };
    const ExpectedHazard expected[] = {
        { 2u, 4u, Graphics::GpuTaskHazardType::WriteAfterWrite },
        { 0u, 5u, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { 3u, 5u, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { 4u, 5u, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { 0u, 6u, Graphics::GpuTaskHazardType::WriteAfterWrite },
        { 3u, 6u, Graphics::GpuTaskHazardType::WriteAfterWrite },
        { 4u, 6u, Graphics::GpuTaskHazardType::WriteAfterWrite },
        { 5u, 6u, Graphics::GpuTaskHazardType::WriteAfterRead },
        { 6u, 7u, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { 1u, 8u, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { 6u, 9u, Graphics::GpuTaskHazardType::WriteAfterWrite },
        { 7u, 9u, Graphics::GpuTaskHazardType::WriteAfterRead },
        { 6u, 10u, Graphics::GpuTaskHazardType::ReadAfterWrite },
        { 9u, 10u, Graphics::GpuTaskHazardType::ReadAfterWrite },
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(expected));
    for(usize index = 0u; index < LengthOf(expected); ++index){
        const auto& edge = analysis.inferredEdges()[index];
        EXPECT_EQ(edge.producer, tasks[expected[index].producer]);
        EXPECT_EQ(edge.consumer, tasks[expected[index].consumer]);
        EXPECT_EQ(edge.hazard, expected[index].hazard);
        EXPECT_EQ(edge.resource, expected[index].consumer == 8u ? bufferB : bufferA);
    }
}

TEST(GpuTaskGraph, HazardTrackingRetiresOnlyCoveredReadersAndPreservesAppendOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto buffer = AddBufferMetadata(graph, Name("tests/hazard_tracking/readers"), "Readers");
    ASSERT_TRUE(buffer.valid());
    const Graphics::GpuTaskResourceUse uses[] = {
        BufferUse(buffer, 0u, 16u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 16u, 16u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 32u, 16u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 16u, 16u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(buffer, 48u, 16u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 0u, 64u, Graphics::GpuTaskResourceAccess::Write),
        BufferUse(buffer, 0u, 16u, Graphics::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 0u, 64u, Graphics::GpuTaskResourceAccess::Write),
    };
    Graphics::GpuTaskId tasks[LengthOf(uses)];
    for(usize index = 0u; index < LengthOf(uses); ++index){
        tasks[index] = AddTask(
            graph,
            IndexedName(Name("tests/hazard_tracking/read_task"), index),
            "Read Task",
            nullptr,
            0u,
            &uses[index],
            1u
        );
        ASSERT_TRUE(tasks[index].valid());
    }
    const usize expectedPairs[][2u] = {
        { 1u, 3u }, { 3u, 5u }, { 0u, 5u }, { 2u, 5u }, { 4u, 5u }, { 5u, 6u }, { 5u, 7u }, { 6u, 7u },
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(expectedPairs));
    for(usize index = 0u; index < LengthOf(expectedPairs); ++index){
        EXPECT_EQ(analysis.inferredEdges()[index].producer, tasks[expectedPairs[index][0u]]);
        EXPECT_EQ(analysis.inferredEdges()[index].consumer, tasks[expectedPairs[index][1u]]);
    }
}

TEST(GpuTaskGraph, HazardTrackingPreservesSameTaskAccessAndResetSemantics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    for(usize iteration = 0u; iteration < 2u; ++iteration){
        const auto buffer = AddBufferMetadata(graph, Name("tests/hazard_tracking/same_task"), "Same Task");
        ASSERT_TRUE(buffer.valid());
        const Graphics::GpuTaskResourceUse firstUses[] = {
            BufferUse(buffer, 0u, 32u, Graphics::GpuTaskResourceAccess::Write),
            BufferUse(buffer, 0u, 16u, Graphics::GpuTaskResourceAccess::Read),
            BufferUse(buffer, 32u, 16u, Graphics::GpuTaskResourceAccess::Read),
        };
        const auto first = AddTask(
            graph,
            Name("tests/hazard_tracking/first"),
            "First",
            nullptr,
            0u,
            firstUses,
            LengthOf(firstUses)
        );
        const auto secondUse = BufferUse(buffer, 0u, 48u, Graphics::GpuTaskResourceAccess::Write);
        const auto second = AddTask(
            graph,
            Name("tests/hazard_tracking/second"),
            "Second",
            nullptr,
            0u,
            &secondUse,
            1u
        );
        const auto thirdUse = BufferUse(buffer, 0u, 48u, Graphics::GpuTaskResourceAccess::ReadWrite);
        const auto third = AddTask(
            graph,
            Name("tests/hazard_tracking/third"),
            "Third",
            nullptr,
            0u,
            &thirdUse,
            1u
        );
        ASSERT_TRUE(first.valid());
        ASSERT_TRUE(second.valid());
        ASSERT_TRUE(third.valid());
        ASSERT_TRUE(Analyze(graph, analysis));
        ASSERT_EQ(analysis.inferredEdges().size(), 4u);
        EXPECT_TRUE(HasInferredHazard(analysis, first, second, buffer, Graphics::GpuTaskHazardType::WriteAfterWrite));
        EXPECT_TRUE(HasInferredHazard(analysis, first, second, buffer, Graphics::GpuTaskHazardType::WriteAfterRead));
        EXPECT_TRUE(HasInferredHazard(analysis, second, third, buffer, Graphics::GpuTaskHazardType::ReadAfterWrite));
        EXPECT_TRUE(HasInferredHazard(analysis, second, third, buffer, Graphics::GpuTaskHazardType::WriteAfterWrite));
        graph.reset();
    }
}

TEST(GpuTaskGraph, DISABLED_HazardTrackingBenchmarkIndependentResources){
    BenchmarkHazards(4096u, 8192u, 4u);
}

TEST(GpuTaskGraph, DISABLED_HazardTrackingBenchmarkRepeatedOverwrites){
    BenchmarkHazards(1u, 4096u, 4u);
}

TEST(GpuTaskGraph, DISABLED_HazardTrackingBenchmarkMediumGraph){
    BenchmarkHazards(64u, 128u, 64u);
}

TEST(GpuTaskGraph, DISABLED_HazardTrackingBenchmarkSparseResourceUse){
    BenchmarkHazards(4u, 16u, 256u, 4096u);
}

TEST(GpuTaskGraph, DISABLED_HazardTrackingBenchmarkSmallGraph){
    BenchmarkHazards(4u, 16u, 256u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

