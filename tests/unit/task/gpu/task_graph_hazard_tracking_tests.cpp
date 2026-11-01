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


[[nodiscard]] static Core::GpuTaskResourceUse BufferUse(const Core::GpuGraphResourceId resource, const u64 offset, const u64 size, const Core::GpuTaskResourceAccess::Enum access)noexcept{
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(offset, size) },
        .requiredState = access == Core::GpuTaskResourceAccess::Read ? Core::ResourceStates::ShaderResource : Core::ResourceStates::UnorderedAccess,
        .access = access,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, HazardTrackingPreservesResourceAndSurvivingWriterOrder){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferAResult = AddBufferMetadata(graph, Name("tests/hazard_tracking/a"), "A");
    ASSERT_TRUE(bufferAResult);
    const auto bufferA = *bufferAResult;
    const auto bufferBResult = AddBufferMetadata(graph, Name("tests/hazard_tracking/b"), "B");
    ASSERT_TRUE(bufferBResult);
    const auto bufferB = *bufferBResult;
    ASSERT_TRUE(bufferA.valid());
    ASSERT_TRUE(bufferB.valid());
    const Array<Core::GpuTaskResourceUse, 11> uses = {
        BufferUse(bufferA, 0u, 16u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferB, 0u, 64u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 16u, 16u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 32u, 16u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 16u, 16u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 0u, 48u, Core::GpuTaskResourceAccess::Read),
        BufferUse(bufferA, 0u, 48u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 0u, 48u, Core::GpuTaskResourceAccess::Read),
        BufferUse(bufferB, 0u, 64u, Core::GpuTaskResourceAccess::Read),
        BufferUse(bufferA, 16u, 16u, Core::GpuTaskResourceAccess::Write),
        BufferUse(bufferA, 0u, 48u, Core::GpuTaskResourceAccess::Read),
    };
    Array<Core::GpuTaskId, LengthOf(uses)> tasks{};
    for(usize index = 0u; index < LengthOf(uses); ++index){
        const auto derivedNameResult = IndexedName(Name("tests/hazard_tracking/task"), index);
        ASSERT_TRUE(derivedNameResult);
        const auto taskAdmission = AddTask(*derivedNameResult, "Task", {}, Span(&uses[index], 1u), {}, {}, {}, graph);
        ASSERT_TRUE(taskAdmission);
        tasks[index] = *taskAdmission;
        ASSERT_TRUE(tasks[index].valid());
    }
    struct ExpectedHazard{
        usize producer;
        usize consumer;
        Core::GpuTaskHazardType::Enum hazard;
    };
    const Array<ExpectedHazard, 14> expected = {
        ExpectedHazard{ 2u, 4u, Core::GpuTaskHazardType::WriteAfterWrite },
        ExpectedHazard{ 0u, 5u, Core::GpuTaskHazardType::ReadAfterWrite },
        ExpectedHazard{ 3u, 5u, Core::GpuTaskHazardType::ReadAfterWrite },
        ExpectedHazard{ 4u, 5u, Core::GpuTaskHazardType::ReadAfterWrite },
        ExpectedHazard{ 0u, 6u, Core::GpuTaskHazardType::WriteAfterWrite },
        ExpectedHazard{ 3u, 6u, Core::GpuTaskHazardType::WriteAfterWrite },
        ExpectedHazard{ 4u, 6u, Core::GpuTaskHazardType::WriteAfterWrite },
        ExpectedHazard{ 5u, 6u, Core::GpuTaskHazardType::WriteAfterRead },
        ExpectedHazard{ 6u, 7u, Core::GpuTaskHazardType::ReadAfterWrite },
        ExpectedHazard{ 1u, 8u, Core::GpuTaskHazardType::ReadAfterWrite },
        ExpectedHazard{ 6u, 9u, Core::GpuTaskHazardType::WriteAfterWrite },
        ExpectedHazard{ 7u, 9u, Core::GpuTaskHazardType::WriteAfterRead },
        ExpectedHazard{ 6u, 10u, Core::GpuTaskHazardType::ReadAfterWrite },
        ExpectedHazard{ 9u, 10u, Core::GpuTaskHazardType::ReadAfterWrite },
    };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
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
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const auto bufferResult = AddBufferMetadata(graph, Name("tests/hazard_tracking/readers"), "Readers");
    ASSERT_TRUE(bufferResult);
    const auto buffer = *bufferResult;
    ASSERT_TRUE(buffer.valid());
    const Array<Core::GpuTaskResourceUse, 8> uses = {
        BufferUse(buffer, 0u, 16u, Core::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 16u, 16u, Core::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 32u, 16u, Core::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 16u, 16u, Core::GpuTaskResourceAccess::Write),
        BufferUse(buffer, 48u, 16u, Core::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 0u, 64u, Core::GpuTaskResourceAccess::Write),
        BufferUse(buffer, 0u, 16u, Core::GpuTaskResourceAccess::Read),
        BufferUse(buffer, 0u, 64u, Core::GpuTaskResourceAccess::Write),
    };
    Array<Core::GpuTaskId, LengthOf(uses)> tasks{};
    for(usize index = 0u; index < LengthOf(uses); ++index){
        const auto derivedNameResult = IndexedName(Name("tests/hazard_tracking/read_task"), index);
        ASSERT_TRUE(derivedNameResult);
        const auto taskAdmission = AddTask(*derivedNameResult, "Read Task", {}, Span(&uses[index], 1u), {}, {}, {}, graph);
        ASSERT_TRUE(taskAdmission);
        tasks[index] = *taskAdmission;
        ASSERT_TRUE(tasks[index].valid());
    }
    const Array<Array<usize, 2u>, 8> expectedPairs = {{ { 1u, 3u }, { 3u, 5u }, { 0u, 5u }, { 2u, 5u }, { 4u, 5u }, { 5u, 6u }, { 5u, 7u }, { 6u, 7u }, }};
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(expectedPairs));
    for(usize index = 0u; index < LengthOf(expectedPairs); ++index){
        EXPECT_EQ(analysis.inferredEdges()[index].producer, tasks[expectedPairs[index][0u]]);
        EXPECT_EQ(analysis.inferredEdges()[index].consumer, tasks[expectedPairs[index][1u]]);
    }
}

