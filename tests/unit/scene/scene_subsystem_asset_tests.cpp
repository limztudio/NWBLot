// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene_asset_test_helpers.h"

#include <tests/common/capturing_logger.h>
#include <tests/unit/scene/name_cases.adhd.h>

#include <impl/assets_graphics/renderer_subsystem.adhd.h>
#include <impl/assets_graphics/transparent_subsystem.adhd.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_mesh/skin_types.adhd.h>
#include <impl/assets_ui_skin/types.adhd.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_subsystem_asset_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core::Assets;
using namespace NWB::Impl;
using namespace NWB::Tests::SceneAssetTestHelpers;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SceneSubsystemAssets, RejectsMismatchedSubsystemTypeTrailingBytesAndCorruptedMagic){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena{Name(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneSubsystemRoundTrip)};
    SkinningSubsystem skinningSubsystem(arena, s_SkinningSubsystemPath);
    PopulateRequiredSubsystemFields(skinningSubsystem);

    const AssetBytes skinningBinary = MakeSubsystemBinary(skinningSubsystem, arena);
    SubsystemAssetCodec codec;

    TypedSubsystemAssetCodec<RendererSubsystem> rendererCodec;
    EXPECT_FALSE(rendererCodec.deserialize(s_SkinningSubsystemPath, skinningBinary, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("subsystem binary type does not match")));

    AssetBytes trailing = skinningBinary;
    trailing.push_back(0u);
    EXPECT_FALSE(codec.deserialize(s_SkinningSubsystemPath, trailing, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("trailing bytes detected")));

    AssetBytes badMagic = skinningBinary;
    badMagic[0] ^= 0xFFu;
    EXPECT_FALSE(codec.deserialize(s_SkinningSubsystemPath, badMagic, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid subsystem asset format")));
}

TEST(SceneSubsystemAssets, RejectsEmptyProjectStartSceneAndEmptySceneSubsystems){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena{Name(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneProjectSceneRoundTrip)};
    ProjectAssetCodec projectCodec;

    const AssetBytes invalidProjectBinary = MakeProjectBinary(s_NameNone, arena);
    EXPECT_FALSE(projectCodec.deserialize(s_DefaultProjectVirtualPath, invalidProjectBinary, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("start_scene must not be empty")));

    SceneAssetCodec sceneCodec;
    SceneAsset::SubsystemRefVector emptySubsystems(arena);
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    const AssetBytes emptySceneBinary = MakeSceneBinary(emptySubsystems, sceneAssets, {}, arena);
    EXPECT_FALSE(sceneCodec.deserialize(Name(NWB::Tests::SceneAssetCases::s_SceneFixtureNameProjectScenesEmpty), emptySceneBinary, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("scene must declare at least one subsystem")));
}

TEST(SceneSubsystemAssets, DeduplicatesSubsystemRefsAndPreservesPreviousSceneOnLoadFailure){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena{Name(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneActiveSceneSubsystems)};
    Core::Alloc::GlobalArena globalArena{Name(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneActiveSceneSubsystemsGlobal)};
    AssetRegistry registry(arena);
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SubsystemAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ProjectAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SceneAssetCodec>()));

    MapAssetBinarySource source(arena);
    AssetManager assets(arena, registry, source);

    RendererSubsystem rendererSubsystem(arena, s_RendererSubsystemPath);
    PopulateRequiredSubsystemFields(rendererSubsystem);

    SkinningSubsystem skinningSubsystem(arena, s_SkinningSubsystemPath);
    PopulateRequiredSubsystemFields(skinningSubsystem);

    TransparentSubsystem transparentSubsystem(arena, s_TransparentSubsystemPath);
    transparentSubsystem.subsystems.push_back(ISubsystem::SubsystemRef(s_RendererSubsystemPath));
    transparentSubsystem.subsystems.push_back(ISubsystem::SubsystemRef(s_SkinningSubsystemPath));
    PopulateRequiredSubsystemFields(transparentSubsystem);

    UiSubsystem uiSubsystem(arena, s_UiSubsystemPath);
    uiSubsystem.subsystems.push_back(ISubsystem::SubsystemRef(s_RendererSubsystemPath));
    uiSubsystem.subsystems.push_back(ISubsystem::SubsystemRef(s_RendererSubsystemPath));
    PopulateRequiredSubsystemFields(uiSubsystem);

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(SceneAsset::SubsystemRef(s_RendererSubsystemPath));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef(s_RendererSubsystemPath));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef(s_UiSubsystemPath));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef(s_TransparentSubsystemPath));
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    sceneAssets.emplace(Name(NWB::Tests::SceneAssetCases::s_SceneCaseCesiumManModel), SceneAsset::AssetRef("project/characters/cesium_man/model"));

    source.setBinary(s_RendererSubsystemPath, MakeSubsystemBinary(rendererSubsystem, arena));
    source.setBinary(s_SkinningSubsystemPath, MakeSubsystemBinary(skinningSubsystem, arena));
    source.setBinary(s_TransparentSubsystemPath, MakeSubsystemBinary(transparentSubsystem, arena));
    source.setBinary(s_UiSubsystemPath, MakeSubsystemBinary(uiSubsystem, arena));
    source.setBinary(s_DefaultProjectVirtualPath, MakeProjectBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectScenesMain), arena));
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectScenesMain), MakeSceneBinary(sceneSubsystems, sceneAssets, {}, arena));

    ActiveSceneSubsystems active(globalArena);
    ASSERT_TRUE(active.loadProjectStartScene(assets));
    EXPECT_EQ(active.subsystemCount(), 4u);
    EXPECT_FALSE(active.findSceneAsset(Name(NWB::Tests::SceneAssetCases::s_SceneFixtureNameNonexistentNickname)).valid());

    const UiSubsystem* const activeUi = active.findSubsystem<UiSubsystem>();
    ASSERT_NE(activeUi, nullptr);
    ASSERT_EQ(activeUi->subsystems.size(), 1u);
    EXPECT_EQ(activeUi->subsystems[0].name(), s_RendererSubsystemPath);

    RendererSubsystem duplicateRendererSubsystem(arena, Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectSubsystemsRendererOverride));
    PopulateRequiredSubsystemFields(duplicateRendererSubsystem);
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectSubsystemsRendererOverride), MakeSubsystemBinary(duplicateRendererSubsystem, arena));
    SceneAsset::SubsystemRefVector duplicateTypeSceneSubsystems(arena);
    duplicateTypeSceneSubsystems.push_back(SceneAsset::SubsystemRef(s_RendererSubsystemPath));
    duplicateTypeSceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/renderer_override"));
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectScenesDuplicateType), MakeSceneBinary(duplicateTypeSceneSubsystems, sceneAssets, {}, arena));
    EXPECT_FALSE(active.loadScene(ActiveSceneSubsystems::SceneRef("project/scenes/duplicate_type"), assets));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate subsystem type")));

    SceneAsset::SubsystemRefVector brokenSceneSubsystems(arena);
    brokenSceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/missing_subsystem"));
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectScenesBroken), MakeSceneBinary(brokenSceneSubsystems, sceneAssets, {}, arena));

    EXPECT_FALSE(active.loadScene(ActiveSceneSubsystems::SceneRef("project/scenes/broken"), assets));
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_EQ(active.subsystemCount(), 4u);
    EXPECT_EQ(active.findSceneAsset(Name(NWB::Tests::SceneAssetCases::s_SceneFixtureNameCesiumManModel)).name(), Name("project/characters/cesium_man/model"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

