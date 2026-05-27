// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"

#include "geometry_asset_binary_payload.h"
#include "skinned_geometry_binary_payload.h"

#include <global/binary.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedGeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(SkinnedGeometry::s_AssetTypeText)
        );
        return false;
    }

    const SkinnedGeometry& geometry = static_cast<const SkinnedGeometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary);
    const bool canReserve = GeometryAssetBinaryPayload::AddGeometryBaseReserveBytes(reserveBytes, geometry)
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.skinStream())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.inverseBindMatrices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary header;
    header.magic = SkinnedGeometryBinaryPayload::s_SkinnedGeometryMagic;
    GeometryAssetBinaryPayload::FillGeometryBaseHeader(header, geometry);
    header.skinCount = static_cast<u64>(geometry.skinStream().size());
    header.skeletonJointCount = static_cast<u64>(geometry.skeletonJointCount());
    header.inverseBindMatrixCount = static_cast<u64>(geometry.inverseBindMatrices().size());
    AppendPOD(outBinary, header);

    const tchar* const serializeFailureContext = NWB_TEXT("SkinnedGeometryAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return GeometryAssetBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!GeometryAssetBinaryPayload::AppendGeometryAttributeStreams(outBinary, geometry, serializeFailureContext))
        return false;
    if(!appendVector(geometry.skinStream(), NWB_TEXT("skin")))
        return false;
    if(!appendVector(geometry.inverseBindMatrices(), NWB_TEXT("inverse bind matrices")))
        return false;
    return GeometryAssetBinaryPayload::AppendGeometryMeshletStreams(outBinary, geometry, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
