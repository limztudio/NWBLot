// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_mesh_asset.h"

#include "mesh_asset_binary_payload.h"
#include "skinned_mesh_binary_payload.h"

#include <global/binary.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedMeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(SkinnedMesh::s_AssetTypeText)
        );
        return false;
    }

    const SkinnedMesh& mesh = static_cast<const SkinnedMesh&>(asset);
    if(!mesh.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinnedMeshBinaryPayload::SkinnedMeshHeaderBinary);
    const bool canReserve = MeshAssetBinaryPayload::AddMeshBaseReserveBytes(reserveBytes, mesh)
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.skinStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.inverseBindMatrices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SkinnedMeshBinaryPayload::SkinnedMeshHeaderBinary header;
    header.magic = SkinnedMeshBinaryPayload::s_SkinnedMeshMagic;
    MeshAssetBinaryPayload::FillMeshBaseHeader(header, mesh);
    header.skinCount = static_cast<u64>(mesh.skinStream().size());
    header.skeletonJointCount = static_cast<u64>(mesh.skeletonJointCount());
    header.inverseBindMatrixCount = static_cast<u64>(mesh.inverseBindMatrices().size());
    AppendPOD(outBinary, header);

    const tchar* const serializeFailureContext = NWB_TEXT("SkinnedMeshAssetCodec::serialize");
    auto appendVector = [&](const auto& values, const tchar* label){
        return MeshAssetBinaryPayload::AppendVector(outBinary, values, serializeFailureContext, label);
    };
    if(!MeshAssetBinaryPayload::AppendMeshAttributeStreams(outBinary, mesh, serializeFailureContext))
        return false;
    if(!appendVector(mesh.skinStream(), NWB_TEXT("skin")))
        return false;
    if(!appendVector(mesh.inverseBindMatrices(), NWB_TEXT("inverse bind matrices")))
        return false;
    return MeshAssetBinaryPayload::AppendMeshletStreams(outBinary, mesh, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