TEST(GpuTaskGraph, HazardTrackingPreservesSameTaskAccessAndResetSemantics){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    for(usize iteration = 0u; iteration < 2u; ++iteration){
        const auto bufferResult = AddBufferMetadata(graph, Name("tests/hazard_tracking/same_task"), "Same Task");
        ASSERT_TRUE(bufferResult);
        const auto buffer = *bufferResult;
        ASSERT_TRUE(buffer.valid());
        const Array<Core::GpuTaskResourceUse, 3> firstUses = {
            BufferUse(buffer, 0u, 32u, Core::GpuTaskResourceAccess::Write),
            BufferUse(buffer, 0u, 16u, Core::GpuTaskResourceAccess::Read),
            BufferUse(buffer, 32u, 16u, Core::GpuTaskResourceAccess::Read),
        };
        const auto firstResult = AddTask(Name("tests/hazard_tracking/first"), "First", {}, Span(firstUses), {}, {}, {}, graph);
        ASSERT_TRUE(firstResult);
        const auto first = *firstResult;
        const auto secondUse = BufferUse(buffer, 0u, 48u, Core::GpuTaskResourceAccess::Write);
        const auto secondResult = AddTask(Name("tests/hazard_tracking/second"), "Second", {}, Span(&secondUse, 1u), {}, {}, {}, graph);
        ASSERT_TRUE(secondResult);
        const auto second = *secondResult;
        const auto thirdUse = BufferUse(buffer, 0u, 48u, Core::GpuTaskResourceAccess::ReadWrite);
        const auto thirdResult = AddTask(Name("tests/hazard_tracking/third"), "Third", {}, Span(&thirdUse, 1u), {}, {}, {}, graph);
        ASSERT_TRUE(thirdResult);
        const auto third = *thirdResult;
        ASSERT_TRUE(first.valid());
        ASSERT_TRUE(second.valid());
        ASSERT_TRUE(third.valid());
        ASSERT_TRUE(Analyze(graph, operationScratchArena, analysis));
        ASSERT_EQ(analysis.inferredEdges().size(), 4u);
        EXPECT_TRUE(HasInferredHazard(analysis, first, second, buffer, Core::GpuTaskHazardType::WriteAfterWrite));
        EXPECT_TRUE(HasInferredHazard(analysis, first, second, buffer, Core::GpuTaskHazardType::WriteAfterRead));
        EXPECT_TRUE(HasInferredHazard(analysis, second, third, buffer, Core::GpuTaskHazardType::ReadAfterWrite));
        EXPECT_TRUE(HasInferredHazard(analysis, second, third, buffer, Core::GpuTaskHazardType::WriteAfterWrite));
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

