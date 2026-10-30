// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload_io.h"
#include "binary_payload.h"
#include "runtime_validation.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_MeshAssetCodecAutoRegistrar, MeshAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Mesh::loadBinary(const Span<const u8> binary){
    if(!checkVirtualPath(MeshBinaryPayload::s_MeshLoadBinaryContext))
        return false;

    const TStringView loadFailureContext = MeshBinaryPayload::s_MeshLoadBinaryContext;
    usize cursor = 0;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<MeshBinaryHeader>(binary, s_MeshBinaryMagic, loadFailureContext, MeshBinaryPayload::s_MeshAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const MeshBinaryHeader& header = *headerResult;

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

    Mesh candidate(m_positionStream.get_allocator().arena(), virtualPath());
    if(!candidate.readGeometryAttributeStreams(binary, header, loadFailureContext, cursor))
        return false;
    if(!candidate.readGeometryMeshletStreams(binary, header, loadFailureContext, cursor))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, loadFailureContext))
        return false;

    Core::Alloc::ScratchArena scratchArena(s_MeshValidatePayloadArena);
    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


b8 Mesh::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    const TString<Core::Alloc::ScratchArena> meshPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(hasIncompleteGeometryPayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has incomplete payload"), meshPathText);
        return false;
    }
    const usize triangleCount = meshletPrimitiveIndices().size() / s_MeshletTriangleIndexCount;
    const u32 tailBits = static_cast<u32>(triangleCount % s_MeshletSolidTriangleWordBits);
    if(tailBits != 0u && (solidTriangleWords().back() >> tailBits) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has invalid solid triangle padding"), meshPathText);
        return false;
    }

    if(!MeshRuntimeValidation::ValidateSharedMeshPayload(*this, NWB_TEXT("Mesh::validatePayload"), meshPathText))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
