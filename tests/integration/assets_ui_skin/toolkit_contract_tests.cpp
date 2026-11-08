// limztudio@gmail.com
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

