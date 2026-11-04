// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_components.h"

#include <impl/assets_graphics/cook_types.adhd.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_scene/cook.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_cook_components{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, DiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, DiagnosticPrefix, { s_GraphicsComponentAssetField }))
        return MakeUnexpected(Failure{});

    const auto assetField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, DiagnosticPrefix, s_GraphicsComponentAssetField, true);
    if(!assetField)
        return MakeUnexpected(Failure{});
    const auto assetRef = Core::Assets::ParseMetadataReservedAssetRefText<ReferencedAssetT>(nwbFilePath, DiagnosticPrefix, s_GraphicsComponentAssetField, assetField->text, scratchArena);
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
static b8 ExtractSingleAssetRefComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    using EntryT = TypedComponentCookEntry<decltype(ComponentAssetT::component)>;
    const auto entry = Core::Assets::ParseCookDependencyEntry<
        EntryT,
        &ParseSingleAssetRefComponentCookMetadata<ComponentAssetT, ReferencedAssetT, FieldPtr, DiagnosticPrefix>
    >(context, DiagnosticPrefix);
    if(!entry)
        return false;
    return collector.addAssetDependency((entry->component.*FieldPtr).name(), ReferencedAssetT::AssetTypeName());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<RendererComponentCookEntry> ParseRendererComponentCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    RendererComponentCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_RendererComponentDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, &IsAllowedRendererComponentMetadataField))
        return MakeUnexpected(Failure{});

    const auto materialField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_RendererComponentMaterialField, true);
    if(!materialField)
        return MakeUnexpected(Failure{});
    const auto materialRef = Core::Assets::ParseMetadataReservedAssetRefText<Material>(nwbFilePath, s_RendererComponentDiagnosticPrefix, s_RendererComponentMaterialField, materialField->text, scratchArena);
    if(!materialRef)
        return MakeUnexpected(Failure{});
    entry.component.material = *materialRef;

    if(const Core::Metascript::Value* visibleValue = asset.findField(s_RendererComponentVisibleField)){
        const auto visible = Core::Assets::ParseMetadataBoolField(nwbFilePath, s_RendererComponentDiagnosticPrefix, *visibleValue, s_RendererComponentVisibleField);
        if(!visible)
            return MakeUnexpected(Failure{});
        entry.component.visible = *visible;
    }

    if(asset.findField(s_RendererComponentOpticalVolumeCoincidenceField)){
        static constexpr Array<::NamedEnumCase<OpticalVolumeCoincidence::Enum>, 3u> s_CoincidenceCases = {{
            { s_OpticalVolumeCoincidenceIndependentText, OpticalVolumeCoincidence::Independent },
            { s_OpticalVolumeCoincidenceIdenticalMaterialText, OpticalVolumeCoincidence::IdenticalMaterial },
            { s_OpticalVolumeCoincidenceSharedGroupText, OpticalVolumeCoincidence::SharedGroup },
        }};
        const auto parsedCoincidence = Core::Assets::ParseNamedMetadataEnumField<OpticalVolumeCoincidence::Enum>(
            nwbFilePath,
            asset,
            s_RendererComponentDiagnosticPrefix,
            s_RendererComponentOpticalVolumeCoincidenceField,
            Span<const ::NamedEnumCase<OpticalVolumeCoincidence::Enum>>(s_CoincidenceCases.data(), s_CoincidenceCases.size()),
            "'independent', 'identical_material', or 'shared_group'"
        );
        if(!parsedCoincidence)
            return MakeUnexpected(Failure{});
        entry.component.opticalVolumeCoincidence = *parsedCoincidence;
    }

    if(asset.findField(s_RendererComponentOpticalVolumeGroupField)){
        const auto groupField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_RendererComponentOpticalVolumeGroupField, true);
        if(!groupField)
            return MakeUnexpected(Failure{});
        if(TrimView(groupField->text).empty() || groupField->text != TrimView(groupField->text)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty or padded"), ConvertString(s_RendererComponentDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_RendererComponentOpticalVolumeGroupField));
            return MakeUnexpected(Failure{});
        }
        const auto groupName = Name::FromText(groupField->text);
        if(!groupName || !*groupName){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' contains an embedded null"), ConvertString(s_RendererComponentDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_RendererComponentOpticalVolumeGroupField));
            return MakeUnexpected(Failure{});
        }
        entry.component.opticalVolumeGroup = *groupName;
    }

    if(asset.findField(s_RendererComponentOpticalBoundaryModeField)){
        static constexpr Array<::NamedEnumCase<OpticalBoundaryMode::Enum>, 3u> s_BoundaryModeCases = {{
            { s_OpticalBoundaryModeUnspecifiedText, OpticalBoundaryMode::Unspecified },
            { s_OpticalBoundaryModeClosedNestedText, OpticalBoundaryMode::ClosedNested },
            { s_OpticalBoundaryModeClosedPriorityText, OpticalBoundaryMode::ClosedPriority },
        }};
        const auto parsedBoundaryMode = Core::Assets::ParseNamedMetadataEnumField<OpticalBoundaryMode::Enum>(
            nwbFilePath,
            asset,
            s_RendererComponentDiagnosticPrefix,
            s_RendererComponentOpticalBoundaryModeField,
            Span<const ::NamedEnumCase<OpticalBoundaryMode::Enum>>(s_BoundaryModeCases.data(), s_BoundaryModeCases.size()),
            "'unspecified', 'closed_nested', or 'closed_priority'"
        );
        if(!parsedBoundaryMode)
            return MakeUnexpected(Failure{});
        entry.component.opticalBoundaryMode = *parsedBoundaryMode;
    }

    if(
        !Core::Assets::ReadOptionalI32Field(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_RendererComponentOpticalVolumePriorityField, entry.component.opticalVolumePriority)
        || !Core::Assets::ReadOptionalI32Field(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_RendererComponentOpticalMediumPriorityField, entry.component.opticalMediumPriority)
    )
        return MakeUnexpected(Failure{});

    if(!SceneAssetCook::BuildComponentAssetFromEntry<RendererComponentAsset>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}

