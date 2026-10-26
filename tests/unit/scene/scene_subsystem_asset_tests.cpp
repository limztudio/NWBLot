// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/csg/subsystem.h>
#include <impl/assets_graphics/refractive/subsystem.h>
#include <impl/assets_graphics/renderer/subsystem.h>
#include <impl/assets_graphics/transparent/subsystem.h>
#include <impl/assets_mesh/subsystem.h>
#include <impl/assets_scene/asset.h>
#include <impl/assets_scene/binary_payload.h>
#include <impl/assets_subsystem/asset.h>
#include <impl/assets_subsystem/binary_payload.h>
#include <impl/assets_ui_skin/subsystem.h>

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

inline constexpr Name s_RendererSubsystemPath("engine/subsystems/renderer");
inline constexpr Name s_SkinningSubsystemPath("engine/subsystems/skinning");
inline constexpr Name s_TransparentSubsystemPath("engine/subsystems/transparent");
inline constexpr Name s_UiSubsystemPath("engine/subsystems/ui");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AssetBytes MakeProjectBinary(AssetArena& arena, const Name& startScene){
    ProjectBinaryPayload::ProjectHeaderBinary header;
    header.startSceneHash = startScene.hash();

    AssetBytes binary(arena);
    binary.reserve(sizeof(header));
    AppendPOD(binary, header);
    return binary;
}

[[nodiscard]] static AssetBytes MakeSceneBinary(AssetArena& arena, const Span<const SceneAsset::SubsystemRef> subsystems, const SceneAsset::AssetMap& assets){
    SceneBinaryPayload::SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.assetCount = static_cast<u64>(assets.size());

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + subsystems.size() * sizeof(NameHash) + assets.size() * sizeof(SceneBinaryPayload::SceneAssetEntryBinary));
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
    const usize count = subsystem.fieldCount();
    for(usize i = 0u; i < count; ++i){
        const SubsystemFieldValueDescriptor descriptor = subsystem.fieldDescriptor(i);
        if(descriptor.isArray){
            if(subsystem.fieldValueCount(i) == 0u)
                subsystem.appendFieldValue(i, Name("engine/test/default_asset"));
        }
        else if(!subsystem.fieldValue(i))
            subsystem.appendFieldValue(i, Name("engine/test/default_asset"));
    }
}

