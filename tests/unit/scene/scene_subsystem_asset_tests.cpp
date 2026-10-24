// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_scene/asset.h>
#include <impl/assets_scene/binary_payload.h>
#include <impl/assets_subsystem/asset.h>
#include <impl/assets_subsystem/binary_payload.h>

#include <core/alloc/general.h>
#include <core/assets/manager.h>
#include <core/assets/registry.h>

#include <global/binary.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_subsystem_asset_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core::Assets;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AssetBytes MakeProjectBinary(AssetArena& arena, const Name& startScene){
    ProjectBinaryPayload::ProjectHeaderBinary header;
    header.startSceneHash = startScene.hash();

    AssetBytes binary(arena);
    binary.reserve(sizeof(header));
    AppendPOD(binary, header);
    return binary;
}

[[nodiscard]] static AssetBytes MakeSceneBinary(
    AssetArena& arena,
    const SceneAsset::SubsystemRefVector& subsystems,
    const SceneAsset::NameVector& enabledFeatures,
    const SceneAsset::AssetRefVector& assets
){
    SceneBinaryPayload::SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.enabledFeatureCount = static_cast<u64>(enabledFeatures.size());
    header.assetCount = static_cast<u64>(assets.size());

    AssetBytes binary(arena);
    binary.reserve(
        sizeof(header)
        + (subsystems.size() + enabledFeatures.size() + assets.size()) * sizeof(NameHash)
    );
    AppendPOD(binary, header);
    for(const SceneAsset::SubsystemRef& ref : subsystems)
        AppendPOD(binary, ref.name().hash());
    for(const Name& feature : enabledFeatures)
        AppendPOD(binary, feature.hash());
    for(const SceneAsset::AssetRef& ref : assets)
        AppendPOD(binary, ref.name().hash());
    return binary;
}

