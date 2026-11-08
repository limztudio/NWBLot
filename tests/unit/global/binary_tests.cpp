// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/global/name_case_types.adhd.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/binary.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_binary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TableAlpha = NWB::Tests::GlobalCases::s_GlobalTestTableAlpha;
static constexpr AStringView s_TableBeta = NWB::Tests::GlobalCases::s_GlobalTestTableBeta;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct U32VectorView{
    using value_type = u32;


public:
    const u32* values = nullptr;
    usize valueCount = 0u;


public:
    [[nodiscard]] u32 operator[](const usize index)const noexcept{ return values[index]; }


public:
    [[nodiscard]] b8 empty()const noexcept{ return valueCount == 0u; }

    [[nodiscard]] usize size()const noexcept{ return valueCount; }

    [[nodiscard]] const u32* data()const noexcept{ return values; }

    [[nodiscard]] const u32* begin()const noexcept{ return values; }

    [[nodiscard]] const u32* end()const noexcept{ return values + valueCount; }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, ExhaustedPodReadsPreserveCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> binary(scratchArena);
    binary.reserve(4u);
    const u32 writtenValue = 0x11223344u;
    AppendPOD(binary, writtenValue);

    usize cursor = 0u;
    const auto readValue = ReadPOD<u32>(binary, cursor);
    ASSERT_TRUE(readValue);
    EXPECT_EQ(*readValue, writtenValue);
    EXPECT_EQ(cursor, sizeof(writtenValue));

    const usize failedCursor = cursor;
    EXPECT_FALSE(ReadPOD<u32>(binary, cursor));
    EXPECT_EQ(cursor, failedCursor);
}

TEST(Global, TruncatedStringReadsDoNotAdvanceCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> truncated(scratchArena);
    truncated.reserve(5u);
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));
    usize cursor = 0u;
    EXPECT_FALSE(ReadString(truncated, cursor));
    EXPECT_EQ(cursor, 0u);
}

TEST(Global, StringTableUsesPrefixedBoundsAndRejectsEmptyAppend){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> stringTable(scratchArena);
    stringTable.reserve(16u);
    const auto alphaOffset = AppendStringTableText(stringTable, s_TableAlpha);
    const auto betaOffset = AppendStringTableText(stringTable, s_TableBeta);
    ASSERT_TRUE(alphaOffset);
    ASSERT_TRUE(betaOffset);

    ::Vector<u8, NWB::Core::Alloc::ScratchArena> prefixedBinary(scratchArena);
    prefixedBinary.reserve(17u);
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    const auto parsed = ReadStringTableText(prefixedBinary, 1u, stringTable.size(), *betaOffset);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->view(), s_TableBeta);

    const usize previousSize = stringTable.size();
    EXPECT_FALSE(AppendStringTableText(stringTable, AStringView()));
    EXPECT_EQ(stringTable.size(), previousSize);
}

TEST(Global, InvalidStringTableReads){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> unterminated(scratchArena);
    unterminated.reserve(2u);
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));
    EXPECT_FALSE(ReadStringTableText(unterminated, 0u, unterminated.size(), 0u));

    ::Vector<u8, NWB::Core::Alloc::ScratchArena> emptyText(scratchArena);
    emptyText.reserve(1u);
    emptyText.push_back(0u);
    EXPECT_FALSE(ReadStringTableText(emptyText, 0u, emptyText.size(), 0u));
}

TEST(Global, EmptyBinaryVectorReadClearsReusedOutputWithoutAdvancing){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> binary(scratchArena);
    binary.reserve(6u);
    ::Vector<u16, NWB::Core::Alloc::ScratchArena> source(scratchArena);
    source.reserve(3u);
    source.push_back(1u);
    source.push_back(2u);
    source.push_back(static_cast<u16>(0xBEEFu));

    ASSERT_TRUE(AppendBinaryVectorPayload(binary, source));

    usize cursor = 0u;
    ::Vector<u16, NWB::Core::Alloc::ScratchArena> parsed(scratchArena);
    parsed.reserve(4u);
    ASSERT_TRUE(ReadBinaryVectorPayload(binary, static_cast<u64>(source.size()), cursor, parsed));

    cursor = 0u;
    parsed.push_back(7u);
    ASSERT_TRUE(ReadBinaryVectorPayload(binary, 0u, cursor, parsed));
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, FixedVectorOverflowDoesNotAdvanceBinaryCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> vectorBinary(scratchArena);
    vectorBinary.reserve(6u);
    const Array<u16, 3u> values = { 4u, 5u, 6u };
    for(const u16 value : values)
        AppendPOD(vectorBinary, value);

    usize vectorCursor = 0u;
    FixedVector<u16, 2u> tooSmall;
    EXPECT_EQ(ReadBinaryVectorPayload(vectorBinary, 3u, vectorCursor, tooSmall), MakeUnexpected(BinaryVectorPayloadFailure::OutputOverflow));
    EXPECT_EQ(vectorCursor, 0u);
    EXPECT_TRUE(tooSmall.empty());
}

TEST(Global, RejectedBinaryVectorPayloadReadsDoNotAdvanceCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> truncated(scratchArena);
    truncated.reserve(5u);
    const u32 source = 0x12345678u;
    AppendPOD(truncated, source);

    usize cursor = 0u;
    ::Vector<u32, NWB::Core::Alloc::ScratchArena> parsed(scratchArena);
    parsed.reserve(4u);
    parsed.push_back(0xAABBCCDDu);
    EXPECT_EQ(ReadBinaryVectorPayload(truncated, 2u, cursor, parsed), MakeUnexpected(BinaryVectorPayloadFailure::SourceTruncated));
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, AppendTriviallyCopyableVectorSelfAppend){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u32, NWB::Core::Alloc::ScratchArena> values(scratchArena);
    values.reserve(3u);
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);

    AppendTriviallyCopyableVector(values, values);

    EXPECT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0u], 1u);
    EXPECT_EQ(values[1u], 2u);
    EXPECT_EQ(values[2u], 3u);
    EXPECT_EQ(values[3u], 1u);
    EXPECT_EQ(values[4u], 2u);
    EXPECT_EQ(values[5u], 3u);
}

TEST(Global, TriviallyCopyableVectorPreservesOverlappingSourceRanges){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u32, NWB::Core::Alloc::ScratchArena> values(scratchArena);
    values.reserve(4u);
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);
    values.push_back(4u);

    const U32VectorView middle{ values.data() + 1u, 2u };
    AppendTriviallyCopyableVector(values, middle);

    EXPECT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0u], 1u);
    EXPECT_EQ(values[1u], 2u);
    EXPECT_EQ(values[2u], 3u);
    EXPECT_EQ(values[3u], 4u);
    EXPECT_EQ(values[4u], 2u);
    EXPECT_EQ(values[5u], 3u);

    const U32VectorView assignedMiddle{ values.data() + 1u, 3u };
    AssignTriviallyCopyableVector(values, assignedMiddle);

    EXPECT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0u], 2u);
    EXPECT_EQ(values[1u], 3u);
    EXPECT_EQ(values[2u], 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

