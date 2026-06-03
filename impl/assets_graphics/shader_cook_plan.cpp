// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook_plan.h"

#include "cook_paths.h"
#include "csg_shader_variants.h"

#include <impl/assets_material/cook.h>
#include <impl/assets_material/shader_stage_names.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook_plan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsGraphicsCookDetail;

static constexpr AStringView s_EnabledImplicitDefineValue = "1";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateShaderDoesNotUseImplicitDefine(ShaderCook::ShaderEntry& entry, const AStringView defineName){
    ShaderCook::CookArena& arena = entry.name.get_allocator().arena();
    CookString defineNameKey(defineName, arena);
    if(entry.defineValues.find(defineNameKey) == entry.defineValues.end())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: shader '{}' uses reserved implicit define '{}' as a variant define")
        , StringConvert(entry.name)
        , StringConvert(defineName)
    );
    return false;
}

static bool SetShaderImplicitDefine(
    ShaderCook::ShaderEntry& entry,
    const AStringView defineName,
    const AStringView defineValue
){
    if(!ValidateShaderDoesNotUseImplicitDefine(entry, defineName))
        return false;

    ShaderCook::CookArena& arena = entry.name.get_allocator().arena();
    CookString defineNameKey(defineName, arena);
    entry.implicitDefines.insert_or_assign(Move(defineNameKey), CookString(defineValue, arena));
    return true;
}

static bool BuildMeshComputeShadowEntry(const ShaderCook::ShaderEntry& sourceEntry, ShaderCook::ShaderEntry& outEntry){
    outEntry = sourceEntry;
    if(!outEntry.archiveStage.assign(MaterialShaderStageNames::MeshComputeArchiveStageText()))
        return false;
    if(!outEntry.stage.assign("cs"))
        return false;
    if(!outEntry.targetProfile.assign(sourceEntry.targetProfile))
        return false;

    return SetShaderImplicitDefine(
        outEntry,
        MaterialShaderStageNames::MeshComputeImplicitDefineText(),
        s_EnabledImplicitDefineValue
    );
}

static bool CountShaderVariants(const ShaderCook::ShaderEntry& entry, u64& outVariantCount){
    outVariantCount = 1;

    for(const auto& [defineName, defineEntry] : entry.defineValues){
        const u64 valueCount = static_cast<u64>(defineEntry.values.size());
        if(valueCount == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: entry '{}' has define '{}' with no values")
                , StringConvert(entry.name)
                , StringConvert(defineName)
            );
            return false;
        }
        if(outVariantCount > Limit<u64>::s_Max / valueCount){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: variant count overflow for entry '{}'"), StringConvert(entry.name));
            return false;
        }
        outVariantCount *= valueCount;
    }

    return true;
}

static AStringView UnquoteProjectEvaluatorModuleInclude(const AStringView defineValue){
    if(defineValue.size() < 2u || defineValue.front() != '"' || defineValue.back() != '"')
        return AStringView{};

    return defineValue.substr(1u, defineValue.size() - 2u);
}

