// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/algorithm.h>
#include <global/containers.h>
#include <global/disjoint_set.h>
#include <global/interval.h>
#include <global/limit.h>
#include <global/projected_index.h>
#include <global/range_algorithms.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_collection_algorithm_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, DirectionalSearchHandlesChangingCountsAndThrows){
    u32 countCalls = 0u;
    const auto countRows = [&countCalls]()noexcept{ ++countCalls; return u64(3u); };
    const auto matchFirst = [](const u64 index)noexcept{ return index == 0u; };
    const auto firstReverse = FindDirectionalIndex(2u, true, countRows, matchFirst);
    ASSERT_TRUE(firstReverse);
    EXPECT_EQ(*firstReverse, 0u);
    EXPECT_EQ(countCalls, 4u);
    countCalls = 0u;
    EXPECT_FALSE(FindDirectionalIndex(3u, false, countRows, matchFirst));
    EXPECT_EQ(countCalls, 1u);
    const auto matchNone = [](const u64)noexcept{ return false; };
    EXPECT_FALSE(FindDirectionalIndex(0u, true, countRows, matchNone));
    const auto throwingMatch = [](const u64) -> b8{ throw RuntimeException("directional predicate failure"); };
    static_assert(!noexcept(FindDirectionalIndex(0u, false, countRows, throwingMatch)));
    EXPECT_THROW(static_cast<void>(FindDirectionalIndex(0u, false, countRows, throwingMatch)), RuntimeException);
    u32 shrinkingCalls = 0u;
    u32 matchedCalls = 0u;
    const auto shrinkingCount = [&shrinkingCalls]()noexcept{ return ++shrinkingCalls == 1u ? u64(3u) : u64(1u); };
    const auto trackMatch = [&matchedCalls](const u64)noexcept{ ++matchedCalls; return true; };
    EXPECT_FALSE(FindDirectionalIndex(2u, false, shrinkingCount, trackMatch));
    EXPECT_EQ(shrinkingCalls, 2u);
    EXPECT_EQ(matchedCalls, 0u);
    const auto maximumRowCount = []()noexcept{ return Limit<u64>::s_Max; };
    EXPECT_FALSE(FindDirectionalIndex(Limit<u64>::s_Max - 1u, false, maximumRowCount, matchNone));
    const auto throwingCount = []() -> u64{ throw RuntimeException("directional count failure"); };
    static_assert(!noexcept(FindDirectionalIndex(0u, false, throwingCount, matchNone)));
    EXPECT_THROW(static_cast<void>(FindDirectionalIndex(0u, false, throwingCount, matchNone)), RuntimeException);
}

