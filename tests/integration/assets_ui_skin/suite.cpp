// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/assets_ui_skin/name_cases.adhd.h>

#include <impl/assets_ui_skin/asset.h>
#include <impl/assets_texture/asset.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_region_index_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_SkinPath = "test/skin";
constexpr AStringView s_LastRegionName = "last";

using namespace Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static UiSkinRegion MakeRegion(const Name name, const u32 ordinal)noexcept{
    UiSkinRegion region;
    region.name = name;
    region.rectangle = { ordinal, 0u, 1u, 1u };
    return region;
}

static void SetRegions(UiSkin& skin, UiSkin::RegionVector&& regions){
    skin.setAtlas(Core::Assets::AssetRef<Texture>("test/skin/texture"), 8192u, 1u, 1.0f, Move(regions));
}

[[nodiscard]] static Name RegionName(const u32 index)noexcept{
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
    UiSkin skin(owner.arena, Name(s_SkinPath));
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
    EXPECT_EQ(skin.findRegion(s_NameNone), nullptr);
    EXPECT_EQ(skin.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameMissing)), nullptr);
    NameHash after{};
    after.qwords[0u] = Limit<u64>::s_Max;
    EXPECT_EQ(skin.findRegion(Name(after)), nullptr);
}

TEST(AssetsUiSkinRegionIndex, CanonicalDuplicateKeepsFirstAuthoredMatchAndFailsAdmission){
    using namespace __hidden_ui_skin_region_index_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena<> owner;
    UiSkin skin(owner.arena, Name(s_SkinPath));
    UiSkin::RegionVector regions(owner.arena);
    regions.push_back(MakeRegion(Name("Button\\Normal"), 0u));
    regions.push_back(MakeRegion(Name(NWB::Tests::UiSkinCases::s_UiTestPanelNormal), 1u));
    regions.push_back(MakeRegion(Name("button/normal"), 2u));
    SetRegions(skin, Move(regions));
    EXPECT_EQ(skin.findRegion(Name("BUTTON/NORMAL")), &skin.regions()[0u]);
    EXPECT_FALSE(skin.validatePayload());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate region")));
}

TEST(AssetsUiSkinRegionIndex, EqualLeadingHashLanesStillResolveDistinctNames){
    using namespace __hidden_ui_skin_region_index_tests;
    TestArena<> owner;
    UiSkin skin(owner.arena, Name(s_SkinPath));
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
    UiSkin skin(owner.arena, Name(s_SkinPath));
    constexpr Array<u32, 3u> s_Counts = { 128u, 129u, 128u };
    Name previous = s_NameNone;
    for(u32 stage = 0u; stage < LengthOf(s_Counts); ++stage){
        UiSkin::RegionVector regions(owner.arena);
        for(u32 index = 0u; index < s_Counts[stage]; ++index)
            regions.push_back(MakeRegion(RegionName(index + stage * 256u), index));
        SetRegions(skin, Move(regions));
        ASSERT_TRUE(skin.validatePayload());
        for(const UiSkinRegion& region : skin.regions())
            EXPECT_EQ(skin.findRegion(region.name), &region);
        EXPECT_EQ(skin.findRegion(previous), nullptr);
        EXPECT_EQ(skin.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameMissing)), nullptr);
        previous = skin.regions().back().name;
    }
}

TEST(AssetsUiSkinRegionIndex, ReplacementMovesAndCopiesDoNotRetainStaleRegionReferences){
    using namespace __hidden_ui_skin_region_index_tests;
    TestArena<> firstOwner;
    TestArena<> secondOwner;
    UiSkin source(firstOwner.arena, Name(s_SkinPath));
    UiSkin::RegionVector regions(firstOwner.arena);
    regions.push_back(MakeRegion(Name(UiSkinCases::s_UiTestOld), 0u));
    SetRegions(source, Move(regions));
    UiSkin::RegionVector replacement(secondOwner.arena);
    replacement.push_back(MakeRegion(Name(UiSkinCases::s_UiTestNew), 1u));
    replacement.push_back(MakeRegion(Name(UiSkinCases::s_UiTestOther), 2u));
    SetRegions(source, Move(replacement));
    EXPECT_EQ(&source.regions().get_allocator().arena(), &secondOwner.arena);
    EXPECT_EQ(source.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameOld)), nullptr);
    UiSkin moved(Move(source));
    EXPECT_EQ(moved.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameNew)), &moved.regions()[0u]);
    UiSkin copied(moved);
    EXPECT_EQ(copied.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameOther)), &copied.regions()[1u]);
    EXPECT_NE(copied.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameOther)), moved.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameOther)));
    UiSkin assigned(firstOwner.arena, Name(UiSkinCases::s_UiTestDestination));
    assigned = Move(moved);
    EXPECT_EQ(assigned.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameNew)), &assigned.regions()[0u]);
    EXPECT_EQ(assigned.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameOld)), nullptr);
    UiSkin::RegionVector empty(secondOwner.arena);
    SetRegions(assigned, Move(empty));
    EXPECT_EQ(assigned.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameNew)), nullptr);
    EXPECT_EQ(copied.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameNew)), &copied.regions()[0u]);
}

