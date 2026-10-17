// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"

#include "csg_shader_variants.h"
#include "mesh_object_shader_plan.h"

#include <impl/assets_material/cook.h>
#include <impl/assets_material/shader_stage_names.h>
#include <impl/assets_shader/source_dependencies.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/graphics/shader_stage_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook_plan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;

static constexpr AStringView s_EnabledImplicitDefineValue = "1";
using IncludeDirectoryScratchSet = HashSet<ScratchString, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;
using DependencyPathScratchSet = HashSet<ScratchString, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendIncludeDirectory(const Path& includeDirectory, const ShaderCook::ShaderEntry& entry, IncludeDirectoryScratchSet& seenIncludeDirectories, CookVector<Path>& outIncludeDirectories, ScratchArena& scratchArena){
    const auto isDirectoryResult = IsDirectory(includeDirectory);
    if(!isDirectoryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to query include root '{}' for entry '{}': {}"), PathToString<tchar>(includeDirectory), StringConvert(entry.name), StringConvert(isDirectoryResult.error().message()));
        return false;
    }
    const bool isDirectory = *isDirectoryResult;
    if(!isDirectory){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: include root is not a directory for entry '{}': '{}'"), StringConvert(entry.name), PathToString<tchar>(includeDirectory));
        return false;
    }

    const ::Path<ScratchArena> normalizedIncludeDirectoryPath = ::Path<ScratchArena>(scratchArena, includeDirectory).lexicallyNormal();
    ScratchString normalizedIncludeDirectory = PathToString(scratchArena, normalizedIncludeDirectoryPath);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(normalizedIncludeDirectory);
#endif
    if(!seenIncludeDirectories.insert(Move(normalizedIncludeDirectory)).second)
        return true;

    outIncludeDirectories.push_back(includeDirectory);
    return true;
}

static Expected<CookVector<Path>> BuildIncludeDirectories(
    const Path& repoRoot,
    const CookVector<Core::Assets::ResolvedAssetRoot>& assetRoots,
    const Span<const NotNull<const Path*>> implicitIncludeRoots,
    const ShaderCook::ShaderEntry& entry,
    ShaderCook::CookArena& arena,
    ScratchArena& scratchArena
){
    IncludeDirectoryScratchSet seenIncludeDirectories(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);

    CookVector<Path> includeDirectories(arena);
    if(entry.includeRoots.size() > Limit<usize>::s_Max - implicitIncludeRoots.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: include root count overflow for entry '{}'"), StringConvert(entry.name));
        return MakeUnexpected(Failure{});
    }

    includeDirectories.reserve(entry.includeRoots.size() + implicitIncludeRoots.size());
    seenIncludeDirectories.reserve(entry.includeRoots.size() + implicitIncludeRoots.size());

    for(const NotNull<const Path*> implicitIncludeRoot : implicitIncludeRoots){
        if(!AppendIncludeDirectory(*implicitIncludeRoot, entry, seenIncludeDirectories, includeDirectories, scratchArena))
            return MakeUnexpected(Failure{});
    }

    for(const CookString& includeRoot : entry.includeRoots){
        auto includeDirectory = Core::Assets::ResolveVirtualAssetPath(includeDirectories.get_allocator().arena(), assetRoots, includeRoot, scratchArena);
        if(!includeDirectory){
            if(Core::Assets::HasReservedAssetVirtualRoot(includeRoot, scratchArena)){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve virtual include root '{}' for entry '{}'"), StringConvert(includeRoot), StringConvert(entry.name));
                return MakeUnexpected(Failure{});
            }
            const auto resolved = ResolveAbsolutePath(includeDirectories.get_allocator().arena(), repoRoot, AStringView(includeRoot));
            if(!resolved){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve include root '{}' for entry '{}': {}"), StringConvert(includeRoot), StringConvert(entry.name), StringConvert(resolved.error().message()));
                return MakeUnexpected(Failure{});
            }
            includeDirectory = *resolved;
        }

        if(!AppendIncludeDirectory(*includeDirectory, entry, seenIncludeDirectories, includeDirectories, scratchArena))
            return MakeUnexpected(Failure{});
    }

    return includeDirectories;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateShaderDoesNotUseImplicitDefine(ShaderCook::ShaderEntry& entry, const AStringView defineName){
    if(entry.defineValues.find(defineName) == entry.defineValues.end())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: shader '{}' uses reserved implicit define '{}' as a variant define"), StringConvert(entry.name), StringConvert(defineName));
    return false;
}

static bool SetShaderImplicitDefine(ShaderCook::ShaderEntry& entry, const AStringView defineName, const AStringView defineValue){
    if(!ValidateShaderDoesNotUseImplicitDefine(entry, defineName))
        return false;

    ShaderCook::CookArena& arena = entry.name.get_allocator().arena();
    CookString defineNameKey(defineName, arena);
    entry.implicitDefines.insert_or_assign(Move(defineNameKey), CookString(defineValue, arena));
    return true;
}

static Expected<ShaderCook::ShaderEntry> BuildMeshComputeShadowEntry(const ShaderCook::ShaderEntry& sourceEntry){
    ShaderCook::ShaderEntry entry = sourceEntry;
    if(!entry.archiveStage.assign(MaterialShaderStageNames::s_MeshComputeArchiveStageText))
        return MakeUnexpected(Failure{});
    if(!entry.stage.assign(Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ComputeStage)))
        return MakeUnexpected(Failure{});

    if(!SetShaderImplicitDefine(entry, MaterialShaderStageNames::s_MeshComputeImplicitDefineText, s_EnabledImplicitDefineValue))
        return MakeUnexpected(Failure{});
    return entry;
}

