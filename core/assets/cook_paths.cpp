// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_paths.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_paths{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_ImplDirectoryName = "impl";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CanonicalizeRootIdentity(ScratchString& identity, const AssetRootDuplicatePolicy::Enum duplicatePolicy)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(duplicatePolicy == AssetRootDuplicatePolicy::HostFilesystem)
        CanonicalizeTextInPlace(identity);
#else
    static_cast<void>(identity);
    static_cast<void>(duplicatePolicy);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CookVector<ResolvedAssetRoot>, AssetRootResolutionError> ResolveAssetRoots(
    const Path& repoRoot,
    const Span<const CookString> sources,
    const bool inferFromInputs,
    const AssetRootDuplicatePolicy::Enum duplicatePolicy,
    ScratchArena& scratchArena
){
    CookArena& arena = repoRoot.arena();
    CookVector<ResolvedAssetRoot> roots(arena);
    roots.reserve(sources.size());
    for(usize sourceIndex = 0u; sourceIndex < sources.size(); ++sourceIndex){
        const AStringView source(sources[sourceIndex]);
        auto resolved = ResolveAbsolutePath(arena, repoRoot, source);
        if(!resolved){
            return MakeUnexpected(AssetRootResolutionError{
                .inputPath = Path(arena, source),
                .sourceIndex = sourceIndex,
                .reason = AssetRootResolutionFailure::ResolvePath,
                .error = resolved.error(),
            });
        }
        Path path = Move(*resolved);
        if(inferFromInputs){
            const auto directory = IsDirectory(path);
            if(!directory){
                return MakeUnexpected(AssetRootResolutionError{
                    .inputPath = Move(path),
                    .sourceIndex = sourceIndex,
                    .reason = AssetRootResolutionFailure::InspectInput,
                    .error = directory.error(),
                });
            }
            if(!*directory)
                path = path.parentPath();
            Path ancestor = path;
            while(!ancestor.empty()){
                ScratchString name = PathToString(scratchArena, ancestor.filename());
                CanonicalizeTextInPlace(name);
                if(name == s_AssetsDirectoryName){
                    path = ancestor;
                    break;
                }
                const Path parent = ancestor.parentPath();
                if(parent == ancestor)
                    break;
                ancestor = parent;
            }
        }

        ScratchString identity = PathToString(scratchArena, path);
        __hidden_cook_paths::CanonicalizeRootIdentity(identity, duplicatePolicy);
        bool duplicate = false;
        for(const ResolvedAssetRoot& root : roots){
            ScratchString existing = PathToString(scratchArena, root.path);
            __hidden_cook_paths::CanonicalizeRootIdentity(existing, duplicatePolicy);
            if(existing == identity){
                duplicate = true;
                break;
            }
        }
        if(duplicate)
            continue;

        ScratchString parentName = PathToString(scratchArena, path.parentPath().filename());
        CanonicalizeTextInPlace(parentName);
        const AStringView virtualRoot = parentName == __hidden_cook_paths::s_ImplDirectoryName
            ? s_EngineVirtualRoot : s_ProjectVirtualRoot
        ;
        roots.emplace_back(Move(path), ACompactString(virtualRoot));
    }
    return roots;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