template<typename SubsystemT>
[[nodiscard]] static AssetBytes MakeSubsystemBinary(AssetArena& arena, const SubsystemT& subsystem){
    const usize fieldCount = subsystem.fieldCount();
    u64 totalAssetCount = 0u;
    for(usize i = 0u; i < fieldCount; ++i)
        totalAssetCount += static_cast<u64>(subsystem.fieldValueCount(i));

    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemTypeHash = SubsystemT::AssetTypeName().hash();
    header.fieldCount = static_cast<u64>(fieldCount);
    header.assetCount = totalAssetCount;

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + fieldCount * sizeof(u64) + static_cast<usize>(totalAssetCount) * sizeof(NameHash));
    AppendPOD(binary, header);
    for(usize i = 0u; i < fieldCount; ++i){
        const u64 valueCount = static_cast<u64>(subsystem.fieldValueCount(i));
        AppendPOD(binary, valueCount);
    }
    for(usize i = 0u; i < fieldCount; ++i){
        const usize valueCount = subsystem.fieldValueCount(i);
        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex)
            AppendPOD(binary, subsystem.fieldValue(i, elementIndex).hash());
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
    RendererSubsystem rendererSubsystem(arena, s_RendererSubsystemPath);
    rendererSubsystem.basicSamplers.push_back(AssetRef<Sampler>("engine/samplers/linear_clamp"));
    rendererSubsystem.basicSamplers.push_back(AssetRef<Sampler>("engine/samplers/linear_wrap"));
    PopulateRequiredSubsystemFields(rendererSubsystem);

    const AssetBytes rendererBinary = MakeSubsystemBinary(arena, rendererSubsystem);
    EXPECT_FALSE(rendererBinary.empty());

    SubsystemAssetCodec codec;
    auto decodedRendererResult = codec.deserialize(arena, s_RendererSubsystemPath, rendererBinary);
    ASSERT_TRUE(decodedRendererResult.has_value());
    const RendererSubsystem* const decodedRenderer = CastAsset<RendererSubsystem>(decodedRendererResult->get());
    ASSERT_NE(decodedRenderer, nullptr);
    ASSERT_EQ(decodedRenderer->basicSamplers.size(), 2u);
    EXPECT_EQ(decodedRenderer->basicSamplers[0].name(), Name("engine/samplers/linear_clamp"));
    EXPECT_EQ(decodedRenderer->basicSamplers[1].name(), Name("engine/samplers/linear_wrap"));

    SkinningSubsystem skinningSubsystem(arena, s_SkinningSubsystemPath);
    skinningSubsystem.skinningCs = AssetRef<ComputeShader>("engine/graphics/skinned_mesh/skinning_cs");
    skinningSubsystem.meshletBoundsCs = AssetRef<ComputeShader>("engine/graphics/skinned_mesh/meshlet_bounds_cs");
    PopulateRequiredSubsystemFields(skinningSubsystem);

    const AssetBytes skinningBinary = MakeSubsystemBinary(arena, skinningSubsystem);
    TypedSubsystemAssetCodec<SkinningSubsystem> skinningCodec;
    auto decodedSkinningResult = skinningCodec.deserialize(arena, s_SkinningSubsystemPath, skinningBinary);
    ASSERT_TRUE(decodedSkinningResult.has_value());
    const SkinningSubsystem* const decodedSkinning = CastAsset<SkinningSubsystem>(decodedSkinningResult->get());
    ASSERT_NE(decodedSkinning, nullptr);
    EXPECT_EQ(decodedSkinning->skinningCs.name(), Name("engine/graphics/skinned_mesh/skinning_cs"));
    EXPECT_EQ(decodedSkinning->meshletBoundsCs.name(), Name("engine/graphics/skinned_mesh/meshlet_bounds_cs"));
    EXPECT_EQ(decodedSkinning->localBoundsCs.name(), Name("engine/test/default_asset"));

    CsgSubsystem csgSubsystem(arena, Name("engine/subsystems/csg"));
    csgSubsystem.basicShapes.push_back(AssetRef<CsgShape>("engine/csg/box/shape"));
    csgSubsystem.basicShapes.push_back(AssetRef<CsgShape>("engine/csg/sphere/shape"));
    PopulateRequiredSubsystemFields(csgSubsystem);

    const AssetBytes csgBinary = MakeSubsystemBinary(arena, csgSubsystem);
    auto decodedCsgResult = codec.deserialize(arena, Name("engine/subsystems/csg"), csgBinary);
    ASSERT_TRUE(decodedCsgResult.has_value());
    const CsgSubsystem* const decodedCsg = CastAsset<CsgSubsystem>(decodedCsgResult->get());
    ASSERT_NE(decodedCsg, nullptr);
    ASSERT_EQ(decodedCsg->basicShapes.size(), 2u);
    EXPECT_EQ(decodedCsg->basicShapes[0].name(), Name("engine/csg/box/shape"));
    EXPECT_EQ(decodedCsg->basicShapes[1].name(), Name("engine/csg/sphere/shape"));

    TypedSubsystemAssetCodec<RendererSubsystem> rendererCodec;
    EXPECT_FALSE(rendererCodec.deserialize(arena, s_SkinningSubsystemPath, skinningBinary).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("subsystem binary type does not match")));

    AssetBytes trailing = skinningBinary;
    trailing.push_back(0u);
    EXPECT_FALSE(codec.deserialize(arena, s_SkinningSubsystemPath, trailing).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("trailing bytes detected")));

    AssetBytes badMagic = skinningBinary;
    badMagic[0] ^= 0xFFu;
    EXPECT_FALSE(codec.deserialize(arena, s_SkinningSubsystemPath, badMagic).has_value());
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
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/renderer"));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/skinning"));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/custom_fx"));
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    sceneAssets.emplace(Name("cesium_man_model"), SceneAsset::AssetRef("project/characters/cesium_man/model"));

    const AssetBytes sceneBinary = MakeSceneBinary(arena, sceneSubsystems, sceneAssets);

    SceneAssetCodec sceneCodec;
    auto decodedSceneResult = sceneCodec.deserialize(arena, Name("project/scenes/main"), sceneBinary);
    ASSERT_TRUE(decodedSceneResult.has_value());
    const auto* decodedScene = static_cast<const SceneAsset*>(decodedSceneResult->get());
    ASSERT_NE(decodedScene, nullptr);
    ASSERT_EQ(decodedScene->subsystems().size(), 3u);
    EXPECT_EQ(decodedScene->subsystems()[0], SceneAsset::SubsystemRef("engine/subsystems/renderer"));
    EXPECT_EQ(decodedScene->subsystems()[1], SceneAsset::SubsystemRef("engine/subsystems/skinning"));
    EXPECT_EQ(decodedScene->subsystems()[2], SceneAsset::SubsystemRef("project/subsystems/custom_fx"));
    ASSERT_EQ(decodedScene->assets().size(), 1u);
    const auto foundModel = decodedScene->assets().find(Name("cesium_man_model"));
    ASSERT_NE(foundModel, decodedScene->assets().end());
    EXPECT_EQ(foundModel.value().name(), Name("project/characters/cesium_man/model"));

    SceneAsset::SubsystemRefVector emptySubsystems(arena);
    const AssetBytes emptySceneBinary = MakeSceneBinary(arena, emptySubsystems, sceneAssets);
    EXPECT_FALSE(sceneCodec.deserialize(arena, Name("project/scenes/empty"), emptySceneBinary).has_value());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("scene must declare at least one subsystem")));
}

