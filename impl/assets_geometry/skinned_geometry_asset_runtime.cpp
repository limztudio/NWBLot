// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"

#include "skinned_geometry_binary_payload.h"
#include "skinned_geometry_payload_logging.h"
#include "geometry_binary_payload.h"

#include <core/assets/asset_auto_registration.h>
#include <core/alloc/scratch.h>
#include <global/binary.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateSkinnedGeometryAssetCodec(){
    return MakeUnique<SkinnedGeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_SkinnedGeometryAssetCodecAutoRegistrar(&CreateSkinnedGeometryAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedGeometry::validatePayload()const{
    Core::Alloc::ScratchArena<> scratchArena;
    const TString<Core::Alloc::ScratchArena<>> geometryPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(!GeometryClassUsesSkinning(m_geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' has invalid geometry class '{}'")
            , geometryPathText
            , StringConvert(GeometryClassText(m_geometryClass))
        );
        return false;
    }

    const bool hasSkin = !m_skin.empty();
    if(!GeometryClassMatchesSkinPayload(m_geometryClass, hasSkin)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' class '{}' does not match skin payload")
            , geometryPathText
            , StringConvert(GeometryClassText(m_geometryClass))
        );
        return false;
    }
    const SkinnedGeometryValidation::RuntimePayloadFailureInfo runtimePayloadFailure =
        SkinnedGeometryValidation::FindRuntimePayloadFailure(
            SkinnedGeometryValidation::RuntimePayloadArrays{
                m_restVertices,
                m_indices,
                m_skeletonJointCount,
                m_skin,
                m_inverseBindMatrices
            }
        )
    ;
    if(runtimePayloadFailure.reason != SkinnedGeometryValidation::RuntimePayloadFailure::None){
        SkinnedGeometryValidation::LogRuntimePayloadFailure(
            NWB_TEXT("SkinnedGeometry::validatePayload failed"),
            NWB_TEXT("geometry"),
            geometryPathText,
            runtimePayloadFailure
        );
        return false;
    }

    return true;
}

bool SkinnedGeometry::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_restVertices.clear();
    m_indices.clear();
    m_geometryClass = GeometryClass::Invalid;
    m_skin.clear();
    m_skeletonJointCount = 0u;
    m_inverseBindMatrices.clear();

    const tchar* const loadFailureContext = NWB_TEXT("SkinnedGeometry::loadBinary");
    usize cursor = 0;
    SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary header;
    if(!GeometryBinaryPayload::ReadHeader(
        binary,
        cursor,
        header,
        SkinnedGeometryBinaryPayload::s_SkinnedGeometryMagic,
        SkinnedGeometryBinaryPayload::s_SkinnedGeometryVersion,
        loadFailureContext
    ))
        return false;

    const u32 geometryClass = header.geometryClass;
    const u64 vertexCount = header.restVertexCount;
    const u64 indexCount = header.indexCount;
    const u64 skinCount = header.skinCount;
    const u64 skeletonJointCount = header.skeletonJointCount;
    const u64 inverseBindMatrixCount = header.inverseBindMatrixCount;
    if(!GeometryClassUsesSkinning(geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: invalid geometry class"));
        return false;
    }
    if(
        vertexCount > static_cast<u64>(Limit<u32>::s_Max)
        || indexCount > static_cast<u64>(Limit<u32>::s_Max)
        || skinCount > static_cast<u64>(Limit<u32>::s_Max)
        || skeletonJointCount > static_cast<u64>(Limit<u32>::s_Max)
        || inverseBindMatrixCount > static_cast<u64>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: payload counts exceed u32 limits"));
        return false;
    }
    if(vertexCount == 0u || indexCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: rest/index payload is empty"));
        return false;
    }
    if((indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: index count is not a multiple of 3"));
        return false;
    }
    if(skinCount != 0u && skinCount != vertexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: skin count must be empty or match vertex count"));
        return false;
    }
    if(skinCount != 0u && skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: skeleton joint count is required when skin is present"));
        return false;
    }
    if(skeletonJointCount > SkinnedGeometryBinaryPayload::s_SkinnedGeometrySkeletonJointLimit){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: skeleton joint count exceeds skin stream limits"));
        return false;
    }
    if(inverseBindMatrixCount != 0u && inverseBindMatrixCount != skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: inverse bind matrix count must be empty or match skeleton joint count"));
        return false;
    }
    if(!GeometryClassMatchesSkinPayload(geometryClass, skinCount != 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: geometry class does not match skin payload"));
        return false;
    }

    auto readVector = [&](const u64 count, auto& outValues, const tchar* label){
        return GeometryBinaryPayload::ReadVector(binary, cursor, count, outValues, loadFailureContext, label);
    };
    if(!readVector(vertexCount, m_restVertices, NWB_TEXT("rest vertices")))
        return false;
    if(!readVector(indexCount, m_indices, NWB_TEXT("indices")))
        return false;
    if(!readVector(skinCount, m_skin, NWB_TEXT("skin")))
        return false;
    m_geometryClass = geometryClass;
    m_skeletonJointCount = static_cast<u32>(skeletonJointCount);
    if(!readVector(inverseBindMatrixCount, m_inverseBindMatrices, NWB_TEXT("inverse bind matrices")))
        return false;

    if(!GeometryBinaryPayload::ReadComplete(binary, cursor, loadFailureContext))
        return false;

    return validatePayload();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

