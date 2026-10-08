// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"

#include <impl/assets_material/shader_stage_names.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/hash_utils.h>

namespace __hidden_shader_cook_checksum{
static constexpr StringView s_NoneOptName = "none";
static constexpr StringView s_DefaultOptName = "default";
static constexpr StringView s_HighOptName = "high";
static constexpr StringView s_MaximalOptName = "maximal";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_checksum{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = ShaderCook::CookString;
using ScratchString = AString<Core::Alloc::ScratchArena>;

struct NormalizedDependencyRootAlias{
    Path root;
    CookString key;
    usize depth = 0u;

    explicit NormalizedDependencyRootAlias(ShaderCook::CookArena& arena)
        : root(arena)
        , key(arena)
    {}
};

static usize PathDepth(const Path& path){
    usize depth = 0u;
    for(auto it = path.begin(); it != path.end(); ++it)
        ++depth;
    return depth;
}

static Path NormalizeDependencyRootAliasPath(Path path){
    path = path.lexicallyNormal();
    while(!path.empty() && !path.hasFilename()){
        const Path parentPath = path.parentPath();
        if(parentPath.empty() || parentPath == path)
            break;
        path = parentPath;
    }
    return path;
}

[[nodiscard]] static AStringView ShaderOptimizationLevelText(
    const ShaderOptimizationLevel::Enum optimizationLevel
)noexcept{
    switch(optimizationLevel){
    case ShaderOptimizationLevel::None: return __hidden_shader_cook_checksum::s_NoneOptName;
    case ShaderOptimizationLevel::Default: return __hidden_shader_cook_checksum::s_DefaultOptName;
    case ShaderOptimizationLevel::High: return __hidden_shader_cook_checksum::s_HighOptName;
    case ShaderOptimizationLevel::Maximal: return __hidden_shader_cook_checksum::s_MaximalOptName;
    default: return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderCook::computeDependencyChecksum(
    const CookVector<Path>& dependencies,
    const InitializerList<DependencyRootAlias> dependencyRootAliases,
    u64& outChecksum,
    bool& outCompilerInputsHaveBom,
    Core::Alloc::ScratchArena& scratchArena
){
    ErrorCode errorCode;
    static constexpr u8 s_NewlineByte = '\n';
    static constexpr u8 s_ZeroByte = 0;

    outChecksum = s_Fnv64OffsetBasis;
    outCompilerInputsHaveBom = false;

    if(dependencyRootAliases.size() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires at least one dependency root alias"));
        return false;
    }

    CookVector<__hidden_cook_checksum::NormalizedDependencyRootAlias> normalizedRootAliases{m_memoryArena};
    normalizedRootAliases.reserve(dependencyRootAliases.size());
    for(const DependencyRootAlias& rootAlias : dependencyRootAliases){
        if(rootAlias.root.empty() || rootAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root aliases"));
            return false;
        }

        __hidden_cook_checksum::NormalizedDependencyRootAlias normalizedAlias{m_memoryArena};
        errorCode.clear();
        normalizedAlias.root = __hidden_cook_checksum::NormalizeDependencyRootAliasPath(AbsolutePath(rootAlias.root, errorCode));
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency root alias '{}' : {}")
                , PathToString<tchar>(rootAlias.root)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        normalizedAlias.key = rootAlias.key;
        CanonicalizeTextInPlace(normalizedAlias.key);
        if(normalizedAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root alias keys"));
            return false;
        }
        normalizedAlias.depth = __hidden_cook_checksum::PathDepth(normalizedAlias.root);
        normalizedRootAliases.push_back(Move(normalizedAlias));
    }

    CookVector<SortedDependencyItem> sortedDependencies{m_memoryArena};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item(m_memoryArena);
        errorCode.clear();
        Path normalizedDependency = AbsolutePath(dependency, errorCode).lexicallyNormal();
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}")
                , PathToString<tchar>(dependency)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        const __hidden_cook_checksum::NormalizedDependencyRootAlias* bestRootAlias = nullptr;
        for(const __hidden_cook_checksum::NormalizedDependencyRootAlias& rootAlias : normalizedRootAliases){
            if(normalizedDependency != rootAlias.root && !PathHasDirectoryAncestor(normalizedDependency, rootAlias.root))
                continue;
            if(!bestRootAlias || rootAlias.depth > bestRootAlias->depth)
                bestRootAlias = &rootAlias;
        }
        if(!bestRootAlias){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum path '{}' is outside the declared dependency root aliases")
                , PathToString<tchar>(dependency)
            );
            return false;
        }

        __hidden_cook_checksum::ScratchString relativePathText = PathToString(scratchArena, normalizedDependency.lexicallyRelative(bestRootAlias->root));
        CanonicalizeTextInPlace(relativePathText);

        item.canonicalPath = bestRootAlias->key;
        if(!relativePathText.empty()){
            item.canonicalPath += '/';
            item.canonicalPath += relativePathText;
        }
        item.path = dependency;
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(), [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs){ return lhs.canonicalPath < rhs.canonicalPath; });

    Vector<u8, Core::Alloc::ScratchArena> dependencyBytes{scratchArena};
    for(const SortedDependencyItem& item : sortedDependencies){
        outChecksum = UpdateFnv64TextExact(outChecksum, AStringView(item.canonicalPath));
        outChecksum = UpdateFnv64(outChecksum, &s_NewlineByte, 1);

        dependencyBytes.clear();
        errorCode.clear();
        if(!ReadBinaryFile(item.path, dependencyBytes, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}' : {}")
                    , PathToString<tchar>(item.path)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}'"), PathToString<tchar>(item.path));
            }
            return false;
        }
        if(
            dependencyBytes.size() >= TextDetail::s_Utf8BomByteCount
            && dependencyBytes[0u] == TextDetail::s_Utf8BomByte0
            && dependencyBytes[1u] == TextDetail::s_Utf8BomByte1
            && dependencyBytes[2u] == TextDetail::s_Utf8BomByte2
        )
            outCompilerInputsHaveBom = true;
        if(!dependencyBytes.empty()){
            outChecksum = UpdateFnv64(
                outChecksum,
                dependencyBytes.data(),
                dependencyBytes.size()
            );
        }

