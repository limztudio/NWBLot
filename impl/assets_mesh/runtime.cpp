// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include "arena_names.h"
#include "binary_payload_io.h"
#include "binary_payload.h"
#include "meshlet_ref_codec.h"
#include "meshlet_payload_packing.h"
#include "payload_validation.h"

#include <global/core/alloc/scratch.h>
#include <global/core/common/log.h>
#include <global/core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateMeshAssetCodec(){
    return MakeUnique<MeshAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_MeshAssetCodecAutoRegistrar(&CreateMeshAssetCodec);

#include "runtime_validation.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Mesh::validatePayload()const{
    Core::Alloc::ScratchArena scratchArena(AssetsMeshArenaScope::s_ValidatePayloadArena);
    const TString<Core::Alloc::ScratchArena> meshPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(hasIncompleteGeometryPayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has incomplete payload")
            , meshPathText
        );
        return false;
    }

    if(!__hidden_runtime::ValidateSharedMeshPayload(
        *this,
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

    clearGeometryPayload();

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

    if(!readGeometryAttributeStreams(binary, cursor, header, loadFailureContext))
        return false;
    if(!readGeometryMeshletStreams(binary, cursor, header, loadFailureContext))
        return false;
    if(!MeshAssetBinaryPayload::ReadComplete(binary, cursor, loadFailureContext))
        return false;

    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