TEST(AssetsUiSkinRegionIndex, FailedBinaryLoadPreservesLookupAndAuthoredOrder){
    using namespace __hidden_ui_skin_region_index_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena<> owner;
    UiSkin source(owner.arena, Name(s_SkinPath));
    UiSkin::RegionVector regions(owner.arena);
    regions.push_back(MakeRegion(Name(s_LastRegionName), 0u));
    regions.push_back(MakeRegion(Name(UiSkinCases::s_UiTestFirst), 1u));
    SetRegions(source, Move(regions));
    UiSkinAssetCodec codec;
    Core::Assets::AssetBytes binary(owner.arena);
    ASSERT_TRUE(codec.serialize(source, binary));
    UiSkin loaded(owner.arena, source.virtualPath());
    ASSERT_TRUE(loaded.loadBinary(binary));
    ASSERT_EQ(loaded.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameFirst)), &loaded.regions()[1u]);
    const UiSkinRegion* retained = loaded.findRegion(Name(s_LastRegionName));
    const usize firstHash = sizeof(UiSkinHeaderBinary);
    const usize secondHash = firstHash + sizeof(UiSkinRegionBinary);
    NWB_MEMCPY(binary.data() + secondHash, sizeof(NameHash), binary.data() + firstHash, sizeof(NameHash));
    EXPECT_FALSE(loaded.loadBinary(binary));
    EXPECT_EQ(loaded.findRegion(Name(s_LastRegionName)), retained);
    EXPECT_EQ(loaded.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameFirst)), &loaded.regions()[1u]);
    EXPECT_EQ(loaded.regions()[0u].name, Name(s_LastRegionName));
}

