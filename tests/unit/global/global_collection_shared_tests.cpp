// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/atomic_identity.h>
#include <global/auto_registration.h>
#include <global/basic_string.h>
#include <global/binary.h>
#include <global/binary_big_endian.h>
#include <global/binary_little_endian.h>
#include <global/bit.h>
#include <global/bit_matrix.h>
#include <global/call_text.h>
#include <global/checked_sum.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/csv_text.h>
#include <global/disjoint_set.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/fixed_buffer.h>
#include <global/hierarchical_index_set.h>
#include <global/integer_rect.h>
#include <global/limit.h>
#include <global/name_hash.h>
#include <global/not_null.h>
#include <global/overflow.h>
#include <global/process_memory_map.h>
#include <global/projected_index.h>
#include <global/quantization.h>
#include <global/simplemath.h>
#include <global/simd/color.h>
#include <global/sha1.h>
#include <global/append_rollback.h>
#include <global/range_partition.h>
#include <global/range_algorithms.h>
#include <global/latin1_text.h>
#include <global/interval.h>
#include <global/source_text.h>
#include <global/string_hash.h>
#include <global/statistics.h>
#include <global/sync.h>
#include <global/termination.h>
#include <global/text_utils.h>
#include <global/text_replacements.h>
#include <global/unicode/grapheme.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_collection_shared_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, CollectionSharedHelpersPreserveEdgeCases){
    EXPECT_FALSE(UnsignedRangesOverlap(1u, 0u, Limit<u64>::s_Max, 2u));
    EXPECT_TRUE(UnsignedRangesOverlap(Limit<u64>::s_Max, 2u, 0u, 1u));
    Array<u64, s_BitsPerU64> matrix{};
    matrix[1u] = u64(1) << 63u;
    TransposeBitMatrix64(matrix);
    EXPECT_EQ(matrix[63u], 2u);
    TransposeBitMatrix64(matrix);
    EXPECT_EQ(matrix[1u], u64(1) << 63u);

    const Array<usize, 4> ordered{ 1u, 3u, 3u, 8u };
    EXPECT_EQ(LowerBoundIndex(Span<const usize>{}, 3u), 0u);
    EXPECT_EQ(LowerBoundIndex(ordered, 3u), 1u);
    EXPECT_EQ(LowerBoundIndex(ordered, 9u), ordered.size());
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    Vector<u32, NWB::Core::Alloc::ScratchArena> cachedValues(scratchArena);
    cachedValues.push_back(1u); cachedValues.push_back(2u); cachedValues.push_back(4u);
    const Array<u32, 3u> requestedValues{ 2u, 3u, 3u };
    const auto sameValue = [](const u32 cached, const u32 requested)noexcept{ return cached == requested; };
    EXPECT_EQ(CountMissingCacheEntries(cachedValues, requestedValues, sameValue), 2u);
    const auto throwingKey = [](const u32, const u32) -> b8 { throw RuntimeException("predicate failure"); };
    EXPECT_THROW(static_cast<void>(CountMissingCacheEntries(cachedValues, requestedValues, throwingKey)), RuntimeException);
    EXPECT_THROW(RetainReferencedCacheEntries(requestedValues, throwingKey, cachedValues), RuntimeException);
    RetainReferencedCacheEntries(requestedValues, sameValue, cachedValues);
    ASSERT_EQ(cachedValues.size(), 1u);
    EXPECT_EQ(cachedValues[0u], 2u);
    Vector<u32, NWB::Core::Alloc::ScratchArena> rollbackValues(scratchArena);
    rollbackValues.push_back(7u);
    u32 underflows = 0u;
    {
        AppendedContainerRollbackScope rollback(rollbackValues, [&underflows]()noexcept{ ++underflows; });
        rollbackValues.push_back(8u);
    }
    EXPECT_EQ(rollbackValues.size(), 1u);
    EXPECT_EQ(rollbackValues[0u], 7u);
    {
        AppendedContainerRollbackScope rollback(rollbackValues, [&underflows]()noexcept{ ++underflows; });
        rollbackValues.pop_back();
    }
    EXPECT_EQ(underflows, 1u);
    Array<u32, 130u> indexFlags{};
    indexFlags.fill(1u);
    indexFlags[0u] = 0u;
    indexFlags[64u] = 0u;
    indexFlags[129u] = 0u;
    HierarchicalIndexSet<NWB::Core::Alloc::ScratchArena> ready(scratchArena);
    ready.initializeZeros(Span<const u32>(indexFlags));
    EXPECT_EQ(ready.takeLowest(), 0u);
    EXPECT_EQ(ready.takeLowest(), 64u);
    EXPECT_EQ(ready.takeLowest(), 129u);
    EXPECT_EQ(ready.takeLowest(), 130u);
    ready.add(64u);
    ready.add(64u);
    EXPECT_EQ(ready.takeLowest(), 64u);
    EXPECT_EQ(ready.takeLowest(), 130u);

    Vector<u64, NWB::Core::Alloc::ScratchArena> denseMatrix(130u, 0u, scratchArena);
    denseMatrix[1u] = 1u;
    denseMatrix[2u] = 1u;
    SymmetrizeDenseBitMatrix(denseMatrix, 65u, 2u);
    EXPECT_EQ(denseMatrix[128u] & 1u, 1u);
    EXPECT_EQ(denseMatrix[0u] & 2u, 2u);

    struct WordRange{ usize begin; usize end; };
    Array<WordRange, 65u> ranges{};
    Array<usize, 65u> offsets{};
    usize wordCount = 0u;
    for(usize i = 0u; i < ranges.size(); ++i){
        ranges[i] = WordRange{ 0u, i == 0u || i == 1u || i == 64u ? 2u : 1u };
        offsets[i] = wordCount;
        wordCount += ranges[i].end;
    }
    Vector<u64, NWB::Core::Alloc::ScratchArena> compactMatrix(wordCount, 0u, scratchArena);
    compactMatrix[offsets[0u] + 1u] = 1u;
    compactMatrix[offsets[1u]] = 1u;
    compactMatrix[offsets[64u]] = 2u;
    SymmetrizeCompactBitMatrix(compactMatrix, offsets, ranges, 65u, 2u);
    EXPECT_EQ(compactMatrix[offsets[64u]] & 1u, 1u);
    EXPECT_EQ(compactMatrix[offsets[0u]] & 2u, 2u);
    EXPECT_EQ(compactMatrix[offsets[1u] + 1u] & 1u, 1u);

    Vector<IntegerRectBounds, NWB::Core::Alloc::ScratchArena> fragments(scratchArena);
    AppendIntegerRectRemainder(IntegerRectBounds{ 0u, 4u, 0u, 4u }, IntegerRectBounds{ 1u, 3u, 1u, 3u }, fragments);
    ASSERT_EQ(fragments.size(), 4u);
    EXPECT_EQ(fragments[0u].xEnd, 1u);
    EXPECT_EQ(fragments[1u].xBegin, 3u);
    EXPECT_EQ(fragments[2u].yEnd, 1u);
    EXPECT_EQ(fragments[3u].yBegin, 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


