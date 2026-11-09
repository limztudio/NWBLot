// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_components.h"
#include "csg_shader_variants.h"
#include "csg_evaluator_dependencies.h"
#include "shader_cook_plan.h"
#include "shader_include_directories.h"
#include "shader_variant_bytecode_cache.h"

#include <impl/assets_graphics/graphics_component_binary_codec.adhd.h>
#include <impl/assets_graphics/cook_types.adhd.h>
#include <impl/assets_graphics/mesh_subsystem.adhd.h>

#include <impl/assets_csg/asset.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_csg/shape_module_writer.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_material/material_avboit_shader_generator.h>
#include <impl/assets_material/material_shadow_dispatch.h>
#include <impl/assets_material/types.adhd.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_scene/cook.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/shader_define_planner.h>
#include <impl/assets_shader/shader_types.h>
#include <impl/assets_shader/slang_compiler.h>
#include <impl/assets_shader/source_dependencies.h>
#include <impl/assets_subsystem/cook.h>

#include <core/assets/asset_file_discovery.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/assets/reserved_asset_ref_parser.h>
#include <core/assets/volume/volume_prepare_registry.h>
#include <core/common/log.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <core/graphics/spirv_entry_point.h>

#include <global/hash_utils.h>
#include <global/overflow.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;


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
    const auto assetRef = Core::Assets::ReservedAssetRefParser::ParseText<ReferencedAssetT>(nwbFilePath, DiagnosticPrefix, s_GraphicsComponentAssetField, assetField->text, scratchArena);
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
    const auto materialRef = Core::Assets::ReservedAssetRefParser::ParseText<Material>(nwbFilePath, s_RendererComponentDiagnosticPrefix, s_RendererComponentMaterialField, materialField->text, scratchArena);
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
        if(!IsUnpaddedRendererOpticalVolumeGroup(groupField->text)){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCsgShaderVariants{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void InsertShaderKey(const Name& shaderName, const Name& stageName, ShaderStageKeySet& inOutShaderKeys){
    if(!shaderName || !stageName)
        return;
    const ShaderStageKey key(shaderName, stageName);
    if(inOutShaderKeys.contains(key))
        return;
    if(!inOutShaderKeys.insert(key).second){
        NWB_ASSERT(false);
        return;
    }
}

static void CollectMaterialMeshShaderKeys(const Span<const MaterialCookEntry> materialEntries, ShaderStageKeySet& inOutShaderKeys){
    const Name& meshStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::MeshStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundMeshShader = materialEntry.stageShaders.find(Core::ShaderType::MeshStage);
        if(foundMeshShader == materialEntry.stageShaders.end())
            continue;

        InsertShaderKey(foundMeshShader.value().name(), meshStageName, inOutShaderKeys);
    }
}

ShaderStageKeySet CollectMaterialClipShaderKeys(const Span<const MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    ShaderStageKeySet shaderKeys{ 0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), scratchArena };
    shaderKeys.reserve(materialEntries.size());

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    for(const MaterialCookEntry& materialEntry : materialEntries){
        const auto foundShader = materialEntry.stageShaders.find(Core::ShaderType::PixelStage);
        if(foundShader != materialEntry.stageShaders.end())
            InsertShaderKey(foundShader.value().name(), pixelStageName, shaderKeys);
    }

    CollectMaterialMeshShaderKeys(materialEntries, shaderKeys);
    return shaderKeys;
}

Expected<ShaderStageKeySet, NameTextError::Enum> CollectAvboitClipShaderKeys(const Span<const MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    ShaderStageKeySet shaderKeys{ 0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), scratchArena };
    shaderKeys.reserve(materialEntries.size());

    CollectMaterialMeshShaderKeys(materialEntries, shaderKeys);

    const Name& pixelStageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::PixelStage);
    // Transparent materials require CSG clip variants for each cook-generated AVBOIT pixel shader (accumulate, occupancy, extinction).
    for(const MaterialCookEntry& materialEntry : materialEntries){
        if(!materialEntry.avboitAccumulatePixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitAccumulatePixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(*shaderName, pixelStageName, shaderKeys);
        }
        if(!materialEntry.avboitOccupancyPixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitOccupancyPixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(*shaderName, pixelStageName, shaderKeys);
        }
        if(!materialEntry.avboitExtinctionPixelShaderName.empty()){
            const auto shaderName = Name::FromText(AStringView(materialEntry.avboitExtinctionPixelShaderName));
            if(!shaderName)
                return MakeUnexpected(shaderName.error());
            InsertShaderKey(*shaderName, pixelStageName, shaderKeys);
        }
    }
    return shaderKeys;
}

Expected<b8, NameTextError::Enum> SupportsClipVariant(const ShaderStageKeySet& shaderKeys, const ShaderCook::ShaderEntry& shaderEntry){
    const auto shaderName = ToName(shaderEntry.name);
    const auto stageName = ToName(shaderEntry.archiveStage);
    if(!shaderName)
        return MakeUnexpected(shaderName.error());
    if(!stageName)
        return MakeUnexpected(stageName.error());
    const ShaderStageKey shaderKey(*shaderName ? **shaderName : s_NameNone, *stageName ? **stageName : s_NameNone);
    return shaderKeys.find(shaderKey) != shaderKeys.end();
}

