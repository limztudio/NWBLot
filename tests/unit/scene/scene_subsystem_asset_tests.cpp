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

template<typename SubsystemT>
static void PopulateRequiredSubsystemFields(SubsystemT& subsystem){
    const Span<const SubsystemFieldDescriptor<SubsystemT>> descriptors = SubsystemT::FieldDescriptors();
    for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors){
        if(!descriptor.get(subsystem))
            descriptor.set(subsystem, Name("engine/test/default_asset"));
    }
}

template<typename SubsystemT>
[[nodiscard]] static AssetBytes MakeSubsystemBinary(AssetArena& arena, const SubsystemT& subsystem){
    const Span<const SubsystemFieldDescriptor<SubsystemT>> descriptors = SubsystemT::FieldDescriptors();
    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemTypeHash = SubsystemT::AssetTypeName().hash();
    header.subsystemCount = static_cast<u64>(subsystem.subsystems().size());
    header.assetCount = static_cast<u64>(descriptors.size());

    AssetBytes binary(arena);
    binary.reserve(
        sizeof(header)
        + subsystem.subsystems().size() * sizeof(NameHash)
        + descriptors.size() * sizeof(NameHash)
    );
    AppendPOD(binary, header);
    for(const ISubsystem::SubsystemRef& ref : subsystem.subsystems())
        AppendPOD(binary, ref.name().hash());
    for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors)
        AppendPOD(binary, descriptor.get(subsystem).hash());
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
    DefaultSubsystem defaultSubsystem(arena, BuiltInSubsystems::s_Default.name());
    ISubsystem::SubsystemRefVector subsystems(arena);
    subsystems.push_back(BuiltInSubsystems::s_Renderer);
    subsystems.push_back(BuiltInSubsystems::s_Ui);
    defaultSubsystem.subsystems() = Move(subsystems);

    const AssetBytes defaultBinary = MakeSubsystemBinary(arena, defaultSubsystem);
    EXPECT_FALSE(defaultBinary.empty());

    SubsystemAssetCodec codec;
    auto decodedDefaultResult = codec.deserialize(arena, BuiltInSubsystems::s_Default.name(), defaultBinary);
    ASSERT_TRUE(decodedDefaultResult.has_value());
    const DefaultSubsystem* const decodedDefault = CastAsset<DefaultSubsystem>(decodedDefaultResult->get());
    ASSERT_NE(decodedDefault, nullptr);
    ASSERT_EQ(decodedDefault->subsystems().size(), 2u);
    EXPECT_EQ(decodedDefault->subsystems()[0], BuiltInSubsystems::s_Renderer);
    EXPECT_EQ(decodedDefault->subsystems()[1], BuiltInSubsystems::s_Ui);

    SkinningSubsystem skinningSubsystem(arena, BuiltInSubsystems::s_Skinning.name());
    skinningSubsystem.skinningCs = AssetRef<ComputeShader>("engine/graphics/skinned_mesh/skinning_cs");
    skinningSubsystem.meshletBoundsCs = AssetRef<ComputeShader>("engine/graphics/skinned_mesh/meshlet_bounds_cs");
    PopulateRequiredSubsystemFields(skinningSubsystem);

    const AssetBytes skinningBinary = MakeSubsystemBinary(arena, skinningSubsystem);
    TypedSubsystemAssetCodec<SkinningSubsystem> skinningCodec;
    auto decodedSkinningResult = skinningCodec.deserialize(arena, BuiltInSubsystems::s_Skinning.name(), skinningBinary);
    ASSERT_TRUE(decodedSkinningResult.has_value());
    const SkinningSubsystem* const decodedSkinning = CastAsset<SkinningSubsystem>(decodedSkinningResult->get());
    ASSERT_NE(decodedSkinning, nullptr);
    EXPECT_EQ(decodedSkinning->skinningCs.name(), Name("engine/graphics/skinned_mesh/skinning_cs"));
    EXPECT_EQ(decodedSkinning->meshletBoundsCs.name(), Name("engine/graphics/skinned_mesh/meshlet_bounds_cs"));
    EXPECT_EQ(decodedSkinning->localBoundsCs.name(), Name("engine/test/default_asset"));

    TypedSubsystemAssetCodec<RendererSubsystem> rendererCodec;
    EXPECT_FALSE(rendererCodec.deserialize(arena, BuiltInSubsystems::s_Skinning.name(), skinningBinary).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("subsystem binary type does not match")));

    AssetBytes trailing = skinningBinary;
    trailing.push_back(0u);
    EXPECT_FALSE(codec.deserialize(arena, BuiltInSubsystems::s_Skinning.name(), trailing).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("trailing bytes detected")));

    AssetBytes badMagic = skinningBinary;
    badMagic[0] ^= 0xFFu;
    EXPECT_FALSE(codec.deserialize(arena, BuiltInSubsystems::s_Skinning.name(), badMagic).has_value());
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

TEST(SceneSubsystemAssets, ActiveSceneSubsystemsResolvesTransitiveSubsystemsAndTypedFields){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);

    AssetArena arena(Name("tests/scene/active_scene_subsystems"));
    Core::Alloc::GlobalArena globalArena(Name("tests/scene/active_scene_subsystems_global"));
    AssetRegistry registry(arena);
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SubsystemAssetCodec>()));
#define NWB_REGISTER_TEST_SUBSYSTEM_CODEC(Type) ASSERT_TRUE(registry.registerCodec(MakeUnique<Type##AssetCodec>()));
    NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_REGISTER_TEST_SUBSYSTEM_CODEC)
