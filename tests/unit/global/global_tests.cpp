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


namespace __hidden_global_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////









////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, AutoRegistrationQueueDeduplicatesAndSnapshots){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::AutoRegistrationQueue<u32, NWB::Core::Alloc::GlobalArena> queue(NWB::Tests::s_TestArena);
    const auto equal = [](const u32 lhs, const u32 rhs)noexcept{ return lhs == rhs; };

    queue.appendUnique(7u, equal);
    queue.appendUnique(7u, equal);
    queue.appendUnique(19u, equal);

    ::Vector<u32, NWB::Core::Alloc::ScratchArena> values(scratchArena);
    values.reserve(6u);
    queue.copyTo(values);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0u], 7u);
    EXPECT_EQ(values[1u], 19u);

    queue.appendUnique(23u, equal);
    EXPECT_EQ(values.size(), 2u);

    queue.copyTo(values);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[2u], 23u);
}

TEST(Global, GenericPathKeepsNativeFilenameCharacters){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    const ::Path<NWB::Core::Alloc::ScratchArena> path(scratchArena, "one\\two");
    const auto text = PathToGenericString<char>(scratchArena, path);
#if defined(NWB_PLATFORM_WINDOWS)
    EXPECT_EQ(AStringView(text), "one/two");
#else
    EXPECT_EQ(AStringView(text), "one\\two");
#endif
}

#if !defined(_MSC_VER)
TEST(Global, BoundedRuntimeWrappersTerminateTruncatedText){
    Array<char, 4u> truncatedText = {};
    EXPECT_NE(NWB_STRCPY(truncatedText.data(), sizeof(truncatedText), "abcdef"), 0);
    EXPECT_STREQ(truncatedText.data(), "abc");

    Array<char, 4u> nullTerminatedText = { 'a', 'b', 'c', 'd' };
    EXPECT_NE(NWB_STRCAT(nullTerminatedText.data(), sizeof(nullTerminatedText), "e"), 0);
    EXPECT_EQ(nullTerminatedText[sizeof(nullTerminatedText) - 1u], '\0');
}
#endif