Expected<ShaderCook::DefineCombo> BuildClipDefineCombo(const AStringView entryName, const ShaderCook::DefineCombo& sourceCombo, ShaderCook::CookArena& cookArena){
    const auto capacity = TryComputeCsgClipDefineCapacity(entryName, sourceCombo.size());
    if(!capacity)
        return MakeUnexpected(Failure{});

    ShaderCook::DefineCombo combo{cookArena};
    combo.reserve(*capacity);
    for(const auto& [defineName, defineValue] : sourceCombo){
        if(!combo.try_emplace(defineName, defineValue).second){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }
    }

    if(!combo.try_emplace(s_CsgIntervalSampleEnabledImplicitDefineName, s_IntervalSampleEnabledImplicitDefineValue).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: reserved CSG shader define '{}' already exists for entry '{}'"), ConvertString(s_CsgIntervalSampleEnabledImplicitDefineName), ConvertString(entryName));
        return MakeUnexpected(Failure{});
    }

    if(combo.try_emplace(s_CsgClipImplicitDefineName, s_EnabledImplicitDefineValue).second)
        return combo;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: reserved CSG shader define '{}' already exists for entry '{}'"), ConvertString(s_CsgClipImplicitDefineName), ConvertString(entryName));
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_validation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;

static b8 ValidateMaterialVariant(const MaterialCookEntry& materialEntry, const PreparedShaderEntry& preparedShaderEntry, const Name& stageName, Core::Assets::ScratchArena& scratchArena){
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' has empty shader_variant"), ConvertString(AStringView(materialEntry.virtualPath)));
        return false;
    }
    const AStringView requestedVariant(materialEntry.shaderVariant.data(), materialEntry.shaderVariant.size());

    const Core::Assets::ScratchString contextLabel = StringFormat(scratchArena, "{} [{}]", AStringView(materialEntry.virtualPath), stageName.resolvedText());
    return ShaderCook::ValidateVariantSignature(contextLabel, requestedVariant, preparedShaderEntry.entry.defineValues, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook_plan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;
using Core::Assets::CookString;

static b8 SetShaderImplicitDefine(const AStringView defineName, const AStringView defineValue, ShaderCook::ShaderEntry& inOutEntry){
    if(!ValidateShaderDoesNotUseImplicitDefine(inOutEntry.name, defineName, inOutEntry.defineValues.find(defineName) != inOutEntry.defineValues.end()))
        return false;

    ShaderCook::CookArena& arena = inOutEntry.name.get_allocator().arena();
    CookString defineNameKey(defineName, arena);
    inOutEntry.implicitDefines.insert_or_assign(Move(defineNameKey), CookString(defineValue, arena));
    return true;
}

static Expected<ShaderCook::ShaderEntry> BuildMeshComputeShadowEntry(const ShaderCook::ShaderEntry& sourceEntry){
    ShaderCook::ShaderEntry entry = sourceEntry;
    entry.archiveStage = ShaderBinaryPayload::s_MeshComputeArchiveStageText;
    entry.stage = Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ComputeStage);

    if(!SetShaderImplicitDefine(ShaderBinaryPayload::s_MeshComputeImplicitDefineText, s_EnabledImplicitDefineValue, entry))
        return MakeUnexpected(Failure{});
    return entry;
}

static Expected<u64> CountShaderVariants(const ShaderCook::ShaderEntry& entry){
    u64 variantCount = 1u;

    for(const auto& [defineName, defineEntry] : entry.defineValues){
        if(!TryAccumulateShaderDefineVariantCount(entry.name, defineName, static_cast<u64>(defineEntry.values.size()), variantCount))
            return MakeUnexpected(Failure{});
    }

    return variantCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_volume_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;
using Core::Assets::CookString;
using Core::Assets::CookEntryPathHashSet;

using ScratchPath = ::Path<ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<usize> ReserveShaderIndexRecords(const Span<const PreparedShaderEntry> preparedEntries, Vector<Core::ShaderArchive::Record, ScratchArena>& outShaderIndexRecords){
    u64 shaderRecordCount = 0;
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        if(!TryAccumulatePreparedShaderRecordCount(preparedEntry.variantCount, shaderRecordCount))
            return MakeUnexpected(Failure{});
    }
    usize count = 0u;
    if(!ValidatePreparedShaderRecordCountFitsCapacity(shaderRecordCount, count))
        return MakeUnexpected(Failure{});

    outShaderIndexRecords.clear();
    outShaderIndexRecords.reserve(count);
    return count;
}

static b8 AppendShaderIndexToManifest(
    const Span<const Core::ShaderArchive::Record> shaderIndexRecords,
    ShaderCook::CookArena& cookArena,
    ScratchArena& scratchArena,
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& inOutManifest,
    CookEntryPathHashSet& inOutSeenVirtualPathHashes
){
    const Name& shaderIndexVirtualPath = Core::ShaderArchive::s_IndexVirtualPathName;
    if(!inOutSeenVirtualPathHashes.insert(shaderIndexVirtualPath.hash()).second){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: duplicate shader archive index virtual path '{}'"), ConvertString(shaderIndexVirtualPath.resolvedText()));
        return false;
    }

    const auto indexBinary = Core::ShaderArchive::SerializeIndex(shaderIndexRecords, cookArena, scratchArena);
    if(!indexBinary){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to serialize shader index"));
        return false;
    }
    if(Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(shaderIndexVirtualPath, *indexBinary, inOutManifest))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to append shader index to manifest"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::CookString;
using Core::Assets::CookVector;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<::Path<ScratchArena>> CsgEvaluatorDependencyCollector::resolve(const AStringView includeName, const Span<const Path> includeDirectories){
    if(includeName.empty())
        return MakeUnexpected(Failure{});

    const ::Path<ScratchArena> includePath(m_scratchArena, includeName);
    if(includePath.isAbsolute()){
        const auto includePathQueryResult = IsRegularFile(includePath);
        if(includePathQueryResult && *includePathQueryResult)
            return includePath.lexicallyNormal();
        if(!includePathQueryResult && !IsMissingPathError(includePathQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to query CSG evaluator module include '{}': {}"), PathToString<tchar>(includePath), ConvertString(includePathQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const ::Path<ScratchArena> candidate = (::Path<ScratchArena>(m_scratchArena, includeDirectory) / includePath).lexicallyNormal();
        const auto candidateQueryResult = IsRegularFile(candidate);
        if(candidateQueryResult && *candidateQueryResult)
            return candidate;
        if(!candidateQueryResult && !IsMissingPathError(candidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to query CSG evaluator module include '{}': {}"), PathToString<tchar>(candidate), ConvertString(candidateQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve CSG evaluator module include '{}'"), ConvertString(includeName));
    return MakeUnexpected(Failure{});
}

b8 CsgEvaluatorDependencyCollector::appendUnique(const ::Path<ScratchArena>& dependency, DependencyPathScratchSet& inOutSeenDependencies){
    auto absoluteDependencyResult = AbsolutePath(dependency);
    if(!absoluteDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve CSG evaluator module dependency '{}': {}"), PathToString<tchar>(dependency), ConvertString(absoluteDependencyResult.error().message()));
        return false;
    }
    const ::Path<ScratchArena> absoluteDependency = absoluteDependencyResult->lexicallyNormal();
    ScratchString canonicalPath = PathToString(m_scratchArena, absoluteDependency);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(canonicalPath);
#endif
    if(inOutSeenDependencies.insert(Move(canonicalPath)).second)
        m_dependencies.emplace_back(m_dependencies.get_allocator().arena(), absoluteDependency);
    return true;
}

b8 CsgEvaluatorDependencyCollector::collect(const ShaderCook::ShaderEntry& entry, const Span<const Path> includeDirectories){
    const auto foundDefine = entry.defineValues.find(s_CsgProjectEvaluatorModuleDefineName);
    if(foundDefine == entry.defineValues.end())
        return true;

    DependencyPathScratchSet seenDependencies(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), m_scratchArena);
    seenDependencies.reserve(m_dependencies.size());
    // Dependency collection already produces normalized absolute paths; retain its first-seen ordering.
    for(const Path& dependency : m_dependencies){
        ScratchString canonicalPath = PathToString(m_scratchArena, dependency);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(canonicalPath);
#endif
        if(seenDependencies.contains(canonicalPath))
            continue;
        if(!seenDependencies.insert(Move(canonicalPath)).second){
            NWB_ASSERT(false);
            return false;
        }
    }

    Vector<::Path<ScratchArena>, ScratchArena> moduleDependencies(m_scratchArena);
    for(const ShaderCook::CookString& defineValue : foundDefine.value().values){
        const AStringView includeName = UnquoteDoubleQuotedView(AStringView(defineValue));
        if(includeName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: CSG evaluator module define value '{}' must be a quoted include path"), ConvertString(defineValue));
            return false;
        }

        const auto modulePath = resolve(includeName, includeDirectories);
        if(!modulePath)
            return false;

        moduleDependencies.clear();
        if(!ShaderSourceDependencies::Gather(*modulePath, includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, m_scratchArena, moduleDependencies))
            return false;
        for(const ::Path<ScratchArena>& dependency : moduleDependencies){
            if(!appendUnique(dependency, seenDependencies))
                return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateMaterials(const Span<const PreparedShaderEntry> preparedEntries, const Span<const MaterialCookEntry> materialEntries, Core::Assets::ScratchArena& scratchArena){
    HashMap<ShaderStageKey, NotNull<const PreparedShaderEntry*>, Core::Assets::ScratchArena, Hasher<ShaderStageKey>, EqualTo<ShaderStageKey>> preparedShaderLookup(0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), scratchArena);
    preparedShaderLookup.reserve(preparedEntries.size());
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const auto shaderName = ToName(preparedEntry.entry.name);
        const auto stageName = ToName(preparedEntry.entry.archiveStage);
        if(!shaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: prepared shader name has invalid text (failure {})"), static_cast<u32>(shaderName.error()));
            return false;
        }
        if(!stageName){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: prepared shader stage has invalid text for entry '{}' (failure {})"), ConvertString(preparedEntry.entry.name), static_cast<u32>(stageName.error()));
            return false;
        }
        const ShaderStageKey shaderKey(*shaderName ? **shaderName : s_NameNone, *stageName ? **stageName : s_NameNone);
        if(!preparedShaderLookup.emplace(shaderKey, MakeNotNull(&preparedEntry)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: duplicate prepared shader key '{}' stage '{}'"), ConvertString(preparedEntry.entry.name), ConvertString(preparedEntry.entry.archiveStage));
            return false;
        }
    }

    for(const MaterialCookEntry& materialEntry : materialEntries){
        b8 hasShaderStage = false;
        b8 hasInterfaceStage = false;

        for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
            hasShaderStage = true;
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            const ShaderStageKey shaderLookupKey(shaderAsset.name(), stageName);
            const auto foundShader = preparedShaderLookup.find(shaderLookupKey);
            if(foundShader == preparedShaderLookup.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown shader '{}' for stage '{}'"), ConvertString(AStringView(materialEntry.virtualPath)), ConvertString(shaderAsset.name().resolvedText()), ConvertString(stageName.resolvedText()));
                return false;
            }

            if(!__hidden_material_validation::ValidateMaterialVariant(materialEntry, *foundShader.value(), stageName, scratchArena))
                return false;

            // Only the pixel surface hook requires the material interface; the shared mesh stage has none.
            if(shaderType == Core::ShaderType::PixelStage){
                hasInterfaceStage = true;
                const Name shaderMaterialInterface = foundShader.value()->materialTypedBindingInterface;
                const Core::Assets::CookString& shaderMaterialInterfacePath = foundShader.value()->materialTypedBindingInterfacePath;
                if(shaderMaterialInterface){
                    const auto materialInterface = Name::FromText(AStringView(materialEntry.materialInterface));
                    if(!materialInterface){
                        NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' interface has invalid text (failure {})"), ConvertString(AStringView(materialEntry.virtualPath)), static_cast<u32>(materialInterface.error()));
                        return false;
                    }
                    if(!ValidateMaterialCookPixelShaderInterface(
                        AStringView(materialEntry.virtualPath),
                        AStringView(materialEntry.materialInterface),
                        shaderAsset.name(),
                        shaderMaterialInterface,
                        AStringView(shaderMaterialInterfacePath),
                        *materialInterface
                    ))
                        return false;
                }
                else if(!ValidateMaterialCookPixelShaderInterface(
                    AStringView(materialEntry.virtualPath),
                    AStringView(materialEntry.materialInterface),
                    shaderAsset.name(),
                    s_NameNone,
                    AStringView(),
                    s_NameNone
                ))
                    return false;
            }
        }

        if(!ValidateMaterialCookStagePresence(
            AStringView(materialEntry.virtualPath),
            AStringView(materialEntry.materialInterface),
            hasShaderStage,
            hasInterfaceStage
        ))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShaderIncludeDirectories::Append(const Path& includeDirectory, const ShaderCook::ShaderEntry& entry, ScratchArena& scratchArena, CookVector<Path>& outIncludeDirectories, IncludeDirectoryScratchSet& inOutSeenIncludeDirectories){
    const auto isDirectoryResult = IsDirectory(includeDirectory);
    if(!isDirectoryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to query include root '{}' for entry '{}': {}"), PathToString<tchar>(includeDirectory), ConvertString(entry.name), ConvertString(isDirectoryResult.error().message()));
        return false;
    }
    const b8 isDirectory = *isDirectoryResult;
    if(!isDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: include root is not a directory for entry '{}': '{}'"), ConvertString(entry.name), PathToString<tchar>(includeDirectory));
        return false;
    }

    const ::Path<ScratchArena> normalizedIncludeDirectoryPath = ::Path<ScratchArena>(scratchArena, includeDirectory).lexicallyNormal();
    ScratchString normalizedIncludeDirectory = PathToString(scratchArena, normalizedIncludeDirectoryPath);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(normalizedIncludeDirectory);
#endif
    if(!inOutSeenIncludeDirectories.insert(Move(normalizedIncludeDirectory)).second)
        return true;

    outIncludeDirectories.push_back(includeDirectory);
    return true;
}

Expected<CookVector<Path>> ShaderIncludeDirectories::Build(
    const Path& repoRoot,
    const Span<const Core::Assets::ResolvedAssetRoot> assetRoots,
    const Span<const NotNull<const Path*>> implicitIncludeRoots,
    const ShaderCook::ShaderEntry& entry,
    ShaderCook::CookArena& arena,
    ScratchArena& scratchArena
){
    IncludeDirectoryScratchSet seenIncludeDirectories(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);

    CookVector<Path> includeDirectories(arena);
    const auto capacity = TryComputeShaderIncludeRootCapacity(entry.name, entry.includeRoots.size(), implicitIncludeRoots.size());
    if(!capacity)
        return MakeUnexpected(Failure{});

    includeDirectories.reserve(*capacity);
    seenIncludeDirectories.reserve(*capacity);

    for(const NotNull<const Path*> implicitIncludeRoot : implicitIncludeRoots){
        if(!ShaderIncludeDirectories::Append(*implicitIncludeRoot, entry, scratchArena, includeDirectories, seenIncludeDirectories))
            return MakeUnexpected(Failure{});
    }

    for(const CookString& includeRoot : entry.includeRoots){
        auto includeDirectory = Core::Assets::ResolveVirtualAssetPath(assetRoots, includeRoot, includeDirectories.get_allocator().arena(), scratchArena);
        if(!includeDirectory){
            if(Core::Assets::HasReservedAssetVirtualRoot(includeRoot, scratchArena)){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve virtual include root '{}' for entry '{}'"), ConvertString(includeRoot), ConvertString(entry.name));
                return MakeUnexpected(Failure{});
            }
            const auto resolved = ResolveAbsolutePath(includeDirectories.get_allocator().arena(), repoRoot, AStringView(includeRoot));
            if(!resolved){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve include root '{}' for entry '{}': {}"), ConvertString(includeRoot), ConvertString(entry.name), ConvertString(resolved.error().message()));
                return MakeUnexpected(Failure{});
            }
            includeDirectory = *resolved;
        }

        if(!ShaderIncludeDirectories::Append(*includeDirectory, entry, scratchArena, includeDirectories, seenIncludeDirectories))
            return MakeUnexpected(Failure{});
    }

    return includeDirectories;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PreparedShaderPlan> PrepareShaderEntriesForCook(
    const Core::Assets::ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const Path& csgShapeIncludeRoot,
    const Path& deferredBxdfIncludeRoot,
    const Path& shadowSurfaceIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    const Span<const MaterialBindEntry> materialBindEntries,
    const Span<const MaterialCookEntry> materialEntries,
    ShaderCook::CookArena& cookArena,
    Core::Assets::ScratchArena& scratchArena,
    const Span<ShaderCook::ShaderEntry> inOutShaderEntries
){
    PreparedShaderPlan plan(cookArena);
    // One shader entry can expand to itself and its mesh-compute shadow entry.
    const auto preparedEntryCapacity = TryComputePreparedShaderEntryCapacity(inOutShaderEntries.size());
    if(!preparedEntryCapacity)
        return MakeUnexpected(Failure{});
    plan.preparedEntries.reserve(*preparedEntryCapacity);
    plan.plannedFileCount = 1; // shader archive index

    const AssetsGraphicsCsgShaderVariants::ShaderStageKeySet materialClipShaderKeys = AssetsGraphicsCsgShaderVariants::CollectMaterialClipShaderKeys(materialEntries, scratchArena);
    const auto avboitClipShaderKeys = AssetsGraphicsCsgShaderVariants::CollectAvboitClipShaderKeys(materialEntries, scratchArena);
    if(!avboitClipShaderKeys){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid generated AVBOIT shader identity (failure {})"), static_cast<u32>(avboitClipShaderKeys.error()));
        return MakeUnexpected(Failure{});
    }

    Vector<NotNull<const Path*>, Core::Assets::ScratchArena> implicitIncludeRoots(scratchArena);
    usize implicitIncludeRootCount = 0u;
    if(!materialBindIncludeRoot.empty())
        ++implicitIncludeRootCount;
    if(!deferredBxdfIncludeRoot.empty())
        ++implicitIncludeRootCount;
    if(!shadowSurfaceIncludeRoot.empty())
        ++implicitIncludeRootCount;
    if(!csgShapeIncludeRoot.empty()){
        ++implicitIncludeRootCount;
        const auto capacity = TryComputeImplicitShaderIncludeRootCapacity(implicitIncludeRootCount, resolvedPaths.assetRoots.size());
        if(!capacity)
            return MakeUnexpected(Failure{});
        implicitIncludeRootCount = *capacity;
    }
    implicitIncludeRoots.reserve(implicitIncludeRootCount);
    if(!materialBindIncludeRoot.empty())
        implicitIncludeRoots.push_back(MakeNotNull(&materialBindIncludeRoot));
    if(!deferredBxdfIncludeRoot.empty())
        implicitIncludeRoots.push_back(MakeNotNull(&deferredBxdfIncludeRoot));
    if(!shadowSurfaceIncludeRoot.empty())
        implicitIncludeRoots.push_back(MakeNotNull(&shadowSurfaceIncludeRoot));
    if(!csgShapeIncludeRoot.empty()){
        implicitIncludeRoots.push_back(MakeNotNull(&csgShapeIncludeRoot));
        for(const Core::Assets::ResolvedAssetRoot& assetRoot : resolvedPaths.assetRoots)
            implicitIncludeRoots.push_back(MakeNotNull(&assetRoot.path));
    }

    // Authored `.bind` files are not compiler inputs; ComputeDependencyChecksum receives empty includeDirectories `{}`.
    for(ShaderCook::ShaderEntry& entry : inOutShaderEntries){
        PreparedShaderEntry preparedEntry(cookArena);
        preparedEntry.entry = Move(entry);

        const auto sourcePath = ResolveAbsolutePath(cookArena, resolvedPaths.repoRoot, AStringView(preparedEntry.entry.source));
        if(!sourcePath){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve source path '{}' for entry '{}': {}"), ConvertString(preparedEntry.entry.source), ConvertString(preparedEntry.entry.name), ConvertString(sourcePath.error().message()));
            return MakeUnexpected(Failure{});
        }
        preparedEntry.sourcePath = *sourcePath;

        const auto sourceExistsResult = FileExists(preparedEntry.sourcePath);
        if(!sourceExistsResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query source path '{}' for entry '{}': {}"), PathToString<tchar>(preparedEntry.sourcePath), ConvertString(preparedEntry.entry.name), ConvertString(sourceExistsResult.error().message()));
            return MakeUnexpected(Failure{});
        }
        const b8 sourceExists = *sourceExistsResult;
        if(!sourceExists){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source does not exist for entry '{}': '{}'"), ConvertString(preparedEntry.entry.name), PathToString<tchar>(preparedEntry.sourcePath));
            return MakeUnexpected(Failure{});
        }

        const auto isRegularSourceFileResult = IsRegularFile(preparedEntry.sourcePath);
        if(!isRegularSourceFileResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to inspect source path '{}' for entry '{}': {}"), PathToString<tchar>(preparedEntry.sourcePath), ConvertString(preparedEntry.entry.name), ConvertString(isRegularSourceFileResult.error().message()));
            return MakeUnexpected(Failure{});
        }
        const b8 isRegularSourceFile = *isRegularSourceFileResult;
        if(!isRegularSourceFile){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source is not a regular file for entry '{}': '{}'"), ConvertString(preparedEntry.entry.name), PathToString<tchar>(preparedEntry.sourcePath));
            return MakeUnexpected(Failure{});
        }

        auto includeDirectories = ShaderIncludeDirectories::Build(resolvedPaths.repoRoot, resolvedPaths.assetRoots, implicitIncludeRoots, preparedEntry.entry, preparedEntry.includeDirectories.get_allocator().arena(), scratchArena);
        if(!includeDirectories)
            return MakeUnexpected(Failure{});
        preparedEntry.includeDirectories = Move(*includeDirectories);
        if(!ShaderSourceDependencies::Gather(preparedEntry.sourcePath, preparedEntry.includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, scratchArena, preparedEntry.dependencies))
            return MakeUnexpected(Failure{});

        ShaderDefinePlanner(cookArena).mergeInheritedDefines(preparedEntry.dependencies, includeMetadata, scratchArena, preparedEntry.entry);
        const usize initialDependencyCount = preparedEntry.dependencies.size();
        if(!CsgEvaluatorDependencyCollector(scratchArena, preparedEntry.dependencies).collect(preparedEntry.entry, preparedEntry.includeDirectories))
            return MakeUnexpected(Failure{});
        if(preparedEntry.dependencies.size() != initialDependencyCount)
            ShaderDefinePlanner(cookArena).mergeInheritedDefines(preparedEntry.dependencies, includeMetadata, scratchArena, preparedEntry.entry);
        if(!ValidateShaderReservedImplicitDefines(
            preparedEntry.entry.name,
            s_MaterialBindTypedBindingImplicitDefineText,
            preparedEntry.entry.defineValues.find(s_MaterialBindTypedBindingImplicitDefineText) != preparedEntry.entry.defineValues.end(),
            preparedEntry.entry.defineValues.find(s_CsgClipImplicitDefineName) != preparedEntry.entry.defineValues.end(),
            preparedEntry.entry.defineValues.find(s_CsgIntervalSampleEnabledImplicitDefineName) != preparedEntry.entry.defineValues.end()
        ))
            return MakeUnexpected(Failure{});

        auto materialDependency = ResolveMaterialBindDependencyInterface(AStringView(preparedEntry.entry.name), materialBindIncludeRoot, materialBindEntries, preparedEntry.dependencies, cookArena, scratchArena);
        if(!materialDependency)
            return MakeUnexpected(Failure{});
        preparedEntry.materialTypedBindingInterface = materialDependency->interfaceName;
        // Typed-.bind readers get the hook via interface deps; the generic mesh shader stays interface-free.
        const AStringView preparedEntryArchiveStage = preparedEntry.entry.archiveStage;
        const b8 preparedEntryStageReadsTypedMaterial =
            preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage)
            || preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage)
            || preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ComputeStage)
            || preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::RayGenerationStage)
            || preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AnyHitStage)
            || preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ClosestHitStage)
            || preparedEntryArchiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MissStage)
        ;
        preparedEntry.usesMaterialTypedBinding = preparedEntryStageReadsTypedMaterial && materialDependency->dependsOnMaterialBind;
        preparedEntry.materialTypedBindingInterfacePath = Move(materialDependency->interfacePath);
        if(preparedEntry.usesMaterialTypedBinding && !__hidden_shader_cook_plan::SetShaderImplicitDefine(s_MaterialBindTypedBindingImplicitDefineText, s_MaterialBindTypedBindingImplicitDefineValueText, preparedEntry.entry))
            return MakeUnexpected(Failure{});
        const auto dependency = ShaderCook::ComputeDependencyChecksum(
            preparedEntry.dependencies,
            preparedEntry.includeDirectories,
            {
                { resolvedPaths.repoRoot, s_DependencyRepoRootAlias },
                { resolvedPaths.cacheDirectory, s_DependencyGeneratedCacheRootAlias },
                { materialBindIncludeRoot, s_MaterialBindGeneratedIncludeCacheDirectoryText },
                { csgShapeIncludeRoot, s_GeneratedIncludeCacheDirectory }
            },
            scratchArena
        );
        if(!dependency)
            return MakeUnexpected(Failure{});
        preparedEntry.dependencyChecksum = dependency->checksum;
        preparedEntry.compilerInputsHaveBom = dependency->compilerInputsHaveBom;
        // Authored `.bind` files are not compiler inputs, so hash them on their own and fold that into the shader checksum.
        if(!materialDependency->interfaceSources.empty()){
            const auto interfaceDependency = ShaderCook::ComputeDependencyChecksum(
                materialDependency->interfaceSources,
                {},
                { { resolvedPaths.repoRoot, s_DependencyRepoRootAlias }, { resolvedPaths.cacheDirectory, s_DependencyGeneratedCacheRootAlias } },
                scratchArena
            );
            if(!interfaceDependency)
                return MakeUnexpected(Failure{});
            Fnv64AppendValue(preparedEntry.dependencyChecksum, interfaceDependency->checksum);
        }
        const auto supportsCsgClipVariant = AssetsGraphicsCsgShaderVariants::SupportsClipVariant(materialClipShaderKeys, preparedEntry.entry);
        const auto supportsAvboitCsgClipVariant = AssetsGraphicsCsgShaderVariants::SupportsClipVariant(*avboitClipShaderKeys, preparedEntry.entry);
        if(!supportsCsgClipVariant){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid CSG clip shader identity for entry '{}' (failure {})"), ConvertString(preparedEntry.entry.name), static_cast<u32>(supportsCsgClipVariant.error()));
            return MakeUnexpected(Failure{});
        }
        if(!supportsAvboitCsgClipVariant){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid AVBOIT clip shader identity for entry '{}' (failure {})"), ConvertString(preparedEntry.entry.name), static_cast<u32>(supportsAvboitCsgClipVariant.error()));
            return MakeUnexpected(Failure{});
        }
        preparedEntry.supportsCsgClipVariant = *supportsCsgClipVariant;
        preparedEntry.supportsAvboitCsgClipVariant = *supportsAvboitCsgClipVariant;
        const auto variantCount = __hidden_shader_cook_plan::CountShaderVariants(preparedEntry.entry);
        if(!variantCount)
            return MakeUnexpected(Failure{});
        preparedEntry.variantCount = *variantCount;
        const u64 baseVariantCount = preparedEntry.variantCount;
        if((preparedEntry.supportsCsgClipVariant || preparedEntry.supportsAvboitCsgClipVariant) && !TryAddCsgClipVariantCount(preparedEntry.entry.name, baseVariantCount, preparedEntry.variantCount))
            return MakeUnexpected(Failure{});
        if(!Core::Assets::AddPlannedFileCount(preparedEntry.variantCount, plan.plannedFileCount))
            return MakeUnexpected(Failure{});

        const b8 emitMeshComputeShadow = preparedEntry.entry.archiveStage == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage) && preparedEntry.entry.emitMeshComputeShadow;
        const usize meshEntryIndex = plan.preparedEntries.size();
        plan.preparedEntries.push_back(Move(preparedEntry));

        if(!emitMeshComputeShadow)
            continue;

        const PreparedShaderEntry& meshShaderEntry = plan.preparedEntries[meshEntryIndex];
        PreparedShaderEntry meshComputeShadowEntry(cookArena);
        auto shadowEntry = __hidden_shader_cook_plan::BuildMeshComputeShadowEntry(meshShaderEntry.entry);
        if(!shadowEntry){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to build mesh-compute shadow entry for '{}'"), ConvertString(meshShaderEntry.entry.name));
            return MakeUnexpected(Failure{});
        }

        meshComputeShadowEntry.entry = Move(*shadowEntry);
        meshComputeShadowEntry.sourcePath = meshShaderEntry.sourcePath;
        meshComputeShadowEntry.includeDirectories = meshShaderEntry.includeDirectories;
        meshComputeShadowEntry.dependencies = meshShaderEntry.dependencies;
        meshComputeShadowEntry.dependencyChecksum = meshShaderEntry.dependencyChecksum;
        meshComputeShadowEntry.compilerInputsHaveBom = meshShaderEntry.compilerInputsHaveBom;
        meshComputeShadowEntry.variantCount = meshShaderEntry.variantCount;
        meshComputeShadowEntry.supportsCsgClipVariant = meshShaderEntry.supportsCsgClipVariant;
        meshComputeShadowEntry.supportsAvboitCsgClipVariant = meshShaderEntry.supportsAvboitCsgClipVariant;
        meshComputeShadowEntry.materialTypedBindingInterfacePath = meshShaderEntry.materialTypedBindingInterfacePath;
        meshComputeShadowEntry.materialTypedBindingInterface = meshShaderEntry.materialTypedBindingInterface;
        meshComputeShadowEntry.usesMaterialTypedBinding = meshShaderEntry.usesMaterialTypedBinding;

        if(!Core::Assets::AddPlannedFileCount(meshComputeShadowEntry.variantCount, plan.plannedFileCount))
            return MakeUnexpected(Failure{});

        plan.preparedEntries.push_back(Move(meshComputeShadowEntry));
    }

    return plan;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AppendPreparedShadersToManifest(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Span<const PreparedShaderEntry> preparedEntries,
    ShaderCook::CookArena& cookArena,
    Core::Assets::ScratchArena& scratchArena,
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest,
    Core::Assets::CookEntryPathHashSet& inOutSeenVirtualPathHashes
){
    Vector<Core::ShaderArchive::Record, Core::Assets::ScratchArena> shaderIndexRecords{scratchArena};
    const auto shaderRecordCount = __hidden_shader_volume_writer::ReserveShaderIndexRecords(preparedEntries, shaderIndexRecords);
    if(!shaderRecordCount)
        return false;

    u64 compilerFingerprint = 0u;
    if(!preparedEntries.empty()){
        const auto fingerprint = SlangShaderCompiler::ComputeCompilerFingerprint(cacheDirectory, scratchArena);
        if(!fingerprint)
            return false;
        compilerFingerprint = *fingerprint;
    }

    Core::GraphicsBytes cookedBytecode{cookArena};
    Core::GraphicsBytes shaderAssetPayload{cookArena};
    ShaderCook::CookVector<ShaderCook::DefineCombo> defineCombinations{ cookArena };
    shaderIndexRecords.clear();

    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const auto shaderNameResult = ToName(entry.name);
        const auto stageNameResult = ToName(entry.archiveStage);
        if(!shaderNameResult || !stageNameResult || !*shaderNameResult || !*stageNameResult || entry.entryPoint.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'"), ConvertString(entry.name), ConvertString(entry.archiveStage), ConvertString(entry.entryPoint));
            return false;
        }
        const Name shaderName = **shaderNameResult;
        const Name stageName = **stageNameResult;

        auto appendShaderVariant = [&](const ShaderCook::DefineCombo& defineCombo) -> b8{
            const Core::Assets::CookString generatedVariantName = ShaderCook::BuildCanonicalVariantName(defineCombo, cookArena, scratchArena);

            const u64 sourceChecksum = ShaderCook::ComputeSourceChecksum(entry, generatedVariantName, preparedEntry.dependencyChecksum, compilerFingerprint, scratchArena);
            const Core::Assets::ScratchString sourceChecksumHex = FormatHex64A(scratchArena, sourceChecksum);

            const __hidden_shader_volume_writer::ScratchPath bytecodeCachePath = ShaderVariantBytecodeCache::BuildPath(cacheDirectory, configurationSafeName, sourceChecksumHex, scratchArena);
            if(!ShaderVariantBytecodeCache::GetBytecode(
                entry,
                generatedVariantName,
                defineCombo,
                preparedEntry.includeDirectories,
                preparedEntry.dependencies,
                preparedEntry.sourcePath,
                bytecodeCachePath,
                preparedEntry.compilerInputsHaveBom,
                scratchArena,
                cookedBytecode
            ))
                return false;

            const Expected<Name> virtualPathResult = Core::ShaderArchive::BuildVirtualPathName(shaderName, generatedVariantName, stageName);
            if(!virtualPathResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'"), ConvertString(entry.name), ConvertString(entry.archiveStage), ConvertString(generatedVariantName));
                return false;
            }
            const Name& virtualPath = *virtualPathResult;

            const NameHash virtualPathHash = virtualPath.hash();
            if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')"), ConvertString(virtualPath.resolvedText()), ConvertString(entry.name), ConvertString(generatedVariantName));
                return false;
            }

            const u64 bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
            const u64 cookKeyHash = BuildShaderVariantCookKeyHash(virtualPathHash, sourceChecksum, bytecodeChecksum);
            const ShaderBinaryPayload::AssetPayloadEncodeFailure::Enum payloadFailure = ShaderBinaryPayload::EncodeAssetPayload(AStringView(entry.entryPoint), cookedBytecode, shaderAssetPayload);
            if(payloadFailure != ShaderBinaryPayload::AssetPayloadEncodeFailure::None){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to package shader payload '{}' (failure {})"), ConvertString(virtualPath.resolvedText()), static_cast<u32>(payloadFailure));
                return false;
            }
            if(!Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(virtualPath, shaderAssetPayload, manifest, cookKeyHash)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to append shader payload '{}' to manifest"), ConvertString(virtualPath.resolvedText()));
                return false;
            }

            Core::ShaderArchive::Record record(cookArena);
            record.shaderName = shaderName;
            record.variantName = generatedVariantName;
            record.stage = stageName;
            record.sourceChecksum = sourceChecksum;
            record.bytecodeChecksum = bytecodeChecksum;
            record.virtualPathHash = virtualPathHash;
            NWB_ASSERT(shaderIndexRecords.size() < *shaderRecordCount);
            shaderIndexRecords.push_back(Move(record));
            return true;
        };

        if(!ShaderDefinePlanner(cookArena).expandDefineCombinations(entry.defineValues, scratchArena, defineCombinations)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: variant combination count exceeds runtime limits for entry '{}'"), ConvertString(entry.name));
            return false;
        }
        if(defineCombinations.empty())
            defineCombinations.push_back(ShaderCook::DefineCombo(cookArena));

        for(const ShaderCook::DefineCombo& defineCombo : defineCombinations){
            if(!appendShaderVariant(defineCombo))
                return false;

            if(preparedEntry.supportsCsgClipVariant || preparedEntry.supportsAvboitCsgClipVariant){
                const auto csgDefineCombo = AssetsGraphicsCsgShaderVariants::BuildClipDefineCombo(AStringView(entry.name), defineCombo, cookArena);
                if(!csgDefineCombo)
                    return false;

                if(!appendShaderVariant(*csgDefineCombo))
                    return false;
            }
        }
    }

    NWB_ASSERT(shaderIndexRecords.size() == *shaderRecordCount);
    return __hidden_shader_volume_writer::AppendShaderIndexToManifest(shaderIndexRecords, cookArena, scratchArena, manifest, inOutSeenVirtualPathHashes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderVariantBytecodeCache::ScratchPath ShaderVariantBytecodeCache::BuildPath(const Path& cacheDirectory, const AStringView configurationSafeName, const AStringView sourceChecksumHex, ScratchArena& scratchArena){
    ScratchString bytecodeFileName(sourceChecksumHex, scratchArena);
    bytecodeFileName += s_ShaderVariantBytecodeExtension;
    return ScratchPath(scratchArena, cacheDirectory) / configurationSafeName / bytecodeFileName;
}

ShaderVariantCacheReadStatus::Enum ShaderVariantBytecodeCache::ReadCached(const ShaderCook::ShaderEntry& entry, const ScratchPath& bytecodePath, Core::GraphicsBytes& outBytecode){
    const auto read = ReadBinaryFile(bytecodePath, outBytecode);
    if(read){
        const auto stageName = ToName(entry.stage);
        if(!stageName){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: shader stage contains an embedded null for entry '{}'"), ConvertString(entry.name));
            return ShaderVariantCacheReadStatus::Error;
        }
        const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(*stageName ? **stageName : s_NameNone);
        if(Core::ResolveSpirvEntryPointName(BinaryByteView{ outBytecode.data(), outBytecode.size() }, AStringView(entry.entryPoint), Core::ShaderType::ToMask(shaderType)))
            return ShaderVariantCacheReadStatus::Hit;

        outBytecode.clear();
        return ShaderVariantCacheReadStatus::Miss;
    }

    if(!IsMissingPathError(read.error())){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to read bytecode cache '{}' for entry '{}': {}"), PathToString<tchar>(bytecodePath), ConvertString(entry.name), ConvertString(read.error().message()));
        return ShaderVariantCacheReadStatus::Error;
    }

    outBytecode.clear();
    return ShaderVariantCacheReadStatus::Miss;
}

b8 ShaderVariantBytecodeCache::GetBytecode(
    const ShaderCook::ShaderEntry& entry,
    const AStringView variantName,
    const ShaderCook::DefineCombo& defineCombo,
    const Span<const Path> includeDirectories,
    const Span<const Path> dependencies,
    const Path& sourcePath,
    const ScratchPath& bytecodeCachePath,
    const b8 compilerInputsHaveBom,
    ScratchArena& scratchArena,
    Core::GraphicsBytes& outBytecode
){
    outBytecode.clear();

    const ShaderVariantCacheReadStatus::Enum bytecodeCacheStatus = ReadCached(entry, bytecodeCachePath, outBytecode);
    if(bytecodeCacheStatus == ShaderVariantCacheReadStatus::Error)
        return false;
    if(bytecodeCacheStatus == ShaderVariantCacheReadStatus::Hit)
        return true;

    HashMap<AStringView, AStringView, ScratchArena, Hasher<AStringView>, EqualTo<AStringView>> mergedDefines(0, Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena);
    // Two live container sizes cannot overflow usize.
    mergedDefines.reserve(defineCombo.size() + entry.implicitDefines.size());
    for(const auto& [defineName, value] : defineCombo)
        mergedDefines.insert_or_assign(defineName, value);
    for(const auto& [defineName, value] : entry.implicitDefines)
        mergedDefines.insert_or_assign(AStringView(defineName), AStringView(value));

    Vector<ShaderBinaryPayload::ShaderMacroDefinition, ScratchArena> compileDefines{ scratchArena };
    if(!ValidateShaderMergedDefineCount(entry.name, mergedDefines.size()))
        return false;
    compileDefines.reserve(mergedDefines.size());
    for(const auto& [defineName, value] : mergedDefines)
        compileDefines.push_back(ShaderBinaryPayload::ShaderMacroDefinition{ defineName, value });
    Sort(compileDefines.begin(), compileDefines.end(), [](const ShaderBinaryPayload::ShaderMacroDefinition& lhs, const ShaderBinaryPayload::ShaderMacroDefinition& rhs)noexcept{
        return lhs.name < rhs.name;
    });

    const ScratchPath cacheParentPath = bytecodeCachePath.parentPath();
    auto ensureDirectoriesResult = EnsureDirectories(cacheParentPath);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to create cache directory '{}': {}"), PathToString<tchar>(cacheParentPath), ConvertString(ensureDirectoriesResult.error().message()));
        return false;
    }

    const ShaderCook::ShaderCompilerRequest compileRequest = {
        .shaderName = entry.name,
        .stage = entry.stage,
        .entryPoint = entry.entryPoint,
        .variantName = variantName,
        .defines = compileDefines,
        .includeDirectories = includeDirectories,
        .dependencies = dependencies,
        .externallyPlannedMacroIncludes = AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes,
        .sourcePath = sourcePath,
        .outputPath = bytecodeCachePath,
        .optimizationLevel = entry.optimizationLevel,
        .rayQuery = entry.rayQuery,
        .compilerInputsHaveBom = compilerInputsHaveBom,
    };
    return SlangShaderCompiler::CompileVariant(compileRequest, scratchArena, outBytecode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_volume_prepare{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialBindEntryVector = ShaderCook::CookVector<MaterialBindEntry>;

struct GraphicsVolumeMetadata{
    ShaderCook shaderCook;
    AssetsGraphicsCookDetail::IncludeMetadataMap includeMetadata;
    AssetsGraphicsCookDetail::ShaderEntryVector shaderEntries;
    MaterialBindEntryVector materialBindEntries;
    AssetsCsgCook::CsgShapeCookEntryVector csgShapeEntries;
    AssetsGraphicsCookDetail::PreparedShaderPlan preparedPlan;
    HashSet<ShaderStageKey, ShaderCook::CookArena, Hasher<ShaderStageKey>, EqualTo<ShaderStageKey>> seenShaderIdentityKeys;


public:
    explicit GraphicsVolumeMetadata(ShaderCook::CookArena& arena)
        : shaderCook(arena)
        , includeMetadata(arena)
        , shaderEntries(arena)
        , materialBindEntries(arena)
        , csgShapeEntries(arena)
        , preparedPlan(arena)
        , seenShaderIdentityKeys(0, Hasher<ShaderStageKey>(), EqualTo<ShaderStageKey>(), arena)
    {}
};

static b8 AppendUniqueShaderEntry(const Path* const nwbFilePath, ShaderCook::ShaderEntry& inOutShaderEntry, GraphicsVolumeMetadata& inOutGraphicsMetadata, AssetsGraphicsCookDetail::ShaderEntryVector& inOutShaderEntries){
    if(inOutShaderEntry.name.empty())
        return true;

    const auto shaderName = ToName(inOutShaderEntry.name);
    const auto stageName = ToName(inOutShaderEntry.archiveStage);
    if(!shaderName){
        if(nwbFilePath)
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid shader identity in meta '{}' (failure {})"), PathToString<tchar>(*nwbFilePath), static_cast<u32>(shaderName.error()));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: generated shader name has invalid text (failure {})"), static_cast<u32>(shaderName.error()));
        return false;
    }
    if(!stageName){
        if(nwbFilePath)
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: invalid shader stage in meta '{}' (failure {})"), PathToString<tchar>(*nwbFilePath), static_cast<u32>(stageName.error()));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: generated shader stage has invalid text (failure {})"), static_cast<u32>(stageName.error()));
        return false;
    }
    const Core::Assets::CookString nwbFilePathText = nwbFilePath ? PathToString(inOutShaderEntries.get_allocator().arena(), *nwbFilePath) : Core::Assets::CookString(inOutShaderEntries.get_allocator().arena());
    if(!TryInsertUniqueShaderIdentityKey(
        *shaderName ? **shaderName : s_NameNone,
        *stageName ? **stageName : s_NameNone,
        inOutShaderEntry.name,
        inOutShaderEntry.archiveStage,
        nwbFilePath != nullptr,
        AStringView(nwbFilePathText),
        inOutGraphicsMetadata.seenShaderIdentityKeys
    ))
        return false;

    inOutShaderEntries.push_back(Move(inOutShaderEntry));
    return true;
}

