// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>
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


inline constexpr AStringView s_TransformDiagnosticPrefix = "Transform component meta";
inline constexpr AStringView s_CameraDiagnosticPrefix = "Camera component meta";
inline constexpr AStringView s_LightDiagnosticPrefix = "Light component meta";
inline constexpr AStringView s_EntityDiagnosticPrefix = "Entity meta";
inline constexpr AStringView s_ProjectDiagnosticPrefix = "Project meta";
inline constexpr AStringView s_SceneDiagnosticPrefix = "Scene meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EntryT>
using ParseMetadataValueFn = Expected<EntryT>(*)(
    Name,
    const Path&,
    const Core::Metascript::Value&,
    Core::Assets::AssetArena&,
    Core::Alloc::ScratchArena&
);

[[nodiscard]] Expected<b8> ParseMetadataBoolField(
    const Path& nwbFilePath,
    AStringView diagnosticPrefix,
    const Core::Metascript::Value& value,
    AStringView fieldLabel
);
[[nodiscard]] b8 ReadOptionalFiniteF32Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    AStringView diagnosticPrefix,
    AStringView fieldLabel,
    f32& outValue
);
[[nodiscard]] b8 ReadOptionalI32Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    AStringView diagnosticPrefix,
    AStringView fieldLabel,
    i32& outValue
);

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn>
[[nodiscard]] Expected<EntryT> ParseCookMetadataDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    const AStringView diagnosticPrefix,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, diagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseValFn(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn, const AStringView& DiagnosticPrefix>
[[nodiscard]] Expected<EntryT> ParseCookEntryDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseCookMetadataDocument<EntryT, ParseValFn>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        DiagnosticPrefix,
        context.cookArena,
        context.scratchArena
    );
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn>
[[nodiscard]] Expected<EntryT> ParseCookEntryValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseValFn(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn>
[[nodiscard]] Expected<EntryT> ParseCookDependencyEntry(
    const Core::Assets::CookDependencyItemContext& context,
    const AStringView diagnosticPrefix
){
    return context.document
        ? ParseCookMetadataDocument<EntryT, ParseValFn>(
            context.assetRoot,
            context.virtualRoot,
            context.nwbFilePath,
            *context.document,
            diagnosticPrefix,
            context.arena,
            context.scratchArena
        )
        : ParseValFn(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
}

template<typename AssetT, typename EntryT>
[[nodiscard]] Expected<AssetT> BuildComponentAssetFromEntry(EntryT& entry, Core::Assets::AssetArena& arena){
    AssetT asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}

template<typename AssetT, typename EntryT>
[[nodiscard]] Expected<AssetT> BuildComponentCookAsset(EntryT& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    return BuildComponentAssetFromEntry<AssetT, EntryT>(entry, arena);
}

template<typename EntryT, typename AssetT, ParseMetadataValueFn<EntryT> ParseValFn, const AStringView& DiagnosticPrefix>
[[nodiscard]] b8 RegisterComponentCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<EntryT, AssetT, TypedComponentAssetCodec<AssetT>>(
        registry,
        ComponentBinaryPayload::s_ComponentAssetKindLabel,
        &ParseCookEntryDocument<EntryT, ParseValFn, DiagnosticPrefix>,
        &ParseCookEntryValue<EntryT, ParseValFn>,
        &BuildComponentCookAsset<AssetT, EntryT>
    );
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn, const AStringView& DiagnosticPrefix>
[[nodiscard]] b8 ExtractLeafComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector&){
    return ParseCookDependencyEntry<EntryT, ParseValFn>(context, DiagnosticPrefix).has_value();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<EntityCookEntry> ParseEntityCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<EntityAsset> BuildEntityAsset(EntityCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<ProjectCookEntry> ParseProjectCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<ProjectAsset> BuildProjectAsset(ProjectCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);

[[nodiscard]] Expected<SceneCookEntry> ParseSceneCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] Expected<SceneAsset> BuildSceneAsset(SceneCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