static Expected<u64> CountShaderVariants(const ShaderCook::ShaderEntry& entry){
    u64 variantCount = 1u;

    for(const auto& [defineName, defineEntry] : entry.defineValues){
        const u64 valueCount = static_cast<u64>(defineEntry.values.size());
        if(valueCount == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: entry '{}' has define '{}' with no values"), StringConvert(entry.name), StringConvert(defineName));
            return MakeUnexpected(Failure{});
        }
        if(variantCount > Limit<u64>::s_Max / valueCount){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: variant count overflow for entry '{}'"), StringConvert(entry.name));
            return MakeUnexpected(Failure{});
        }
        variantCount *= valueCount;
    }

    return variantCount;
}

static Expected<::Path<ScratchArena>> ResolveProjectEvaluatorModuleIncludePath(const AStringView includeName, const ShaderCook::CookVector<Path>& includeDirectories, ScratchArena& scratchArena){
    if(includeName.empty())
        return MakeUnexpected(Failure{});

    const ::Path<ScratchArena> includePath(scratchArena, includeName);
    if(includePath.isAbsolute()){
        const auto includePathQueryResult = IsRegularFile(includePath);
        if(includePathQueryResult && *includePathQueryResult){
            return includePath.lexicallyNormal();
        }
        if(!includePathQueryResult && !IsMissingPathError(includePathQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to query CSG evaluator module include '{}': {}"), PathToString<tchar>(includePath), StringConvert(includePathQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const ::Path<ScratchArena> candidate = (::Path<ScratchArena>(scratchArena, includeDirectory) / includePath).lexicallyNormal();
        const auto candidateQueryResult = IsRegularFile(candidate);
        if(candidateQueryResult && *candidateQueryResult){
            return candidate;
        }
        if(!candidateQueryResult && !IsMissingPathError(candidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to query CSG evaluator module include '{}': {}"), PathToString<tchar>(candidate), StringConvert(candidateQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve CSG evaluator module include '{}'"), StringConvert(includeName));
    return MakeUnexpected(Failure{});
}

static bool AppendUniqueDependency(ShaderCook::CookVector<Path>& inOutDependencies, const ::Path<ScratchArena>& dependency, DependencyPathScratchSet& seenDependencies, ScratchArena& scratchArena){
    auto absoluteDependencyResult = AbsolutePath(dependency);
    if(!absoluteDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve CSG evaluator module dependency '{}': {}"), PathToString<tchar>(dependency), StringConvert(absoluteDependencyResult.error().message()));
        return false;
    }
    const ::Path<ScratchArena> absoluteDependency = absoluteDependencyResult->lexicallyNormal();
    ScratchString canonicalPath = PathToString(scratchArena, absoluteDependency);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(canonicalPath);
#endif
    if(seenDependencies.insert(Move(canonicalPath)).second)
        inOutDependencies.emplace_back(inOutDependencies.get_allocator().arena(), absoluteDependency);
    return true;
}

static bool AppendCsgProjectEvaluatorModuleDependencies(const ShaderCook::ShaderEntry& entry, const ShaderCook::CookVector<Path>& includeDirectories, ShaderCook::CookVector<Path>& inOutDependencies, ScratchArena& scratchArena){
    const auto foundDefine = entry.defineValues.find(AssetsGraphicsCsgShaderVariants::s_ProjectEvaluatorModuleDefineName);
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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: CSG evaluator module define value '{}' must be a quoted include path"), StringConvert(defineValue));
            return false;
        }

        const auto modulePath = ResolveProjectEvaluatorModuleIncludePath(includeName, includeDirectories, scratchArena);
        if(!modulePath)
            return false;

        moduleDependencies.clear();
        if(!ShaderSourceDependencies::Gather(*modulePath, includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, moduleDependencies, scratchArena))
            return false;
        for(const ::Path<ScratchArena>& dependency : moduleDependencies){
            if(!AppendUniqueDependency(inOutDependencies, dependency, seenDependencies, scratchArena))
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
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const Path& csgShapeIncludeRoot,
    const Path& deferredBxdfIncludeRoot,
    const Path& shadowSurfaceIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    ShaderEntryVector& inOutShaderEntries,
    const ShaderCook::CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){

    PreparedShaderPlan plan(cookArena);
    if(inOutShaderEntries.size() > Limit<usize>::s_Max / 3u){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: prepared shader entry reserve count overflows"));
        return MakeUnexpected(Failure{});
    }
    plan.preparedEntries.reserve(inOutShaderEntries.size() * 3u);
    plan.plannedFileCount = 1; // shader archive index

    AssetsGraphicsCsgShaderVariants::ShaderStageKeySet materialClipShaderKeys{ 0, ShaderStageKeyHasher(), EqualTo<ShaderStageKey>(), scratchArena };
    AssetsGraphicsCsgShaderVariants::ShaderStageKeySet avboitClipShaderKeys{ 0, ShaderStageKeyHasher(), EqualTo<ShaderStageKey>(), scratchArena };
    AssetsGraphicsCsgShaderVariants::CollectMaterialClipShaderKeys(materialEntries, materialClipShaderKeys);
    const auto avboitKeys = AssetsGraphicsCsgShaderVariants::CollectAvboitClipShaderKeys(materialEntries, avboitClipShaderKeys);
    if(!avboitKeys){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid generated AVBOIT shader identity (failure {})"), static_cast<u32>(avboitKeys.error()));
        return MakeUnexpected(Failure{});
    }

    Vector<NotNull<const Path*>, ScratchArena> implicitIncludeRoots(scratchArena);
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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: implicit shader include root count overflows"));
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

    for(ShaderCook::ShaderEntry& entry : inOutShaderEntries){
        PreparedShaderEntry preparedEntry(cookArena);
        preparedEntry.entry = Move(entry);

        const auto sourcePath = ResolveAbsolutePath(cookArena, resolvedPaths.repoRoot, AStringView(preparedEntry.entry.source));
        if(!sourcePath){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve source path '{}' for entry '{}': {}"), StringConvert(preparedEntry.entry.source), StringConvert(preparedEntry.entry.name), StringConvert(sourcePath.error().message()));
            return MakeUnexpected(Failure{});
        }
        preparedEntry.sourcePath = *sourcePath;

        const auto sourceExistsResult = FileExists(preparedEntry.sourcePath);
        if(!sourceExistsResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query source path '{}' for entry '{}': {}"), PathToString<tchar>(preparedEntry.sourcePath), StringConvert(preparedEntry.entry.name), StringConvert(sourceExistsResult.error().message()));
            return MakeUnexpected(Failure{});
        }
        const bool sourceExists = *sourceExistsResult;
        if(!sourceExists){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source does not exist for entry '{}': '{}'"), StringConvert(preparedEntry.entry.name), PathToString<tchar>(preparedEntry.sourcePath));
            return MakeUnexpected(Failure{});
        }

        const auto isRegularSourceFileResult = IsRegularFile(preparedEntry.sourcePath);
        if(!isRegularSourceFileResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to inspect source path '{}' for entry '{}': {}"), PathToString<tchar>(preparedEntry.sourcePath), StringConvert(preparedEntry.entry.name), StringConvert(isRegularSourceFileResult.error().message()));
            return MakeUnexpected(Failure{});
        }
        const bool isRegularSourceFile = *isRegularSourceFileResult;
        if(!isRegularSourceFile){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source is not a regular file for entry '{}': '{}'"), StringConvert(preparedEntry.entry.name), PathToString<tchar>(preparedEntry.sourcePath));
            return MakeUnexpected(Failure{});
        }

        auto includeDirectories = __hidden_shader_cook_plan::BuildIncludeDirectories(resolvedPaths.repoRoot, resolvedPaths.assetRoots, implicitIncludeRoots, preparedEntry.entry, preparedEntry.includeDirectories.get_allocator().arena(), scratchArena);
        if(!includeDirectories)
            return MakeUnexpected(Failure{});
        preparedEntry.includeDirectories = Move(*includeDirectories);
        if(!ShaderSourceDependencies::Gather(preparedEntry.sourcePath, preparedEntry.includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, preparedEntry.dependencies, scratchArena))
            return MakeUnexpected(Failure{});

        shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata, scratchArena);
        const usize initialDependencyCount = preparedEntry.dependencies.size();
        if(!__hidden_shader_cook_plan::AppendCsgProjectEvaluatorModuleDependencies(preparedEntry.entry, preparedEntry.includeDirectories, preparedEntry.dependencies, scratchArena))
            return MakeUnexpected(Failure{});
        if(preparedEntry.dependencies.size() != initialDependencyCount)
            shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata, scratchArena);
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, MaterialBindNames::TypedBindingImplicitDefineText()))
            return MakeUnexpected(Failure{});
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, AssetsGraphicsCsgShaderVariants::s_ClipImplicitDefineName))
            return MakeUnexpected(Failure{});
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, AssetsGraphicsCsgShaderVariants::s_IntervalSampleEnabledImplicitDefineName))
            return MakeUnexpected(Failure{});

        auto materialDependency = ResolveMaterialBindDependencyInterface(cookArena, AStringView(preparedEntry.entry.name), materialBindIncludeRoot, preparedEntry.dependencies, scratchArena);
        if(!materialDependency)
            return MakeUnexpected(Failure{});
        preparedEntry.materialTypedBindingInterface = materialDependency->interfaceName;
        // Pixel/compute/rgen stages that read the typed .bind (surface hook, incl. the shadow dispatch) receive it when depending on a material interface; the generic mesh shader stays interface-free.
        const AStringView preparedEntryArchiveStage = preparedEntry.entry.archiveStage.view();
        const bool preparedEntryStageReadsTypedMaterial =
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
        if(preparedEntry.usesMaterialTypedBinding && !__hidden_shader_cook_plan::SetShaderImplicitDefine(preparedEntry.entry, MaterialBindNames::TypedBindingImplicitDefineText(), MaterialBindNames::TypedBindingImplicitDefineValueText()))
            return MakeUnexpected(Failure{});
        const auto dependency = shaderCook.computeDependencyChecksum(
            preparedEntry.dependencies,
            preparedEntry.includeDirectories,
            {
                { resolvedPaths.repoRoot, "repo" },
                { resolvedPaths.cacheDirectory, "generated_cache" },
                { materialBindIncludeRoot, MaterialBindNames::GeneratedIncludeCacheDirectoryText() },
                { csgShapeIncludeRoot, "csg_modules" }
            },
            scratchArena
        );
        if(!dependency)
            return MakeUnexpected(Failure{});
        preparedEntry.dependencyChecksum = dependency->checksum;
        preparedEntry.compilerInputsHaveBom = dependency->compilerInputsHaveBom;
        const auto supportsCsgClipVariant = AssetsGraphicsCsgShaderVariants::SupportsClipVariant(materialClipShaderKeys, preparedEntry.entry);
        const auto supportsAvboitCsgClipVariant = AssetsGraphicsCsgShaderVariants::SupportsClipVariant(avboitClipShaderKeys, preparedEntry.entry);
        if(!supportsCsgClipVariant){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid CSG clip shader identity for entry '{}' (failure {})"), StringConvert(preparedEntry.entry.name), static_cast<u32>(supportsCsgClipVariant.error()));
            return MakeUnexpected(Failure{});
        }
        if(!supportsAvboitCsgClipVariant){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: invalid AVBOIT clip shader identity for entry '{}' (failure {})"), StringConvert(preparedEntry.entry.name), static_cast<u32>(supportsAvboitCsgClipVariant.error()));
            return MakeUnexpected(Failure{});
        }
        preparedEntry.supportsCsgClipVariant = *supportsCsgClipVariant;
        preparedEntry.supportsAvboitCsgClipVariant = *supportsAvboitCsgClipVariant;
        const auto variantCount = __hidden_shader_cook_plan::CountShaderVariants(preparedEntry.entry);
        if(!variantCount)
            return MakeUnexpected(Failure{});
        preparedEntry.variantCount = *variantCount;
        const u64 baseVariantCount = preparedEntry.variantCount;
        if((preparedEntry.supportsCsgClipVariant || preparedEntry.supportsAvboitCsgClipVariant) && !AssetsGraphicsCsgShaderVariants::AddClipVariantCount(preparedEntry.entry, baseVariantCount, preparedEntry.variantCount))
            return MakeUnexpected(Failure{});
        if(!Core::Assets::AddPlannedFileCount(preparedEntry.variantCount, plan.plannedFileCount))
            return MakeUnexpected(Failure{});

        const bool emitMeshComputeShadow = preparedEntry.entry.archiveStage.view() == Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage) && preparedEntry.entry.emitMeshComputeShadow;
        const usize meshEntryIndex = plan.preparedEntries.size();
        plan.preparedEntries.push_back(Move(preparedEntry));
        if(!AppendMeshObjectShaderEntries(cookArena, shaderCook, resolvedPaths, plan.preparedEntries[meshEntryIndex], plan, scratchArena))
            return MakeUnexpected(Failure{});

        if(!emitMeshComputeShadow)
            continue;

        const PreparedShaderEntry& meshShaderEntry = plan.preparedEntries[meshEntryIndex];
        PreparedShaderEntry meshComputeShadowEntry(cookArena);
        auto shadowEntry = __hidden_shader_cook_plan::BuildMeshComputeShadowEntry(meshShaderEntry.entry);
        if(!shadowEntry){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to build mesh-compute shadow entry for '{}'"), StringConvert(meshShaderEntry.entry.name));
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

