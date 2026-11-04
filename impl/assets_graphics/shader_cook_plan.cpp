// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"

#include "csg_shader_variants.h"

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
using Core::Assets::CookVector;
using Core::Assets::ResolvedCookPaths;
using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;

using IncludeDirectoryScratchSet = HashSet<ScratchString, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;
using DependencyPathScratchSet = HashSet<ScratchString, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 AppendIncludeDirectory(const Path& includeDirectory, const ShaderCook::ShaderEntry& entry, ScratchArena& scratchArena, CookVector<Path>& outIncludeDirectories, IncludeDirectoryScratchSet& inOutSeenIncludeDirectories){
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

static Expected<CookVector<Path>> BuildIncludeDirectories(
    const Path& repoRoot,
    const Span<const Core::Assets::ResolvedAssetRoot> assetRoots,
    const Span<const NotNull<const Path*>> implicitIncludeRoots,
    const ShaderCook::ShaderEntry& entry,
    ShaderCook::CookArena& arena,
    ScratchArena& scratchArena
){
    IncludeDirectoryScratchSet seenIncludeDirectories(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);

    CookVector<Path> includeDirectories(arena);
    if(entry.includeRoots.size() > Limit<usize>::s_Max - implicitIncludeRoots.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: include root count overflow for entry '{}'"), ConvertString(entry.name));
        return MakeUnexpected(Failure{});
    }

    includeDirectories.reserve(entry.includeRoots.size() + implicitIncludeRoots.size());
    seenIncludeDirectories.reserve(entry.includeRoots.size() + implicitIncludeRoots.size());

    for(const NotNull<const Path*> implicitIncludeRoot : implicitIncludeRoots){
        if(!AppendIncludeDirectory(*implicitIncludeRoot, entry, scratchArena, includeDirectories, seenIncludeDirectories))
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

        if(!AppendIncludeDirectory(*includeDirectory, entry, scratchArena, includeDirectories, seenIncludeDirectories))
            return MakeUnexpected(Failure{});
    }

    return includeDirectories;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

static Expected<::Path<ScratchArena>> ResolveProjectEvaluatorModuleIncludePath(const AStringView includeName, const Span<const Path> includeDirectories, ScratchArena& scratchArena){
    if(includeName.empty())
        return MakeUnexpected(Failure{});

    const ::Path<ScratchArena> includePath(scratchArena, includeName);
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
        const ::Path<ScratchArena> candidate = (::Path<ScratchArena>(scratchArena, includeDirectory) / includePath).lexicallyNormal();
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

static b8 AppendUniqueDependency(const ::Path<ScratchArena>& dependency, ScratchArena& scratchArena, DependencyPathScratchSet& inOutSeenDependencies, ShaderCook::CookVector<Path>& inOutDependencies){
    auto absoluteDependencyResult = AbsolutePath(dependency);
    if(!absoluteDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve CSG evaluator module dependency '{}': {}"), PathToString<tchar>(dependency), ConvertString(absoluteDependencyResult.error().message()));
        return false;
    }
    const ::Path<ScratchArena> absoluteDependency = absoluteDependencyResult->lexicallyNormal();
    ScratchString canonicalPath = PathToString(scratchArena, absoluteDependency);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(canonicalPath);
#endif
    if(inOutSeenDependencies.insert(Move(canonicalPath)).second)
        inOutDependencies.emplace_back(inOutDependencies.get_allocator().arena(), absoluteDependency);
    return true;
}

static b8 AppendCsgProjectEvaluatorModuleDependencies(const ShaderCook::ShaderEntry& entry, const Span<const Path> includeDirectories, ScratchArena& scratchArena, ShaderCook::CookVector<Path>& inOutDependencies){
    const auto foundDefine = entry.defineValues.find(s_CsgProjectEvaluatorModuleDefineName);
    if(foundDefine == entry.defineValues.end())
        return true;

    DependencyPathScratchSet seenDependencies(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);
    seenDependencies.reserve(inOutDependencies.size());
    // Dependency collection already produces normalized absolute paths; retain its first-seen ordering.
    for(const Path& dependency : inOutDependencies){
        ScratchString canonicalPath = PathToString(scratchArena, dependency);
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

    Vector<::Path<ScratchArena>, ScratchArena> moduleDependencies(scratchArena);
    for(const ShaderCook::CookString& defineValue : foundDefine.value().values){
        const AStringView includeName = UnquoteDoubleQuotedView(AStringView(defineValue));
        if(includeName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: CSG evaluator module define value '{}' must be a quoted include path"), ConvertString(defineValue));
            return false;
        }

        const auto modulePath = ResolveProjectEvaluatorModuleIncludePath(includeName, includeDirectories, scratchArena);
        if(!modulePath)
            return false;

        moduleDependencies.clear();
        if(!ShaderSourceDependencies::Gather(*modulePath, includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, scratchArena, moduleDependencies))
            return false;
        for(const ::Path<ScratchArena>& dependency : moduleDependencies){
            if(!AppendUniqueDependency(dependency, scratchArena, seenDependencies, inOutDependencies))
                return false;
        }
    }

    return true;
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
    ShaderCook& shaderCook,
    const Span<ShaderCook::ShaderEntry> inOutShaderEntries
){

    PreparedShaderPlan plan(cookArena);
    // One shader entry can expand to itself and its mesh-compute shadow entry.
    if(inOutShaderEntries.size() > Limit<usize>::s_Max / s_MaxPreparedEntriesPerShaderEntry){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: prepared shader entry reserve count overflows"));
        return MakeUnexpected(Failure{});
    }
    plan.preparedEntries.reserve(inOutShaderEntries.size() * s_MaxPreparedEntriesPerShaderEntry);
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
        if(implicitIncludeRootCount > Limit<usize>::s_Max - resolvedPaths.assetRoots.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: implicit shader include root count overflows"));
            return MakeUnexpected(Failure{});
        }
        implicitIncludeRootCount += resolvedPaths.assetRoots.size();
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

        auto includeDirectories = __hidden_shader_cook_plan::BuildIncludeDirectories(resolvedPaths.repoRoot, resolvedPaths.assetRoots, implicitIncludeRoots, preparedEntry.entry, preparedEntry.includeDirectories.get_allocator().arena(), scratchArena);
        if(!includeDirectories)
            return MakeUnexpected(Failure{});
        preparedEntry.includeDirectories = Move(*includeDirectories);
        if(!ShaderSourceDependencies::Gather(preparedEntry.sourcePath, preparedEntry.includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, scratchArena, preparedEntry.dependencies))
            return MakeUnexpected(Failure{});

        shaderCook.mergeInheritedDefines(preparedEntry.dependencies, includeMetadata, scratchArena, preparedEntry.entry);
        const usize initialDependencyCount = preparedEntry.dependencies.size();
        if(!__hidden_shader_cook_plan::AppendCsgProjectEvaluatorModuleDependencies(preparedEntry.entry, preparedEntry.includeDirectories, scratchArena, preparedEntry.dependencies))
            return MakeUnexpected(Failure{});
        if(preparedEntry.dependencies.size() != initialDependencyCount)
            shaderCook.mergeInheritedDefines(preparedEntry.dependencies, includeMetadata, scratchArena, preparedEntry.entry);
        if(!ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry.name, s_MaterialBindTypedBindingImplicitDefineText, preparedEntry.entry.defineValues.find(s_MaterialBindTypedBindingImplicitDefineText) != preparedEntry.entry.defineValues.end()))
            return MakeUnexpected(Failure{});
        if(!ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry.name, s_CsgClipImplicitDefineName, preparedEntry.entry.defineValues.find(s_CsgClipImplicitDefineName) != preparedEntry.entry.defineValues.end()))
            return MakeUnexpected(Failure{});
        if(!ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry.name, s_CsgIntervalSampleEnabledImplicitDefineName, preparedEntry.entry.defineValues.find(s_CsgIntervalSampleEnabledImplicitDefineName) != preparedEntry.entry.defineValues.end()))
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

