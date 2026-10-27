// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "components.h"
#include "bvh/subsystem.h"
#include "csg/subsystem.h"
#include "deferred/subsystem.h"
#include "gi/subsystem.h"
#include "mesh/subsystem.h"
#include "reflection/subsystem.h"
#include "refractive/subsystem.h"
#include "renderer/subsystem.h"
#include "shadow/subsystem.h"
#include "transparent/subsystem.h"

#include <impl/assets_subsystem/cook.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AssetField = "asset";
static constexpr AStringView s_MaterialField = "material";
static constexpr AStringView s_VisibleField = "visible";
static constexpr AStringView s_ModelComponentDiagnosticPrefix = "Model component meta";
static constexpr AStringView s_MeshComponentDiagnosticPrefix = "Mesh component meta";
static constexpr AStringView s_RendererComponentDiagnosticPrefix = "Renderer component meta";


struct ModelComponentCookEntry{
    Name virtualPath = s_NameNone;
    ModelComponent component;


public:
    explicit ModelComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct MeshComponentCookEntry{
    Name virtualPath = s_NameNone;
    MeshComponent component;


public:
    explicit MeshComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};

struct RendererComponentCookEntry{
    Name virtualPath = s_NameNone;
    RendererComponent component;


public:
    explicit RendererComponentCookEntry(Core::Assets::AssetArena&)noexcept{}
};


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseGraphicsComponentAssetRef(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    const AStringView text,
    Core::Alloc::ScratchArena& scratchArena
){
    if(TrimView(text).empty() || text != TrimView(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    if(text.find('\\') != AStringView::npos || !Core::Assets::HasReservedAssetVirtualRoot(text, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    const auto ref = Core::Assets::AssetRef<TAsset>::FromText(text);
    if(!ref || !ref->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' is invalid"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    return *ref;
}

[[nodiscard]] static Expected<ModelComponentCookEntry> ParseModelComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    ModelComponentCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_ModelComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_ModelComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_ModelComponentDiagnosticPrefix, { s_AssetField }))
        return MakeUnexpected(Failure{});

    const auto assetField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_ModelComponentDiagnosticPrefix, s_AssetField, true);
    if(!assetField)
        return MakeUnexpected(Failure{});
    const auto modelRef = ParseGraphicsComponentAssetRef<Model>(nwbFilePath, s_ModelComponentDiagnosticPrefix, s_AssetField, assetField->text, scratchArena);
    if(!modelRef)
        return MakeUnexpected(Failure{});
    entry.component.model = *modelRef;

    ModelComponentAsset candidate(arena, entry.virtualPath);
    candidate.component = entry.component;
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

[[nodiscard]] static Expected<ModelComponentCookEntry> ParseModelComponentDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, context.scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, s_ModelComponentDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseModelComponentCookMetadata(*virtualPathResult, nwbFilePath, **asset, context.cookArena, context.scratchArena);
}

[[nodiscard]] static Expected<ModelComponentCookEntry> ParseModelComponentValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseModelComponentCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

[[nodiscard]] static Expected<ModelComponentAsset> BuildModelComponentAsset(ModelComponentCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    ModelComponentAsset asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}

static bool ExtractModelComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entry = context.document
        ? [&]() -> Expected<ModelComponentCookEntry>{
            auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(context.assetRoot, context.virtualRoot, context.nwbFilePath, context.scratchArena);
            if(!virtualPathResult)
                return MakeUnexpected(Failure{});
            const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(context.nwbFilePath, *context.document, s_ModelComponentDiagnosticPrefix);
            if(!asset)
                return MakeUnexpected(asset.error());
            return ParseModelComponentCookMetadata(*virtualPathResult, context.nwbFilePath, **asset, context.arena, context.scratchArena);
        }()
        : ParseModelComponentCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!entry)
        return false;
    return collector.addAssetDependency(entry->component.model.name(), Model::AssetTypeName());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<MeshComponentCookEntry> ParseMeshComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    MeshComponentCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_MeshComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_MeshComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_MeshComponentDiagnosticPrefix, { s_AssetField }))
        return MakeUnexpected(Failure{});

    const auto assetField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_MeshComponentDiagnosticPrefix, s_AssetField, true);
    if(!assetField)
        return MakeUnexpected(Failure{});
    const auto meshRef = ParseGraphicsComponentAssetRef<Mesh>(nwbFilePath, s_MeshComponentDiagnosticPrefix, s_AssetField, assetField->text, scratchArena);
    if(!meshRef)
        return MakeUnexpected(Failure{});
    entry.component.mesh = *meshRef;

    MeshComponentAsset candidate(arena, entry.virtualPath);
    candidate.component = entry.component;
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

[[nodiscard]] static Expected<MeshComponentCookEntry> ParseMeshComponentDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, context.scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, s_MeshComponentDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseMeshComponentCookMetadata(*virtualPathResult, nwbFilePath, **asset, context.cookArena, context.scratchArena);
}

[[nodiscard]] static Expected<MeshComponentCookEntry> ParseMeshComponentValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseMeshComponentCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

[[nodiscard]] static Expected<MeshComponentAsset> BuildMeshComponentAsset(MeshComponentCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    MeshComponentAsset asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}