#undef NWB_REGISTER_TEST_SUBSYSTEM_CODEC
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ProjectAssetCodec>()));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<SceneAssetCodec>()));

    MapAssetBinarySource source(arena);
    AssetManager assets(arena, registry, source);

    RendererSubsystem rendererSubsystem(arena, BuiltInSubsystems::s_Renderer.name());
    rendererSubsystem.linearClamp = AssetRef<Sampler>("engine/samplers/linear_clamp");
    PopulateRequiredSubsystemFields(rendererSubsystem);

    SkinningSubsystem skinningSubsystem(arena, BuiltInSubsystems::s_Skinning.name());
    skinningSubsystem.skinningCs = AssetRef<ComputeShader>("engine/graphics/skinned_mesh/skinning_cs");
    PopulateRequiredSubsystemFields(skinningSubsystem);

    TransparentSubsystem transparentSubsystem(arena, BuiltInSubsystems::s_Transparent.name());
    transparentSubsystem.avboitDepthWarpCs = AssetRef<ComputeShader>("engine/graphics/avboit/depth_warp_cs");
    PopulateRequiredSubsystemFields(transparentSubsystem);

    UiSubsystem uiSubsystem(arena, BuiltInSubsystems::s_Ui.name());
    uiSubsystem.defaultSkin = AssetRef<UiSkin>("engine/ui/skins/default/atlas");
    PopulateRequiredSubsystemFields(uiSubsystem);

    DefaultSubsystem defaultSubsystem(arena, BuiltInSubsystems::s_Default.name());
    ISubsystem::SubsystemRefVector defaultSubsystems(arena);
    defaultSubsystems.push_back(BuiltInSubsystems::s_Renderer);
    defaultSubsystems.push_back(BuiltInSubsystems::s_Ui);
    defaultSubsystem.subsystems() = Move(defaultSubsystems);

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Default);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Skinning);
    sceneSubsystems.push_back(BuiltInSubsystems::s_Transparent);
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    sceneAssets.emplace(Name("cesium_man_model"), SceneAsset::AssetRef("project/characters/cesium_man/model"));

    source.setBinary(BuiltInSubsystems::s_Renderer.name(), MakeSubsystemBinary(arena, rendererSubsystem));
    source.setBinary(BuiltInSubsystems::s_Skinning.name(), MakeSubsystemBinary(arena, skinningSubsystem));
    source.setBinary(BuiltInSubsystems::s_Transparent.name(), MakeSubsystemBinary(arena, transparentSubsystem));
    source.setBinary(BuiltInSubsystems::s_Ui.name(), MakeSubsystemBinary(arena, uiSubsystem));
    source.setBinary(BuiltInSubsystems::s_Default.name(), MakeSubsystemBinary(arena, defaultSubsystem));
    source.setBinary(ProjectAssetDefaults::s_DefaultProjectVirtualPath, MakeProjectBinary(arena, Name("project/scenes/main")));
    source.setBinary(Name("project/scenes/main"), MakeSceneBinary(arena, sceneSubsystems, sceneAssets));

    ActiveSceneSubsystems active(globalArena);
    ASSERT_TRUE(active.loadProjectStartScene(assets));
    EXPECT_TRUE(active.valid());
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem<DefaultSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<RendererSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<UiSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<SkinningSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<TransparentSubsystem>());
    EXPECT_FALSE(active.hasSubsystem<RefractiveSubsystem>());
    EXPECT_FALSE(active.hasSubsystem<CsgSubsystem>());
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Default));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Renderer));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Ui));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Skinning));
    EXPECT_TRUE(active.hasSubsystem(BuiltInSubsystems::s_Transparent));
    EXPECT_FALSE(active.hasSubsystem(BuiltInSubsystems::s_Refractive));
    EXPECT_FALSE(active.hasSubsystem(BuiltInSubsystems::s_Csg));
    EXPECT_FALSE(active.hasSubsystem(Core::Assets::AssetRef<ISubsystem>("project/subsystems/custom")));

    EXPECT_EQ(active.findSceneAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
    EXPECT_FALSE(active.findSceneAsset(Name("nonexistent_nickname")).valid());

    const RendererSubsystem* const activeRenderer = active.findSubsystem<RendererSubsystem>();
    ASSERT_NE(activeRenderer, nullptr);
    EXPECT_EQ(activeRenderer->linearClamp.name(), Name("engine/samplers/linear_clamp"));

    const UiSubsystem* const activeUi = active.findSubsystem<UiSubsystem>();
    ASSERT_NE(activeUi, nullptr);
    EXPECT_EQ(activeUi->defaultSkin.name(), Name("engine/ui/skins/default/atlas"));

    const SkinningSubsystem* const activeSkinning = active.findSubsystem<SkinningSubsystem>();
    ASSERT_NE(activeSkinning, nullptr);
    EXPECT_EQ(activeSkinning->skinningCs.name(), Name("engine/graphics/skinned_mesh/skinning_cs"));

    const TransparentSubsystem* const activeTransparent = active.findSubsystem<TransparentSubsystem>();
    ASSERT_NE(activeTransparent, nullptr);
    EXPECT_EQ(activeTransparent->avboitDepthWarpCs.name(), Name("engine/graphics/avboit/depth_warp_cs"));

    SceneAsset::SubsystemRefVector brokenSceneSubsystems(arena);
    brokenSceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/missing_subsystem"));
    source.setBinary(Name("project/scenes/broken"), MakeSceneBinary(arena, brokenSceneSubsystems, sceneAssets));

    EXPECT_FALSE(active.loadScene(assets, ActiveSceneSubsystems::SceneRef("project/scenes/broken")));
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem<DefaultSubsystem>());
    EXPECT_EQ(active.findSceneAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