TEST(AssetsUiSkinRegionIndex, OversizedInvalidSkinRemainsInspectableWithoutIndexedAdmission){
    using namespace __hidden_ui_skin_region_index_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena<> owner;
    UiSkin skin(owner.arena, Name(s_SkinPath));
    UiSkin::RegionVector regions(owner.arena);
    regions.reserve(s_UiSkinMaxRegionCount + 1u);
    for(u32 index = 0u; index <= s_UiSkinMaxRegionCount; ++index)
        regions.push_back(MakeRegion(RegionName(index), index));
    SetRegions(skin, Move(regions));
    EXPECT_FALSE(skin.validatePayload());
    EXPECT_EQ(skin.findRegion(RegionName(s_UiSkinMaxRegionCount)), &skin.regions().back());
    EXPECT_EQ(skin.findRegion(Name(NWB::Tests::UiSkinCases::s_UiLookupNameMissing)), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/assets_ui_skin/name_cases.adhd.h>

#include <impl/assets_ui_skin/cook.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_toolkit_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_EngineDirectory = "impl";
constexpr AStringView s_AssetsDirectory = "assets";
constexpr AStringView s_UiDirectory = "ui";
constexpr AStringView s_SkinsDirectory = "skins";
constexpr AStringView s_DefaultSkinDirectory = "default";
constexpr AStringView s_AtlasMetadataFilename = "atlas.nwb";
constexpr AStringView s_WhiteRegionName = "white";

using namespace Impl;

struct ToolkitSkinArenaTag{};
using ToolkitSkinArena = TestArena<ToolkitSkinArenaTag>;

static constexpr Name s_ScratchArena = NWB::Tests::UiSkinCases::s_OneOffNameTestsIntegrationAssetsUiSkinToolkitContract;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AuthoredSkin{
    UiSkin skin;
    b8 complete;
};

[[nodiscard]] static Expected<AuthoredSkin> LoadAuthoredSkin(const Path& assetRoot, const Path& atlasPath, const AStringView virtualRoot, ToolkitSkinArena& testArena){
    Core::Assets::AssetString metadata(testArena.arena);
    if(!ReadTextFile(atlasPath, metadata))
        return MakeUnexpected(Failure{});
    Core::Metascript::Document document(testArena.arena);
    if(!document.parse(metadata))
        return MakeUnexpected(Failure{});
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    auto entryResult = ParseUiSkinCookMetadata(assetRoot, virtualRoot, atlasPath, document, testArena.arena, scratchArena);
    if(!entryResult)
        return MakeUnexpected(Failure{});
    auto skinResult = BuildUiSkinAsset(*entryResult, testArena.arena);
    if(!skinResult)
        return MakeUnexpected(Failure{});
    return AuthoredSkin{ Move(*skinResult), entryResult->completeToolkitSkin };
}

static void CopyWithoutRegion(const UiSkin& source, const Name& removed, ToolkitSkinArena& testArena, UiSkin& outSkin){
    UiSkin::RegionVector regions(testArena.arena);
    for(const UiSkinRegion& region : source.regions()){
        if(region.name != removed)
            regions.push_back(region);
    }
    outSkin.setAtlas(source.texture(), source.atlasWidth(), source.atlasHeight(), source.referenceDensity(), Move(regions));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsUiSkinToolkitContract, RejectsMissingBasePartsButAcceptsDeclaredStateFallbacks){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ToolkitSkinArena testArena;
    const Path assetRoot = Path(testArena.arena, NWB_REPO_ROOT) / s_EngineDirectory / s_AssetsDirectory;
    const Path atlasPath = assetRoot / s_UiDirectory / s_SkinsDirectory / s_DefaultSkinDirectory / s_AtlasMetadataFilename;
    auto sourceResult = LoadAuthoredSkin(assetRoot, atlasPath, "engine", testArena);
    ASSERT_TRUE(sourceResult);
    const UiSkin& source = sourceResult->skin;
    EXPECT_TRUE(sourceResult->complete);

    const Array<Name, 6u> required = { Name(UiSkinCases::s_UiTitle), Name(UiSkinCases::s_UiCheck), Name(UiSkinCases::s_UiSelected), Name(UiSkinCases::s_UiArrow), Name(UiSkinCases::s_UiFill), Name(UiSkinCases::s_UiFocus) };
    for(const Name& removed : required){
        UiSkin candidate(testArena.arena, source.virtualPath());
        CopyWithoutRegion(source, removed, testArena, candidate);
        EXPECT_TRUE(candidate.validatePayload());
        EXPECT_FALSE(ValidateUiSkinToolkitContract(candidate)) << removed.resolvedText();
    }

    const Array<Name, 5u> optional = { Name(UiSkinCases::s_UiTestButtonHover), Name(UiSkinCases::s_UiTestCheckboxChecked), Name(UiSkinCases::s_UiTestEditFocused), Name(UiSkinCases::s_UiTestComboOpen), Name(UiSkinCases::s_UiTestSliderThumbHover) };
    for(const Name& removed : optional){
        UiSkin candidate(testArena.arena, source.virtualPath());
        CopyWithoutRegion(source, removed, testArena, candidate);
        EXPECT_TRUE(ValidateUiSkinToolkitContract(candidate)) << removed.resolvedText();
    }

    UiSkin noResize(testArena.arena, source.virtualPath());
    CopyWithoutRegion(source, Name(s_WhiteRegionName), testArena, noResize);
    EXPECT_FALSE(ValidateUiSkinToolkitContract(noResize));

    UiSkin::RegionVector regions(testArena.arena);
    for(const UiSkinRegion& region : noResize.regions())
        regions.push_back(region);
    UiSkinRegion resize = *source.findRegion(Name(s_WhiteRegionName));
    resize.name = Name(UiSkinCases::s_UiTestWindowResize);
    regions.push_back(resize);
    UiSkin explicitResize(testArena.arena, source.virtualPath());
    explicitResize.setAtlas(source.texture(), source.atlasWidth(), source.atlasHeight(), source.referenceDensity(), Move(regions));
    EXPECT_TRUE(ValidateUiSkinToolkitContract(explicitResize));
}

TEST(AssetsUiSkinToolkitContract, CookOptInRejectsIncompleteMetadataWhileGenericSkinRemainsValid){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ToolkitSkinArena testArena;
    const Path assetRoot(testArena.arena, "C:/ui_skin_contract_tests/assets");
    const Path atlasPath = assetRoot / s_UiDirectory / s_AtlasMetadataFilename;
    const Path defaultAtlasPath = Path(testArena.arena, NWB_REPO_ROOT) / s_EngineDirectory / s_AssetsDirectory / s_UiDirectory / s_SkinsDirectory / s_DefaultSkinDirectory / s_AtlasMetadataFilename;
    TestAString genericMetadata;
    ASSERT_TRUE(ReadTextFile(defaultAtlasPath, genericMetadata));
    const AStringView contractField = "asset.toolkit_contract = \"widgets\";";
    const usize contractOffset = genericMetadata.find(contractField);
    ASSERT_NE(contractOffset, TestAString::npos);
    genericMetadata.erase(contractOffset, contractField.size());
    genericMetadata.append("asset.regions = [{ \"name\": \"panel.normal\", \"rect\": [0, 0, 16, 16] }];\r\n");
    TestAString completeMetadata(genericMetadata);
    completeMetadata.append(contractField);

    Core::Metascript::Document genericDocument(testArena.arena);
    ASSERT_TRUE(genericDocument.parse(genericMetadata));
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    auto genericEntryParseResult = ParseUiSkinCookMetadata(assetRoot, "project", atlasPath, genericDocument, testArena.arena, scratchArena);
    ASSERT_TRUE(genericEntryParseResult);
    const UiSkinCookEntry& genericEntry = *genericEntryParseResult;
    EXPECT_FALSE(genericEntry.completeToolkitSkin);
    UiSkin genericSkin(testArena.arena);
    auto genericSkinBuildResult = BuildUiSkinAsset(genericEntry, genericEntry.arena);
    ASSERT_TRUE(genericSkinBuildResult);
    genericSkin = Move(*genericSkinBuildResult);
    EXPECT_FALSE(ValidateUiSkinToolkitContract(genericSkin));

    Core::Metascript::Document completeDocument(testArena.arena);
    ASSERT_TRUE(completeDocument.parse(completeMetadata));
    EXPECT_FALSE(ParseUiSkinCookMetadata(assetRoot, "project", atlasPath, completeDocument, testArena.arena, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("missing required region 'window.normal'")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/assets_ui_skin/name_cases.adhd.h>

#include <impl/assets_ui_skin/cook.h>
#include <impl/assets_texture/asset.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_ui_skin_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_SkinAssetRoot = "C:/ui_skin_tests/assets";
constexpr AStringView s_UiDirectory = "ui";
constexpr AStringView s_AtlasMetadataFilename = "atlas.nwb";
constexpr AStringView s_TexturePath = "project/ui/texture";
constexpr AStringView s_AtlasPath = "project/ui/atlas";

using namespace Impl;

struct UiSkinTestArenaTag{};
using SkinTestArena = TestArena<UiSkinTestArenaTag>;

static constexpr Name s_ScratchArena = NWB::Tests::UiSkinCases::s_OneOffNameTestsIntegrationAssetsUiSkinCook;
static constexpr AStringView s_Metadata =
    "ui_skin asset;\r\n"
    "asset.texture = \"project/ui/texture\";\r\n"
    "asset.atlas_extent = [64, 32];\r\n"
    "asset.reference_density = 2.0;\r\n"
    "asset.regions = [\r\n"
    "  { \"name\": \"panel.normal\", \"rect\": [2, 4, 24, 20], \"draw_mode\": \"nine_slice\",\r\n"
    "    \"slice\": [3, 4, 5, 6], \"padding\": [1.0, 2.0, 3.0, 4.0], \"minimum_size\": [8.0, 10.0] },\r\n"
    "  { \"name\": \"combo.arrow\", \"rect\": [28, 4, 12, 8] },\r\n"
    "];\r\n"
;
static constexpr Array<AStringView, UiSkinColorRole::kCount> s_PaletteRoleNames = {
    "text.normal", "text.disabled", "text.tooltip", "edit.background", "edit.selection",
    "edit.inactive_selection", "edit.caret", "edit.preedit", "scrollbar.track", "scrollbar.thumb",
    "scrollbar.disabled", "popup.backdrop", "control.hover_tint", "control.pressed_tint",
    "control.disabled_tint", "progress.track_tint", "progress.fill_tint",
};
static_assert(LengthOf(s_PaletteRoleNames) == UiSkinColorRole::kCount, "UI skin palette role fixture drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<UiSkinCookEntry> ParseMetadata(const AStringView metadata, SkinTestArena& testArena){
    Core::Metascript::Document document(testArena.arena);
    if(!document.parse(metadata))
        return MakeUnexpected(Failure{});
    const Path assetRoot(testArena.arena, s_SkinAssetRoot);
    const Path metadataPath = assetRoot / s_UiDirectory / s_AtlasMetadataFilename;
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    return ParseUiSkinCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena);
}

[[nodiscard]] static Core::Assets::AssetBytes MakeBinary(const UiSkinHeaderBinary& header, const UiSkinRegionBinary& region, SkinTestArena& testArena){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header) + sizeof(region) + UiSkinColorRole::kCount * sizeof(UiSkinColor) + sizeof(UiSkinTypography));
    AppendPOD(binary, header);
    AppendPOD(binary, region);
    for(const UiSkinColor& color : UiSkinPalette{}.colors)
        AppendPOD(binary, color);
    AppendPOD(binary, UiSkinTypography{});
    return binary;
}

[[nodiscard]] static UiSkinHeaderBinary ValidHeader(){
    UiSkinHeaderBinary header;
    header.textureNameHash = Name(s_TexturePath).hash();
    header.atlasWidth = 64u;
    header.atlasHeight = 32u;
    header.referenceDensity = 2.0f;
    header.regionCount = 1u;
    return header;
}

[[nodiscard]] static UiSkinRegionBinary ValidRegion(){
    UiSkinRegionBinary region;
    region.nameHash = Name(UiSkinCases::s_UiTestPanelNormal).hash();
    region.x = 2u;
    region.y = 4u;
    region.width = 24u;
    region.height = 20u;
    region.sliceLeft = 3u;
    region.sliceTop = 4u;
    region.sliceRight = 5u;
    region.sliceBottom = 6u;
    region.drawMode = UiSkinDrawMode::NineSlice;
    return region;
}

[[nodiscard]] static TestAString PaletteMetadata(const u32 variant = 0u, const b8 includeTypography = true){
    TestAString metadata(s_Metadata);
    if(includeTypography)
        metadata.append("asset.typography = {\"default_font_size\": 16.0};\r\n");
    if(variant == 1u)
        return metadata;
    metadata.append("asset.colors = [\r\n");
    const usize count = variant == 2u ? UiSkinColorRole::kCount - 1u : UiSkinColorRole::kCount;
    for(usize index = 0u; index < count; ++index){
        const AStringView name = variant == 3u && index == count - 1u ? s_PaletteRoleNames[0u] : variant == 4u && index == count - 1u ? AStringView("unknown.role") : s_PaletteRoleNames[index];
        metadata.append("  { \"name\": \"");
        metadata.append(name);
        metadata.append("\", \"rgba\": ");
        metadata.append(variant == 5u && index == 0u ? "[16.1, 0.5, 0.75, 1.0]" : variant == 6u && index == 0u ? "[0.25, 0.5, 0.75, 1.1]" : index == UiSkinColorRole::ControlHoverTint ? "[1.08, 1.08, 1.08, 1.0]" : "[0.25, 0.5, 0.75, 1.0]");
        metadata.append(" },\r\n");
    }
    metadata.append("];\r\n");
    return metadata;
}

[[nodiscard]] static TestAString TypographyMetadata(const AStringView typography = "{\"default_font_size\": 21.5}"){
    TestAString metadata = PaletteMetadata(0u, false);
    if(!typography.empty()){
        metadata.append("asset.typography = ");
        metadata.append(typography);
        metadata.append(";\r\n");
    }
    return metadata;
}

[[nodiscard]] static Core::Assets::AssetBytes MakeTypographyBinary(const UiSkinColor& color, const f32 defaultFontSize, SkinTestArena& testArena){
    UiSkinHeaderBinary header = ValidHeader();
    Core::Assets::AssetBytes binary = MakeBinary(header, ValidRegion(), testArena);
    binary.resize(sizeof(header) + sizeof(UiSkinRegionBinary));
    for(u32 index = 0u; index < UiSkinColorRole::kCount; ++index)
        AppendPOD(binary, color);
    UiSkinTypography typography;
    typography.defaultFontSize = defaultFontSize;
    AppendPOD(binary, typography);
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsUiSkin, TypographyRejectsMalformedFooterWithoutReplacingSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const UiSkinColor color(0.25f, 0.5f, 0.75f, 1.0f);
    UiSkin skin(testArena.arena, Name(s_AtlasPath));
    ASSERT_TRUE(skin.loadBinary(MakeTypographyBinary(color, 21.5f, testArena)));

    for(u32 variant = 0u; variant < 5u; ++variant){
        const f32 fontSize = variant == 2u ? 0.0f : variant == 3u ? 2049.0f : variant == 4u ? Limit<f32>::s_QuietNaN : 21.5f;
        Core::Assets::AssetBytes binary = MakeTypographyBinary(color, fontSize, testArena);
        if(variant == 0u)
            binary.pop_back();
        if(variant == 1u)
            binary.push_back(0u);
        EXPECT_FALSE(skin.loadBinary(binary)) << variant;
        EXPECT_FLOAT_EQ(skin.typography().defaultFontSize, 21.5f);
    }
}

TEST(AssetsUiSkin, TypographyMetadataRequiresValidFontSize){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    auto entryResult = ParseMetadata(TypographyMetadata(), testArena);
    ASSERT_TRUE(entryResult);
    const Array<AStringView, 6u> malformed = { "", "{}", "{\"default_font_size\": 0.0}", "{\"default_font_size\": 2049.0}", "{\"default_font_size\": \"large\"}", "{\"default_font_size\": 16.0, \"body\": 18.0}" };
    for(u32 index = 0u; index < LengthOf(malformed); ++index)
        EXPECT_FALSE(ParseMetadata(TypographyMetadata(malformed[index]), testArena)) << index;
}

TEST(AssetsUiSkin, PaletteRejectsMalformedFooterWithoutReplacingSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinColor color;
    color.r = 0.25f;
    color.g = 0.5f;
    color.b = 0.75f;
    color.a = 1.0f;
    UiSkin skin(testArena.arena, Name(s_AtlasPath));
    ASSERT_TRUE(skin.loadBinary(MakeTypographyBinary(color, 16.0f, testArena)));

    for(u32 variant = 0u; variant < 6u; ++variant){
        UiSkinColor candidateColor = color;
        if(variant == 2u)
            candidateColor.r = Limit<f32>::s_QuietNaN;
        if(variant == 3u)
            candidateColor.g = -0.1f;
        if(variant == 4u)
            candidateColor.b = 16.1f;
        if(variant == 5u)
            candidateColor.a = 1.1f;
        Core::Assets::AssetBytes binary = MakeTypographyBinary(candidateColor, 16.0f, testArena);
        if(variant == 0u)
            binary.resize(binary.size() - sizeof(UiSkinTypography) - 1u);
        if(variant == 1u)
            binary.push_back(0u);
        EXPECT_FALSE(skin.loadBinary(binary)) << variant;
        ASSERT_EQ(skin.regions().size(), 1u);
        EXPECT_EQ(skin.regions().front().name, Name(UiSkinCases::s_UiTestPanelNormal));
        EXPECT_FLOAT_EQ(skin.palette().colors[UiSkinColorRole::TextNormal].r, 0.25f);
    }
}

TEST(AssetsUiSkin, PaletteMetadataRequiresEveryKnownRoleAndValidRgba){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    auto entryResult = ParseMetadata(PaletteMetadata(), testArena);
    ASSERT_TRUE(entryResult);
    for(u32 variant = 1u; variant <= 6u; ++variant)
        EXPECT_FALSE(ParseMetadata(PaletteMetadata(variant), testArena)) << variant;
}

TEST(AssetsUiSkin, RejectsObsoleteBinaryVersionsWithoutReplacingCurrentSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkin skin(testArena.arena, Name(s_AtlasPath));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(ValidHeader(), ValidRegion(), testArena)));
    for(const u32 version : { 1u, 2u }){
        UiSkinHeaderBinary header = ValidHeader();
        header.version = version;
        Core::Assets::AssetBytes binary = MakeBinary(header, ValidRegion(), testArena);
        EXPECT_FALSE(skin.loadBinary(binary)) << version;
        binary.resize(sizeof(header) + sizeof(UiSkinRegionBinary) + (version == 2u ? UiSkinColorRole::kCount * sizeof(UiSkinColor) : 0u));
        EXPECT_FALSE(skin.loadBinary(binary)) << version;
        EXPECT_EQ(skin.regions().size(), 1u);
        EXPECT_FLOAT_EQ(skin.typography().defaultFontSize, 16.0f);
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("recook required")));
}

