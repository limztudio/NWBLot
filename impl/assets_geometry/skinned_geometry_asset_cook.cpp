// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"

#include "skinned_geometry_binary_payload.h"
#include "geometry_binary_payload.h"

#include <core/alloc/scratch.h>
#include <global/binary.h>
#include <core/common/log.h>

#include <cstddef>


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
    bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.restVertices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.skin())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.inverseBindMatrices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary header;
    header.geometryClass = geometry.geometryClass();
    header.restVertexCount = static_cast<u64>(geometry.restVertices().size());
    header.indexCount = static_cast<u64>(geometry.indices().size());
    header.skinCount = static_cast<u64>(geometry.skin().size());
    header.skeletonJointCount = static_cast<u64>(geometry.skeletonJointCount());
    header.inverseBindMatrixCount = static_cast<u64>(geometry.inverseBindMatrices().size());
    AppendPOD(outBinary, header);

    const tchar* const serializeFailureContext = NWB_TEXT("SkinnedGeometryAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return GeometryBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!appendVector(geometry.restVertices(), NWB_TEXT("rest vertices")))
        return false;
    if(!appendVector(geometry.indices(), NWB_TEXT("indices")))
        return false;
    if(!appendVector(geometry.skin(), NWB_TEXT("skin")))
        return false;
    if(!appendVector(geometry.inverseBindMatrices(), NWB_TEXT("inverse bind matrices")))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

