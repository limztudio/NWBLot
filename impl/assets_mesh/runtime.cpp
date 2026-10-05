// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"
#include "binary_payload_io.h"
#include "binary_payload.h"
#include "meshlet_ref_codec.h"
#include "meshlet_payload_packing.h"
#include "payload_validation.h"
#include "meshlet_ref_validation.h"
#include "runtime_validation.h"
#include "runtime_validation_diagnostics.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_MeshAssetCodecAutoRegistrar, MeshAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Mesh::validatePayload()const{
    Core::Alloc::ScratchArena scratchArena(AssetsMeshArenaScope::s_ValidatePayloadArena);
    const TString<Core::Alloc::ScratchArena> meshPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(hasIncompleteGeometryPayload()){
        NWB_LOGGER_ERROR(GLB_TEXT("Mesh::validatePayload failed: mesh '{}' has incomplete payload")
            , meshPathText
        );
        return false;
    }

    if(!MeshRuntimeValidation::validateSharedMeshPayload(
        *this,
        0u,
        false,
        GLB_TEXT("Mesh::validatePayload"),
        meshPathText
    ))
        return false;

    return true;
}


bool Mesh::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!checkVirtualPath(MeshBinaryPayload::s_MeshLoadBinaryContext))
        return false;

    clearGeometryPayload();

    const TStringView loadFailureContext = MeshBinaryPayload::s_MeshLoadBinaryContext;
    usize cursor = 0;
    MeshBinaryPayload::MeshHeaderBinary header;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        cursor,
        header,
        MeshBinaryPayload::s_MeshMagic,
        loadFailureContext,
        MeshBinaryPayload::s_MeshAssetKindLabel
    ))
        return false;

    if(header.meshClass != Core::Mesh::MeshClass::Static){
        NWB_LOGGER_ERROR(GLB_TEXT("Mesh::loadBinary failed: invalid mesh class"));
        return false;
    }
    if(!MeshAssetBinaryPayload::MeshBaseHeaderComplete(header)){
        NWB_LOGGER_ERROR(GLB_TEXT("Mesh::loadBinary failed: mesh payload is incomplete"));
        return false;
    }
    if(header.skinCount != 0u || header.skeletonJointCount != 0u || header.inverseBindMatrixCount != 0u){
        NWB_LOGGER_ERROR(GLB_TEXT("Mesh::loadBinary failed: static mesh contains skinned payload"));
        return false;
    }

    if(!readGeometryAttributeStreams(binary, cursor, header, loadFailureContext))
        return false;
    if(!readGeometryMeshletStreams(binary, cursor, header, loadFailureContext))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, loadFailureContext))
        return false;

    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