TEST(AssetsUiSkin, LoadRejectsMalformedHeaderAndCountWithoutReplacingSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const UiSkinHeaderBinary validHeader = ValidHeader();
    const UiSkinRegionBinary validRegion = ValidRegion();
    UiSkin skin(testArena.arena, Name(s_AtlasPath));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(validHeader, validRegion, testArena)));

    for(u32 variant = 0u; variant < 10u; ++variant){
        UiSkinHeaderBinary header = validHeader;
        switch(variant){
        case 0u: header.magic = 0u; break;
        case 1u: header.version = 4u; break;
        case 2u: header.reserved0 = 1u; break;
        case 3u: header.reserved1 = 1u; break;
        case 4u: header.regionCount = 0u; break;
        case 5u: header.regionCount = Limit<u32>::s_Max; break;
        case 6u: header.textureNameHash = {}; break;
        case 7u: header.atlasWidth = 0u; break;
        case 8u: header.referenceDensity = 0.0f; break;
        case 9u: header.referenceDensity = Limit<f32>::s_QuietNaN; break;
        }
        const Core::Assets::AssetBytes binary = MakeBinary(header, validRegion, testArena);
        EXPECT_FALSE(skin.loadBinary(binary)) << variant;
        ASSERT_EQ(skin.regions().size(), 1u);
        EXPECT_EQ(skin.atlasWidth(), validHeader.atlasWidth);
    }

    Core::Assets::AssetBytes binary = MakeBinary(validHeader, validRegion, testArena);
    binary.pop_back();
    EXPECT_FALSE(skin.loadBinary(binary));
    binary.resize(sizeof(UiSkinHeaderBinary) - 1u);
    EXPECT_FALSE(skin.loadBinary(binary));
    binary = MakeBinary(validHeader, validRegion, testArena);
    binary.push_back(0u);
    EXPECT_FALSE(skin.loadBinary(binary));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("trailing bytes")));
}

