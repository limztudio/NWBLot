// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "slang_compiler.h"

#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook_checksum{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr StringView s_NoneOptName = "none";
static constexpr StringView s_DefaultOptName = "default";
static constexpr StringView s_HighOptName = "high";
static constexpr StringView s_MaximalOptName = "maximal";

using ScratchPath = ::Path<Core::Alloc::ScratchArena>;
using ScratchString = AString<Core::Alloc::ScratchArena>;

struct NormalizedDependencyRootAlias{
    ScratchPath root;
    ScratchString key;
    usize depth = 0u;


public:
    explicit NormalizedDependencyRootAlias(Core::Alloc::ScratchArena& scratchArena)noexcept
        : root(scratchArena)
        , key(scratchArena)
    {}
};

struct SortedDependencyItem{
    ScratchString canonicalPath;
    NotNull<const Path*> path;


public:
    SortedDependencyItem(Core::Alloc::ScratchArena& scratchArena, const Path& dependency)noexcept
        : canonicalPath(scratchArena)
        , path(&dependency)
    {}
};

static usize PathDepth(const ScratchPath& path)noexcept{
    usize depth = 0u;
    for(auto it = path.begin(); it != path.end(); ++it)
        ++depth;
    return depth;
}

static ScratchPath NormalizeDependencyRootAliasPath(const ScratchPath& source){
    ScratchPath path = source.lexicallyNormal();
    while(!path.empty() && !path.hasFilename()){
        const ScratchPath parentPath = path.parentPath();
        if(parentPath.empty() || parentPath == path)
            break;
        path = parentPath;
    }
    return path;
}

static Expected<ScratchString> BuildCanonicalDependencyPath(const Path& dependency, const Span<const NormalizedDependencyRootAlias> normalizedRootAliases, Core::Alloc::ScratchArena& scratchArena, const bool allowUnaliasedPath){
    const auto normalizedDependencyResult = AbsolutePath(ScratchPath(scratchArena, dependency));
    if(!normalizedDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}"), PathToString<tchar>(dependency), StringConvert(normalizedDependencyResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    const ScratchPath normalizedDependency = normalizedDependencyResult->lexicallyNormal();

    const NormalizedDependencyRootAlias* bestRootAlias = nullptr;
    for(const NormalizedDependencyRootAlias& rootAlias : normalizedRootAliases){
        if(normalizedDependency != rootAlias.root && !PathHasDirectoryAncestor(normalizedDependency, rootAlias.root))
            continue;
        if(!bestRootAlias || rootAlias.depth > bestRootAlias->depth)
            bestRootAlias = &rootAlias;
    }
    if(!bestRootAlias){
        if(!allowUnaliasedPath){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum path '{}' is outside the declared dependency root aliases"), PathToString<tchar>(dependency));
            return MakeUnexpected(Failure{});
        }
        const ScratchString absolutePathText = PathToString(scratchArena, normalizedDependency);
        ScratchString canonicalPath("absolute:", scratchArena);
        canonicalPath.reserve(AddSize(canonicalPath.size(), absolutePathText.size()));
        canonicalPath += absolutePathText;
        return canonicalPath;
    }

    const ScratchString relativePathText = PathToString(scratchArena, normalizedDependency.lexicallyRelative(bestRootAlias->root));

    const AStringView pathPrefix = allowUnaliasedPath ? AStringView("alias:") : AStringView();
    ScratchString canonicalPath(scratchArena);
    const usize relativePathSize = relativePathText.empty() ? 0u : AddSize(1u, relativePathText.size());
    canonicalPath.reserve(AddSize(AddSize(pathPrefix.size(), bestRootAlias->key.size()), relativePathSize));
    canonicalPath += pathPrefix;
    canonicalPath += bestRootAlias->key;
    if(!relativePathText.empty()){
        canonicalPath += '/';
        canonicalPath += relativePathText;
    }
    return canonicalPath;
}

