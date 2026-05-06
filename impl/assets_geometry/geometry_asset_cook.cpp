// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset.h"

#include "geometry_binary_payload.h"

#include <global/binary.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Geometry::s_AssetTypeText)
        );
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = sizeof(GeometryBinaryPayload::GeometryHeaderBinary);
    const bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.vertices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    GeometryBinaryPayload::GeometryHeaderBinary header;
    header.vertexCount = static_cast<u64>(geometry.vertices().size());
    header.indexCount = static_cast<u64>(geometry.indices().size());
    AppendPOD(outBinary, header);
    const tchar* const serializeFailureContext = NWB_TEXT("GeometryAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return GeometryBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!appendVector(geometry.vertices(), NWB_TEXT("vertices")))
        return false;

    return appendVector(geometry.indices(), NWB_TEXT("indices"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