TEST(AssetsUiSkin, LoadRejectsInvalidRegionBoundsMetricsAndFlags){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const UiSkinHeaderBinary header = ValidHeader();
    for(u32 variant = 0u; variant < 14u; ++variant){
        UiSkinRegionBinary region = ValidRegion();
        switch(variant){
        case 0u: region.nameHash = {}; break;
        case 1u: region.nameHash = Name("").hash(); break;
        case 2u: region.width = 0u; break;
        case 3u: region.x = Limit<u32>::s_Max; break;
        case 4u: region.y = 32u; break;
        case 5u: region.width = Limit<u32>::s_Max; break;
        case 6u: region.sliceLeft = 25u; break;
        case 7u: region.sliceTop = Limit<u32>::s_Max; break;
        case 8u: region.drawMode = 256u; break;
        case 9u: region.reserved = 1u; break;
        case 10u: region.paddingLeft = -1.0f; break;
        case 11u: region.minimumWidth = Limit<f32>::s_QuietNaN; break;
        case 12u: region.minimumHeight = Limit<f32>::s_Infinity; break;
        case 13u: region.drawMode = UiSkinDrawMode::Sprite; break;
        }
        UiSkin skin(testArena.arena, Name(s_AtlasPath));
        EXPECT_FALSE(skin.loadBinary(MakeBinary(header, region, testArena))) << variant;
        EXPECT_TRUE(skin.regions().empty());
    }
}