TEST(Global, UnicodeAndAsciiHelpersRejectSurrogatesOverlongsAndInvalidIdentifiers){
    EXPECT_FALSE(IsUtf16HighSurrogate(0xD7FFu));
    EXPECT_TRUE(IsUtf16HighSurrogate(0xD800u));
    EXPECT_TRUE(IsUtf16HighSurrogate(0xDBFFu));
    EXPECT_FALSE(IsUtf16HighSurrogate(0xDC00u));
    EXPECT_FALSE(IsUtf16LowSurrogate(0xDBFFu));
    EXPECT_TRUE(IsUtf16LowSurrogate(0xDC00u));
    EXPECT_TRUE(IsUtf16LowSurrogate(0xDFFFu));
    EXPECT_FALSE(IsUtf16LowSurrogate(0xE000u));
    EXPECT_TRUE(IsUnicodeScalarValue(0xD7FFu));
    EXPECT_FALSE(IsUnicodeScalarValue(0xD800u));
    EXPECT_FALSE(IsUnicodeScalarValue(0xDFFFu));
    EXPECT_TRUE(IsUnicodeScalarValue(0xE000u));
    EXPECT_TRUE(IsUnicodeScalarValue(s_UnicodeMaxCodePoint));
    EXPECT_FALSE(IsUnicodeScalarValue(s_UnicodeMaxCodePoint + 1u));
    EXPECT_EQ(DecodeUtf16SurrogatePair(0xD800u, 0xDC00u), 0x10000u);
    EXPECT_EQ(DecodeUtf16SurrogatePair(0xDBFFu, 0xDFFFu), s_UnicodeMaxCodePoint);

    const Array<char, 4u> bomPrefixed = { static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF), 'a' };
    EXPECT_TRUE(HasUtf8Bom(AStringView(bomPrefixed.data(), bomPrefixed.size())));
    EXPECT_FALSE(HasUtf8Bom(AStringView(bomPrefixed.data(), 2u)));

    Array<char, s_Utf8MaxSequenceBytes> encoded = {};
    EXPECT_FALSE(EncodeUtf8CodePoint(0xD800u, encoded));
    EXPECT_FALSE(EncodeUtf8CodePoint(s_UnicodeMaxCodePoint + 1u, encoded));
    const auto encodedBytes = EncodeUtf8CodePoint(s_UnicodeMaxCodePoint, encoded);
    ASSERT_TRUE(encodedBytes);
    ASSERT_EQ(*encodedBytes, 4u);
    EXPECT_TRUE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 0u));
    EXPECT_FALSE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 1u));
    EXPECT_TRUE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 4u));
    EXPECT_FALSE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 5u));

    const auto decoded = DecodeUtf8Scalar(AStringView(encoded.data(), encoded.size()));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->codePoint, s_UnicodeMaxCodePoint);
    EXPECT_EQ(decoded->byteCount, 4);
    EXPECT_FALSE(DecodeUtf8Scalar(AStringView(encoded.data(), 3u)));
    const Array<char, 3u> surrogateBytes = { static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80) };
    EXPECT_FALSE(DecodeUtf8Scalar(AStringView(surrogateBytes.data(), surrogateBytes.size())));

    EXPECT_TRUE(IsUnicodeWhitespace(0x2000u));
    EXPECT_TRUE(IsUnicodeWhitespace(0x200au));
    EXPECT_FALSE(IsUnicodeWhitespace(0x1fffu));
    EXPECT_FALSE(IsUnicodeWhitespace(0x200bu));
    EXPECT_TRUE(IsUnicodeWhitespace(0xa0u));
    EXPECT_FALSE(IsUnicodeWhitespace(0x180eu));
    EXPECT_TRUE(IsAsciiPunctuation(0x21u));
    EXPECT_TRUE(IsAsciiPunctuation(0x7eu));
    EXPECT_FALSE(IsAsciiPunctuation(0x20u));
    EXPECT_FALSE(IsAsciiPunctuation(0x7fu));
    EXPECT_EQ(StripMatchingTrailingByte("f", 'f', 'F'), "f");
    EXPECT_EQ(StripMatchingTrailingByte("4F", 'f', 'F'), "4");
    usize tokenCursor = 0u;
    const auto token = ReadCommaOrSpaceDelimitedToken(" , x,,", tokenCursor);
    ASSERT_TRUE(token);
    EXPECT_EQ(*token, "x");
    EXPECT_EQ(tokenCursor, 4u);
    EXPECT_FALSE(ReadCommaOrSpaceDelimitedToken(" , x,,", tokenCursor));
    EXPECT_EQ(tokenCursor, 6u);
    EXPECT_EQ(*AlignU32PowerOfTwoChecked(Limit<u32>::s_Max - 3u, 4u), Limit<u32>::s_Max - 3u);
    EXPECT_FALSE(AlignU32PowerOfTwoChecked(Limit<u32>::s_Max, 4u));
    EXPECT_FALSE(AlignU32PowerOfTwoChecked(1u, 3u));
    EXPECT_EQ(*ComputeStridedRangeByteSize(0u, Limit<u64>::s_Max, Limit<u64>::s_Max), 0u);
    EXPECT_EQ(*ComputeStridedRangeByteSize(2u, 0u, Limit<u64>::s_Max), Limit<u64>::s_Max);
    EXPECT_FALSE(ComputeStridedRangeByteSize(2u, 1u, Limit<u64>::s_Max));
    EXPECT_EQ(MultiplySaturatingU64(Limit<u64>::s_Max, 0u), 0u);
    EXPECT_EQ(MultiplySaturatingU64(Limit<u64>::s_Max, 2u), Limit<u64>::s_Max);
    EXPECT_EQ(SaturateI64ToI32(Limit<i64>::s_Min), Limit<i32>::s_Min);
    EXPECT_EQ(SaturateI64ToI32(Limit<i64>::s_Max), Limit<i32>::s_Max);
    EXPECT_TRUE(IsRelativeSlashPathWithoutDotSegments("c:segment/file"));
    EXPECT_FALSE(IsRelativeSlashPathWithoutDotSegments("a/../b"));
    EXPECT_FALSE(IsRelativeSlashPathWithoutDotSegments("a//b"));
    EXPECT_FALSE(IsRelativeSlashPathWithoutDotSegments("a\\b"));
    EXPECT_FALSE(FitsFixedText(AStringView{}, 0u));
    EXPECT_TRUE(FitsFixedText(AStringView{}, 1u));
    EXPECT_FALSE(FitsFixedText(AStringView("a\0b", 3u), 4u));
    EXPECT_FALSE(TryFiniteF32FromF64(Limit<f64>::s_Infinity));
    EXPECT_FALSE(TryFiniteF32FromF64(static_cast<f64>(Limit<f32>::s_Max) * 2.0));
    EXPECT_TRUE(ParseFiniteF32ViaF64("-0.0"));
    EXPECT_EQ(BitCast<u32>(*ParseFiniteF32ViaF64("-0.0")), 0x80000000u);
    EXPECT_FALSE(IsAsciiIdentifier(AStringView()));
    EXPECT_FALSE(IsAsciiIdentifier(AStringView("9abc")));
    EXPECT_FALSE(IsAsciiIdentifier(AStringView("a-b")));
    EXPECT_TRUE(IsAsciiIdentifier(AStringView("_a9")));
    EXPECT_EQ(ToAsciiLowerByte(0xC3u), 0xC3u);
    EXPECT_NE(ToAsciiLowerByte(static_cast<u8>('\\')), ToAsciiLowerByte(static_cast<u8>('/')));
}

TEST(Global, OwnershipInvariantTerminationIsAlwaysActive){
    EXPECT_DEATH(TerminateInvariant(), "");
}





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

