// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_include_directories.h"

#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::CookString;
using Core::Assets::CookVector;
using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;

b8 ShaderIncludeDirectories::append(const Path& includeDirectory, const ShaderCook::ShaderEntry& entry, ScratchArena& scratchArena, CookVector<Path>& outIncludeDirectories, IncludeDirectoryScratchSet& inOutSeenIncludeDirectories){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CookVector<Path>> ShaderIncludeDirectories::build(
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
        if(!append(*implicitIncludeRoot, entry, scratchArena, includeDirectories, seenIncludeDirectories))
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

        if(!append(*includeDirectory, entry, scratchArena, includeDirectories, seenIncludeDirectories))
            return MakeUnexpected(Failure{});
    }

    return includeDirectories;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