TEST(AssetsUiSkin, RejectsOverLimitCountsBeforeReadingOrCopyingRegions){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkin skin(testArena.arena, Name(s_AtlasPath));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(ValidHeader(), ValidRegion(), testArena)));

    UiSkinHeaderBinary header = ValidHeader();
    header.regionCount = s_UiSkinMaxRegionCount + 1u;
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header));
    AppendPOD(binary, header);
    EXPECT_FALSE(skin.loadBinary(binary));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("UiSkin::loadBinary failed: region count 4097 exceeds schema limit 4096")));
    ASSERT_EQ(skin.regions().size(), 1u);
    EXPECT_EQ(skin.regions().front().name, Name(UiSkinCases::s_UiTestPanelNormal));

    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(PaletteMetadata()));
    Core::Metascript::Value* const asset = document.findVariable("asset");
    ASSERT_NE(asset, nullptr);
    Core::Metascript::Value& regions = asset->field("regions");
    const Core::Metascript::Value regionTemplate(regions.asList().front());
    regions.asList().clear();
    regions.asList().reserve(s_UiSkinMaxRegionCount + 1u);
    for(u32 index = 0u; index <= s_UiSkinMaxRegionCount; ++index)
        regions.asList().push_back(regionTemplate);
    const Path assetRoot(testArena.arena, s_SkinAssetRoot);
    const Path metadataPath = assetRoot / s_UiDirectory / s_AtlasMetadataFilename;
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    EXPECT_FALSE(ParseUiSkinCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("regions exceed schema limit 4096")));

    UiSkin::RegionVector oversized(testArena.arena);
    oversized.reserve(s_UiSkinMaxRegionCount + 1u);
    for(u32 index = 0u; index <= s_UiSkinMaxRegionCount; ++index)
        oversized.push_back(skin.regions().front());
    UiSkin constructed(testArena.arena, skin.virtualPath());
    constructed.setAtlas(skin.texture(), skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity(), Move(oversized));
    EXPECT_FALSE(constructed.validatePayload());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("UiSkin::validatePayload failed: region count 4097 exceeds schema limit 4096")));
}

