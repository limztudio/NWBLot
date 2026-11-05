// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook_checksum{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchPath = ::Path<Core::Alloc::ScratchArena>;
using ScratchString = Core::Assets::ScratchString;

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
    SortedDependencyItem(const Path& dependency, Core::Alloc::ScratchArena& scratchArena)noexcept
        : canonicalPath(scratchArena)
        , path(&dependency)
    {}
};

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

static Expected<ScratchString> BuildCanonicalDependencyPath(const Path& dependency, const Span<const NormalizedDependencyRootAlias> normalizedRootAliases, const b8 allowUnaliasedPath, Core::Alloc::ScratchArena& scratchArena){
    const auto normalizedDependencyResult = AbsolutePath(ScratchPath(scratchArena, dependency));
    if(!normalizedDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}"), PathToString<tchar>(dependency), ConvertString(normalizedDependencyResult.error().message()));
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
        ScratchString canonicalPath(ShaderBinaryPayload::s_ShaderChecksumAbsolutePrefix, scratchArena);
        canonicalPath.reserve(AddSize(canonicalPath.size(), absolutePathText.size()));
        canonicalPath += absolutePathText;
        return canonicalPath;
    }

    const ScratchString relativePathText = PathToString(scratchArena, normalizedDependency.lexicallyRelative(bestRootAlias->root));

    const AStringView pathPrefix = allowUnaliasedPath ? ShaderBinaryPayload::s_ShaderChecksumAliasPrefix : AStringView();
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ShaderBinaryPayload::DependencyChecksum> ShaderCook::ComputeDependencyChecksum(
    const Span<const Path> dependencies,
    const Span<const Path> includeDirectories,
    const InitializerList<DependencyRootAlias> dependencyRootAliases,
    Core::Alloc::ScratchArena& scratchArena
){
    using __hidden_shader_cook_checksum::SortedDependencyItem;

    ShaderBinaryPayload::DependencyChecksum result;
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
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency root alias '{}' : {}"), PathToString<tchar>(rootAlias.root), ConvertString(rootPath.error().message()));
            return MakeUnexpected(Failure{});
        }

        normalizedAlias.root = __hidden_shader_cook_checksum::NormalizeDependencyRootAliasPath(*rootPath);
        normalizedAlias.key = rootAlias.key;
        CanonicalizeTextInPlace(normalizedAlias.key);
        if(normalizedAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root alias keys"));
            return MakeUnexpected(Failure{});
        }
        normalizedAlias.depth = PathDepth(normalizedAlias.root);
        normalizedRootAliases.push_back(Move(normalizedAlias));
    }

    Fnv64AppendValue(result.checksum, ShaderBinaryPayload::s_ShaderDependencyChecksumVersion);
    Fnv64AppendValue(result.checksum, includeDirectories.size());
    for(const Path& includeDirectory : includeDirectories){
        const auto canonicalPath = __hidden_shader_cook_checksum::BuildCanonicalDependencyPath(includeDirectory, normalizedRootAliases, true, scratchArena);
        if(!canonicalPath)
            return MakeUnexpected(Failure{});
        result.checksum = UpdateFnv64TextExact(result.checksum, AStringView(*canonicalPath));
    }
    Fnv64AppendValue(result.checksum, dependencies.size());

    Vector<SortedDependencyItem, Core::Alloc::ScratchArena> sortedDependencies{scratchArena};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item(dependency, scratchArena);
        auto canonicalPath = __hidden_shader_cook_checksum::BuildCanonicalDependencyPath(dependency, normalizedRootAliases, false, scratchArena);
        if(!canonicalPath)
            return MakeUnexpected(Failure{});
        item.canonicalPath = Move(*canonicalPath);
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(), [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs)noexcept(noexcept(lhs.canonicalPath < rhs.canonicalPath)){ return lhs.canonicalPath < rhs.canonicalPath; });

    Vector<u8, Core::Alloc::ScratchArena> dependencyBytes{scratchArena};
    for(const SortedDependencyItem& item : sortedDependencies){
        dependencyBytes.clear();
        auto readBinaryFileResult = ReadBinaryFile(*item.path, dependencyBytes);
        if(!readBinaryFileResult){
            if(readBinaryFileResult.error()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}' : {}"), PathToString<tchar>(*item.path), ConvertString(readBinaryFileResult.error().message()));
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}'"), PathToString<tchar>(*item.path));
            }
            return MakeUnexpected(Failure{});
        }
        ShaderBinaryPayload::AppendShaderDependencyItemChecksum(
            AStringView(item.canonicalPath),
            Span<const u8>(dependencyBytes.data(), dependencyBytes.size()),
            result.checksum,
            result.compilerInputsHaveBom
        );
    }

    return result;
}

u64 ShaderCook::ComputeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, const u64 dependencyChecksum, const u64 compilerFingerprint, Core::Alloc::ScratchArena& scratchArena){
    u64 checksum = ShaderBinaryPayload::BeginShaderSourceChecksum(
        AStringView(entry.name),
        entry.stage,
        entry.archiveStage,
        entry.rayQuery,
        entry.optimizationLevel,
        AStringView(entry.entryPoint),
        variantSignature
    );
    if(entry.implicitDefines.size() <= 1u){
        for(const auto& [defineName, defineValue] : entry.implicitDefines){
            ShaderBinaryPayload::AppendShaderChecksumLine(defineName, checksum);
            ShaderBinaryPayload::AppendShaderChecksumLine(defineValue, checksum);
        }
    }
    else{
        for(const auto& entryDefine : SortedDefineEntries(entry.implicitDefines, scratchArena)){
            ShaderBinaryPayload::AppendShaderChecksumLine(*entryDefine.key, checksum);
            ShaderBinaryPayload::AppendShaderChecksumLine(*entryDefine.value, checksum);
        }
    }
    return ShaderBinaryPayload::FinalizeShaderSourceChecksum(dependencyChecksum, compilerFingerprint, checksum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