[[nodiscard]] static AssetBytes MakeSubsystemBinary(
    AssetArena& arena,
    const Subsystem::SubsystemRefVector& subsystems,
    const Subsystem::AssetRefVector& assets,
    const Subsystem::FeatureVector& features
){
    u64 featureAssetCount = 0u;
    for(const SubsystemFeature& feature : features)
        featureAssetCount += static_cast<u64>(feature.assets.size());

    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.assetCount = static_cast<u64>(assets.size());
    header.featureCount = static_cast<u64>(features.size());
    header.featureAssetCount = featureAssetCount;

    AssetBytes binary(arena);
    binary.reserve(
        sizeof(header)
        + (subsystems.size() + assets.size() + static_cast<usize>(featureAssetCount)) * sizeof(NameHash)
        + features.size() * sizeof(SubsystemBinaryPayload::SubsystemFeatureBinary)
    );
    AppendPOD(binary, header);
    for(const Subsystem::SubsystemRef& ref : subsystems)
        AppendPOD(binary, ref.name().hash());
    for(const Subsystem::AssetRef& ref : assets)
        AppendPOD(binary, ref.name().hash());

    u64 offset = 0u;
    for(const SubsystemFeature& feature : features){
        SubsystemBinaryPayload::SubsystemFeatureBinary record;
        record.featureNameHash = feature.name.hash();
        record.assetOffset = offset;
        record.assetCount = static_cast<u64>(feature.assets.size());
        AppendPOD(binary, record);
        offset += record.assetCount;
    }
    for(const SubsystemFeature& feature : features){
        for(const SubsystemFeature::AssetRef& ref : feature.assets)
            AppendPOD(binary, ref.name().hash());
    }
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MapAssetBinarySource final : public IAssetBinarySource{
public:
    struct Entry{
        Name path = s_NameNone;
        AssetBytes binary;

        Entry(const Name& inPath, AssetBytes&& inBinary)noexcept
            : path(inPath)
            , binary(Move(inBinary))
        {}
    };


public:
    explicit MapAssetBinarySource(AssetArena& arena)noexcept
        : m_arena(arena)
        , m_entries(arena)
    {}


public:
    void setBinary(const Name& virtualPath, const AssetBytes& binary){
        AssetBytes copy(m_arena);
        copy.assign(binary.begin(), binary.end());
        for(Entry& entry : m_entries){
            if(entry.path == virtualPath){
                entry.binary = Move(copy);
                return;
            }
        }
        m_entries.emplace_back(virtualPath, Move(copy));
    }

    virtual bool readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const override{
        outBinary.clear();
        for(const Entry& entry : m_entries){
            if(entry.path == virtualPath){
                outBinary.assign(entry.binary.begin(), entry.binary.end());
                return true;
            }
        }
        return false;
    }


private:
    AssetArena& m_arena;
    AssetVector<Entry> m_entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SceneSubsystemAssets, SubsystemBinaryDecodeAndValidation){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena(Name("tests/scene/subsystem_round_trip"));
    Subsystem::SubsystemRefVector subsystems(arena);
    subsystems.push_back(BuiltInSubsystems::s_Renderer);
    subsystems.push_back(BuiltInSubsystems::s_Ui);

    Subsystem::AssetRefVector assets(arena);
    assets.push_back(Subsystem::AssetRef("engine/shaders/hzb_2d_cs"));

    Subsystem::FeatureVector features(arena);
    SubsystemFeature skinningFeature(arena);
    skinningFeature.name = BuiltInSubsystemFeatures::s_Skinning;
    skinningFeature.assets.push_back(Subsystem::AssetRef("engine/shaders/skinning_cs"));
    features.push_back(Move(skinningFeature));

    const AssetBytes binary = MakeSubsystemBinary(arena, subsystems, assets, features);
    EXPECT_FALSE(binary.empty());

    SubsystemAssetCodec codec;
    auto decodedResult = codec.deserialize(arena, BuiltInSubsystems::s_Default.name(), binary);
    ASSERT_TRUE(decodedResult.has_value());
    const auto* decoded = static_cast<const Subsystem*>(decodedResult->get());
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(decoded->subsystems().size(), 2u);
    EXPECT_EQ(decoded->subsystems()[0], BuiltInSubsystems::s_Renderer);
    EXPECT_EQ(decoded->subsystems()[1], BuiltInSubsystems::s_Ui);
    ASSERT_EQ(decoded->assets().size(), 1u);
    EXPECT_EQ(decoded->assets()[0].name(), Name("engine/shaders/hzb_2d_cs"));
    ASSERT_EQ(decoded->features().size(), 1u);
    const SubsystemFeature* foundSkinning = decoded->findFeature(BuiltInSubsystemFeatures::s_Skinning);
    ASSERT_NE(foundSkinning, nullptr);
    ASSERT_EQ(foundSkinning->assets.size(), 1u);
    EXPECT_EQ(foundSkinning->assets[0].name(), Name("engine/shaders/skinning_cs"));

    AssetBytes trailing = binary;
    trailing.push_back(0u);
    EXPECT_FALSE(codec.deserialize(arena, BuiltInSubsystems::s_Default.name(), trailing).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("trailing bytes detected")));

    AssetBytes badMagic = binary;
    badMagic[0] ^= 0xFFu;
    EXPECT_FALSE(codec.deserialize(arena, BuiltInSubsystems::s_Default.name(), badMagic).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid subsystem asset format")));
}

TEST(SceneSubsystemAssets, ProjectAndSceneBinaryDecodeAndValidation){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena(Name("tests/scene/project_scene_round_trip"));
    const AssetBytes projectBinary = MakeProjectBinary(arena, Name("project/scenes/main"));

    ProjectAssetCodec projectCodec;
    auto decodedProjectResult = projectCodec.deserialize(arena, ProjectAssetDefaults::s_DefaultProjectVirtualPath, projectBinary);
    ASSERT_TRUE(decodedProjectResult.has_value());
    const auto* decodedProject = static_cast<const ProjectAsset*>(decodedProjectResult->get());
    ASSERT_NE(decodedProject, nullptr);
    EXPECT_EQ(decodedProject->startScene().name(), Name("project/scenes/main"));

    const AssetBytes invalidProjectBinary = MakeProjectBinary(arena, s_NameNone);
    EXPECT_FALSE(projectCodec.deserialize(arena, ProjectAssetDefaults::s_DefaultProjectVirtualPath, invalidProjectBinary).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("start_scene must not be empty")));

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Default);
    SceneAsset::NameVector sceneFeatures(arena);
    sceneFeatures.push_back(BuiltInSubsystemFeatures::s_Skinning);
    sceneFeatures.push_back(Name("custom_fx"));
    SceneAsset::AssetRefVector sceneAssets(arena);
    sceneAssets.push_back(SceneAsset::AssetRef("project/characters/cesium_man/model"));

    const AssetBytes sceneBinary = MakeSceneBinary(arena, sceneSubsystems, sceneFeatures, sceneAssets);

    SceneAssetCodec sceneCodec;
    auto decodedSceneResult = sceneCodec.deserialize(arena, Name("project/scenes/main"), sceneBinary);
    ASSERT_TRUE(decodedSceneResult.has_value());
    const auto* decodedScene = static_cast<const SceneAsset*>(decodedSceneResult->get());
    ASSERT_NE(decodedScene, nullptr);
    EXPECT_TRUE(decodedScene->hasSubsystem(BuiltInSubsystems::s_Default));
    EXPECT_TRUE(decodedScene->hasEnabledFeature(BuiltInSubsystemFeatures::s_Skinning));
    EXPECT_TRUE(decodedScene->hasEnabledFeature(Name("custom_fx")));
    ASSERT_EQ(decodedScene->assets().size(), 1u);
    EXPECT_EQ(decodedScene->assets()[0].name(), Name("project/characters/cesium_man/model"));

    SceneAsset::SubsystemRefVector emptySubsystems(arena);
    const AssetBytes emptySceneBinary = MakeSceneBinary(arena, emptySubsystems, sceneFeatures, sceneAssets);
    EXPECT_FALSE(sceneCodec.deserialize(arena, Name("project/scenes/empty"), emptySceneBinary).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("scene must declare at least one subsystem")));
}

