// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_ui_skin/asset.h>
#include <impl/assets_ui_skin/binary_payload.h>
#include <impl/assets_texture/asset.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_region_index_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static UiSkinRegion MakeRegion(const Name name, const u32 ordinal){
    UiSkinRegion region;
    region.name = name;
    region.rectangle = { ordinal, 0u, 1u, 1u };
    return region;
}

static void SetRegions(UiSkin& skin, UiSkin::RegionVector&& regions){
    skin.setAtlas(Core::Assets::AssetRef<Texture>("test/skin/texture"), 8192u, 1u, 1.0f, Move(regions));
}

[[nodiscard]] static Name RegionName(const u32 index){
    NameHash hash{};
    hash.qwords[0u] = 1u + (index * 271u) % s_UiSkinMaxRegionCount;
    hash.qwords[s_NameHashLaneCount - 1u] = index + 1u;
    return Name(hash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsUiSkinRegionIndex, MaximumCountPreservesAuthoredOrderAcrossHashOrderAndMisses){
    using namespace __hidden_ui_skin_region_index_tests;
    TestArena<> owner;
    UiSkin skin(owner.arena, Name("test/skin"));
    UiSkin::RegionVector regions(owner.arena);
    regions.reserve(s_UiSkinMaxRegionCount);
    for(u32 index = 0u; index < s_UiSkinMaxRegionCount; ++index)
        regions.push_back(MakeRegion(RegionName(index), index));
    SetRegions(skin, Move(regions));
    ASSERT_TRUE(skin.validatePayload());
    for(u32 index = 0u; index < s_UiSkinMaxRegionCount; ++index){
        ASSERT_EQ(skin.regions()[index].rectangle.x, index);
        ASSERT_EQ(skin.regions()[index].name, RegionName(index));
        ASSERT_EQ(skin.findRegion(RegionName(index)), &skin.regions()[index]);
    }
    EXPECT_EQ(skin.findRegion(NAME_NONE), nullptr);
    EXPECT_EQ(skin.findRegion(Name("missing")), nullptr);
    NameHash after{};
    after.qwords[0u] = Limit<u64>::s_Max;
    EXPECT_EQ(skin.findRegion(Name(after)), nullptr);
}

TEST(AssetsUiSkinRegionIndex, CanonicalDuplicateKeepsFirstAuthoredMatchAndFailsAdmission){
    using namespace __hidden_ui_skin_region_index_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena<> owner;
    UiSkin skin(owner.arena, Name("test/skin"));
    UiSkin::RegionVector regions(owner.arena);
    regions.push_back(MakeRegion(Name("Button\\Normal"), 0u));
    regions.push_back(MakeRegion(Name("panel.normal"), 1u));
    regions.push_back(MakeRegion(Name("button/normal"), 2u));
    SetRegions(skin, Move(regions));
    EXPECT_EQ(skin.findRegion(Name("BUTTON/NORMAL")), &skin.regions()[0u]);
    EXPECT_FALSE(skin.validatePayload());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate region")));
}

TEST(AssetsUiSkinRegionIndex, EqualLeadingHashLanesStillResolveDistinctNames){
    using namespace __hidden_ui_skin_region_index_tests;
    TestArena<> owner;
    UiSkin skin(owner.arena, Name("test/skin"));
    UiSkin::RegionVector regions(owner.arena);
    for(u32 index = 0u; index < 129u; ++index){
        NameHash hash{};
        hash.qwords[0u] = 123u;
        hash.qwords[s_NameHashLaneCount - 1u] = 129u - index;
        regions.push_back(MakeRegion(Name(hash), index));
    }
    SetRegions(skin, Move(regions));
    ASSERT_TRUE(skin.validatePayload());
    for(const UiSkinRegion& region : skin.regions())
        EXPECT_EQ(skin.findRegion(region.name), &region);
    NameHash missing = skin.regions()[0u].name.hash();
    missing.qwords[s_NameHashLaneCount - 1u] = 130u;
    EXPECT_EQ(skin.findRegion(Name(missing)), nullptr);
}

TEST(AssetsUiSkinRegionIndex, ReplacementAcrossSmallSkinBoundaryDoesNotKeepStaleMatches){
    using namespace __hidden_ui_skin_region_index_tests;
    TestArena<> owner;
    UiSkin skin(owner.arena, Name("test/skin"));
    constexpr u32 counts[]{ 128u, 129u, 128u };
    Name previous = NAME_NONE;
    for(u32 stage = 0u; stage < LengthOf(counts); ++stage){
        UiSkin::RegionVector regions(owner.arena);
        for(u32 index = 0u; index < counts[stage]; ++index)
            regions.push_back(MakeRegion(RegionName(index + stage * 256u), index));
        SetRegions(skin, Move(regions));
        ASSERT_TRUE(skin.validatePayload());
        for(const UiSkinRegion& region : skin.regions())
            EXPECT_EQ(skin.findRegion(region.name), &region);
        EXPECT_EQ(skin.findRegion(previous), nullptr);
        EXPECT_EQ(skin.findRegion(Name("missing")), nullptr);
        previous = skin.regions().back().name;
    }
}

TEST(AssetsUiSkinRegionIndex, ReplacementMovesAndCopiesDoNotRetainStaleRegionReferences){
    using namespace __hidden_ui_skin_region_index_tests;
    TestArena<> firstOwner;
    TestArena<> secondOwner;
    UiSkin source(firstOwner.arena, Name("test/skin"));
    UiSkin::RegionVector regions(firstOwner.arena);
    regions.push_back(MakeRegion(Name("old"), 0u));
    SetRegions(source, Move(regions));
    UiSkin::RegionVector replacement(secondOwner.arena);
    replacement.push_back(MakeRegion(Name("new"), 1u));
    replacement.push_back(MakeRegion(Name("other"), 2u));
    SetRegions(source, Move(replacement));
    EXPECT_EQ(&source.regions().get_allocator().arena(), &secondOwner.arena);
    EXPECT_EQ(source.findRegion(Name("old")), nullptr);
    UiSkin moved(Move(source));
    EXPECT_EQ(moved.findRegion(Name("new")), &moved.regions()[0u]);
    UiSkin copied(moved);
    EXPECT_EQ(copied.findRegion(Name("other")), &copied.regions()[1u]);
    EXPECT_NE(copied.findRegion(Name("other")), moved.findRegion(Name("other")));
    UiSkin assigned(firstOwner.arena, Name("test/destination"));
    assigned = Move(moved);
    EXPECT_EQ(assigned.findRegion(Name("new")), &assigned.regions()[0u]);
    EXPECT_EQ(assigned.findRegion(Name("old")), nullptr);
    UiSkin::RegionVector empty(secondOwner.arena);
    SetRegions(assigned, Move(empty));
    EXPECT_EQ(assigned.findRegion(Name("new")), nullptr);
    EXPECT_EQ(copied.findRegion(Name("new")), &copied.regions()[0u]);
}

TEST(AssetsUiSkinRegionIndex, FailedBinaryLoadPreservesLookupAndAuthoredOrder){
    using namespace __hidden_ui_skin_region_index_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena<> owner;
    UiSkin source(owner.arena, Name("test/skin"));
    UiSkin::RegionVector regions(owner.arena);
    regions.push_back(MakeRegion(Name("last"), 0u));
    regions.push_back(MakeRegion(Name("first"), 1u));
    SetRegions(source, Move(regions));
    UiSkinAssetCodec codec;
    Core::Assets::AssetBytes binary(owner.arena);
    ASSERT_TRUE(codec.serialize(source, binary));
    UiSkin loaded(owner.arena, source.virtualPath());
    ASSERT_TRUE(loaded.loadBinary(binary));
    ASSERT_EQ(loaded.findRegion(Name("first")), &loaded.regions()[1u]);
    const UiSkinRegion* retained = loaded.findRegion(Name("last"));
    const usize firstHash = sizeof(UiSkinBinaryPayload::HeaderBinary);
    const usize secondHash = firstHash + sizeof(UiSkinBinaryPayload::RegionBinary);
    NWB_MEMCPY(binary.data() + secondHash, sizeof(NameHash), binary.data() + firstHash, sizeof(NameHash));
    EXPECT_FALSE(loaded.loadBinary(binary));
    EXPECT_EQ(loaded.findRegion(Name("last")), retained);
    EXPECT_EQ(loaded.findRegion(Name("first")), &loaded.regions()[1u]);
    EXPECT_EQ(loaded.regions()[0u].name, Name("last"));
}

TEST(AssetsUiSkinRegionIndex, OversizedInvalidSkinRemainsInspectableWithoutIndexedAdmission){
    using namespace __hidden_ui_skin_region_index_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena<> owner;
    UiSkin skin(owner.arena, Name("test/skin"));
    UiSkin::RegionVector regions(owner.arena);
    regions.reserve(s_UiSkinMaxRegionCount + 1u);
    for(u32 index = 0u; index <= s_UiSkinMaxRegionCount; ++index)
        regions.push_back(MakeRegion(RegionName(index), index));
    SetRegions(skin, Move(regions));
    EXPECT_FALSE(skin.validatePayload());
    EXPECT_EQ(skin.findRegion(RegionName(s_UiSkinMaxRegionCount)), &skin.regions().back());
    EXPECT_EQ(skin.findRegion(Name("missing")), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

