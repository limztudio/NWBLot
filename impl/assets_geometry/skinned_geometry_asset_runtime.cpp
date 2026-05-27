// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"

#include "geometry_asset_binary_payload.h"
#include "geometry_payload_validation.h"
#include "skinned_geometry_binary_payload.h"
#include "skinned_geometry_validation.h"

#include <core/assets/asset_auto_registration.h>
#include <core/alloc/scratch.h>
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

#include "geometry_asset_runtime_validation.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedGeometry::validatePayload()const{
    Core::Alloc::ScratchArena scratchArena;
    const TString<Core::Alloc::ScratchArena> geometryPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(!Core::Geometry::GeometryClassUsesSkinning(m_geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' has invalid geometry class '{}'")
            , geometryPathText
            , StringConvert(Core::Geometry::GeometryClassText(m_geometryClass))
        );
        return false;
    }

    if(
        m_positionStream.empty()
        || m_normalStream.empty()
        || m_tangentStream.empty()
        || m_uv0Stream.empty()
        || m_colorStream.empty()
        || m_skin.empty()
        || m_vertexRefs.empty()
        || m_meshlets.empty()
        || m_meshletBounds.empty()
        || m_meshletVertexRefs.empty()
        || m_meshletPrimitiveIndices.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' has incomplete payload")
            , geometryPathText
        );
        return false;
    }

    if(m_skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' has skin but no skeleton joint count")
            , geometryPathText
        );
        return false;
    }
    if(m_skeletonJointCount > SkinnedGeometryBinaryPayload::s_SkinnedGeometrySkeletonJointLimit){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' skeleton joint count exceeds skin stream limits")
            , geometryPathText
        );
        return false;
    }
    if(m_inverseBindMatrices.size() != m_skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' inverse bind matrix count must match skeleton joint count")
            , geometryPathText
        );
        return false;
    }
    if(!SkinnedGeometryValidation::ValidInverseBindMatrices(m_inverseBindMatrices, m_skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' inverse bind matrices are invalid")
            , geometryPathText
        );
        return false;
    }

    if(!__hidden_skinned_geometry_asset::ValidateSharedGeometryPayload(
        m_positionStream,
        m_normalStream,
        m_tangentStream,
        m_uv0Stream,
        m_colorStream,
        m_vertexRefs,
        m_meshlets,
        m_meshletBounds,
        m_meshletVertexRefs,
        m_meshletPrimitiveIndices,
        m_skin.size(),
        true,
        NWB_TEXT("SkinnedGeometry::validatePayload"),
        geometryPathText
    ))
        return false;

    for(usize i = 0u; i < m_skin.size(); ++i){
        if(SkinnedGeometryValidation::ValidSkinInfluence(m_skin[i])){
            u32 failedJoint = 0u;
            if(SkinnedGeometryValidation::SkinInfluenceFitsSkeleton(m_skin[i], m_skeletonJointCount, failedJoint))
                continue;
        }

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::validatePayload failed: geometry '{}' skin influence {} is invalid")
            , geometryPathText
            , i
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

    m_positionStream.clear();
    m_normalStream.clear();
    m_tangentStream.clear();
    m_uv0Stream.clear();
    m_colorStream.clear();
    m_skin.clear();
    m_inverseBindMatrices.clear();
    m_vertexRefs.clear();
    m_meshlets.clear();
    m_meshletBounds.clear();
    m_meshletVertexRefs.clear();
    m_meshletPrimitiveIndices.clear();
    m_geometryClass = Core::Geometry::GeometryClass::Invalid;
    m_skeletonJointCount = 0u;

    const tchar* const loadFailureContext = NWB_TEXT("SkinnedGeometry::loadBinary");
    usize cursor = 0;
    SkinnedGeometryBinaryPayload::SkinnedGeometryHeaderBinary header;
    if(!GeometryAssetBinaryPayload::ReadHeader(
        binary,
        cursor,
        header,
        SkinnedGeometryBinaryPayload::s_SkinnedGeometryMagic,
        loadFailureContext
    ))
        return false;

    if(!Core::Geometry::GeometryClassUsesSkinning(header.geometryClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: invalid geometry class"));
        return false;
    }
    if(
        !GeometryAssetBinaryPayload::GeometryBaseHeaderComplete(header)
        || header.skinCount == 0u
        || header.skeletonJointCount == 0u
        || header.inverseBindMatrixCount != header.skeletonJointCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: geometry payload is incomplete"));
        return false;
    }
    if(header.skeletonJointCount > SkinnedGeometryBinaryPayload::s_SkinnedGeometrySkeletonJointLimit){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometry::loadBinary failed: skeleton joint count exceeds skin stream limits"));
        return false;
    }

    if(!GeometryAssetBinaryPayload::ReadGeometryAttributeStreams(
        binary,
        cursor,
        header,
        m_positionStream,
        m_normalStream,
        m_tangentStream,
        m_uv0Stream,
        m_colorStream,
        loadFailureContext
    ))
        return false;
    if(!GeometryAssetBinaryPayload::ReadVector(binary, cursor, header.skinCount, m_skin, loadFailureContext, NWB_TEXT("skin")))
        return false;
    if(!GeometryAssetBinaryPayload::ReadVector(
        binary,
        cursor,
        header.inverseBindMatrixCount,
        m_inverseBindMatrices,
        loadFailureContext,
        NWB_TEXT("inverse bind matrices")
    ))
        return false;
    if(!GeometryAssetBinaryPayload::ReadGeometryMeshletStreams(
        binary,
        cursor,
        header,
        m_vertexRefs,
        m_meshlets,
        m_meshletBounds,
        m_meshletVertexRefs,
        m_meshletPrimitiveIndices,
        loadFailureContext
    ))
        return false;
    if(!GeometryAssetBinaryPayload::ReadComplete(binary, cursor, loadFailureContext))
        return false;

    m_geometryClass = header.geometryClass;
    m_skeletonJointCount = static_cast<u32>(header.skeletonJointCount);

    return validatePayload();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
