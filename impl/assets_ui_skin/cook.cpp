// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiSkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("UiSkinAssetCodec::serialize")))
        return false;
    const UiSkin& skin = *MakeNotNull(checked_cast<const UiSkin*>(&asset));
    if(!skin.validatePayload())
        return false;

    usize reserveBytes = sizeof(UiSkinHeaderBinary);
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, skin.regions().size(), sizeof(UiSkinRegionBinary))){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkinAssetCodec::serialize failed: payload size overflows"));
        return false;
    }
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, UiSkinColorRole::kCount, sizeof(UiSkinColor))){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkinAssetCodec::serialize failed: palette payload size overflows"));
        return false;
    }
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, 1u, sizeof(UiSkinTypography))){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkinAssetCodec::serialize failed: typography payload size overflows"));
        return false;
    }

    const UiSkinHeaderBinary header = MakeUiSkinHeaderBinary(
        skin.texture().name().hash(),
        skin.atlasWidth(),
        skin.atlasHeight(),
        skin.referenceDensity(),
        static_cast<u32>(skin.regions().size())
    );

    outBinary.clear();
    outBinary.reserve(reserveBytes);
    AppendPOD(outBinary, header);
    for(const UiSkinRegion& region : skin.regions())
        AppendPOD(outBinary, PackUiSkinRegionBinary(region));
    for(const UiSkinColor& color : skin.palette().colors)
        AppendPOD(outBinary, color);
    AppendPOD(outBinary, skin.typography());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UiSkin> BuildUiSkinAsset(const UiSkinCookEntry& entry, Core::Assets::AssetArena& arena){
    if(!ValidateUiSkinCookRegionCount(entry.regions.size()))
        return MakeUnexpected(Failure{});
    UiSkin candidate(arena, entry.virtualPath);
    UiSkin::RegionVector regions(entry.regions.begin(), entry.regions.end(), arena);
    candidate.setAtlas(entry.texture, entry.atlasWidth, entry.atlasHeight, entry.referenceDensity, Move(regions));
    candidate.setPalette(entry.palette);
    candidate.setTypography(entry.typography);
    if(entry.completeToolkitSkin ? !ValidateUiSkinToolkitContract(candidate) : !candidate.validatePayload())
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

