// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/cook_metadata.h>
#include <core/assets/expanded_metadata.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<b8, NameTextError::Enum> HasAssetBunchDeclaration(const Core::Metascript::Document& doc);
[[nodiscard]] Expected<b8, NameTextError::Enum> HasCompositeAssetDeclarations(const Core::Metascript::Document& doc);
[[nodiscard]] Expected<ExpandedAssetMetadataVector> ExpandAssetBunch(const Path& assetRoot, AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<ExpandedAssetMetadataVector> ExpandCompositeAsset(const Path& assetRoot, AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Alloc::ScratchArena& scratchArena);

template<typename RootArenaT, typename FileArenaT>
    requires (!IsSame_V<RootArenaT, Alloc::GlobalArena> || !IsSame_V<FileArenaT, Alloc::GlobalArena>)
[[nodiscard]] NWB_INLINE Expected<ExpandedAssetMetadataVector> ExpandAssetBunch(const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<FileArenaT>& nwbFilePath, const Core::Metascript::Document& doc, Alloc::ScratchArena& scratchArena){
    Alloc::GlobalArena pathArena(scratchArena.allocationLog());
    return ExpandAssetBunch(Path(pathArena, assetRoot), virtualRoot, Path(pathArena, nwbFilePath), doc, scratchArena);
}

template<typename RootArenaT, typename FileArenaT>
    requires (!IsSame_V<RootArenaT, Alloc::GlobalArena> || !IsSame_V<FileArenaT, Alloc::GlobalArena>)
[[nodiscard]] NWB_INLINE Expected<ExpandedAssetMetadataVector> ExpandCompositeAsset(const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<FileArenaT>& nwbFilePath, const Core::Metascript::Document& doc, Alloc::ScratchArena& scratchArena){
    Alloc::GlobalArena pathArena(scratchArena.allocationLog());
    return ExpandCompositeAsset(Path(pathArena, assetRoot), virtualRoot, Path(pathArena, nwbFilePath), doc, scratchArena);
}
[[nodiscard]] Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> ExpandAssetBunchForAssetCook(const AssetBunchExpandContext& context);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