TEST(Global, CollectionValidationAndOrderingPreserveBoundaries){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    const Array<u32, 3u> valuesToValidate{ 2u, 3u, 0u };
    Array<u32, 4u> validationEvents{};
    usize validationEventCount = 0u;
    const auto recordValid = [&validationEvents, &validationEventCount](const u32 value)noexcept{ validationEvents[validationEventCount++] = 100u + value; return value != 0u; };
    const auto recordDuplicate = [&validationEvents, &validationEventCount](const u32 current, const u32 previous)noexcept{ validationEvents[validationEventCount++] = current * 10u + previous; return current == previous; };
    EXPECT_TRUE(AllValidAndDistinct(Span<const u32>{}, recordValid, recordDuplicate));
    EXPECT_TRUE(AllValidAndDistinct(Span<const u32>(valuesToValidate.data(), 1u), [](const u32 value)noexcept{ return value != 0u; }, [](const u32 lhs, const u32 rhs)noexcept{ return lhs == rhs; }));
    EXPECT_FALSE(AllValidAndDistinct(Span<const u32>(valuesToValidate), recordValid, recordDuplicate));
    EXPECT_EQ(validationEventCount, 4u);
    EXPECT_EQ(validationEvents, (Array<u32, 4u>{ 102u, 103u, 32u, 100u }));
    const Array<u32, 4u> duplicateValues{ 2u, 3u, 3u, 5u };
    u32 visitedValues = 0u;
    u32 comparedValues = 0u;
    const auto countValid = [&visitedValues](const u32)noexcept{ ++visitedValues; return true; };
    const auto countDuplicate = [&comparedValues](const u32 current, const u32 previous)noexcept{ ++comparedValues; return current == previous; };
    EXPECT_FALSE(AllValidAndDistinct(Span<const u32>(duplicateValues), countValid, countDuplicate));
    EXPECT_EQ(visitedValues, 3u);
    EXPECT_EQ(comparedValues, 3u);
    const auto throwingValid = [](const u32) -> b8{ throw RuntimeException("validity predicate failure"); };
    static_assert(!noexcept(AllValidAndDistinct(Span<const u32>{}, throwingValid, recordDuplicate)));
    EXPECT_THROW(static_cast<void>(AllValidAndDistinct(Span<const u32>(valuesToValidate), throwingValid, recordDuplicate)), RuntimeException);
    struct KeyedValue{ u32 key; u32 order; };
    Vector<KeyedValue, NWB::Core::Alloc::ScratchArena> duplicates(scratchArena);
    duplicates.push_back({ 2u, 0u });
    duplicates.push_back({ 1u, 1u });
    duplicates.push_back({ 2u, 2u });
    duplicates.push_back({ 1u, 3u });
    duplicates.push_back({ 3u, 4u });
    StableDeduplicate(duplicates, [](const KeyedValue& lhs, const KeyedValue& rhs)noexcept{ return lhs.key == rhs.key; });
    ASSERT_EQ(duplicates.size(), 3u);
    EXPECT_EQ(duplicates[0u].order, 0u);
    EXPECT_EQ(duplicates[1u].order, 1u);
    EXPECT_EQ(duplicates[2u].order, 4u);
    const auto throwingSameKey = [](const KeyedValue&, const KeyedValue&) -> b8{ throw RuntimeException("deduplication predicate failure"); };
    EXPECT_THROW(StableDeduplicate(duplicates, throwingSameKey), RuntimeException);
    EXPECT_EQ(duplicates.size(), 3u);
    struct ThrowingAssignment{
        u32 key = 0u;
        ThrowingAssignment& operator=(ThrowingAssignment& other)noexcept{ key = other.key; return *this; }
        ThrowingAssignment& operator=(const ThrowingAssignment&){ throw RuntimeException("const assignment failure"); }
    };
    struct SmallSequence{
        Array<ThrowingAssignment, 3u> entries{ ThrowingAssignment{ 1u }, ThrowingAssignment{ 1u }, ThrowingAssignment{ 2u } };
        usize count = 3u;
        [[nodiscard]] usize size()const noexcept{ return count; }
        ThrowingAssignment& operator[](const usize index)noexcept{ return entries[index]; }
        void resize(const usize value)noexcept{ count = value; }
    };
    struct ThrowingComparison{
        [[nodiscard]] b8 operator()(ThrowingAssignment& lhs, ThrowingAssignment& rhs)const noexcept{ return lhs.key == rhs.key; }
        [[nodiscard]] b8 operator()(ThrowingAssignment&, const ThrowingAssignment&)const{ throw RuntimeException("const comparison failure"); }
    };
    const auto sameAssignmentKey = [](const ThrowingAssignment& lhs, const ThrowingAssignment& rhs)noexcept{ return lhs.key == rhs.key; };
    SmallSequence assignmentValues;
    static_assert(!noexcept(StableDeduplicate(assignmentValues, sameAssignmentKey)));
    EXPECT_THROW(StableDeduplicate(assignmentValues, sameAssignmentKey), RuntimeException);
    SmallSequence comparisonValues;
    const ThrowingComparison throwingComparison;
    static_assert(!noexcept(StableDeduplicate(comparisonValues, throwingComparison)));
    EXPECT_THROW(StableDeduplicate(comparisonValues, throwingComparison), RuntimeException);
    struct ThrowingIndexSequence{
        KeyedValue value{};
        [[nodiscard]] usize size()const noexcept{ return 1u; }
        KeyedValue& operator[](usize&){ throw RuntimeException("lvalue index failure"); }
        KeyedValue& operator[](usize&&)noexcept{ return value; }
        void resize(usize&)noexcept{}
        void resize(usize&&)noexcept{}
    };
    const auto sameKeyedValue = [](const KeyedValue& lhs, const KeyedValue& rhs)noexcept{ return lhs.key == rhs.key; };
    ThrowingIndexSequence indexValues;
    static_assert(!noexcept(StableDeduplicate(indexValues, sameKeyedValue)));
    EXPECT_THROW(StableDeduplicate(indexValues, sameKeyedValue), RuntimeException);
    struct ThrowingResizeSequence{
        KeyedValue value{};
        [[nodiscard]] usize size()const noexcept{ return 0u; }
        KeyedValue& operator[](usize&)noexcept{ return value; }
        KeyedValue& operator[](usize&&)noexcept{ return value; }
        void resize(usize&){ throw RuntimeException("lvalue resize failure"); }
        void resize(usize&&)noexcept{}
    };
    ThrowingResizeSequence resizeValues;
    static_assert(!noexcept(StableDeduplicate(resizeValues, sameKeyedValue)));
    EXPECT_THROW(StableDeduplicate(resizeValues, sameKeyedValue), RuntimeException);
    const Array<i32, 4u> projectedValues{ 3, 0, 1, -2 };
    Vector<usize, NWB::Core::Alloc::ScratchArena> projectedIndices(scratchArena);
    BuildProjectedRecordIndex(projectedIndices, projectedValues, [](const i32 value)noexcept{ return value != 0; }, [](const i32 lhs, const i32 rhs)noexcept{ return lhs < rhs; });
    ASSERT_EQ(projectedIndices.size(), 3u);
    EXPECT_EQ(projectedIndices[0u], 3u);
    EXPECT_EQ(projectedIndices[1u], 2u);
    EXPECT_EQ(projectedIndices[2u], 0u);
    const u32 one = 1u;
    const u32 two = 2u;
    const u32 three = 3u;
    const Array<const u32*, 4u> items{ &one, &two, nullptr, &three };
    Vector<u8, NWB::Core::Alloc::ScratchArena> state(items.size(), 0u, scratchArena);
    Vector<const u32*, NWB::Core::Alloc::ScratchArena> sorted(scratchArena);
    TopologicallySortPointers(Span<const u32* const>(items.data(), items.size()), [](const u32 current, const u32 dependency)noexcept{
        return (current == 1u && dependency == 2u) || (current == 2u && dependency == 1u);
    }, state, sorted);
    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0u], &two);
    EXPECT_EQ(sorted[1u], &one);
    EXPECT_EQ(sorted[2u], nullptr);
    EXPECT_EQ(sorted[3u], &three);
}

