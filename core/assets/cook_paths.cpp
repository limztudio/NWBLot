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


[[maybe_unused]] static void CanonicalizeRootIdentity([[maybe_unused]] const AssetRootDuplicatePolicy::Enum duplicatePolicy, [[maybe_unused]] ScratchString& identity)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    if(duplicatePolicy == AssetRootDuplicatePolicy::HostFilesystem)
        CanonicalizeTextInPlace(identity);
#endif
}
#if defined(FOUNDATION_PLATFORM_WINDOWS)
[[nodiscard]] static b8 IsAsciiPath(const TStringView text)noexcept{
    for(const tchar ch : text){
        if(static_cast<u32>(ch) >= 0x80u)
            return false;
    }
    return true;
}

[[nodiscard]] static b8 AsciiRootIdentityEquals(const TStringView lhs, const TStringView rhs, const AssetRootDuplicatePolicy::Enum duplicatePolicy)noexcept{
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        const tchar left = duplicatePolicy == AssetRootDuplicatePolicy::HostFilesystem ? Canonicalize(lhs[i]) : lhs[i];
        const tchar right = duplicatePolicy == AssetRootDuplicatePolicy::HostFilesystem ? Canonicalize(rhs[i]) : rhs[i];
        if(left != right)
            return false;
    }
    return true;
}
#endif



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CookVector<ResolvedAssetRoot>, AssetRootResolutionError> ResolveAssetRoots(const Path& repoRoot, const Span<const CookString> sources, const b8 inferFromInputs, const AssetRootDuplicatePolicy::Enum duplicatePolicy, ScratchArena& scratchArena){
    CookArena& arena = repoRoot.arena();
    CookVector<ResolvedAssetRoot> roots(arena);
    roots.reserve(sources.size());
    for(usize sourceIndex = 0u; sourceIndex < sources.size(); ++sourceIndex){
        const AStringView source(sources[sourceIndex]);
        auto resolved = ResolveAbsolutePath(arena, repoRoot, source);
        if(!resolved){
            return MakeUnexpected(AssetRootResolutionError{ .inputPath = Path(arena, source), .sourceIndex = sourceIndex, .reason = AssetRootResolutionFailure::ResolvePath, .error = resolved.error(), });
        }
        Path path = Move(*resolved);
        if(inferFromInputs){
            const auto directory = IsDirectory(path);
            if(!directory){
                return MakeUnexpected(AssetRootResolutionError{ .inputPath = Move(path), .sourceIndex = sourceIndex, .reason = AssetRootResolutionFailure::InspectInput, .error = directory.error(), });
            }
            if(!*directory)
                path = Move(path).parentPath();
            ::Path<ScratchArena> ancestor(scratchArena, path);
            while(!ancestor.empty()){
                static_assert(s_AssetsDirectoryName == "assets");
                if(EqualsAsciiIgnoreCase(ancestor.filenameView(), FOUNDATION_TEXT("assets"))){
                    path = Path(arena, ancestor);
                    break;
                }
                if(ancestor.parentPathView() == ancestor.native())
                    break;
                ancestor = Move(ancestor).parentPath();
            }
        }

        b8 duplicate = false;
        b8 needsOwnedComparison = true;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        b8 allAscii = __hidden_cook_paths::IsAsciiPath(path.native());
        if(allAscii){
            for(const ResolvedAssetRoot& root : roots){
                if(!__hidden_cook_paths::IsAsciiPath(root.path.native())){
                    allAscii = false;
                    break;
                }
            }
        }
        if(allAscii){
            needsOwnedComparison = false;
            for(const ResolvedAssetRoot& root : roots){
                if(__hidden_cook_paths::AsciiRootIdentityEquals(root.path.native(), path.native(), duplicatePolicy)){
                    duplicate = true;
                    break;
                }
            }
        }
#elif !defined(FOUNDATION_UNICODE)
        needsOwnedComparison = false;
        for(const ResolvedAssetRoot& root : roots){
            if(root.path.native() == path.native()){
                duplicate = true;
                break;
            }
        }
#endif
        if(needsOwnedComparison){
            ScratchString identity = PathToString(scratchArena, path);
            __hidden_cook_paths::CanonicalizeRootIdentity(duplicatePolicy, identity);
            for(const ResolvedAssetRoot& root : roots){
                ScratchString existing = PathToString(scratchArena, root.path);
                __hidden_cook_paths::CanonicalizeRootIdentity(duplicatePolicy, existing);
                if(existing == identity){
                    duplicate = true;
                    break;
                }
            }
        }
        if(duplicate)
            continue;

        const b8 isImplRoot = AssetPathsDetail::CanonicalNativeTextEquals(::Path<ScratchArena>(scratchArena, path.parentPathView()).filenameView(), __hidden_cook_paths::s_ImplDirectoryName, scratchArena);
        const AStringView virtualRoot = isImplRoot ? s_EngineVirtualRoot : s_ProjectVirtualRoot;
        roots.emplace_back(Move(path), virtualRoot);
    }
    return roots;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

