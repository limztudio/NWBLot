// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_asset.h"

#include "binary_payload_io.h"
#include "meshlet_ref_codec.h"
#include "meshlet_payload_packing.h"
#include "payload_validation.h"
#include "skinned_binary_payload.h"
#include "skinned_validation.h"

#include <core/assets/auto_registration.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateSkinnedMeshAssetCodec(){
    return MakeUnique<SkinnedMeshAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_SkinnedMeshAssetCodecAutoRegistrar(&CreateSkinnedMeshAssetCodec);

#include "runtime_validation.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedMesh::validatePayload()const{
    Core::Alloc::ScratchArena scratchArena;
    const TString<Core::Alloc::ScratchArena> meshPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(!Core::Mesh::MeshClassUsesSkinning(m_meshClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' has invalid mesh class '{}'")
            , meshPathText
            , StringConvert(Core::Mesh::MeshClassText(m_meshClass))
        );
        return false;
    }

    if(hasIncompleteGeometryPayload() || m_skin.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' has incomplete payload")
            , meshPathText
        );
        return false;
    }

    if(m_skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' has skin but no skeleton joint count")
            , meshPathText
        );
        return false;
    }
    if(m_skeletonJointCount > SkinnedMeshBinaryPayload::s_SkinnedMeshSkeletonJointLimit){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' skeleton joint count exceeds skin stream limits")
            , meshPathText
        );
        return false;
    }
    if(m_inverseBindMatrices.size() != m_skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' inverse bind matrix count must match skeleton joint count")
            , meshPathText
        );
        return false;
    }
    if(!SkinnedMeshValidation::ValidInverseBindMatrices(m_inverseBindMatrices, m_skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' inverse bind matrices are invalid")
            , meshPathText
        );
        return false;
    }

    if(!__hidden_skinned_asset::ValidateSharedMeshPayload(
        *this,
        m_skin.size(),
        true,
        NWB_TEXT("SkinnedMesh::validatePayload"),
        meshPathText
    ))
        return false;

    for(usize i = 0u; i < m_skin.size(); ++i){
        const SkinInfluence4& skin = m_skin[i];
        const SIMDVector weights = VectorSet(
            skin.weight[0u],
            skin.weight[1u],
            skin.weight[2u],
            skin.weight[3u]
        );
        if(SkinnedMeshValidation::ValidSkinInfluenceWeights(weights)){
            u32 failedJoint = 0u;
            if(SkinnedMeshValidation::SkinInfluenceFitsSkeleton(skin, m_skeletonJointCount, failedJoint))
                continue;
        }

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::validatePayload failed: mesh '{}' skin influence {} is invalid")
            , meshPathText
            , i
        );
        return false;
    }

    return true;
}

bool SkinnedMesh::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::loadBinary failed: virtual path is empty"));
        return false;
    }

    clearGeometryPayload();
    m_skin.clear();
    m_inverseBindMatrices.clear();
    m_meshClass = Core::Mesh::MeshClass::Invalid;
    m_skeletonJointCount = 0u;

    const tchar* const loadFailureContext = NWB_TEXT("SkinnedMesh::loadBinary");
    usize cursor = 0;
    SkinnedMeshBinaryPayload::SkinnedMeshHeaderBinary header;
    if(!MeshAssetBinaryPayload::ReadHeader(
        binary,
        cursor,
        header,
        SkinnedMeshBinaryPayload::s_SkinnedMeshMagic,
        loadFailureContext
    ))
        return false;

    if(!Core::Mesh::MeshClassUsesSkinning(header.meshClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::loadBinary failed: invalid mesh class"));
        return false;
    }
    if(
        !MeshAssetBinaryPayload::MeshBaseHeaderComplete(header)
        || header.skinCount == 0u
        || header.skeletonJointCount == 0u
        || header.inverseBindMatrixCount != header.skeletonJointCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::loadBinary failed: mesh payload is incomplete"));
        return false;
    }
    if(header.skeletonJointCount > SkinnedMeshBinaryPayload::s_SkinnedMeshSkeletonJointLimit){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh::loadBinary failed: skeleton joint count exceeds skin stream limits"));
        return false;
    }

    if(!readGeometryAttributeStreams(binary, cursor, header, loadFailureContext))
        return false;
    if(!MeshAssetBinaryPayload::ReadVector(binary, cursor, header.skinCount, m_skin, loadFailureContext, NWB_TEXT("skin")))
        return false;
    if(!MeshAssetBinaryPayload::ReadVector(
        binary,
        cursor,
        header.inverseBindMatrixCount,
        m_inverseBindMatrices,
        loadFailureContext,
        NWB_TEXT("inverse bind matrices")
    ))
        return false;
    if(!readGeometryMeshletStreams(binary, cursor, header, loadFailureContext))
        return false;
    if(!MeshAssetBinaryPayload::ReadComplete(binary, cursor, loadFailureContext))
        return false;

    m_meshClass = header.meshClass;
    m_skeletonJointCount = static_cast<u32>(header.skeletonJointCount);

    return validatePayload();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