static Core::Assets::AssetMetadataParseResult::Enum ParseGraphicsDocumentMetadata(Core::Assets::AssetDocumentMetadataParseContext& context){
    using namespace Core::Assets;

    if(ShaderAssetTypes::ShaderTypeFromAssetType(context.assetType) != Core::ShaderType::Invalid){
        GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        auto shaderEntryResult = graphicsMetadata.shaderCook.parseShaderMeta(context.discoveredNwbFile.filePath, context.doc, context.scratchArena);
        if(!shaderEntryResult)
            return AssetMetadataParseResult::Error;
        ShaderCook::ShaderEntry& shaderEntry = *shaderEntryResult;

        auto shaderName = AssetPathsDetail::BuildDerivedAssetVirtualPathText(context.discoveredNwbFile.assetRoot, context.discoveredNwbFile.virtualRoot, ::Path<ScratchArena>(context.scratchArena, shaderEntry.source), context.cookArena, context.scratchArena);
        if(!shaderName)
            return AssetMetadataParseResult::Error;
        shaderEntry.name = Move(*shaderName);

        if(!AppendUniqueShaderEntry(&context.discoveredNwbFile.filePath, shaderEntry, graphicsMetadata, graphicsMetadata.shaderEntries))
            return AssetMetadataParseResult::Error;
        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == s_IncludeAssetTypeName){
        GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        auto includeEntryResult = graphicsMetadata.shaderCook.parseIncludeMeta(context.discoveredNwbFile.filePath, context.doc, context.scratchArena);
        if(!includeEntryResult)
            return AssetMetadataParseResult::Error;
        ShaderCook::IncludeEntry& includeEntry = *includeEntryResult;

        if(!includeEntry.source.empty() && !includeEntry.defineValues.empty()){
            const ::Path<ScratchArena> sourcePath(context.scratchArena, includeEntry.source);
            auto absSourceResult = AbsolutePath(sourcePath);
            if(!absSourceResult){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve include metadata source '{}' from '{}': {}")
                    , ConvertString(includeEntry.source)
                    , PathToString<tchar>(context.discoveredNwbFile.filePath)
                    , ConvertString(absSourceResult.error().message())
                );
                return AssetMetadataParseResult::Error;
            }
            const ::Path<ScratchArena> absSource = absSourceResult->lexicallyNormal();

            CookString key = PathToString(context.cookArena, absSource);
#if defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(key);
#endif
            if(!graphicsMetadata.includeMetadata.emplace(Move(key), Move(includeEntry)).second){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: duplicate include metadata for source '{}'"), PathToString<tchar>(absSource));
                return AssetMetadataParseResult::Error;
            }
        }

        return AssetMetadataParseResult::Parsed;
    }

    if(context.assetType == CsgShape::AssetTypeName()){
        GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        auto csgShapeEntryResult = AssetsCsgCook::ParseCsgShapeCookMetadata(context.discoveredNwbFile.filePath, context.doc, context.cookArena, context.scratchArena);
        if(!csgShapeEntryResult)
            return AssetMetadataParseResult::Error;

        graphicsMetadata.csgShapeEntries.push_back(Move(*csgShapeEntryResult));
        return AssetMetadataParseResult::Parsed;
    }

    return AssetMetadataParseResult::Unsupported;
}

