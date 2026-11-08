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


namespace __hidden_global_hash_shared_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, HashSharedHelpersPreserveEdgeCases){
    EXPECT_EQ(MixMurmur64Finalizer(0u), 0u);
    EXPECT_EQ(MixMurmur64Finalizer(1u), 0xb456bcfc34c2cb2cull);
    EXPECT_EQ(BoolToYesNoText(true), "yes");
    EXPECT_EQ(BoolToYesNoText(false), "no");
    EXPECT_EQ(UpdateFnv64U32LittleEndian(s_Fnv64OffsetBasis, 0x12345678u), 0xcccfd053e47c3365ull);
    EXPECT_EQ(UpdateFnv64U64LittleEndian(s_Fnv64OffsetBasis, 0x0123456789abcdefull), 0x37eb3f3347761c55ull);
    NameHash lanes{};
    lanes.qwords = { 0x0123456789abcdefull, 0u, 0xffffffffffffffffull, 0x0102030405060708ull, 5u, 6u, 7u, 8u };
    EXPECT_EQ(UpdateFnv64NameHash(0x123456789abcdef0ull, lanes), 0x08d4d531507706dcull);
    EXPECT_EQ(FoldNameHash64(lanes), 0xe33b773ee6cabb20ull);
    struct CharSink{
        Array<char, Sha1Digest::s_HexTextLength> data{};
        usize size = 0u;
        CharSink& operator+=(const char ch)noexcept{ data[size++] = ch; return *this; }
    };
    CharSink sink;
    AppendHexByte(0xAFu, sink);
    EXPECT_EQ(sink.data[0u], 'a');
    EXPECT_EQ(sink.data[1u], 'f');
    sink.size = 0u;
    AppendSha1Hex(Sha1Digest{}, sink);
    EXPECT_EQ(sink.size, Sha1Digest::s_HexTextLength);
    EXPECT_EQ(sink.data[0u], '0');
    EXPECT_EQ(sink.data[sink.size - 1u], '0');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