static b8 ExtractRendererComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entry = Core::Assets::ParseCookDependencyEntry<RendererComponentCookEntry, &ParseRendererComponentCookMetadata>(context, s_RendererComponentDiagnosticPrefix);
    if(!entry)
        return false;
    return collector.addAssetDependency(entry->component.material.name(), Material::AssetTypeName());
}

[[nodiscard]] static b8 SerializeSingleAssetRefComponentPayload(const Name& assetName, Core::Assets::AssetBytes& outPayload){
    const SingleAssetRefComponentPayloadBinary binaryPayload = PackSingleAssetRefComponentPayloadBinary(assetName);
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ModelComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return __hidden_assets_graphics_cook_components::SerializeSingleAssetRefComponentPayload(component.model.name(), outPayload);
}

b8 MeshComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return __hidden_assets_graphics_cook_components::SerializeSingleAssetRefComponentPayload(component.mesh.name(), outPayload);
}

b8 RendererComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    const RendererComponentPayloadBinary binaryPayload = PackRendererComponentPayloadBinary(component);
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RegisterGraphicsComponentCookEntries(Core::Assets::CookEntryRegistry& registry){
    using namespace __hidden_assets_graphics_cook_components;

    return
        SceneAssetCook::RegisterComponentCookEntry<
            ModelComponentCookEntry,
            ModelComponentAsset,
            &ParseSingleAssetRefComponentCookMetadata<ModelComponentAsset, Model, &ModelComponent::model, s_ModelComponentDiagnosticPrefix>,
            s_ModelComponentDiagnosticPrefix
        >(registry)
        && SceneAssetCook::RegisterComponentCookEntry<
            MeshComponentCookEntry,
            MeshComponentAsset,
            &ParseSingleAssetRefComponentCookMetadata<MeshComponentAsset, Mesh, &MeshComponent::mesh, s_MeshComponentDiagnosticPrefix>,
            s_MeshComponentDiagnosticPrefix
        >(registry)
        && SceneAssetCook::RegisterComponentCookEntry<
            RendererComponentCookEntry,
            RendererComponentAsset,
            &ParseRendererComponentCookMetadata,
            s_RendererComponentDiagnosticPrefix
        >(registry)
    ;
}

b8 RegisterGraphicsComponentCookDependencies(Core::Assets::CookDependencyRegistry& registry){
    using namespace __hidden_assets_graphics_cook_components;

    return
        registry.registerType(ModelComponentAsset::AssetTypeName(), &ExtractSingleAssetRefComponentDependencies<ModelComponentAsset, Model, &ModelComponent::model, s_ModelComponentDiagnosticPrefix>, IComponent::AssetTypeName())
        && registry.registerType(MeshComponentAsset::AssetTypeName(), &ExtractSingleAssetRefComponentDependencies<MeshComponentAsset, Mesh, &MeshComponent::mesh, s_MeshComponentDiagnosticPrefix>, IComponent::AssetTypeName())
        && registry.registerType(RendererComponentAsset::AssetTypeName(), &ExtractRendererComponentDependencies, IComponent::AssetTypeName())
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

