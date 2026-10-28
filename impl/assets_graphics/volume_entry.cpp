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

#include <impl/assets_scene/binary_payload.h>
#include <impl/assets_scene/cook.h>
#include <impl/assets_subsystem/cook.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>


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

using ModelComponentCookEntry = TypedComponentCookEntry<ModelComponent>;
using MeshComponentCookEntry = TypedComponentCookEntry<MeshComponent>;
using RendererComponentCookEntry = TypedComponentCookEntry<RendererComponent>;


template<
    typename ComponentAssetT,
    typename ReferencedAssetT,
    Core::Assets::AssetRef<ReferencedAssetT> decltype(ComponentAssetT::component)::* FieldPtr,
    const AStringView& DiagnosticPrefix
>
[[nodiscard]] static Expected<TypedComponentCookEntry<decltype(ComponentAssetT::component)>> ParseSingleAssetRefComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using EntryT = TypedComponentCookEntry<decltype(ComponentAssetT::component)>;

    EntryT entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, DiagnosticPrefix, { s_AssetField }))
        return MakeUnexpected(Failure{});

    const auto assetField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, DiagnosticPrefix, s_AssetField, true);
    if(!assetField)
        return MakeUnexpected(Failure{});
    const auto assetRef = Core::Assets::ParseMetadataReservedAssetRefText<ReferencedAssetT>(nwbFilePath, DiagnosticPrefix, s_AssetField, assetField->text, scratchArena);
    if(!assetRef)
        return MakeUnexpected(Failure{});
    entry.component.*FieldPtr = *assetRef;

    if(!SceneAssetCook::BuildComponentAssetFromEntry<ComponentAssetT>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}

template<
    typename ComponentAssetT,
    typename ReferencedAssetT,
    Core::Assets::AssetRef<ReferencedAssetT> decltype(ComponentAssetT::component)::* FieldPtr,
    const AStringView& DiagnosticPrefix
>
static bool ExtractSingleAssetRefComponentDependencies(
    const Core::Assets::CookDependencyItemContext& context,
    Core::Assets::ICookDependencyCollector& collector
){
    using EntryT = TypedComponentCookEntry<decltype(ComponentAssetT::component)>;
    const auto entry = SceneAssetCook::ParseCookDependencyEntry<
        EntryT,
        &ParseSingleAssetRefComponentCookMetadata<ComponentAssetT, ReferencedAssetT, FieldPtr, DiagnosticPrefix>
    >(context, DiagnosticPrefix);
    if(!entry)
        return false;
    return collector.addAssetDependency((entry->component.*FieldPtr).name(), ReferencedAssetT::AssetTypeName());
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
    const auto materialRef = Core::Assets::ParseMetadataReservedAssetRefText<Material>(nwbFilePath, s_RendererComponentDiagnosticPrefix, s_MaterialField, materialField->text, scratchArena);
    if(!materialRef)
        return MakeUnexpected(Failure{});
    entry.component.material = *materialRef;

    if(const Core::Metascript::Value* visibleValue = asset.findField(s_VisibleField)){
        const auto visible = SceneAssetCook::ParseMetadataBoolField(nwbFilePath, s_RendererComponentDiagnosticPrefix, *visibleValue, s_VisibleField);
        if(!visible)
            return MakeUnexpected(Failure{});
        entry.component.visible = *visible;
    }

    if(!SceneAssetCook::BuildComponentAssetFromEntry<RendererComponentAsset>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}

static bool ExtractRendererComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entry = SceneAssetCook::ParseCookDependencyEntry<RendererComponentCookEntry, &ParseRendererComponentCookMetadata>(
        context,
        s_RendererComponentDiagnosticPrefix
    );
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
        && SceneAssetCook::RegisterComponentCookEntry<
            ModelComponentCookEntry,
            ModelComponentAsset,
            &ParseSingleAssetRefComponentCookMetadata<ModelComponentAsset, Model, &ModelComponent::model, s_ModelComponentDiagnosticPrefix>,
            s_ModelComponentDiagnosticPrefix
        >(registry, ComponentBinaryPayload::s_ModelComponentAssetKindLabel)
        && SceneAssetCook::RegisterComponentCookEntry<
            MeshComponentCookEntry,
            MeshComponentAsset,
            &ParseSingleAssetRefComponentCookMetadata<MeshComponentAsset, Mesh, &MeshComponent::mesh, s_MeshComponentDiagnosticPrefix>,
            s_MeshComponentDiagnosticPrefix
        >(registry, ComponentBinaryPayload::s_MeshComponentAssetKindLabel)
        && SceneAssetCook::RegisterComponentCookEntry<
            RendererComponentCookEntry,
            RendererComponentAsset,
            &ParseRendererComponentCookMetadata,
            s_RendererComponentDiagnosticPrefix
        >(registry, ComponentBinaryPayload::s_RendererComponentAssetKindLabel)
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
        && registry.registerComponentType(
            ModelComponentAsset::AssetTypeName(),
            &ExtractSingleAssetRefComponentDependencies<ModelComponentAsset, Model, &ModelComponent::model, s_ModelComponentDiagnosticPrefix>
        )
        && registry.registerComponentType(
            MeshComponentAsset::AssetTypeName(),
            &ExtractSingleAssetRefComponentDependencies<MeshComponentAsset, Mesh, &MeshComponent::mesh, s_MeshComponentDiagnosticPrefix>
        )
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