TEST(AssetsUiSkin, CookRejectsObsoleteMetadataUnknownFieldsAndMalformedArrays){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    static constexpr Array<AStringView, 22u> s_Overrides = {
        "asset.schema_version = 1;\r\n",
        "asset.schema_version = 2;\r\n",
        "asset.schema_version = 3;\r\n",
        "asset.schema_version = 4;\r\n",
        "asset.version = 3;\r\n",
        "asset.revision = 1;\r\n",
        "asset.toolkit_contract = \"widgets_v1\";\r\n",
        "asset.unknown_field = 1;\r\n",
        "asset.texture = \"\";\r\n",
        "asset.atlas_extent = [64];\r\n",
        "asset.atlas_extent = [64.0, 32];\r\n",
        "asset.atlas_extent = [4294967296, 32];\r\n",
        "asset.atlas_extent = [0, 32];\r\n",
        "asset.reference_density = -1.0;\r\n",
        "asset.regions = [];\r\n",
        "asset.regions = [1];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"unknown\": 1 }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"tile\" }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"nine_slice\" }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"padding\": [0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"minimum_size\": [\"bad\", 1] }];\r\n",
    };
    for(const AStringView overrideText : s_Overrides){
        TestAString metadata(PaletteMetadata());
        metadata.append(overrideText);
        EXPECT_FALSE(ParseMetadata(metadata, testArena)) << overrideText;
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("toolkit_contract must be 'widgets'")));
}

