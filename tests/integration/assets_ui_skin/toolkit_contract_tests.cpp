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


[[nodiscard]] static bool LoadAuthoredSkin(
    ToolkitSkinArena& testArena,
    const Path& assetRoot,
    const Path& atlasPath,
    const AStringView virtualRoot,
    UiSkin& outSkin,
    bool& outComplete){
    Core::Assets::AssetString metadata(testArena.arena);
    if(!ReadTextFile(atlasPath, metadata))
        return false;
    Core::Metascript::Document document(testArena.arena);
    if(!document.parse(metadata))
        return false;
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    UiSkinCookEntry entry(testArena.arena);
    if(!ParseUiSkinCookMetadata(assetRoot, virtualRoot, atlasPath, document, entry, scratchArena))
        return false;
    outComplete = entry.completeToolkitSkin;
    return BuildUiSkinAsset(entry, outSkin);
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


TEST(AssetsUiSkinToolkitContract, BothAuthoredSkinsSatisfyCompleteProfile){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ToolkitSkinArena testArena;

    const Path defaultRoot = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets";
    const Path defaultPath = defaultRoot / "ui" / "skins" / "default" / "atlas.nwb";
    UiSkin defaultSkin(testArena.arena);
    bool defaultComplete = false;
    ASSERT_TRUE(LoadAuthoredSkin(testArena, defaultRoot, defaultPath, "engine", defaultSkin, defaultComplete));
    EXPECT_TRUE(defaultComplete);
    EXPECT_TRUE(ValidateUiSkinToolkitContract(defaultSkin));

    const Path alternateRoot = Path(testArena.arena, NWB_REPO_ROOT) / "tests" / "smoke" / "ui_layer" / "assets";
    const Path alternatePath = alternateRoot / "ui" / "skins" / "alternate" / "atlas.nwb";
    UiSkin alternateSkin(testArena.arena);
    bool alternateComplete = false;
    ASSERT_TRUE(LoadAuthoredSkin(testArena, alternateRoot, alternatePath, "project", alternateSkin, alternateComplete));
    EXPECT_TRUE(alternateComplete);
    EXPECT_TRUE(ValidateUiSkinToolkitContract(alternateSkin));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsUiSkinToolkitContract, RejectsMissingBasePartsButAcceptsDeclaredStateFallbacks){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ToolkitSkinArena testArena;
    const Path assetRoot = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets";
    const Path atlasPath = assetRoot / "ui" / "skins" / "default" / "atlas.nwb";
    UiSkin source(testArena.arena);
    bool complete = false;
    ASSERT_TRUE(LoadAuthoredSkin(testArena, assetRoot, atlasPath, "engine", source, complete));
    EXPECT_TRUE(complete);

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
    UiSkinCookEntry genericEntry(testArena.arena);
    ASSERT_TRUE(ParseUiSkinCookMetadata(assetRoot, "project", atlasPath, genericDocument, genericEntry, scratchArena));
    EXPECT_FALSE(genericEntry.completeToolkitSkin);
    UiSkin genericSkin(testArena.arena);
    ASSERT_TRUE(BuildUiSkinAsset(genericEntry, genericSkin));
    EXPECT_FALSE(ValidateUiSkinToolkitContract(genericSkin));

    Core::Metascript::Document completeDocument(testArena.arena);
    ASSERT_TRUE(completeDocument.parse(completeMetadata));
    UiSkinCookEntry completeEntry(testArena.arena);
    EXPECT_FALSE(ParseUiSkinCookMetadata(assetRoot, "project", atlasPath, completeDocument, completeEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(GLOBAL_TEXT("missing required region 'window.normal'")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