        outChecksum = UpdateFnv64(outChecksum, &s_ZeroByte, 1);
    }

    return true;
}

u64 ShaderCook::computeSourceChecksum(
    const ShaderEntry& entry,
    const AStringView variantSignature,
    const u64 dependencyChecksum,
    const u64 compilerFingerprint,
    Core::Alloc::ScratchArena& scratchArena
){
    static constexpr AStringView s_ChecksumVersionTag = "shader-source-v4";
    const u8 newlineByte = '\n';

    u64 checksum = s_Fnv64OffsetBasis;

    const auto appendChecksumLine = [&checksum, &newlineByte](const AStringView text){
        checksum = UpdateFnv64TextExact(checksum, text);
        checksum = UpdateFnv64(checksum, &newlineByte, 1);
    };

    appendChecksumLine(s_ChecksumVersionTag);
    appendChecksumLine(AStringView(entry.name));
    appendChecksumLine(entry.stage.view());
    appendChecksumLine(entry.archiveStage.view());
    appendChecksumLine(entry.rayQuery
        ? MaterialShaderStageNames::s_Spirv15RayQueryTargetProfileText
        : MaterialShaderStageNames::s_Spirv15TargetProfileText
    );
    appendChecksumLine(__hidden_cook_checksum::ShaderOptimizationLevelText(entry.optimizationLevel));
    appendChecksumLine(AStringView(entry.entryPoint));
    appendChecksumLine(variantSignature);
    if(entry.implicitDefines.size() <= 1u){
        for(const auto& [defineName, defineValue] : entry.implicitDefines){
            appendChecksumLine(defineName);
            appendChecksumLine(defineValue);
        }
    }
    else{
        for(const auto& entryDefine : sortedDefineEntries(entry.implicitDefines, scratchArena)){
            appendChecksumLine(*entryDefine.key);
            appendChecksumLine(*entryDefine.value);
        }
    }
    Fnv64AppendValue(checksum, dependencyChecksum);
    Fnv64AppendValue(checksum, compilerFingerprint);
    return checksum;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

