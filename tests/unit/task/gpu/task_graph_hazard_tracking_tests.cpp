// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_hazard_tracking_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


[[nodiscard]] static Name IndexedName(const Name& prefix, const usize index){
    char text[32u];
    return DeriveName(prefix, FormatDecimal(index, text));
}

[[nodiscard]] static Graphics::GpuTaskResourceUse BufferUse(
    const Graphics::GpuGraphResourceId resource,
    const u64 offset,
    const u64 size,
    const Graphics::GpuTaskResourceAccess::Enum access
){
    return Graphics::GpuTaskResourceUse{
        .resource = resource,
        .range = Graphics::GpuTaskResourceRange{ .bufferRange = Graphics::BufferRange(offset, size) },
        .requiredState = access == Graphics::GpuTaskResourceAccess::Read
            ? Graphics::ResourceStates::ShaderResource
            : Graphics::ResourceStates::UnorderedAccess,
        .access = access,
    };
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

