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


namespace __hidden_global_binary_shared_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingByteSize{};
struct ThrowingByteSizeComparison{
    explicit operator b8()const{ throw RuntimeException("byte size comparison failure"); }
};
[[nodiscard]] inline ThrowingByteSizeComparison operator>(const usize, const ThrowingByteSize)noexcept{ return {}; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, BinarySharedHelpersPreserveEdgeCases){
    const Array<u8, 2> bytes{ 0u, 255u };
    const Array<u8, 2> different{ 0u, 254u };
    EXPECT_TRUE(BytesMatch(Span<const u8>{}, Span<const u8>{}));
    EXPECT_FALSE(BytesMatch(bytes, Span<const u8>(bytes.data(), 1u)));
    EXPECT_FALSE(BytesMatch(bytes, different));
    const Array<u8, 3> pattern{ 1u, 2u, 3u };
    Array<u8, 7> repeated{};
    FillRepeatingBytes(repeated.size(), pattern, repeated.data());
    EXPECT_EQ(repeated, (Array<u8, 7>{ 1u, 2u, 3u, 1u, 2u, 3u, 1u }));
    FillRepeatingBytes(0u, pattern, nullptr);
    usize emptyCursor = 0u;
    const auto emptyText = ReadBinaryText(BinaryByteView{}, 0u, emptyCursor);
    ASSERT_TRUE(emptyText);
    EXPECT_TRUE(emptyText->empty());
    EXPECT_EQ(emptyCursor, 0u);
    usize cursor = bytes.size();
    EXPECT_FALSE(ReadBinaryText(BinaryByteView{ bytes.data(), bytes.size() }, 1u, cursor));
    EXPECT_EQ(cursor, bytes.size());

    const Array<u8, 3u> partialBytes{ 0x12u, 0x34u, 0x56u };
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, 0u), 0x00563412u);
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, 2u), 0x56u);
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, 3u), 0u);
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, Limit<usize>::s_Max), 0x56341200u);
    EXPECT_EQ(ReadAdmittedU16BigEndian(partialBytes, 1u), 0x3456u);
    struct ThrowingByteContainer{
        [[nodiscard]] usize size()const noexcept{ return 4u; }
        u8& operator[](usize){ throw RuntimeException("byte assignment failure"); }
    };
    ThrowingByteContainer throwingBytes;
    EXPECT_THROW(WriteF32LittleEndianAt(1.0f, 0u, throwingBytes), RuntimeException);
    struct ThrowingLvalueByteContainer{
        u8 byte = 0u;
        [[nodiscard]] usize size()const noexcept{ return 4u; }
        u8& operator[](const usize&){ throw RuntimeException("lvalue byte index failure"); }
        u8& operator[](usize&&)noexcept{ return byte; }
    };
    ThrowingLvalueByteContainer lvalueBytes;
    static_assert(!noexcept(WriteF32LittleEndianAt(1.0f, 0u, lvalueBytes)));
    EXPECT_THROW(WriteF32LittleEndianAt(1.0f, 0u, lvalueBytes), RuntimeException);
    struct ThrowingSizeByteContainer{
        u8 byte = 0u;
        [[nodiscard]] ThrowingByteSize size()const noexcept{ return {}; }
        u8& operator[](usize)noexcept{ return byte; }
    };
    ThrowingSizeByteContainer sizeBytes;
    static_assert(!noexcept(WriteF32LittleEndianAt(1.0f, 0u, sizeBytes)));
    EXPECT_THROW(WriteF32LittleEndianAt(1.0f, 0u, sizeBytes), RuntimeException);
    FixedVector<u8, 8u> floatBytes;
    floatBytes.resize(7u);
    WriteF32LittleEndianAt(BitCast<f32>(0x80000000u), 0u, floatBytes);
    EXPECT_EQ(floatBytes[3u], 0x80u);
    WriteF32LittleEndianAt(BitCast<f32>(0x7fc01234u), 4u, floatBytes);
    EXPECT_EQ(floatBytes[4u], 0u);
    floatBytes.resize(8u);
    WriteF32LittleEndianAt(BitCast<f32>(0x7fc01234u), 4u, floatBytes);
    EXPECT_EQ(floatBytes[4u], 0x34u);
    EXPECT_EQ(floatBytes[5u], 0x12u);
    EXPECT_EQ(floatBytes[6u], 0xc0u);
    EXPECT_EQ(floatBytes[7u], 0x7fu);
    EXPECT_DEATH(WriteF32LittleEndianAt(0.0f, Limit<usize>::s_Max, floatBytes), "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


