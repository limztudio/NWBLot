// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <tests/common/test_context.h>

#include <global/span.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_typed_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_ScratchArena("tests/ecs_graphics/scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, ShadowMaterialConstantsReuseOnlyTheSameMaterialAndLayout){
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedConstantRangeMap;
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> uploadBytes(scratchArena);
    constexpr Array<u8, 4> s_Constants = { 0xA1u, 0xB2u, 0xC3u, 0xD4u };
    RangeMap ranges(0, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
    ranges.reserve(3u);
    const Name material("project/materials/shared");
    const Name otherMaterial("project/materials/other");

    const auto first = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(material, 17u, s_Constants, uploadBytes, ranges);
    ASSERT_TRUE(first);
    const usize firstUploadSize = uploadBytes.size();
    for(u32 instance = 0u; instance < 8u; ++instance){
        const auto repeated = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(material, 17u, s_Constants, uploadBytes, ranges);
        ASSERT_TRUE(repeated);
        EXPECT_EQ(repeated->byteOffset, first->byteOffset);
        EXPECT_EQ(repeated->byteCount, first->byteCount);
        EXPECT_EQ(uploadBytes.size(), firstUploadSize);
    }
    const auto differentLayout = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(material, 18u, s_Constants, uploadBytes, ranges);
    const auto differentMaterial = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(otherMaterial, 17u, s_Constants, uploadBytes, ranges);
    ASSERT_TRUE(differentLayout);
    ASSERT_TRUE(differentMaterial);
    EXPECT_NE(differentLayout->byteOffset, first->byteOffset);
    EXPECT_NE(differentMaterial->byteOffset, first->byteOffset);
    EXPECT_NE(differentMaterial->byteOffset, differentLayout->byteOffset);
    ASSERT_EQ(ranges.size(), 3u);
    for(const auto& [key, range] : ranges){
        EXPECT_TRUE(key.materialName == material || key.materialName == otherMaterial);
        ASSERT_EQ(range.byteCount, sizeof(s_Constants));
        ASSERT_LE(range.byteOffset + range.byteCount, uploadBytes.size());
        EXPECT_EQ(NWB_MEMCMP(uploadBytes.data() + range.byteOffset, s_Constants.data(), sizeof(s_Constants)), 0);
    }
}


TEST(EcsGraphics, MaterialTypedByteRangeRepeatedHitsAvoidScratchAllocations){
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;
    using ByteRange = NWB::Impl::MaterialTypedByteRange;

    Array<u8, 4096> sourceBytes{};
    for(usize index = 0u; index < sourceBytes.size(); ++index)
        sourceBytes[index] = static_cast<u8>((index * 31u) & 255u);

    const Array<usize, 2> byteCounts = { 512u, 4096u };
    for(const usize byteCount : byteCounts){
        NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
        ByteVector uploadBytes(scratchArena);
        uploadBytes.reserve(byteCount);
        RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
        ranges.reserve(1u);
        const Span<const u8> bytes(sourceBytes.data(), byteCount);
        ByteRange firstRange;
        const auto rangeResult1 = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(bytes, uploadBytes, ranges);
        ASSERT_TRUE(rangeResult1);
        firstRange = *rangeResult1;

        const ArenaMemoryStats before = scratchArena.memoryStats();
        for(usize lookup = 0u; lookup < 256u; ++lookup){
            ByteRange range;
            const auto rangeResult3 = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(bytes, uploadBytes, ranges);
            ASSERT_TRUE(rangeResult3);
            range = *rangeResult3;
            EXPECT_EQ(range.byteOffset, firstRange.byteOffset);
            EXPECT_EQ(range.byteCount, firstRange.byteCount);
        }
        const Span<const u8> emptyBytes;
        ByteRange emptyRange{ 4u, 4u };
        const auto rangeResult4 = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(emptyBytes, uploadBytes, ranges);
        ASSERT_TRUE(rangeResult4);
        emptyRange = *rangeResult4;
        const ArenaMemoryStats after = scratchArena.memoryStats();
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.reallocationCount, before.reallocationCount);
        EXPECT_EQ(after.deallocationCount, before.deallocationCount);
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        EXPECT_EQ(emptyRange.byteOffset, 0u);
        EXPECT_EQ(emptyRange.byteCount, 0u);
        EXPECT_EQ(uploadBytes.size(), byteCount);
        EXPECT_EQ(ranges.size(), 1u);
    }
}