static bool ExtractMeshComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entry = context.document
        ? [&]() -> Expected<MeshComponentCookEntry>{
            auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(context.assetRoot, context.virtualRoot, context.nwbFilePath, context.scratchArena);
            if(!virtualPathResult)
                return MakeUnexpected(Failure{});
            const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(context.nwbFilePath, *context.document, s_MeshComponentDiagnosticPrefix);
            if(!asset)
                return MakeUnexpected(asset.error());
            return ParseMeshComponentCookMetadata(*virtualPathResult, context.nwbFilePath, **asset, context.arena, context.scratchArena);
        }()
        : ParseMeshComponentCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!entry)
        return false;
    return collector.addAssetDependency(entry->component.mesh.name(), Mesh::AssetTypeName());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<RendererComponentCookEntry> ParseRendererComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    RendererComponentCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_RendererComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, { s_MaterialField, s_VisibleField }))
        return MakeUnexpected(Failure{});

    const auto materialField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_MaterialField, true);
    if(!materialField)
        return MakeUnexpected(Failure{});
    const auto materialRef = ParseGraphicsComponentAssetRef<Material>(nwbFilePath, s_RendererComponentDiagnosticPrefix, s_MaterialField, materialField->text, scratchArena);
    if(!materialRef)
        return MakeUnexpected(Failure{});
    entry.component.material = *materialRef;

    if(const Core::Metascript::Value* visibleValue = asset.findField(s_VisibleField)){
        if(!visibleValue->isInteger() || (visibleValue->asInteger() != 0 && visibleValue->asInteger() != 1)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be 0 or 1"), ConvertString(s_RendererComponentDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_VisibleField));
            return MakeUnexpected(Failure{});
        }
        entry.component.visible = visibleValue->asInteger() != 0;
    }

    RendererComponentAsset candidate(arena, entry.virtualPath);
    candidate.component = entry.component;
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

[[nodiscard]] static Expected<RendererComponentCookEntry> ParseRendererComponentDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, context.scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, s_RendererComponentDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseRendererComponentCookMetadata(*virtualPathResult, nwbFilePath, **asset, context.cookArena, context.scratchArena);
}

[[nodiscard]] static Expected<RendererComponentCookEntry> ParseRendererComponentValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseRendererComponentCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

[[nodiscard]] static Expected<RendererComponentAsset> BuildRendererComponentAsset(RendererComponentCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    RendererComponentAsset asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}

static bool ExtractRendererComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entry = context.document
        ? [&]() -> Expected<RendererComponentCookEntry>{
            auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(context.assetRoot, context.virtualRoot, context.nwbFilePath, context.scratchArena);
            if(!virtualPathResult)
                return MakeUnexpected(Failure{});
            const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(context.nwbFilePath, *context.document, s_RendererComponentDiagnosticPrefix);
            if(!asset)
                return MakeUnexpected(asset.error());
            return ParseRendererComponentCookMetadata(*virtualPathResult, context.nwbFilePath, **asset, context.arena, context.scratchArena);
        }()
        : ParseRendererComponentCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!entry)
        return false;
    return collector.addAssetDependency(entry->component.material.name(), Material::AssetTypeName());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool RegisterGraphicsSubsystemCookEntries(Core::Assets::CookEntryRegistry& registry){
    return
        AssetsSubsystemCook::RegisterSubsystemCookEntry<RendererSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<MeshSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<DeferredSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<BvhSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<GiSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<ReflectionSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<ShadowSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<TransparentSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<RefractiveSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<CsgSubsystem>(registry)
        && Core::Assets::RegisterDocumentValueCookEntry<ModelComponentCookEntry, ModelComponentAsset, TypedComponentAssetCodec<ModelComponentAsset>>(
            registry,
            ComponentBinaryPayload::s_ModelComponentAssetKindLabel,
            &ParseModelComponentDocument,
            &ParseModelComponentValue,
            &BuildModelComponentAsset
        )
        && Core::Assets::RegisterDocumentValueCookEntry<MeshComponentCookEntry, MeshComponentAsset, TypedComponentAssetCodec<MeshComponentAsset>>(
            registry,
            ComponentBinaryPayload::s_MeshComponentAssetKindLabel,
            &ParseMeshComponentDocument,
            &ParseMeshComponentValue,
            &BuildMeshComponentAsset
        )
        && Core::Assets::RegisterDocumentValueCookEntry<RendererComponentCookEntry, RendererComponentAsset, TypedComponentAssetCodec<RendererComponentAsset>>(
            registry,
            ComponentBinaryPayload::s_RendererComponentAssetKindLabel,
            &ParseRendererComponentDocument,
            &ParseRendererComponentValue,
            &BuildRendererComponentAsset
        )
    ;
}

static bool RegisterGraphicsSubsystemCookDependencies(Core::Assets::CookDependencyRegistry& registry){
    return
        AssetsSubsystemCook::RegisterSubsystemCookDependency<RendererSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<MeshSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<DeferredSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<BvhSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<GiSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<ReflectionSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<ShadowSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<TransparentSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<RefractiveSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<CsgSubsystem>(registry)
        && registry.registerComponentType(ModelComponentAsset::AssetTypeName(), &ExtractModelComponentDependencies)
        && registry.registerComponentType(MeshComponentAsset::AssetTypeName(), &ExtractMeshComponentDependencies)
        && registry.registerComponentType(RendererComponentAsset::AssetTypeName(), &ExtractRendererComponentDependencies)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_GraphicsSubsystemCookEntryRegistrar, RegisterGraphicsSubsystemCookEntries);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_GraphicsSubsystemCookDependencyRegistrar, RegisterGraphicsSubsystemCookDependencies);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

