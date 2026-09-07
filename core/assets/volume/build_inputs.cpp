// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build_inputs.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


namespace __hidden_build_inputs{

static bool ContainsPath(const Path& root, const Path& file, ScratchArena& scratchArena){
    ScratchString rootText = PathToString(scratchArena, root.lexically_normal());
    ScratchString fileText = PathToString(scratchArena, file.lexically_normal());
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(rootText);
    CanonicalizeTextInPlace(fileText);
#endif
    if(rootText == fileText)
        return true;
    if(rootText.empty())
        return false;
    if(rootText.back() != '/')
        rootText += '/';
    return AStringView(fileText).starts_with(AStringView(rootText));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SelectBuildInputs(
    const AssetBuildOptions& options,
    const ResolvedCookPaths& paths,
    DiscoveredNwbFileVector& files,
    ScratchArena& scratchArena){
    Vector<u8, ScratchArena> selected(files.size(), u8(0), scratchArena);
    for(const AssetString& input : options.inputs){
        ErrorCode error;
        Path path(paths.repoRoot.arena());
        const ScratchString inputText(input, scratchArena);
        if(!ResolveAbsolutePath(paths.repoRoot, inputText, path, error)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve input '{}'"), StringConvert(input));
            return false;
        }

        const bool isDirectory = IsDirectory(path, error);
        if(error || (!isDirectory && !IsRegularFile(path, error))){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input is not a file or directory '{}'"), PathToString<tchar>(path));
            return false;
        }

        bool matched = false;
        if(isDirectory){
            for(const ResolvedAssetRoot& root : paths.assetRoots){
                if(__hidden_build_inputs::ContainsPath(root.path, path, scratchArena)){
                    matched = true;
                    break;
                }
            }
            if(!matched){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input directory is outside the asset roots '{}'"), PathToString<tchar>(path));
                return false;
            }
        }

        ScratchString normalized = PathToString(scratchArena, path.lexically_normal());
        CanonicalizeTextInPlace(normalized);
        for(usize i = 0; i < files.size(); ++i){
            if(isDirectory ? __hidden_build_inputs::ContainsPath(path, files[i].filePath, scratchArena) : AStringView(files[i].normalizedPathText) == AStringView(normalized)){
                selected[i] = 1u;
                matched = true;
            }
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


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

