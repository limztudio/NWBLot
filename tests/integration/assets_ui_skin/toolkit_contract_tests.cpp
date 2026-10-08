// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_ui_skin/cook.h>
#include <impl/assets_ui_skin/toolkit_contract.h>

#include <core/assets/paths.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_toolkit_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct ToolkitSkinArenaTag{};
using ToolkitSkinArena = TestArena<ToolkitSkinArenaTag>;

static constexpr Name s_ScratchArena("tests/integration/assets_ui_skin/toolkit_contract");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AuthoredSkin{
    UiSkin skin;
    bool complete;
};

[[nodiscard]] static Expected<AuthoredSkin> LoadAuthoredSkin(
    ToolkitSkinArena& testArena,
    const Path& assetRoot,
    const Path& atlasPath,
    const AStringView virtualRoot
){
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

static void CopyWithoutRegion(ToolkitSkinArena& testArena, const UiSkin& source, const Name& removed, UiSkin& outSkin){
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
    const Path assetRoot = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets";
    const Path atlasPath = assetRoot / "ui" / "skins" / "default" / "atlas.nwb";
    auto sourceResult = LoadAuthoredSkin(testArena, assetRoot, atlasPath, "engine");
    ASSERT_TRUE(sourceResult);
    const UiSkin& source = sourceResult->skin;
    EXPECT_TRUE(sourceResult->complete);

    const Name required[]{ Name("window.title"), Name("checkbox.mark"), Name("list.row.selected"),
        Name("combo.arrow"), Name("progress.fill"), Name("focus.overlay") };
    for(const Name& removed : required){
        UiSkin candidate(testArena.arena, source.virtualPath());
        CopyWithoutRegion(testArena, source, removed, candidate);
        EXPECT_TRUE(candidate.validatePayload());
        EXPECT_FALSE(ValidateUiSkinToolkitContract(candidate)) << removed.resolvedText();
    }

    const Name optional[]{ Name("button.hover"), Name("checkbox.checked"), Name("edit.focused"),
        Name("combo.open"), Name("slider.thumb.hover") };
    for(const Name& removed : optional){
        UiSkin candidate(testArena.arena, source.virtualPath());
        CopyWithoutRegion(testArena, source, removed, candidate);
        EXPECT_TRUE(ValidateUiSkinToolkitContract(candidate)) << removed.resolvedText();
    }

    UiSkin noResize(testArena.arena, source.virtualPath());
    CopyWithoutRegion(testArena, source, Name("white"), noResize);
    EXPECT_FALSE(ValidateUiSkinToolkitContract(noResize));

    UiSkin::RegionVector regions(testArena.arena);
    for(const UiSkinRegion& region : noResize.regions())
        regions.push_back(region);
    UiSkinRegion resize = *source.findRegion(Name("white"));
    resize.name = Name("window.resize");
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
    const Path atlasPath = assetRoot / "ui" / "atlas.nwb";
    const Path defaultAtlasPath = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "skins" / "default" / "atlas.nwb";
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

