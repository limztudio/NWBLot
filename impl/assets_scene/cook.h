// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/alloc/scratch.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ComponentT>
struct TypedComponentCookEntry{
    Name virtualPath = s_NameNone;
    ComponentT component;


public:
    explicit TypedComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};

using TransformComponentCookEntry = TypedComponentCookEntry<Scene::TransformComponent>;
using CameraComponentCookEntry = TypedComponentCookEntry<Scene::CameraComponent>;
using LightComponentCookEntry = TypedComponentCookEntry<Scene::LightComponent>;

struct EntityCookEntry{
    Name virtualPath = s_NameNone;
    EntityAsset::ComponentRefVector components;


public:
    explicit EntityCookEntry(Core::Assets::AssetArena& arena)noexcept
        : components(arena)
    {}
};

struct ProjectCookEntry{
    Name virtualPath = s_NameNone;
    ProjectAsset::SceneRef startScene;


public:
    explicit ProjectCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct SceneCookEntry{
    Name virtualPath = s_NameNone;
    SceneAsset::SubsystemRefVector subsystems;
    SceneAsset::EntityRefVector entities;
    SceneAsset::AssetMap assets;


public:
    explicit SceneCookEntry(Core::Assets::AssetArena& arena)noexcept
        : subsystems(arena)
        , entities(arena)
        , assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SceneAssetCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename AssetT, typename EntryT>
[[nodiscard]] inline Expected<AssetT> BuildComponentAssetFromEntry(const EntryT& entry, Core::Assets::AssetArena& arena){
    AssetT asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}

template<
    typename EntryT,
    typename AssetT,
    Core::Assets::ParseMetadataValueFn<EntryT> ParseValFn,
    const AStringView& DiagnosticPrefix,
    typename CodecT = typename AssetT::AssetCodecType
>
[[nodiscard]] inline b8 RegisterComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<EntryT, AssetT, CodecT>(
        s_ComponentAssetKindLabel,
        [](const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
            return Core::Assets::ParseCookMetadataDocument<EntryT, ParseValFn>(assetRoot, virtualRoot, nwbFilePath, doc, DiagnosticPrefix, context.cookArena, context.scratchArena);
        },
        [](const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
            return ParseValFn(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
        },
        [](EntryT& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
            return BuildComponentAssetFromEntry<AssetT>(entry, arena);
        },
        true,
        registry
    );
}

template<typename EntryT, Core::Assets::ParseMetadataValueFn<EntryT> ParseValFn, const AStringView& DiagnosticPrefix>
[[nodiscard]] inline b8 ExtractLeafComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector&){
    return Core::Assets::ParseCookDependencyEntry<EntryT, ParseValFn>(context, DiagnosticPrefix).has_value();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<EntityCookEntry> ParseEntityCookMetadata(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<EntityAsset> BuildEntityAsset(EntityCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<ProjectCookEntry> ParseProjectCookMetadata(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<ProjectAsset> BuildProjectAsset(ProjectCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<SceneCookEntry> ParseSceneCookMetadata(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<SceneAsset> BuildSceneAsset(SceneCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

