// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, MakeNotNull(NWB_TEXT("UiSkinAssetCodec::serialize"))))
        return false;
    const UiSkin& skin = *checked_cast<const UiSkin*>(&asset);
    if(!skin.validatePayload())
        return false;

    usize reserveBytes = sizeof(UiSkinBinaryPayload::HeaderBinary);
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, skin.regions().size(), sizeof(UiSkinBinaryPayload::RegionBinary))){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSkinAssetCodec::serialize failed: payload size overflows"));
        return false;
    }

    UiSkinBinaryPayload::HeaderBinary header;
    header.textureNameHash = skin.texture().name().hash();
    header.atlasWidth = skin.atlasWidth();
    header.atlasHeight = skin.atlasHeight();
    header.referenceDensity = skin.referenceDensity();
    header.regionCount = static_cast<u32>(skin.regions().size());

    outBinary.clear();
    outBinary.reserve(reserveBytes);
    AppendPOD(outBinary, header);
    for(const UiSkinRegion& region : skin.regions()){
        UiSkinBinaryPayload::RegionBinary packed;
        packed.nameHash = region.name.hash();
        packed.x = region.rectangle.x;
        packed.y = region.rectangle.y;
        packed.width = region.rectangle.width;
        packed.height = region.rectangle.height;
        packed.sliceLeft = region.sliceInsets.left;
        packed.sliceTop = region.sliceInsets.top;
        packed.sliceRight = region.sliceInsets.right;
        packed.sliceBottom = region.sliceInsets.bottom;
        packed.paddingLeft = region.padding.left;
        packed.paddingTop = region.padding.top;
        packed.paddingRight = region.padding.right;
        packed.paddingBottom = region.padding.bottom;
        packed.minimumWidth = region.minimumWidth;
        packed.minimumHeight = region.minimumHeight;
        packed.drawMode = static_cast<u32>(region.drawMode);
        AppendPOD(outBinary, packed);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildUiSkinAsset(const UiSkinCookEntry& entry, UiSkin& outSkin){
    if(entry.regions.size() > s_UiSkinMaxRegionCount){
        NWB_LOGGER_ERROR(NWB_TEXT("BuildUiSkinAsset failed: region count {} exceeds schema limit {}"), entry.regions.size(), s_UiSkinMaxRegionCount);
        return false;
    }
    UiSkin candidate(entry.arena, entry.virtualPath);
    UiSkin::RegionVector regions(entry.regions.begin(), entry.regions.end(), entry.arena);
    candidate.setAtlas(entry.texture, entry.atlasWidth, entry.atlasHeight, entry.referenceDensity, Move(regions));
    if(!candidate.validatePayload())
        return false;
    outSkin = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

