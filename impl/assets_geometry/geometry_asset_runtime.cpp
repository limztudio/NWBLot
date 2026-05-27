// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset.h"

#include "geometry_asset_binary_payload.h"
#include "geometry_binary_payload.h"
#include "geometry_payload_validation.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateGeometryAssetCodec(){
    return MakeUnique<GeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_GeometryAssetCodecAutoRegistrar(&CreateGeometryAssetCodec);

#include "geometry_asset_runtime_validation.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Geometry::validatePayload()const{
    Core::Alloc::ScratchArena scratchArena;
    const TString<Core::Alloc::ScratchArena> geometryPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(
        m_positionStream.empty()
        || m_normalStream.empty()
        || m_tangentStream.empty()
        || m_uv0Stream.empty()
        || m_colorStream.empty()
        || m_vertexRefs.empty()
        || m_meshlets.empty()
        || m_meshletBounds.empty()
        || m_meshletVertexRefs.empty()
        || m_meshletPrimitiveIndices.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' has incomplete payload")
            , geometryPathText
        );
        return false;
    }

    if(!__hidden_geometry_asset::ValidateSharedGeometryPayload(
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
        0u,
        false,
        NWB_TEXT("Geometry::validatePayload"),
        geometryPathText
    ))
        return false;

    return true;
}


bool Geometry::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_positionStream.clear();
    m_normalStream.clear();
    m_tangentStream.clear();
    m_uv0Stream.clear();
    m_colorStream.clear();
    m_vertexRefs.clear();
    m_meshlets.clear();
    m_meshletBounds.clear();
    m_meshletVertexRefs.clear();
    m_meshletPrimitiveIndices.clear();

    const tchar* const loadFailureContext = NWB_TEXT("Geometry::loadBinary");
    usize cursor = 0;
    GeometryBinaryPayload::GeometryHeaderBinary header;
    if(!GeometryAssetBinaryPayload::ReadHeader(
        binary,
        cursor,
        header,
        GeometryBinaryPayload::s_GeometryMagic,
        loadFailureContext
    ))
        return false;

    if(header.geometryClass != Core::Geometry::GeometryClass::Static){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: invalid geometry class"));
        return false;
    }
    if(!GeometryAssetBinaryPayload::GeometryBaseHeaderComplete(header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: geometry payload is incomplete"));
        return false;
    }
    if(header.skinCount != 0u || header.skeletonJointCount != 0u || header.inverseBindMatrixCount != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: static geometry contains skinned payload"));
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

    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

