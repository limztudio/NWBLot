// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"
#include <impl/assets_shader/shader_define_planner.h>
#include "shader_include_directories.h"

#include "csg_shader_variants.h"
#include "csg_evaluator_dependencies.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_material/types.adhd.h>
#include <impl/assets_shader/source_dependencies.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/graphics/shader_stage_names.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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


namespace AssetsGraphicsCookDetail{


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

        auto includeDirectories = ShaderIncludeDirectories::build(resolvedPaths.repoRoot, resolvedPaths.assetRoots, implicitIncludeRoots, preparedEntry.entry, preparedEntry.includeDirectories.get_allocator().arena(), scratchArena);
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