TEST(AssetsUiSkin, RetiredSpriteSliceFieldsAreRejected){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    auto entryResult = ParseMetadata(PaletteMetadata(), testArena);
    ASSERT_TRUE(entryResult);
    static constexpr Array<AStringView, 3u> s_Overrides = {
        "asset.regions = [{ \"name\": \"replacement\", \"rect\": [0, 0, 4, 4], \"slice\": [0, 0, 0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"replacement\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"sprite\", \"slice\": [0, 0, 0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"replacement\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"sprite\", \"slice\": [1, 0, 0, 0] }];\r\n",
    };
    for(const AStringView overrideText : s_Overrides){
        TestAString metadata(PaletteMetadata());
        metadata.append("asset.texture = \"project/other_texture\";\r\n");
        metadata.append(overrideText);
        EXPECT_FALSE(ParseMetadata(metadata, testArena));
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'slice'")));
    TestAString spriteMetadata(PaletteMetadata());
    spriteMetadata.append("asset.regions = [{ \"name\": \"sprite\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"sprite\" }];\r\n");
    auto spriteEntryResult = ParseMetadata(spriteMetadata, testArena);
    ASSERT_TRUE(spriteEntryResult);
    const UiSkinCookEntry& spriteEntry = *spriteEntryResult;
    ASSERT_EQ(spriteEntry.regions.size(), 1u);
}

TEST(AssetsUiSkin, CookRejectsDuplicateNamesAndAtlasOrSliceOverflow){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    static constexpr Array<AStringView, 6u> s_Overrides = {
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4] }, { \"name\": \"A\", \"rect\": [4, 0, 4, 4] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [63, 0, 2, 4] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 31, 4, 2] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"nine_slice\", \"slice\": [3, 0, 2, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"padding\": [-1, 0, 0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"minimum_size\": [1, -1] }];\r\n",
    };
    for(const AStringView overrideText : s_Overrides){
        TestAString metadata(PaletteMetadata());
        metadata.append(overrideText);
        EXPECT_FALSE(ParseMetadata(metadata, testArena)) << overrideText;
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate region")));
}

TEST(AssetsUiSkin, TextureValidationRejectsIdentityDimensionAndExtentMismatch){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkin skin(testArena.arena, Name(s_AtlasPath));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(ValidHeader(), ValidRegion(), testArena)));
    for(u32 variant = 0u; variant < 4u; ++variant){
        Texture texture(testArena.arena, variant == 0u ? Name(UiSkinCases::s_UiTestProjectOther) : Name(s_TexturePath));
        Texture::MipLevelVector mips(testArena.arena);
        Core::Assets::AssetBytes payload(testArena.arena);
        texture.setPayload(
            variant == 1u ? 63u : 64u,
            variant == 2u ? 31u : 32u,
            Move(mips),
            Move(payload),
            variant == 3u ? TextureDimension::TextureCube : TextureDimension::Texture2D,
            1u,
            TexturePayloadFormat::UastcLdr4x4,
            TextureAlphaMode::EmbeddedLdr,
            TextureFormat::s_OpaqueAlphaUnorm8
        );
        EXPECT_FALSE(skin.validateTexture(texture)) << variant;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
