// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build_inputs.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_build_inputs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct IndexedPath{
    AStringView text;
    usize fileIndex;
};

struct PathSelectionIndex{
    Vector<IndexedPath, Assets::ScratchArena> entries;
    Optional<Vector<Assets::ScratchString, Assets::ScratchArena>> ownedPaths;

    PathSelectionIndex(
        const Assets::DiscoveredNwbFileVector& files,
        Assets::ScratchArena& scratchArena,
        const bool preserveCase)
        : entries(scratchArena){
        entries.reserve(files.size());
        if(preserveCase){
            ownedPaths.emplace(scratchArena);
            ownedPaths->reserve(files.size());
        }
        for(usize i = 0u; i < files.size(); ++i){
            if(preserveCase){
                ownedPaths->emplace_back(PathToString(scratchArena, files[i].filePath.lexically_normal()));
                entries.push_back({ AStringView(ownedPaths->back()), i });
            }
            else
                entries.push_back({ AStringView(files[i].normalizedPathText), i });
        }
        Sort(entries.begin(), entries.end(), [](const IndexedPath& lhs, const IndexedPath& rhs){ return lhs.text < rhs.text; });
    }
};

[[nodiscard]] static bool ContainsPath(const AStringView root, const AStringView file){
    if(root == file)
        return true;
    return !root.empty() && file.size() > root.size() && file.starts_with(root)
        && (root.back() == '/' || file[root.size()] == '/');
}

[[nodiscard]] static bool SelectPathRange(
    const PathSelectionIndex& index,
    const AStringView path,
    const bool prefix,
    Vector<u8, Assets::ScratchArena>& selected){
    auto entry = LowerBound(index.entries.begin(), index.entries.end(), path,
        [](const IndexedPath& candidate, const AStringView value){ return candidate.text < value; });
    bool matched = false;
    for(; entry != index.entries.end(); ++entry){
        if(prefix ? !entry->text.starts_with(path) : entry->text != path)
            break;
        selected[entry->fileIndex] = 1u;
        matched = true;
    }
    return matched;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SelectBuildInputs(
    const AssetBuildOptions& options,
    const Assets::ResolvedCookPaths& paths,
    Assets::DiscoveredNwbFileVector& files,
    Assets::ScratchArena& scratchArena){
    if(options.inputs.empty()){
        files.clear();
        return true;
    }

    Vector<u8, Assets::ScratchArena> selected(files.size(), u8(0), scratchArena);
    // Borrow discovered canonical paths until selection finishes; compact the owning file vector only afterward.
    const __hidden_build_inputs::PathSelectionIndex fileIndex(files, scratchArena, false);
#if !defined(NWB_PLATFORM_WINDOWS)
    Optional<__hidden_build_inputs::PathSelectionIndex> directoryIndex;
#endif
    for(const Assets::AssetString& input : options.inputs){
        ErrorCode error;
        Path path(paths.repoRoot.arena());
        const Assets::ScratchString inputText(input, scratchArena);
        if(!ResolveAbsolutePath(paths.repoRoot, inputText, path, error)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve input '{}'"), StringConvert(input));
            return false;
        }

        const bool isDirectory = IsDirectory(path, error);
        if(error || (!isDirectory && !IsRegularFile(path, error))){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input is not a file or directory '{}'"), PathToString<tchar>(path));
            return false;
        }

        Assets::ScratchString normalized = PathToString(scratchArena, path.lexically_normal());
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(normalized);
#else
        if(!isDirectory)
            CanonicalizeTextInPlace(normalized);
#endif
        bool matched = false;
        if(isDirectory){
            for(const Assets::ResolvedAssetRoot& root : paths.assetRoots){
                Assets::ScratchString rootText = PathToString(scratchArena, root.path.lexically_normal());
#if defined(NWB_PLATFORM_WINDOWS)
                CanonicalizeTextInPlace(rootText);
#endif
                if(__hidden_build_inputs::ContainsPath(rootText, normalized)){
                    matched = true;
                    break;
                }
            }
            if(!matched){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input directory is outside the asset roots '{}'"), PathToString<tchar>(path));
                return false;
            }
        }

        const __hidden_build_inputs::PathSelectionIndex* selectionIndex = &fileIndex;
#if !defined(NWB_PLATFORM_WINDOWS)
        // Directory containment follows host case rules; explicit asset identities retain their canonical spelling.
        if(isDirectory){
            if(!directoryIndex)
                directoryIndex.emplace(files, scratchArena, true);
            selectionIndex = &*directoryIndex;
        }
#endif
        matched |= __hidden_build_inputs::SelectPathRange(*selectionIndex, normalized, false, selected);
        if(isDirectory){
            if(!normalized.empty() && normalized.back() != '/')
                normalized += '/';
            matched |= __hidden_build_inputs::SelectPathRange(*selectionIndex, normalized, true, selected);
        }
        if(!matched){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input is not a .nwb asset within the asset roots '{}'"), PathToString<tchar>(path));
            return false;
        }
    }

    usize next = 0u;
    for(usize i = 0u; i < files.size(); ++i){
        if(!selected[i])
            continue;
        if(next != i)
            files[next] = Move(files[i]);
        ++next;
    }
    files.erase(files.begin() + next, files.end());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