static b8 ParseMaterialBindFiles(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context, GraphicsVolumeMetadata& inOutGraphicsMetadata){
    const auto bindFiles = Core::Assets::AssetFileDiscovery::Discover(context.resolvedPaths.assetRoots, s_MaterialBindSourceExtensionText, context.arena, context.scratchArena);
    if(!bindFiles)
        return false;

    inOutGraphicsMetadata.materialBindEntries.reserve(bindFiles->size());
    for(const Core::Assets::DiscoveredNwbFile& discoveredBindFile : *bindFiles){
        auto bindResult = ParseMaterialBindSource(discoveredBindFile.filePath, context.arena, context.scratchArena);
        if(!bindResult)
            return false;
        MaterialBindEntry& bindEntry = *bindResult;

        auto virtualPath = Core::Assets::AssetPathsDetail::BuildDerivedAssetVirtualPathText(discoveredBindFile.assetRoot, discoveredBindFile.virtualRoot, discoveredBindFile.filePath, context.arena, context.scratchArena);
        if(!virtualPath)
            return false;
        bindEntry.virtualPath = Move(*virtualPath);

        inOutGraphicsMetadata.materialBindEntries.push_back(Move(bindEntry));
    }

    return true;
}

static b8 PrepareGraphicsVolumeAssets(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    const GraphicsVolumeMetadata* selectedGraphics = Core::Assets::FindParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata);
    const Core::Assets::ICookEntryBucket* materialBucket = context.parsedMetadata.entryRegistry.find(Material::AssetTypeName());
    // Only selected graphics metadata or materials require generated modules and the shader index.
    if(!selectedGraphics && (!materialBucket || materialBucket->size() == 0u))
        return true;

    GraphicsVolumeMetadata& graphicsMetadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
    if(!ParseMaterialBindFiles(context, graphicsMetadata))
        return false;
    if(!AssetsCsgCook::AssignCsgShapeCookIds(graphicsMetadata.csgShapeEntries, context.scratchArena))
        return false;

    auto& materialEntries = context.parsedMetadata.entryRegistry.entries<MaterialCookEntry>(Material::AssetTypeName());
    if(graphicsMetadata.shaderEntries.empty() && !materialEntries.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: material assets require at least one shader entry"));
        return false;
    }

    if(!ValidateMaterialCookInterfaces(graphicsMetadata.materialBindEntries, materialEntries, context.scratchArena))
        return false;

    auto materialBindIncludeRootResult = EmitMaterialBindIncludes(context.resolvedPaths.cacheDirectory, context.configurationSafeName, graphicsMetadata.materialBindEntries, context.arena, context.scratchArena);
    if(!materialBindIncludeRootResult)
        return false;
    const Path& materialBindIncludeRoot = *materialBindIncludeRootResult;

    const auto resolveIncludeVirtualSource = [&context](const AStringView ownerKind, const AStringView ownerName, const AStringView fieldLabel, auto& inOutVirtualSource) -> b8{
        if(inOutVirtualSource.empty())
            return true;

        const auto resolvedSource = Core::Assets::ResolveVirtualAssetPath(context.resolvedPaths.assetRoots, AStringView(inOutVirtualSource), context.arena, context.scratchArena);
        if(!resolvedSource){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: {} '{}' {} '{}' does not resolve against any asset root"), ConvertString(ownerKind), ConvertString(ownerName), ConvertString(fieldLabel), ConvertString(AStringView(inOutVirtualSource)));
            return false;
        }

        auto resolvedText = PathToString(context.scratchArena, *resolvedSource);
        Replace(resolvedText.begin(), resolvedText.end(), '\\', '/');
        inOutVirtualSource.assign(AStringView(resolvedText));
        return true;
    };

    // Resolve each CSG eval path to its absolute source (verbatim include, checksum-covered). Runs before shape includes.
    for(auto& csgShapeEntry : graphicsMetadata.csgShapeEntries){
        if(!resolveIncludeVirtualSource("CSG shape", csgShapeEntry.shapeName.resolvedText(), "eval", csgShapeEntry.evalInclude))
            return false;
    }

    auto csgShapeIncludeRootResult = AssetsCsgCook::CsgShapeModuleWriter().emit(context.resolvedPaths.cacheDirectory, context.configurationSafeName, graphicsMetadata.csgShapeEntries, context.scratchArena);
    if(!csgShapeIncludeRootResult)
        return false;
    Path& csgShapeIncludeRoot = *csgShapeIncludeRootResult;

    // Resolve each material bxdf/surface path to its absolute source (verbatim include, checksum-covered).
    for(auto& materialEntry : materialEntries){
        const AStringView materialName(materialEntry.virtualPath);
        if(!resolveIncludeVirtualSource("material", materialName, s_MaterialBxdfSourceLabel, materialEntry.bxdfSource))
            return false;
        if(!resolveIncludeVirtualSource("material", materialName, s_MaterialSurfaceSourceLabel, materialEntry.surfaceSource))
            return false;
    }

    // Assign BXDF ids before building materials; emit dispatch before shader preparation to include it in checksums.
    if(!AssignMaterialShadingModelIds(materialEntries, context.scratchArena))
        return false;

    auto deferredBxdfIncludeRootResult = EmitDeferredBxdfDispatchModule(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialEntries, context.scratchArena);
    if(!deferredBxdfIncludeRootResult)
        return false;
    const Path& deferredBxdfIncludeRoot = *deferredBxdfIncludeRootResult;

    // Trace dispatch also needs assigned surface ids and must participate in shader dependency checksums.
    auto shadowSurfaceIncludeRootResult = ShadowSurfaceDispatchGenerator().emit(context.resolvedPaths.cacheDirectory, context.configurationSafeName, graphicsMetadata.materialBindEntries, materialEntries, context.scratchArena);
    if(!shadowSurfaceIncludeRootResult)
        return false;
    const Path& shadowSurfaceIncludeRoot = *shadowSurfaceIncludeRootResult;

    Name sharedMeshShaderName = s_NameNone;
    if(const Core::Assets::ICookEntryBucket* const meshSubsystemBucket = context.parsedMetadata.entryRegistry.find(MeshSubsystem::AssetTypeName()); meshSubsystemBucket && meshSubsystemBucket->size() > 0u){
        const auto& meshSubsystemEntries = context.parsedMetadata.entryRegistry.entries<SubsystemCookEntry<MeshSubsystem>>(MeshSubsystem::AssetTypeName());
        sharedMeshShaderName = meshSubsystemEntries[0u].subsystem.sharedMs.name();
    }

    // Generate per-surface G-buffer PS (pixel generated, mesh shared), like authored entries. Before shader prep.
    auto& materialCookArena = materialEntries.get_allocator().arena();
    auto generatedPixelShadersResult = EmitMaterialPixelShaders(context.resolvedPaths.cacheDirectory, context.configurationSafeName, sharedMeshShaderName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedPixelShadersResult)
        return false;

    // Generate AVBOIT accumulation from the same surface hook before shader preparation and material validation.
    auto generatedAvboitAccumulatePixelShadersResult = MaterialAvboitShaderGenerator::EmitAccumulate(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedAvboitAccumulatePixelShadersResult)
        return false;

    // Occupancy, extinction, and accumulation must share the surface hook so renderCoverage agrees across passes.
    auto generatedAvboitOccupancyPixelShadersResult = MaterialAvboitShaderGenerator::EmitOccupancy(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedAvboitOccupancyPixelShadersResult)
        return false;

    auto generatedAvboitExtinctionPixelShadersResult = MaterialAvboitShaderGenerator::EmitExtinction(context.resolvedPaths.cacheDirectory, context.configurationSafeName, materialCookArena, context.scratchArena, materialEntries);
    if(!generatedAvboitExtinctionPixelShadersResult)
        return false;

    usize shaderEntryCapacity = 0u;
    if(!TryAccumulateGeneratedShaderEntryCapacity(
        graphicsMetadata.shaderEntries.size(),
        generatedPixelShadersResult->size(),
        generatedAvboitAccumulatePixelShadersResult->size(),
        generatedAvboitOccupancyPixelShadersResult->size(),
        generatedAvboitExtinctionPixelShadersResult->size(),
        shaderEntryCapacity
    ))
        return false;
    graphicsMetadata.shaderEntries.reserve(shaderEntryCapacity);
    graphicsMetadata.seenShaderIdentityKeys.reserve(shaderEntryCapacity);

    auto& shaderCookArena = graphicsMetadata.shaderEntries.get_allocator().arena();
    const auto appendGeneratedPixelShaderEntry = [&](const GeneratedMaterialPixelShader& generatedPixelShader) -> b8{
        ShaderCook::ShaderEntry pixelShaderEntry(shaderCookArena);
        pixelShaderEntry.name.assign(AStringView(generatedPixelShader.name));
        pixelShaderEntry.source.assign(AStringView(generatedPixelShader.source));
        pixelShaderEntry.stage = Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage);
        pixelShaderEntry.archiveStage = pixelShaderEntry.stage;
        pixelShaderEntry.includeRoots.push_back(ShaderCook::CookString(s_EngineGraphicsIncludeRoot, shaderCookArena));
        pixelShaderEntry.emitMeshComputeShadow = false;
        return AppendUniqueShaderEntry(nullptr, pixelShaderEntry, graphicsMetadata, graphicsMetadata.shaderEntries);
    };
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedPixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedAvboitAccumulatePixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedAvboitOccupancyPixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }
    for(const GeneratedMaterialPixelShader& generatedPixelShader : *generatedAvboitExtinctionPixelShadersResult){
        if(!appendGeneratedPixelShaderEntry(generatedPixelShader))
            return false;
    }

    auto preparedPlan = AssetsGraphicsCookDetail::PrepareShaderEntriesForCook(
        context.resolvedPaths,
        materialBindIncludeRoot,
        csgShapeIncludeRoot,
        deferredBxdfIncludeRoot,
        shadowSurfaceIncludeRoot,
        graphicsMetadata.includeMetadata,
        graphicsMetadata.materialBindEntries,
        materialEntries,
        context.arena,
        context.scratchArena,
        graphicsMetadata.shaderEntries
    );
    if(!preparedPlan)
        return false;
    graphicsMetadata.preparedPlan = Move(*preparedPlan);

    if(!AssetsGraphicsCookDetail::ValidateMaterials(graphicsMetadata.preparedPlan.preparedEntries, materialEntries, context.scratchArena))
        return false;
    if(!Core::Assets::AddPlannedFileCount(graphicsMetadata.preparedPlan.plannedFileCount, context.plannedFileCount))
        return false;

    context.manifestCookers.emplace_back([&context](Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest& manifest, Core::Assets::CookEntryPathHashSet& seenVirtualPathHashes, Core::Assets::ScratchArena& writeScratchArena){
        GraphicsVolumeMetadata& metadata = Core::Assets::RequireParsedMetadataExtension<GraphicsVolumeMetadata>(s_GraphicsVolumeMetadataExtensionName, context.parsedMetadata, context.parsedMetadata.arena);
        return AssetsGraphicsCookDetail::AppendPreparedShadersToManifest(
            context.resolvedPaths.cacheDirectory,
            context.configurationSafeName,
            metadata.preparedPlan.preparedEntries,
            context.arena,
            writeScratchArena,
            manifest,
            seenVirtualPathHashes
        );
    });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar S_PrepareGraphicsVolumeAssetsRegistrar(&PrepareGraphicsVolumeAssets);
static Core::Assets::AssetMetadataParserAutoRegistrar S_GraphicsVolumeMetadataParserRegistrar(&ParseGraphicsDocumentMetadata, nullptr);
static Core::Assets::NonStandaloneAssetTypeAutoRegistrar S_MeshSubsystemNonStandaloneAssetTypeRegistrar(MeshSubsystem::AssetTypeName(), "mesh_subsystem");
static Core::Assets::NonStandaloneAssetTypeAutoRegistrar S_IncludeNonStandaloneAssetTypeRegistrar(s_IncludeAssetTypeName, "include");
static Core::Assets::NonStandaloneAssetTypeAutoRegistrar S_ShaderNonStandaloneAssetTypeRegistrar(Name("shader"), "shader");
static Core::Assets::NonStandaloneFirstTokenSuffixAutoRegistrar S_ShaderSuffixNonStandaloneRegistrar("_shader");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ModelComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return GraphicsComponentBinaryCodec{}.serializeAssetRef(component.model.name(), outPayload);
}

b8 MeshComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return GraphicsComponentBinaryCodec{}.serializeAssetRef(component.mesh.name(), outPayload);
}

b8 RendererComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return GraphicsComponentBinaryCodec{}.serializeRenderer(component, outPayload);
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