TEST(SceneSubsystemAssets, ActiveSceneSubsystemsResolvesTransitiveSubsystemsAndFeatures){
    AssetArena arena(Name("tests/scene/active_scene_subsystems"));
    Core::Alloc::GlobalArena globalArena(Name("tests/scene/active_scene_subsystems_global"));
    AssetRegistry registry(arena);
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SubsystemAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ProjectAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SceneAssetCodec>()));

    MapAssetBinarySource source(arena);
    AssetManager assets(arena, registry, source);

    Subsystem::SubsystemRefVector emptySubsystems(arena);
    Subsystem::FeatureVector emptyFeatures(arena);

    Subsystem::AssetRefVector rendererAssets(arena);
    rendererAssets.push_back(Subsystem::AssetRef("engine/shaders/depth_prepass_vs"));
    Subsystem::FeatureVector rendererFeatures(arena);
    SubsystemFeature skinningFeature(arena);
    skinningFeature.name = BuiltInSubsystemFeatures::s_Skinning;
    skinningFeature.assets.push_back(Subsystem::AssetRef("engine/shaders/skinning_cs"));
    rendererFeatures.push_back(Move(skinningFeature));

    Subsystem::AssetRefVector uiAssets(arena);
    uiAssets.push_back(Subsystem::AssetRef("engine/ui/skins/default"));

    Subsystem::SubsystemRefVector defaultSubsystems(arena);
    defaultSubsystems.push_back(BuiltInSubsystems::s_Renderer);
    defaultSubsystems.push_back(BuiltInSubsystems::s_Ui);
    Subsystem::AssetRefVector emptyAssets(arena);

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Default);
    SceneAsset::NameVector sceneFeatures(arena);
    sceneFeatures.push_back(BuiltInSubsystemFeatures::s_Skinning);
    sceneFeatures.push_back(BuiltInSubsystemFeatures::s_Transparent);
    SceneAsset::AssetRefVector sceneAssets(arena);
    sceneAssets.push_back(SceneAsset::AssetRef("project/characters/cesium_man/model"));

    source.setBinary(BuiltInSubsystems::s_Renderer.name(), MakeSubsystemBinary(arena, emptySubsystems, rendererAssets, rendererFeatures));
    source.setBinary(BuiltInSubsystems::s_Ui.name(), MakeSubsystemBinary(arena, emptySubsystems, uiAssets, emptyFeatures));
    source.setBinary(BuiltInSubsystems::s_Default.name(), MakeSubsystemBinary(arena, defaultSubsystems, emptyAssets, emptyFeatures));
    source.setBinary(ProjectAssetDefaults::s_DefaultProjectVirtualPath, MakeProjectBinary(arena, Name("project/scenes/main")));
    source.setBinary(Name("project/scenes/main"), MakeSceneBinary(arena, sceneSubsystems, sceneFeatures, sceneAssets));

    ActiveSceneSubsystems active(globalArena);
    ASSERT_TRUE(active.loadProjectStartScene(assets));
    EXPECT_TRUE(active.valid());
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Default));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Renderer));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Ui));
    EXPECT_FALSE(active.hasSubsystem(Core::Assets::AssetRef<Subsystem>("project/subsystems/custom")));

    EXPECT_TRUE(active.isFeatureEnabled(BuiltInSubsystemFeatures::s_Skinning));
    EXPECT_TRUE(active.isFeatureEnabled(BuiltInSubsystemFeatures::s_Transparent));
    EXPECT_FALSE(active.isFeatureEnabled(BuiltInSubsystemFeatures::s_Refractive));
    EXPECT_FALSE(active.isFeatureEnabled(BuiltInSubsystemFeatures::s_Csg));

    active.enableFeature(BuiltInSubsystemFeatures::s_Csg);
    EXPECT_TRUE(active.isFeatureEnabled(BuiltInSubsystemFeatures::s_Csg));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