static bool ResolveProjectEvaluatorModuleIncludePath(
    const AStringView includeName,
    const ShaderCook::CookVector<Path>& includeDirectories,
    Path& outPath,
    ScratchArena& scratchArena
){
    outPath.clear();
    if(includeName.empty())
        return false;

    ErrorCode errorCode;
    ScratchString includeText(includeName, scratchArena);
    const Path includePath(includeText.c_str());
    if(includePath.is_absolute()){
        errorCode.clear();
        if(IsRegularFile(includePath, errorCode)){
            outPath = includePath.lexically_normal();
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to query CSG evaluator module include '{}': {}")
                , PathToString<tchar>(includePath)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path candidate = (includeDirectory / includePath).lexically_normal();
        errorCode.clear();
        if(IsRegularFile(candidate, errorCode)){
            outPath = candidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to query CSG evaluator module include '{}': {}")
                , PathToString<tchar>(candidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve CSG evaluator module include '{}'"), StringConvert(includeName));
    return false;
}

static bool SameDependencyPath(const Path& lhs, const Path& rhs, ScratchArena& scratchArena){
    ErrorCode errorCode;
    const Path lhsAbsolute = AbsolutePath(lhs, errorCode).lexically_normal();
    if(errorCode)
        return false;
    const Path rhsAbsolute = AbsolutePath(rhs, errorCode).lexically_normal();
    if(errorCode)
        return false;

    ScratchString lhsText = PathToString(scratchArena, lhsAbsolute);
    ScratchString rhsText = PathToString(scratchArena, rhsAbsolute);
    CanonicalizeTextInPlace(lhsText);
    CanonicalizeTextInPlace(rhsText);
    return lhsText == rhsText;
}

static bool AppendUniqueDependency(
    ShaderCook::CookVector<Path>& inOutDependencies,
    const Path& dependency,
    ScratchArena& scratchArena
){
    for(const Path& existingDependency : inOutDependencies){
        if(SameDependencyPath(existingDependency, dependency, scratchArena))
            return true;
    }

    ErrorCode errorCode;
    Path absoluteDependency = AbsolutePath(dependency, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to resolve CSG evaluator module dependency '{}': {}")
            , PathToString<tchar>(dependency)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    inOutDependencies.push_back(Move(absoluteDependency));
    return true;
}

static bool AppendCsgProjectEvaluatorModuleDependencies(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ShaderCook::ShaderEntry& entry,
    const ShaderCook::CookVector<Path>& includeDirectories,
    ShaderCook::CookVector<Path>& inOutDependencies,
    ScratchArena& scratchArena
){
    ShaderCook::CookString defineName(AssetsGraphicsCsgShaderVariants::s_ProjectEvaluatorModuleDefineName, cookArena);
    const auto foundDefine = entry.defineValues.find(defineName);
    if(foundDefine == entry.defineValues.end())
        return true;

    ShaderCook::CookVector<Path> moduleDependencies(cookArena);
    for(const ShaderCook::CookString& defineValue : foundDefine.value().values){
        const AStringView includeName = UnquoteProjectEvaluatorModuleInclude(AStringView(defineValue));
        if(includeName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: CSG evaluator module define value '{}' must be a quoted include path")
                , StringConvert(defineValue)
            );
            return false;
        }

        Path modulePath;
        if(!ResolveProjectEvaluatorModuleIncludePath(includeName, includeDirectories, modulePath, scratchArena))
            return false;

        moduleDependencies.clear();
        if(!shaderCook.gatherShaderDependencies(modulePath, includeDirectories, moduleDependencies, scratchArena))
            return false;
        for(const Path& dependency : moduleDependencies){
            if(!AppendUniqueDependency(inOutDependencies, dependency, scratchArena))
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


bool AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PrepareShaderEntriesForCook(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    ShaderEntryVector& inOutShaderEntries,
    const MaterialCookEntryVector& materialEntries,
    PreparedShaderPlan& outPreparedPlan,
    ScratchArena& scratchArena
){
    ErrorCode errorCode;

    outPreparedPlan.preparedEntries.clear();
    if(inOutShaderEntries.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: prepared shader entry reserve count overflows"));
        return false;
    }
    outPreparedPlan.preparedEntries.reserve(inOutShaderEntries.size() * 2u);
    outPreparedPlan.plannedFileCount = 1; // shader archive index

    AssetsGraphicsCsgShaderVariants::ShaderStageKeySet materialClipShaderKeys{
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        scratchArena
    };
    AssetsGraphicsCsgShaderVariants::ShaderStageKeySet avboitClipShaderKeys{
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        scratchArena
    };
    AssetsGraphicsCsgShaderVariants::CollectMaterialClipShaderKeys(materialEntries, materialClipShaderKeys);
    AssetsGraphicsCsgShaderVariants::CollectAvboitClipShaderKeys(materialEntries, avboitClipShaderKeys);

    for(ShaderCook::ShaderEntry& entry : inOutShaderEntries){
        PreparedShaderEntry preparedEntry(cookArena);
        preparedEntry.entry = Move(entry);

        errorCode.clear();
        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, preparedEntry.entry.source, preparedEntry.sourcePath, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve source path '{}' for entry '{}': {}")
                    , StringConvert(preparedEntry.entry.source)
                    , StringConvert(preparedEntry.entry.name)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve source path '{}' for entry '{}': path is empty or invalid")
                    , StringConvert(preparedEntry.entry.source)
                    , StringConvert(preparedEntry.entry.name)
                );
            }
            return false;
        }

        errorCode.clear();
        const bool sourceExists = FileExists(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query source path '{}' for entry '{}': {}")
                , PathToString<tchar>(preparedEntry.sourcePath)
                , StringConvert(preparedEntry.entry.name)
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!sourceExists){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source does not exist for entry '{}': '{}'")
                , StringConvert(preparedEntry.entry.name)
                , PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        errorCode.clear();
        const bool isRegularSourceFile = IsRegularFile(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to inspect source path '{}' for entry '{}': {}")
                , PathToString<tchar>(preparedEntry.sourcePath)
                , StringConvert(preparedEntry.entry.name)
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isRegularSourceFile){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader source is not a regular file for entry '{}': '{}'")
                , StringConvert(preparedEntry.entry.name)
                , PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        if(!BuildIncludeDirectories(
            resolvedPaths.repoRoot,
            resolvedPaths.assetRoots,
            materialBindIncludeRoot,
            preparedEntry.entry,
            preparedEntry.includeDirectories,
            scratchArena
        ))
            return false;
        if(!shaderCook.gatherShaderDependencies(
            preparedEntry.sourcePath,
            preparedEntry.includeDirectories,
            preparedEntry.dependencies,
            scratchArena
        ))
            return false;

        shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata);
        if(!__hidden_shader_cook_plan::AppendCsgProjectEvaluatorModuleDependencies(
            cookArena,
            shaderCook,
            preparedEntry.entry,
            preparedEntry.includeDirectories,
            preparedEntry.dependencies,
            scratchArena
        ))
            return false;
        shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata);
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, MaterialBindNames::TypedBindingImplicitDefineText()))
            return false;
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, AssetsGraphicsCsgShaderVariants::s_ClipImplicitDefineName))
            return false;
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, AssetsGraphicsCsgShaderVariants::s_ClipSetImplicitDefineName))
            return false;
        if(!__hidden_shader_cook_plan::ValidateShaderDoesNotUseImplicitDefine(preparedEntry.entry, AssetsGraphicsCsgShaderVariants::s_OpeningMaskWriteDefineName))
            return false;

        CookString materialTypedBindingInterfaceText{cookArena};
        if(!ResolveMaterialBindDependencyInterface(
            AStringView(preparedEntry.entry.name),
            materialBindIncludeRoot,
            preparedEntry.dependencies,
            materialTypedBindingInterfaceText,
            preparedEntry.materialTypedBindingInterface,
            scratchArena
        ))
            return false;
        preparedEntry.usesMaterialTypedBinding = preparedEntry.entry.archiveStage.view() == "mesh" && preparedEntry.materialTypedBindingInterface;
        preparedEntry.materialTypedBindingInterfacePath = Move(materialTypedBindingInterfaceText);
        if(preparedEntry.usesMaterialTypedBinding && !__hidden_shader_cook_plan::SetShaderImplicitDefine(
            preparedEntry.entry,
            MaterialBindNames::TypedBindingImplicitDefineText(),
            MaterialBindNames::TypedBindingImplicitDefineValueText()
        ))
            return false;
        if(!shaderCook.computeDependencyChecksum(
            preparedEntry.dependencies,
            {
                { resolvedPaths.repoRoot, "repo" },
                { materialBindIncludeRoot, MaterialBindNames::GeneratedIncludeCacheDirectoryText() }
            },
            preparedEntry.dependencyChecksum,
            scratchArena
        ))
            return false;
        preparedEntry.supportsCsgClipVariant = AssetsGraphicsCsgShaderVariants::SupportsClipVariant(materialClipShaderKeys, preparedEntry.entry);
        preparedEntry.supportsAvboitCsgClipVariant = AssetsGraphicsCsgShaderVariants::SupportsClipVariant(avboitClipShaderKeys, preparedEntry.entry);
        if(!__hidden_shader_cook_plan::CountShaderVariants(preparedEntry.entry, preparedEntry.variantCount))
            return false;
        const u64 baseVariantCount = preparedEntry.variantCount;
        if(preparedEntry.supportsCsgClipVariant && !AssetsGraphicsCsgShaderVariants::AddClipVariantCount(preparedEntry.entry, baseVariantCount, preparedEntry.variantCount))
            return false;
        if(preparedEntry.supportsAvboitCsgClipVariant && !AssetsGraphicsCsgShaderVariants::AddClipVariantCount(preparedEntry.entry, baseVariantCount, preparedEntry.variantCount))
            return false;
        if(!AddPlannedFileCount(preparedEntry.variantCount, outPreparedPlan.plannedFileCount))
            return false;

        const bool emitMeshComputeShadow = preparedEntry.entry.archiveStage.view() == "mesh";
        outPreparedPlan.preparedEntries.push_back(Move(preparedEntry));

        if(!emitMeshComputeShadow)
            continue;

        const PreparedShaderEntry& meshShaderEntry = outPreparedPlan.preparedEntries.back();
        PreparedShaderEntry meshComputeShadowEntry(cookArena);
        if(!__hidden_shader_cook_plan::BuildMeshComputeShadowEntry(meshShaderEntry.entry, meshComputeShadowEntry.entry)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsAssetCooker: failed to build mesh-compute shadow entry for '{}'")
                , StringConvert(meshShaderEntry.entry.name)
            );
            return false;
        }

        meshComputeShadowEntry.sourcePath = meshShaderEntry.sourcePath;
        meshComputeShadowEntry.includeDirectories = meshShaderEntry.includeDirectories;
        meshComputeShadowEntry.dependencies = meshShaderEntry.dependencies;
        meshComputeShadowEntry.dependencyChecksum = meshShaderEntry.dependencyChecksum;
        meshComputeShadowEntry.variantCount = meshShaderEntry.variantCount;
        meshComputeShadowEntry.supportsCsgClipVariant = meshShaderEntry.supportsCsgClipVariant;
        meshComputeShadowEntry.supportsAvboitCsgClipVariant = meshShaderEntry.supportsAvboitCsgClipVariant;
        meshComputeShadowEntry.materialTypedBindingInterfacePath = meshShaderEntry.materialTypedBindingInterfacePath;
        meshComputeShadowEntry.materialTypedBindingInterface = meshShaderEntry.materialTypedBindingInterface;
        meshComputeShadowEntry.usesMaterialTypedBinding = meshShaderEntry.usesMaterialTypedBinding;

        if(!AddPlannedFileCount(meshComputeShadowEntry.variantCount, outPreparedPlan.plannedFileCount))
            return false;

        outPreparedPlan.preparedEntries.push_back(Move(meshComputeShadowEntry));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

