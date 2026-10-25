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
    const Span<const SceneAsset::SubsystemRef> subsystems,
    const SceneAsset::AssetMap& assets
){
    SceneBinaryPayload::SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.assetCount = static_cast<u64>(assets.size());

    AssetBytes binary(arena);
    binary.reserve(
        sizeof(header)
        + subsystems.size() * sizeof(NameHash)
        + assets.size() * sizeof(SceneBinaryPayload::SceneAssetEntryBinary)
    );
    AppendPOD(binary, header);
    for(const SceneAsset::SubsystemRef& ref : subsystems)
        AppendPOD(binary, ref.name().hash());
    for(const auto& [nickname, ref] : assets){
        SceneBinaryPayload::SceneAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = ref.name().hash();
        AppendPOD(binary, entry);
    }
    return binary;
}

[[nodiscard]] static AssetBytes MakeSubsystemBinary(
    AssetArena& arena,
    const Span<const Subsystem::SubsystemRef> subsystems,
    const Subsystem::AssetMap& assets
){
    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.assetCount = static_cast<u64>(assets.size());

    AssetBytes binary(arena);
    binary.reserve(
        sizeof(header)
        + subsystems.size() * sizeof(NameHash)
        + assets.size() * sizeof(SubsystemBinaryPayload::SubsystemAssetEntryBinary)
    );
    AppendPOD(binary, header);
    for(const Subsystem::SubsystemRef& ref : subsystems)
        AppendPOD(binary, ref.name().hash());
    for(const auto& [nickname, ref] : assets){
        SubsystemBinaryPayload::SubsystemAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = ref.name().hash();
        AppendPOD(binary, entry);
    }
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MapAssetBinarySource final : public IAssetBinarySource{
public:
    struct Entry{
        Name path = s_NameNone;
        AssetBytes binary;


    public:
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
    void setBinary(const Name& virtualPath, const Span<const u8> binary){
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

    Subsystem::AssetMap assets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    assets.emplace(Name("hzb_2d_cs"), Subsystem::AssetRef("engine/shaders/hzb_2d_cs"));
    assets.emplace(Name("skinning_cs"), Subsystem::AssetRef("engine/shaders/skinning_cs"));

    const AssetBytes binary = MakeSubsystemBinary(arena, subsystems, assets);
    EXPECT_FALSE(binary.empty());

    SubsystemAssetCodec codec;
    auto decodedResult = codec.deserialize(arena, BuiltInSubsystems::s_Default.name(), binary);
    ASSERT_TRUE(decodedResult.has_value());
    const auto* decoded = static_cast<const Subsystem*>(decodedResult->get());
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(decoded->subsystems().size(), 2u);
    EXPECT_EQ(decoded->subsystems()[0], BuiltInSubsystems::s_Renderer);
    EXPECT_EQ(decoded->subsystems()[1], BuiltInSubsystems::s_Ui);
    ASSERT_EQ(decoded->assets().size(), 2u);
    EXPECT_EQ(decoded->findAsset(Name("hzb_2d_cs")).name(), Name("engine/shaders/hzb_2d_cs"));
    EXPECT_EQ(decoded->findAsset(Name("skinning_cs")).name(), Name("engine/shaders/skinning_cs"));
    EXPECT_FALSE(decoded->findAsset(Name("missing_asset")).valid());

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
    sceneSubsystems.push_back(BuiltInSubsystems::s_Skinning);
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/custom_fx"));
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    sceneAssets.emplace(Name("cesium_man_model"), SceneAsset::AssetRef("project/characters/cesium_man/model"));

    const AssetBytes sceneBinary = MakeSceneBinary(arena, sceneSubsystems, sceneAssets);

    SceneAssetCodec sceneCodec;
    auto decodedSceneResult = sceneCodec.deserialize(arena, Name("project/scenes/main"), sceneBinary);
    ASSERT_TRUE(decodedSceneResult.has_value());
    const auto* decodedScene = static_cast<const SceneAsset*>(decodedSceneResult->get());
    ASSERT_NE(decodedScene, nullptr);
    EXPECT_TRUE(decodedScene->hasSubsystem(BuiltInSubsystems::s_Default));
    EXPECT_TRUE(decodedScene->hasSubsystem(BuiltInSubsystems::s_Skinning));
    EXPECT_TRUE(decodedScene->hasSubsystem(SceneAsset::SubsystemRef("project/subsystems/custom_fx")));
    ASSERT_EQ(decodedScene->assets().size(), 1u);
    EXPECT_EQ(decodedScene->findAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
    EXPECT_FALSE(decodedScene->findAsset(Name("missing_model")).valid());

    SceneAsset::SubsystemRefVector emptySubsystems(arena);
    const AssetBytes emptySceneBinary = MakeSceneBinary(arena, emptySubsystems, sceneAssets);
    EXPECT_FALSE(sceneCodec.deserialize(arena, Name("project/scenes/empty"), emptySceneBinary).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("scene must declare at least one subsystem")));
}

TEST(SceneSubsystemAssets, ActiveSceneSubsystemsResolvesTransitiveSubsystemsAndNicknames){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena(Name("tests/scene/active_scene_subsystems"));
    Core::Alloc::GlobalArena globalArena(Name("tests/scene/active_scene_subsystems_global"));
    AssetRegistry registry(arena);
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SubsystemAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ProjectAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SceneAssetCodec>()));

    MapAssetBinarySource source(arena);
    AssetManager assets(arena, registry, source);

    Subsystem::SubsystemRefVector emptySubsystems(arena);
    Subsystem::AssetMap emptyAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);

    Subsystem::AssetMap rendererAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    rendererAssets.emplace(Name("depth_prepass_vs"), Subsystem::AssetRef("engine/shaders/depth_prepass_vs"));

    Subsystem::AssetMap skinningAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    skinningAssets.emplace(Name("skinning_cs"), Subsystem::AssetRef("engine/graphics/skinned_mesh/skinning_cs"));

    Subsystem::AssetMap transparentAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    transparentAssets.emplace(Name("avboit_depth_warp_cs"), Subsystem::AssetRef("engine/graphics/avboit/depth_warp_cs"));

    Subsystem::AssetMap uiAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    uiAssets.emplace(Name("default_skin"), Subsystem::AssetRef("engine/ui/skins/default/atlas"));

    Subsystem::SubsystemRefVector defaultSubsystems(arena);
    defaultSubsystems.push_back(BuiltInSubsystems::s_Renderer);
    defaultSubsystems.push_back(BuiltInSubsystems::s_Ui);

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Default);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Skinning);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Transparent);
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    sceneAssets.emplace(Name("cesium_man_model"), SceneAsset::AssetRef("project/characters/cesium_man/model"));

    source.setBinary(BuiltInSubsystems::s_Renderer.name(), MakeSubsystemBinary(arena, emptySubsystems, rendererAssets));
    source.setBinary(BuiltInSubsystems::s_Skinning.name(), MakeSubsystemBinary(arena, emptySubsystems, skinningAssets));
    source.setBinary(BuiltInSubsystems::s_Transparent.name(), MakeSubsystemBinary(arena, emptySubsystems, transparentAssets));
    source.setBinary(BuiltInSubsystems::s_Ui.name(), MakeSubsystemBinary(arena, emptySubsystems, uiAssets));
    source.setBinary(BuiltInSubsystems::s_Default.name(), MakeSubsystemBinary(arena, defaultSubsystems, emptyAssets));
    source.setBinary(ProjectAssetDefaults::s_DefaultProjectVirtualPath, MakeProjectBinary(arena, Name("project/scenes/main")));
    source.setBinary(Name("project/scenes/main"), MakeSceneBinary(arena, sceneSubsystems, sceneAssets));

    ActiveSceneSubsystems active(globalArena);
    ASSERT_TRUE(active.loadProjectStartScene(assets));
    EXPECT_TRUE(active.valid());
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Default));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Renderer));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Ui));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Skinning));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Transparent));
    EXPECT_FALSE(active.hasSubsystem(BuiltInSubsystems::s_Refractive));
    EXPECT_FALSE(active.hasSubsystem(BuiltInSubsystems::s_Csg));
    EXPECT_FALSE(active.hasSubsystem(Core::Assets::AssetRef<Subsystem>("project/subsystems/custom")));

    EXPECT_EQ(active.findSceneAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
    EXPECT_EQ(active.findSubsystemAsset(BuiltInSubsystems::s_Renderer, Name("depth_prepass_vs")).name(), Name("engine/shaders/depth_prepass_vs"));
    EXPECT_EQ(active.findSubsystemAsset(BuiltInSubsystems::s_Default, Name("default_skin")).name(), Name("engine/ui/skins/default/atlas"));
    EXPECT_EQ(active.findAsset(Name("skinning_cs")).name(), Name("engine/graphics/skinned_mesh/skinning_cs"));
    EXPECT_EQ(active.findAsset(Name("avboit_depth_warp_cs")).name(), Name("engine/graphics/avboit/depth_warp_cs"));
    EXPECT_EQ(active.findAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
    EXPECT_FALSE(active.findAsset(Name("nonexistent_nickname")).valid());

    SceneAsset::SubsystemRefVector brokenSceneSubsystems(arena);
    brokenSceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/missing_subsystem"));
    source.setBinary(Name("project/scenes/broken"), MakeSceneBinary(arena, brokenSceneSubsystems, sceneAssets));

    EXPECT_FALSE(active.loadScene(assets, ActiveSceneSubsystems::SceneRef("project/scenes/broken")));
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Default));
    EXPECT_EQ(active.findSceneAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

