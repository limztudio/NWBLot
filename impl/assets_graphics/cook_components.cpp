// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_components.h"
#include "component_binary_payload.h"

#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_model/asset.h>

#include <impl/assets_scene/cook.h>

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_cook_components{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AssetField = "asset";
static constexpr AStringView s_MaterialField = "material";
static constexpr AStringView s_VisibleField = "visible";
static constexpr AStringView s_OpticalVolumeCoincidenceField = "optical_volume_coincidence";
static constexpr AStringView s_OpticalVolumeGroupField = "optical_volume_group";
static constexpr AStringView s_OpticalVolumePriorityField = "optical_volume_priority";
static constexpr AStringView s_OpticalBoundaryModeField = "optical_boundary_mode";
static constexpr AStringView s_OpticalMediumPriorityField = "optical_medium_priority";
static constexpr AStringView s_ModelComponentDiagnosticPrefix = "Model component meta";
static constexpr AStringView s_MeshComponentDiagnosticPrefix = "Mesh component meta";
static constexpr AStringView s_RendererComponentDiagnosticPrefix = "Renderer component meta";

using ModelComponentCookEntry = TypedComponentCookEntry<ModelComponent>;
using MeshComponentCookEntry = TypedComponentCookEntry<MeshComponent>;
using RendererComponentCookEntry = TypedComponentCookEntry<RendererComponent>;


[[nodiscard]] static b8 SerializeSingleAssetRefComponentPayload(
    const Name& assetName,
    Core::Assets::AssetBytes& outPayload
){
    ComponentBinaryPayload::SingleAssetRefComponentPayloadBinary binaryPayload;
    binaryPayload.assetHash = assetName.hash();
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}


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
static b8 ExtractSingleAssetRefComponentDependencies(
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
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_RendererComponentDiagnosticPrefix,
        {
            s_MaterialField,
            s_VisibleField,
            s_OpticalVolumeCoincidenceField,
            s_OpticalVolumeGroupField,
            s_OpticalVolumePriorityField,
            s_OpticalBoundaryModeField,
            s_OpticalMediumPriorityField
        }
    ))
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

    if(asset.findField(s_OpticalVolumeCoincidenceField)){
        static constexpr ::NamedEnumCase<OpticalVolumeCoincidence::Enum> s_CoincidenceCases[] = {
            { "independent", OpticalVolumeCoincidence::Independent },
            { "identical_material", OpticalVolumeCoincidence::IdenticalMaterial },
            { "shared_group", OpticalVolumeCoincidence::SharedGroup },
        };
        const auto parsedCoincidence = Core::Assets::ParseNamedMetadataEnumField<OpticalVolumeCoincidence::Enum>(
            nwbFilePath,
            asset,
            s_RendererComponentDiagnosticPrefix,
            s_OpticalVolumeCoincidenceField,
            Span<const ::NamedEnumCase<OpticalVolumeCoincidence::Enum>>(s_CoincidenceCases, LengthOf(s_CoincidenceCases)),
            "must be 'independent', 'identical_material', or 'shared_group'"
        );
        if(!parsedCoincidence)
            return MakeUnexpected(Failure{});
        entry.component.opticalVolumeCoincidence = *parsedCoincidence;
    }

    if(asset.findField(s_OpticalVolumeGroupField)){
        const auto groupField = Core::Assets::ReadMetadataStringField(
            nwbFilePath,
            asset,
            s_RendererComponentDiagnosticPrefix,
            s_OpticalVolumeGroupField,
            true
        );
        if(!groupField)
            return MakeUnexpected(Failure{});
        if(TrimView(groupField->text).empty() || groupField->text != TrimView(groupField->text)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{} '{}': field '{}' must not be empty or padded"),
                ConvertString(s_RendererComponentDiagnosticPrefix),
                PathToString<tchar>(nwbFilePath),
                ConvertString(s_OpticalVolumeGroupField)
            );
            return MakeUnexpected(Failure{});
        }
        const auto groupName = Name::FromText(groupField->text);
        if(!groupName || !*groupName){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{} '{}': field '{}' contains an embedded null"),
                ConvertString(s_RendererComponentDiagnosticPrefix),
                PathToString<tchar>(nwbFilePath),
                ConvertString(s_OpticalVolumeGroupField)
            );
            return MakeUnexpected(Failure{});
        }
        entry.component.opticalVolumeGroup = *groupName;
    }

    if(asset.findField(s_OpticalBoundaryModeField)){
        static constexpr ::NamedEnumCase<OpticalBoundaryMode::Enum> s_BoundaryCases[] = {
            { "unspecified", OpticalBoundaryMode::Unspecified },
            { "closed_nested", OpticalBoundaryMode::ClosedNested },
            { "closed_priority", OpticalBoundaryMode::ClosedPriority },
        };
        const auto parsedBoundary = Core::Assets::ParseNamedMetadataEnumField<OpticalBoundaryMode::Enum>(
            nwbFilePath,
            asset,
            s_RendererComponentDiagnosticPrefix,
            s_OpticalBoundaryModeField,
            Span<const ::NamedEnumCase<OpticalBoundaryMode::Enum>>(s_BoundaryCases, LengthOf(s_BoundaryCases)),
            "must be 'unspecified', 'closed_nested', or 'closed_priority'"
        );
        if(!parsedBoundary)
            return MakeUnexpected(Failure{});
        entry.component.opticalBoundaryMode = *parsedBoundary;
    }

    if(
        !SceneAssetCook::ReadOptionalI32Field(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_OpticalVolumePriorityField, entry.component.opticalVolumePriority)
        || !SceneAssetCook::ReadOptionalI32Field(nwbFilePath, asset, s_RendererComponentDiagnosticPrefix, s_OpticalMediumPriorityField, entry.component.opticalMediumPriority)
    )
        return MakeUnexpected(Failure{});

    if(!SceneAssetCook::BuildComponentAssetFromEntry<RendererComponentAsset>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}

