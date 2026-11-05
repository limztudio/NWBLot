// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_ModelAssetCodecAutoRegistrar, ModelAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Model::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Model::validatePayload")))
        return false;

    return ValidateModelPayloadObjects(m_skeletonObjects, m_staticMeshObjects, m_skinnedMeshObjects, scratchArena);
}

b8 Model::loadBinary(const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(s_AssetsModelLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ModelHeaderBinary>(binary, s_ModelBinaryMagic, s_ModelLoadBinaryContext, s_ModelAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const ModelHeaderBinary& header = *headerResult;

    if(!ValidateModelHeaderBinary(header))
        return false;
    Model candidate(m_skeletonObjects.get_allocator().arena(), virtualPath());
    Vector<ModelSkeletonObjectBinary, Core::Alloc::ScratchArena> skeletonObjectBinaries(scratchArena);
    Vector<ModelStaticMeshObjectBinary, Core::Alloc::ScratchArena> staticMeshObjectBinaries(scratchArena);
    Vector<ModelSkinnedMeshObjectBinary, Core::Alloc::ScratchArena> skinnedMeshObjectBinaries(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.skeletonObjectCount, s_ModelLoadBinaryContext, s_ModelSkeletonObjectsPayloadLabel, cursor, skeletonObjectBinaries))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.staticMeshObjectCount, s_ModelLoadBinaryContext, s_ModelStaticMeshObjectsPayloadLabel, cursor, staticMeshObjectBinaries))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.skinnedMeshObjectCount, s_ModelLoadBinaryContext, s_ModelSkinnedMeshObjectsPayloadLabel, cursor, skinnedMeshObjectBinaries))
        return false;

    UnpackModelSkeletonObjectBinaries(skeletonObjectBinaries, candidate.m_skeletonObjects);
    UnpackModelStaticMeshObjectBinaries(staticMeshObjectBinaries, candidate.m_staticMeshObjects);
    UnpackModelSkinnedMeshObjectBinaries(skinnedMeshObjectBinaries, candidate.m_skinnedMeshObjects);

    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_ModelLoadBinaryContext) || !candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}

void Model::setObjects(SkeletonObjectVector&& skeletonObjects, StaticMeshObjectVector&& staticMeshObjects, SkinnedMeshObjectVector&& skinnedMeshObjects)noexcept{
    m_skeletonObjects = Move(skeletonObjects);
    m_staticMeshObjects = Move(staticMeshObjects);
    m_skinnedMeshObjects = Move(skinnedMeshObjects);
}

void Model::takeObjects(SkeletonObjectVector& outSkeletonObjects, StaticMeshObjectVector& outStaticMeshObjects, SkinnedMeshObjectVector& outSkinnedMeshObjects)noexcept{
    outSkeletonObjects = Move(m_skeletonObjects);
    outStaticMeshObjects = Move(m_staticMeshObjects);
    outSkinnedMeshObjects = Move(m_skinnedMeshObjects);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

