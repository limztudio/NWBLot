// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SelectBuildInputs(const AssetBuildOptions& options, const Assets::ResolvedCookPaths& paths, Assets::ScratchArena& scratchArena, Assets::DiscoveredNwbFileVector& files){
    if(options.inputs.empty()){
        files.clear();
        return true;
    }

    Vector<u8, Assets::ScratchArena> selected(files.size(), u8(0), scratchArena);
    // Borrow discovered canonical paths until selection finishes; compact the owning file vector only afterward.
    const auto selectionResult = Assets::SelectDiscoveredNwbInputs(paths.repoRoot, paths.assetRoots, files, options.inputs, true, scratchArena, selected);
    if(!selectionResult){
        const auto& error = selectionResult.error();
        switch(error.reason){
            case Assets::CookInputSelectionFailure::ResolveInput:
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve input '{}'"), ConvertString(error.rawInput));
                break;
            case Assets::CookInputSelectionFailure::NotFileOrDirectory:
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input is not a file or directory '{}'"), PathToString<tchar>(error.resolvedPath));
                break;
            case Assets::CookInputSelectionFailure::DirectoryOutsideAssetRoots:
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input directory is outside the asset roots '{}'"), PathToString<tchar>(error.resolvedPath));
                break;
            case Assets::CookInputSelectionFailure::NoMatchingAsset:
            case Assets::CookInputSelectionFailure::None:
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: input is not a .nwb asset within the asset roots '{}'"), PathToString<tchar>(error.resolvedPath));
                break;
        }
        return false;
    }

    Assets::CompactSelectedDiscoveredNwbFiles(selected, files);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSET_BUILDER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