TEST(Global, IntervalCoverageHandlesOverlapAndThrows){
    struct TestInterval{ u64 first; u64 last; u32 index; };
    const Array<TestInterval, 3u> unsortedIntervals{ TestInterval{ 5u, 10u, 0u }, TestInterval{ 0u, 5u, 1u }, TestInterval{ 2u, 3u, 2u } };
    const auto eligible = [](const TestInterval&)noexcept{ return true; };
    const auto intervalBegin = [](const TestInterval& interval)noexcept{ return interval.first; };
    const auto intervalEnd = [](const TestInterval& interval)noexcept{ return interval.last; };
    static_assert(noexcept(CoversHalfOpenRange(0u, 0u, Span<const TestInterval>{}, eligible, intervalBegin, intervalEnd)));
    EXPECT_TRUE(CoversHalfOpenRange(0u, 0u, Span<const TestInterval>{}, eligible, intervalBegin, intervalEnd));
    const auto throwingEligible = [](const TestInterval&) -> b8{ throw RuntimeException("interval predicate failure"); };
    static_assert(!noexcept(CoversHalfOpenRange(0u, 1u, Span<const TestInterval>{}, throwingEligible, intervalBegin, intervalEnd)));
    EXPECT_THROW(static_cast<void>(CoversHalfOpenRange(0u, 10u, Span<const TestInterval>(unsortedIntervals), throwingEligible, intervalBegin, intervalEnd)), RuntimeException);
    EXPECT_TRUE(CoversHalfOpenRange(0u, 10u, Span<const TestInterval>(unsortedIntervals), eligible, intervalBegin, intervalEnd));
    EXPECT_FALSE(CoversHalfOpenRange(0u, 11u, Span<const TestInterval>(unsortedIntervals), eligible, intervalBegin, intervalEnd));
    const Array<TestInterval, 5u> sortedIntervals{ TestInterval{ 0u, 10u, 0u }, TestInterval{ 1u, 2u, 1u }, TestInterval{ 3u, 4u, 2u }, TestInterval{ 10u, 11u, 3u }, TestInterval{ 10u, 11u, 4u } };
    Array<u8, 5u> overlapFlags{};
    const auto noVisit = [](const TestInterval&)noexcept{};
    static_assert(noexcept(VisitOverlappingSortedIntervals(Span<const TestInterval>{}, intervalBegin, intervalEnd, noVisit)));
    const auto throwingVisit = [](const TestInterval&){ throw RuntimeException("interval visitor failure"); };
    static_assert(!noexcept(VisitOverlappingSortedIntervals(Span<const TestInterval>{}, intervalBegin, intervalEnd, throwingVisit)));
    EXPECT_THROW(VisitOverlappingSortedIntervals(Span<const TestInterval>(sortedIntervals), intervalBegin, intervalEnd, throwingVisit), RuntimeException);
    VisitOverlappingSortedIntervals(Span<const TestInterval>(sortedIntervals), intervalBegin, intervalEnd, [&overlapFlags](const TestInterval& interval)noexcept{ overlapFlags[interval.index] = 1u; });
    for(const u8 overlap : overlapFlags)
        EXPECT_EQ(overlap, 1u);
}

TEST(Global, DisjointSetJoinsPreserveRoots){
    Array<u32, 4> parents{ 0u, 1u, 2u, 3u };
    Array<u8, 4> ranks{ 0u, 0u, 0u, 0u };
    JoinDisjointSets(0u, 1u, parents, ranks);
    JoinDisjointSets(2u, 3u, parents, ranks);
    JoinDisjointSets(0u, 2u, parents, ranks);
    EXPECT_EQ(FindDisjointRoot(3u, parents), 0u);
    EXPECT_EQ(parents[3u], 0u);
    EXPECT_EQ(ranks[0u], 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

