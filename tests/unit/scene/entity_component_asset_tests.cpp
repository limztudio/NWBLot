// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene_asset_test_helpers.h"

#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_graphics/renderer/subsystem.adhd.h>
#include <impl/assets_scene/component_assets.adhd.h>

#include <core/alloc/general.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_entity_component_asset_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core::Assets;
using namespace NWB::Impl;
using namespace NWB::Tests::SceneAssetTestHelpers;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SceneSubsystemAssets, EntityAndComponentAssetsDecodeAndInstantiate){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena(Name("tests/scene/entity_component_assets"));
    Core::Alloc::GlobalArena globalArena(Name("tests/scene/entity_component_assets_global"));
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
    source.setBinary(s_RendererSubsystemPath, MakeSubsystemBinary(arena, rendererSubsystem));

    TransformComponentPayloadBinary transformPayload;
    transformPayload.position = Float4(1.0f, 2.0f, 3.0f, 0.0f);
    transformPayload.rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    transformPayload.scale = Float4(2.0f, 2.0f, 2.0f, 0.0f);
    source.setBinary(Name("project/entities/hero/transform"), MakeComponentBinary<TransformComponentAsset>(arena, transformPayload));

    SingleAssetRefComponentPayloadBinary modelPayload;
    modelPayload.assetHash = Name("project/characters/female/model").hash();
    source.setBinary(Name("project/entities/hero/model"), MakeComponentBinary<ModelComponentAsset>(arena, modelPayload));

    RendererComponentPayloadBinary rendererPayload;
    rendererPayload.materialHash = Name("project/materials/mat_skinned_uv").hash();
    rendererPayload.opticalVolumeGroupHash = Name("glass_prism").hash();
    rendererPayload.opticalVolumePriority = 3;
    rendererPayload.opticalMediumPriority = 5;
    rendererPayload.visible = 1u;
    rendererPayload.opticalVolumeCoincidence = static_cast<u8>(OpticalVolumeCoincidence::SharedGroup);
    rendererPayload.opticalBoundaryMode = static_cast<u8>(OpticalBoundaryMode::ClosedPriority);
    source.setBinary(Name("project/entities/hero/renderer"), MakeComponentBinary<RendererComponentAsset>(arena, rendererPayload));

    EntityAsset::ComponentRefVector heroComponents(arena);
    heroComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/transform"));
    heroComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/model"));
    heroComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/renderer"));
    source.setBinary(Name("project/entities/hero"), MakeEntityBinary(arena, heroComponents));

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(SceneAsset::SubsystemRef(s_RendererSubsystemPath));
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    SceneAsset::EntityRefVector sceneEntities(arena);
    sceneEntities.push_back(SceneAsset::EntityRef("project/entities/hero"));
    source.setBinary(Name("project/scenes/entity_scene"), MakeSceneBinary(arena, sceneSubsystems, sceneAssets, sceneEntities));

    ActiveSceneSubsystems active(globalArena);
    ASSERT_TRUE(active.loadScene(assets, ActiveSceneSubsystems::SceneRef("project/scenes/entity_scene")));
    ASSERT_EQ(active.entities().size(), 1u);
    EXPECT_EQ(active.entities()[0].entity.name(), Name("project/entities/hero"));
    ASSERT_EQ(active.entities()[0].components.size(), 3u);

    Tests::EcsTestWorld testWorld;
    ASSERT_TRUE(active.instantiateEntities(testWorld.world));

    usize entityCount = 0u;
    const auto view = testWorld.world.view<Scene::TransformComponent, ModelComponent, RendererComponent>();
    for(auto it = view.begin(); it != view.end(); ++it){
        auto&& [entityId, transform, model, renderer] = *it;
        EXPECT_TRUE(entityId.valid());
        EXPECT_FLOAT_EQ(transform.position.x, 1.0f);
        EXPECT_FLOAT_EQ(transform.position.y, 2.0f);
        EXPECT_FLOAT_EQ(transform.position.z, 3.0f);
        EXPECT_FLOAT_EQ(transform.scale.x, 2.0f);
        EXPECT_EQ(model.model.name(), Name("project/characters/female/model"));
        EXPECT_EQ(renderer.material.name(), Name("project/materials/mat_skinned_uv"));
        EXPECT_TRUE(renderer.visible);
        EXPECT_EQ(renderer.opticalVolumeCoincidence, OpticalVolumeCoincidence::SharedGroup);
        EXPECT_EQ(renderer.opticalVolumeGroup, Name("glass_prism"));
        EXPECT_EQ(renderer.opticalVolumePriority, 3);
        EXPECT_EQ(renderer.opticalBoundaryMode, OpticalBoundaryMode::ClosedPriority);
        EXPECT_EQ(renderer.opticalMediumPriority, 5);
        ++entityCount;
    }
    EXPECT_EQ(entityCount, 1u);

    RendererComponentPayloadBinary invalidRendererPayload = rendererPayload;
    invalidRendererPayload.opticalVolumeGroupHash = {};
    ComponentAssetCodec componentCodec;
    EXPECT_FALSE(componentCodec.deserialize(arena, Name("project/entities/hero/invalid_renderer"), MakeComponentBinary<RendererComponentAsset>(arena, invalidRendererPayload)).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("shared_group optical volume requires optical_volume_group")));

    source.setBinary(Name("project/entities/hero/transform_duplicate"), MakeComponentBinary<TransformComponentAsset>(arena, transformPayload));
    EntityAsset::ComponentRefVector duplicateComponents(arena);
    duplicateComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/transform"));
    duplicateComponents.push_back(EntityAsset::ComponentRef("project/entities/hero/transform_duplicate"));
    source.setBinary(Name("project/entities/duplicate_comp_entity"), MakeEntityBinary(arena, duplicateComponents));

    SceneAsset::EntityRefVector duplicateSceneEntities(arena);
    duplicateSceneEntities.push_back(SceneAsset::EntityRef("project/entities/duplicate_comp_entity"));
    source.setBinary(Name("project/scenes/duplicate_comp_scene"), MakeSceneBinary(arena, sceneSubsystems, sceneAssets, duplicateSceneEntities));
    EXPECT_FALSE(active.loadScene(assets, ActiveSceneSubsystems::SceneRef("project/scenes/duplicate_comp_scene")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate component type")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

