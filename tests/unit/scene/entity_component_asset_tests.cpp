// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene_asset_test_helpers.h"

#include <tests/common/capturing_logger.h>
#include <tests/unit/scene/name_cases.adhd.h>

#include <impl/assets_graphics/renderer_subsystem.adhd.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_scene/component_assets.adhd.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_entity_component_asset_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core::Assets;
using namespace NWB::Impl;
using namespace NWB::Tests::SceneAssetTestHelpers;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SceneSubsystemAssets, RejectsInvalidLightFlagsAndTrailingComponentBytes){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AssetArena arena(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneComponentBinaryEdges);
    ComponentAssetCodec codec;

    LightComponentPayloadBinary payload;
    payload.enableCaustics = 2u;
    const auto invalidFlags = MakeComponentBinary<LightComponentAsset>(payload, arena);
    EXPECT_FALSE(codec.deserialize(NWB::Tests::SceneAssetCases::s_SceneCaseProjectEntitiesInvalidLight, invalidFlags, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid light flags")));

    payload.enableCaustics = 1u;
    auto trailing = MakeComponentBinary<LightComponentAsset>(payload, arena);
    trailing.push_back(0u);
    EXPECT_FALSE(codec.deserialize(NWB::Tests::SceneAssetCases::s_SceneCaseProjectEntitiesTrailingLight, trailing, arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("trailing bytes detected")));
}


TEST(SceneSubsystemAssets, RejectsMissingSharedOpticalGroupAndDuplicateEntityComponentTypes){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena{Name(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneEntityComponentAssets)};
    Core::Alloc::GlobalArena globalArena{Name(NWB::Tests::SceneAssetCases::s_SceneCaseTestsSceneEntityComponentAssetsGlobal)};
    AssetRegistry registry(arena);
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SubsystemAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ComponentAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<EntityAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ProjectAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SceneAssetCodec>()));

    MapAssetBinarySource source(arena);
    AssetManager assets(arena, registry, source);

    RendererSubsystem rendererSubsystem(arena, s_RendererSubsystemPath);
    PopulateRequiredSubsystemFields(rendererSubsystem);
    source.setBinary(s_RendererSubsystemPath, MakeSubsystemBinary(rendererSubsystem, arena));

    RendererComponentPayloadBinary invalidRendererPayload;
    invalidRendererPayload.materialHash = Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectMaterialsMatSkinnedUv).hash();
    invalidRendererPayload.opticalVolumeGroupHash = {};
    invalidRendererPayload.opticalVolumeCoincidence = static_cast<u8>(OpticalVolumeCoincidence::SharedGroup);
    invalidRendererPayload.opticalBoundaryMode = static_cast<u8>(OpticalBoundaryMode::ClosedPriority);
    ComponentAssetCodec componentCodec;
    EXPECT_FALSE(componentCodec.deserialize(Name(NWB::Tests::SceneAssetCases::s_SceneFixtureNameProjectEntitiesHeroInvalidRenderer), MakeComponentBinary<RendererComponentAsset>(invalidRendererPayload, arena), arena).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("shared_group optical volume requires optical_volume_group")));

    TransformComponentPayloadBinary transformPayload;
    transformPayload.position = Float4(1.0f, 2.0f, 3.0f, 0.0f);
    transformPayload.rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    transformPayload.scale = Float4(2.0f, 2.0f, 2.0f, 0.0f);
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectEntitiesHeroTransform), MakeComponentBinary<TransformComponentAsset>(transformPayload, arena));
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectEntitiesHeroTransformDuplicate), MakeComponentBinary<TransformComponentAsset>(transformPayload, arena));

    EntityAsset::ComponentRefVector duplicateComponents(arena);
    duplicateComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/transform"));
    duplicateComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/transform_duplicate"));
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectEntitiesDuplicateCompEntity), MakeEntityBinary(duplicateComponents, arena));

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(SceneAsset::SubsystemRef(s_RendererSubsystemPath));
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    SceneAsset::EntityRefVector duplicateSceneEntities(arena);
    duplicateSceneEntities.push_back(SceneAsset::EntityRef("project/entities/duplicate_comp_entity"));
    source.setBinary(Name(NWB::Tests::SceneAssetCases::s_SceneCaseProjectScenesDuplicateCompScene), MakeSceneBinary(sceneSubsystems, sceneAssets, duplicateSceneEntities, arena));

    ActiveSceneSubsystems active(globalArena);
    EXPECT_FALSE(active.loadScene(ActiveSceneSubsystems::SceneRef("project/scenes/duplicate_comp_scene"), assets));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate component type")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