TEST(SceneSubsystemAssets, ActiveSceneSubsystemsLoadsFocusedSubsystemsAndTypedFields){
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

    RendererSubsystem rendererSubsystem(arena, s_RendererSubsystemPath);
    rendererSubsystem.basicSamplers.push_back(AssetRef<Sampler>("engine/samplers/linear_clamp"));
    PopulateRequiredSubsystemFields(rendererSubsystem);

    SkinningSubsystem skinningSubsystem(arena, s_SkinningSubsystemPath);
    skinningSubsystem.skinningCs = AssetRef<ComputeShader>("engine/graphics/skinned_mesh/skinning_cs");
    PopulateRequiredSubsystemFields(skinningSubsystem);

    TransparentSubsystem transparentSubsystem(arena, s_TransparentSubsystemPath);
    transparentSubsystem.avboitDepthWarpCs = AssetRef<ComputeShader>("engine/graphics/avboit/depth_warp_cs");
    PopulateRequiredSubsystemFields(transparentSubsystem);

    UiSubsystem uiSubsystem(arena, s_UiSubsystemPath);
    uiSubsystem.defaultSkin = AssetRef<UiSkin>("engine/ui/skins/default/atlas");
    PopulateRequiredSubsystemFields(uiSubsystem);

    SceneAsset::SubsystemRefVector sceneSubsystems(arena);
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/renderer"));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/ui"));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/skinning"));
    sceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/transparent"));
    SceneAsset::AssetMap sceneAssets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    sceneAssets.emplace(Name("cesium_man_model"), SceneAsset::AssetRef("project/characters/cesium_man/model"));

    source.setBinary(s_RendererSubsystemPath, MakeSubsystemBinary(arena, rendererSubsystem));
    source.setBinary(s_SkinningSubsystemPath, MakeSubsystemBinary(arena, skinningSubsystem));
    source.setBinary(s_TransparentSubsystemPath, MakeSubsystemBinary(arena, transparentSubsystem));
    source.setBinary(s_UiSubsystemPath, MakeSubsystemBinary(arena, uiSubsystem));
    source.setBinary(ProjectAssetDefaults::s_DefaultProjectVirtualPath, MakeProjectBinary(arena, Name("project/scenes/main")));
    source.setBinary(Name("project/scenes/main"), MakeSceneBinary(arena, sceneSubsystems, sceneAssets));

    ActiveSceneSubsystems active(globalArena);
    ASSERT_TRUE(active.loadProjectStartScene(assets));
    EXPECT_TRUE(active.valid());
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem<RendererSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<UiSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<SkinningSubsystem>());
    EXPECT_TRUE(active.hasSubsystem<TransparentSubsystem>());
    EXPECT_FALSE(active.hasSubsystem<RefractiveSubsystem>());
    EXPECT_FALSE(active.hasSubsystem<CsgSubsystem>());

    EXPECT_EQ(active.findSceneAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
    EXPECT_FALSE(active.findSceneAsset(Name("nonexistent_nickname")).valid());

    const RendererSubsystem* const activeRenderer = active.findSubsystem<RendererSubsystem>();
    ASSERT_NE(activeRenderer, nullptr);
    ASSERT_EQ(activeRenderer->basicSamplers.size(), 1u);
    EXPECT_EQ(activeRenderer->basicSamplers[0].name(), Name("engine/samplers/linear_clamp"));

    const UiSubsystem* const activeUi = active.findSubsystem<UiSubsystem>();
    ASSERT_NE(activeUi, nullptr);
    EXPECT_EQ(activeUi->defaultSkin.name(), Name("engine/ui/skins/default/atlas"));

    const SkinningSubsystem* const activeSkinning = active.findSubsystem<SkinningSubsystem>();
    ASSERT_NE(activeSkinning, nullptr);
    EXPECT_EQ(activeSkinning->skinningCs.name(), Name("engine/graphics/skinned_mesh/skinning_cs"));

    const TransparentSubsystem* const activeTransparent = active.findSubsystem<TransparentSubsystem>();
    ASSERT_NE(activeTransparent, nullptr);
    EXPECT_EQ(activeTransparent->avboitDepthWarpCs.name(), Name("engine/graphics/avboit/depth_warp_cs"));

    RendererSubsystem duplicateRendererSubsystem(arena, Name("project/subsystems/renderer_override"));
    PopulateRequiredSubsystemFields(duplicateRendererSubsystem);
    source.setBinary(Name("project/subsystems/renderer_override"), MakeSubsystemBinary(arena, duplicateRendererSubsystem));
    SceneAsset::SubsystemRefVector duplicateTypeSceneSubsystems(arena);
    duplicateTypeSceneSubsystems.push_back(SceneAsset::SubsystemRef("engine/subsystems/renderer"));
    duplicateTypeSceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/renderer_override"));
    source.setBinary(Name("project/scenes/duplicate_type"), MakeSceneBinary(arena, duplicateTypeSceneSubsystems, sceneAssets));
    EXPECT_FALSE(active.loadScene(assets, ActiveSceneSubsystems::SceneRef("project/scenes/duplicate_type")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate subsystem type")));

    SceneAsset::SubsystemRefVector brokenSceneSubsystems(arena);
    brokenSceneSubsystems.push_back(SceneAsset::SubsystemRef("project/subsystems/missing_subsystem"));
    source.setBinary(Name("project/scenes/broken"), MakeSceneBinary(arena, brokenSceneSubsystems, sceneAssets));

    EXPECT_FALSE(active.loadScene(assets, ActiveSceneSubsystems::SceneRef("project/scenes/broken")));
    EXPECT_EQ(active.scene().name(), Name("project/scenes/main"));
    EXPECT_TRUE(active.hasSubsystem<RendererSubsystem>());
    EXPECT_EQ(active.findSceneAsset(Name("cesium_man_model")).name(), Name("project/characters/cesium_man/model"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