static b8 ExtractRendererComponentDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto entry = SceneAssetCook::ParseCookDependencyEntry<RendererComponentCookEntry, &ParseRendererComponentCookMetadata>(
        context,
        s_RendererComponentDiagnosticPrefix
    );
    if(!entry)
        return false;
    return collector.addAssetDependency(entry->component.material.name(), Material::AssetTypeName());
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
    ComponentBinaryPayload::RendererComponentPayloadBinary binaryPayload;
    binaryPayload.materialHash = component.material.name().hash();
    binaryPayload.opticalVolumeGroupHash = component.opticalVolumeGroup.hash();
    binaryPayload.opticalVolumePriority = component.opticalVolumePriority;
    binaryPayload.opticalMediumPriority = component.opticalMediumPriority;
    binaryPayload.visible = component.visible ? 1u : 0u;
    binaryPayload.opticalVolumeCoincidence = static_cast<u8>(component.opticalVolumeCoincidence);
    binaryPayload.opticalBoundaryMode = static_cast<u8>(component.opticalBoundaryMode);
    binaryPayload.reserved0 = 0u;
    binaryPayload.reserved1 = 0u;
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
        registry.registerType(
            ModelComponentAsset::AssetTypeName(),
            &ExtractSingleAssetRefComponentDependencies<ModelComponentAsset, Model, &ModelComponent::model, s_ModelComponentDiagnosticPrefix>,
            IComponent::AssetTypeName()
        )
        && registry.registerType(
            MeshComponentAsset::AssetTypeName(),
            &ExtractSingleAssetRefComponentDependencies<MeshComponentAsset, Mesh, &MeshComponent::mesh, s_MeshComponentDiagnosticPrefix>,
            IComponent::AssetTypeName()
        )
        && registry.registerType(
            RendererComponentAsset::AssetTypeName(),
            &ExtractRendererComponentDependencies,
            IComponent::AssetTypeName()
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