TEST(EcsGraphics, MaterialTypedByteRangeHashCollisionRetainsOwnedBytes){
    using ByteKey = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKey;
    using ByteLookup = NWB::Impl::MaterialTypedByteContentLookup;
    using ByteRange = NWB::Impl::MaterialTypedByteRange;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
    ranges.reserve(2u);
    constexpr u64 s_CollisionHash = 41u;
    const Array<u8, 4> originalFirstBytes = { 1u, 2u, 3u, 4u };
    const Array<u8, 4> secondBytes = { 1u, 2u, 3u, 5u };
    {
        Array<u8, 4> firstBytes = originalFirstBytes;
        const ByteLookup lookup{ s_CollisionHash, firstBytes };
        ASSERT_TRUE(ranges.emplace(ByteKey(scratchArena, lookup), ByteRange{ 0u, 4u }).second);
        firstBytes.fill(0u);
    }

    // Compare payload bytes even when hash and byte length match.
    const ByteLookup firstLookup{ s_CollisionHash, originalFirstBytes };
    const ByteLookup secondLookup{ s_CollisionHash, secondBytes };
    const auto firstFound = ranges.find(firstLookup);
    ASSERT_NE(firstFound, ranges.end());
    EXPECT_EQ(firstFound.value().byteOffset, 0u);
    EXPECT_EQ(ranges.find(secondLookup), ranges.end());
    ASSERT_TRUE(ranges.emplace(ByteKey(scratchArena, secondLookup), ByteRange{ 4u, 4u }).second);
    const auto secondFound = ranges.find(secondLookup);
    ASSERT_NE(secondFound, ranges.end());
    EXPECT_EQ(secondFound.value().byteOffset, 4u);
    EXPECT_EQ(ranges.size(), 2u);
}


TEST(EcsGraphics, MaterialTypedByteRangeUploadAliasSurvivesGrowthAndMutation){
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using ByteRange = NWB::Impl::MaterialTypedByteRange;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;

    Array<u8, 32> expectedBytes{};
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    ByteVector uploadBytes(scratchArena);
    uploadBytes.reserve(expectedBytes.size());
    uploadBytes.resize(uploadBytes.capacity());
    for(usize index = 0u; index < uploadBytes.size(); ++index){
        uploadBytes[index] = static_cast<u8>((index * 31u) & 255u);
        expectedBytes[index] = uploadBytes[index];
    }
    const usize initialCapacity = uploadBytes.capacity();
    const Span<const u8> aliasedBytes(uploadBytes.data(), uploadBytes.size());
    RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
    ranges.reserve(1u);
    ByteRange appendedRange;
    const auto rangeResult5 = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(aliasedBytes, uploadBytes, ranges);
    ASSERT_TRUE(rangeResult5);
    appendedRange = *rangeResult5;
    ASSERT_GT(uploadBytes.capacity(), initialCapacity);
    ASSERT_EQ(appendedRange.byteCount, expectedBytes.size());
    ASSERT_EQ(uploadBytes.size(), expectedBytes.size() * 2u);
    EXPECT_EQ(NWB_MEMCMP(uploadBytes.data() + appendedRange.byteOffset, expectedBytes.data(), expectedBytes.size()), 0);

    // Mutating the original upload bytes cannot change the retained dedup key.
    for(usize index = 0u; index < expectedBytes.size(); ++index)
        uploadBytes[index] ^= 255u;
    const usize uploadByteCount = uploadBytes.size();
    ByteRange foundRange;
    const auto rangeResult6 = NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(expectedBytes, uploadBytes, ranges);
    ASSERT_TRUE(rangeResult6);
    foundRange = *rangeResult6;
    EXPECT_EQ(foundRange.byteOffset, appendedRange.byteOffset);
    EXPECT_EQ(foundRange.byteCount, appendedRange.byteCount);
    EXPECT_EQ(uploadBytes.size(), uploadByteCount);
    EXPECT_EQ(ranges.size(), 1u);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

