// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/base64.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Base64, RejectsMalformedAlphabetLengthPaddingAndUnusedBits){
    TestArena<> context;
    using Bytes = ::Vector<u8, Core::Alloc::GlobalArena>;
    const typename Bytes::allocator_type allocator(context.arena);
    const AStringView invalid[] = {
        "Z", "Zg", "Zg=", "Zg===", "=g==", "Z===", "Zg=A", "Zg==AAAA", "AAAAZg==AAAA",
        "Zm9=", "Zh==", "AB==", "AAB=", "Zg-_", "Zg\r\n", "Zg \t", "Zg#=", "====",
        AStringView("Zg\0=", 4u), AStringView("Zg\xff=", 4u),
    };
    for(const AStringView text : invalid){
        EXPECT_FALSE(Base64DecodedSize(text, 1024u));
        EXPECT_FALSE(DecodeBase64<Bytes>(text, allocator, 1024u));
    }
}

TEST(Base64, ExactDecodedBoundsAreCheckedBeforeAllocation){
    TestArena<> context;
    using Bytes = ::Vector<u8, Core::Alloc::GlobalArena>;
    const typename Bytes::allocator_type allocator(context.arena);
    const auto before = context.arena.memoryStats();
    EXPECT_FALSE(Base64DecodedSize("Zm9v", 2u));
    EXPECT_FALSE(DecodeBase64<Bytes>("Zm9v", allocator, 2u));
    EXPECT_EQ(context.arena.memoryStats().allocationCount, before.allocationCount);
    const auto size = Base64DecodedSize("Zm9v", 3u);
    ASSERT_TRUE(size);
    EXPECT_EQ(*size, 3u);
    const auto output = DecodeBase64<Bytes>("Zm9v", allocator, 3u);
    ASSERT_TRUE(output);
    ASSERT_EQ(output->size(), 3u);
    EXPECT_EQ((*output)[0u], static_cast<u8>('f'));
    const auto empty = DecodeBase64<Bytes>("", allocator, 0u);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());
}

TEST(Base64, InvalidEncodeInputAndSizeOverflowDoNotAllocate){
    TestArena<> context;
    using Text = ::AString<Core::Alloc::GlobalArena>;
    const typename Text::allocator_type allocator(context.arena);
    const auto before = context.arena.memoryStats();
    EXPECT_FALSE(EncodeBase64<Text>({ nullptr, 1u }, allocator));
    const u8 byte = 0u;
    EXPECT_FALSE(EncodeBase64<Text>({ &byte, Limit<usize>::s_Max }, allocator));
    EXPECT_EQ(context.arena.memoryStats().allocationCount, before.allocationCount);
    const auto empty = EncodeBase64<Text>({}, allocator);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

