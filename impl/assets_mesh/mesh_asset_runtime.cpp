// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_asset.h"

#include "mesh_asset_binary_payload.h"
#include "mesh_binary_payload.h"
#include "mesh_payload_validation.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateMeshAssetCodec(){
    return MakeUnique<MeshAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_MeshAssetCodecAutoRegistrar(&CreateMeshAssetCodec);

#include "mesh_asset_runtime_validation.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Mesh::validatePayload()const{
    Core::Alloc::ScratchArena scratchArena;
    const TString<Core::Alloc::ScratchArena> meshPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

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
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has incomplete payload")
            , meshPathText
        );
        return false;
    }

    if(!__hidden_mesh_asset::ValidateSharedMeshPayload(
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
        NWB_TEXT("Mesh::validatePayload"),
        meshPathText
    ))
        return false;

    return true;
}


bool Mesh::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: virtual path is empty"));
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

    const tchar* const loadFailureContext = NWB_TEXT("Mesh::loadBinary");
    usize cursor = 0;
    MeshBinaryPayload::MeshHeaderBinary header;
    if(!MeshAssetBinaryPayload::ReadHeader(
        binary,
        cursor,
        header,
        MeshBinaryPayload::s_MeshMagic,
        loadFailureContext
    ))
        return false;

    if(header.meshClass != Core::Mesh::MeshClass::Static){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: invalid mesh class"));
        return false;
    }
    if(!MeshAssetBinaryPayload::MeshBaseHeaderComplete(header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: mesh payload is incomplete"));
        return false;
    }
    if(header.skinCount != 0u || header.skeletonJointCount != 0u || header.inverseBindMatrixCount != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: static mesh contains skinned payload"));
        return false;
    }

    if(!MeshAssetBinaryPayload::ReadMeshAttributeStreams(
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
    if(!MeshAssetBinaryPayload::ReadMeshletStreams(
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
    if(!MeshAssetBinaryPayload::ReadComplete(binary, cursor, loadFailureContext))
        return false;

    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