[[nodiscard]] static AStringView ShaderOptimizationLevelText(const ShaderOptimizationLevel::Enum optimizationLevel)noexcept{
    switch(optimizationLevel){
    case ShaderOptimizationLevel::None: return s_NoneOptName;
    case ShaderOptimizationLevel::Default: return s_DefaultOptName;
    case ShaderOptimizationLevel::High: return s_HighOptName;
    case ShaderOptimizationLevel::Maximal: return s_MaximalOptName;
    default: return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ShaderCook::DependencyChecksum> ShaderCook::computeDependencyChecksum(
    const CookVector<Path>& dependencies,
    const CookVector<Path>& includeDirectories,
    const InitializerList<DependencyRootAlias> dependencyRootAliases,
    Core::Alloc::ScratchArena& scratchArena
){
    using __hidden_shader_cook_checksum::SortedDependencyItem;

    static constexpr u32 s_DependencyChecksumVersion = 3u;
    static constexpr u8 s_NewlineByte = '\n';
    static constexpr u8 s_ZeroByte = 0;

    DependencyChecksum result;
    result.checksum = s_Fnv64OffsetBasis;

    if(dependencyRootAliases.size() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires at least one dependency root alias"));
        return MakeUnexpected(Failure{});
    }

    Vector<__hidden_shader_cook_checksum::NormalizedDependencyRootAlias, Core::Alloc::ScratchArena> normalizedRootAliases{scratchArena};
    normalizedRootAliases.reserve(dependencyRootAliases.size());
    for(const DependencyRootAlias& rootAlias : dependencyRootAliases){
        if(rootAlias.root.empty() || rootAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root aliases"));
            return MakeUnexpected(Failure{});
        }

        __hidden_shader_cook_checksum::NormalizedDependencyRootAlias normalizedAlias{scratchArena};
        const auto rootPath = AbsolutePath(__hidden_shader_cook_checksum::ScratchPath(scratchArena, rootAlias.root));
        if(!rootPath){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency root alias '{}' : {}"), PathToString<tchar>(rootAlias.root), StringConvert(rootPath.error().message()));
            return MakeUnexpected(Failure{});
        }

        normalizedAlias.root = __hidden_shader_cook_checksum::NormalizeDependencyRootAliasPath(*rootPath);
        normalizedAlias.key = rootAlias.key;
        CanonicalizeTextInPlace(normalizedAlias.key);
        if(normalizedAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root alias keys"));
            return MakeUnexpected(Failure{});
        }
        normalizedAlias.depth = __hidden_shader_cook_checksum::PathDepth(normalizedAlias.root);
        normalizedRootAliases.push_back(Move(normalizedAlias));
    }

    Fnv64AppendValue(result.checksum, s_DependencyChecksumVersion);
    Fnv64AppendValue(result.checksum, includeDirectories.size());
    for(const Path& includeDirectory : includeDirectories){
        const auto canonicalPath = __hidden_shader_cook_checksum::BuildCanonicalDependencyPath(includeDirectory, normalizedRootAliases, scratchArena, true);
        if(!canonicalPath)
            return MakeUnexpected(Failure{});
        Fnv64AppendBuffer(result.checksum, reinterpret_cast<const u8*>(canonicalPath->data()), canonicalPath->size());
    }
    Fnv64AppendValue(result.checksum, dependencies.size());

    Vector<SortedDependencyItem, Core::Alloc::ScratchArena> sortedDependencies{scratchArena};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item(scratchArena, dependency);
        auto canonicalPath = __hidden_shader_cook_checksum::BuildCanonicalDependencyPath(dependency, normalizedRootAliases, scratchArena, false);
        if(!canonicalPath)
            return MakeUnexpected(Failure{});
        item.canonicalPath = Move(*canonicalPath);
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(), [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs)noexcept(noexcept(lhs.canonicalPath < rhs.canonicalPath)){ return lhs.canonicalPath < rhs.canonicalPath; });

    Vector<u8, Core::Alloc::ScratchArena> dependencyBytes{scratchArena};
    for(const SortedDependencyItem& item : sortedDependencies){
        result.checksum = UpdateFnv64TextExact(result.checksum, AStringView(item.canonicalPath));
        result.checksum = UpdateFnv64(result.checksum, &s_NewlineByte, 1);

        dependencyBytes.clear();
        auto readBinaryFileResult = ReadBinaryFile(*item.path, dependencyBytes);
        if(!readBinaryFileResult){
            if(readBinaryFileResult.error()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}' : {}"), PathToString<tchar>(*item.path), StringConvert(readBinaryFileResult.error().message()));
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}'"), PathToString<tchar>(*item.path));
            }
            return MakeUnexpected(Failure{});
        }
        if(dependencyBytes.size() >= TextDetail::s_Utf8BomByteCount && dependencyBytes[0u] == TextDetail::s_Utf8BomByte0 && dependencyBytes[1u] == TextDetail::s_Utf8BomByte1 && dependencyBytes[2u] == TextDetail::s_Utf8BomByte2)
            result.compilerInputsHaveBom = true;
        if(!dependencyBytes.empty()){
            result.checksum = UpdateFnv64(result.checksum, dependencyBytes.data(), dependencyBytes.size());
        }

        result.checksum = UpdateFnv64(result.checksum, &s_ZeroByte, 1);
    }

    return result;
}

u64 ShaderCook::computeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, const u64 dependencyChecksum, const u64 compilerFingerprint, Core::Alloc::ScratchArena& scratchArena){
    static constexpr AStringView s_ChecksumVersionTag = "shader-source-v6";
    const u8 newlineByte = '\n';

    u64 checksum = s_Fnv64OffsetBasis;

    const auto appendChecksumLine = [&checksum, &newlineByte](const AStringView text)noexcept{
        checksum = UpdateFnv64TextExact(checksum, text);
        checksum = UpdateFnv64(checksum, &newlineByte, 1);
    };

    appendChecksumLine(s_ChecksumVersionTag);
    appendChecksumLine(AStringView(entry.name));
    appendChecksumLine(entry.stage);
    appendChecksumLine(entry.archiveStage);
    appendChecksumLine(entry.rayQuery ? SlangShaderCompiler::s_Spirv15RayQueryTargetProfileText : SlangShaderCompiler::s_Spirv15TargetProfileText);
    appendChecksumLine(__hidden_shader_cook_checksum::ShaderOptimizationLevelText(entry.optimizationLevel));
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

