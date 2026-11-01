// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_ModelAssetCodecAutoRegistrar, ModelAssetCodec);


using ObjectNameMap = HashMap<Name, u8, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Model::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Model::validatePayload")))
        return false;
    if(m_skeletonObjects.empty() && m_staticMeshObjects.empty() && m_skinnedMeshObjects.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: model has no objects"));
        return false;
    }

    usize objectCount = 0u;
    if(!TryComputeTotalModelObjectCount(m_skeletonObjects.size(), m_staticMeshObjects.size(), m_skinnedMeshObjects.size(), objectCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: total object count overflows"));
        return false;
    }
    Optional<__hidden_model_runtime::ObjectNameMap> objectNames;
    // One object is necessarily unique and a mesh cannot have a separate skeleton parent in that model.
    // Larger models build the complete name index once, preserving validation order across all object kinds.
    if(objectCount > 1u){
        if(objectCount > Limit<usize>::s_Max / 2u){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: name index capacity overflows"));
            return false;
        }
        // Construct the final bucket array at the map's default 0.5 load factor so scratch storage never needs a reserve-time rehash.
        objectNames.emplace(objectCount * 2u, scratchArena);
        const auto indexObjects = [&objectNames](const auto& objects, const u8 flags){
            for(const auto& object : objects){
                const auto inserted = objectNames->try_emplace(object.name, flags);
                if(!inserted.second)
                    inserted.first.value() |= s_ModelDuplicateObjectNameFlag;
            }
        };
        indexObjects(m_skeletonObjects, s_ModelSkeletonObjectNameFlag);
        indexObjects(m_staticMeshObjects, 0u);
        indexObjects(m_skinnedMeshObjects, 0u);
    }
    const auto nameIsUnique = [&objectNames](const Name name)noexcept{
        return !objectNames || (objectNames->at(name) & s_ModelDuplicateObjectNameFlag) == 0u;
    };

    for(usize i = 0u; i < m_skeletonObjects.size(); ++i){
        const ModelSkeletonObject& object = m_skeletonObjects[i];
        if(!MatrixIsAffine(LoadFloat(object.transform), 0.0f)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skeleton object {} has non-finite transform"), i);
            return false;
        }
        if(!IsCompleteModelSkeletonObject(object)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skeleton object {} is incomplete"), i);
            return false;
        }
        if(!nameIsUnique(object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skeleton object {} name is duplicated in the model"), i);
            return false;
        }
    }

    for(usize i = 0u; i < m_staticMeshObjects.size(); ++i){
        const ModelStaticMeshObject& object = m_staticMeshObjects[i];
        if(!MatrixIsAffine(LoadFloat(object.transform), 0.0f)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} has non-finite transform"), i);
            return false;
        }
        if(!IsCompleteModelStaticMeshObject(object)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} is incomplete"), i);
            return false;
        }
        if(!IsValidOptionalModelMaterialRef(object.material)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} has invalid material reference"), i);
            return false;
        }
        if(!nameIsUnique(object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} name is duplicated in the model"), i);
            return false;
        }
        if(!object.parentObject && object.parentJoint){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} has joint parent without object parent"), i);
            return false;
        }
        if(object.parentObject){
            b8 parentExists = object.parentObject == object.name;
            if(objectNames){
                const auto parent = objectNames->find(object.parentObject);
                if(parent != objectNames->end() && (parent.value() & s_ModelSkeletonObjectNameFlag) != 0u)
                    continue;
                parentExists = parent != objectNames->end();
            }
            if(parentExists){
                NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} parent_object must reference a skeleton object"), i);
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} targets a missing skeleton object parent"), i);
            }
            return false;
        }
    }

    for(usize i = 0u; i < m_skinnedMeshObjects.size(); ++i){
        const ModelSkinnedMeshObject& object = m_skinnedMeshObjects[i];
        if(!MatrixIsAffine(LoadFloat(object.transform), 0.0f)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} has non-finite transform"), i);
            return false;
        }
        if(!IsCompleteModelSkinnedMeshObject(object)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} is incomplete"), i);
            return false;
        }
        if(!IsValidOptionalModelMaterialRef(object.material)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} has invalid material reference"), i);
            return false;
        }
        if(!nameIsUnique(object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} name is duplicated in the model"), i);
            return false;
        }
        if(!objectNames){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} targets a missing skeleton object"), i);
            return false;
        }
        const auto parent = objectNames->find(object.skeletonObject);
        if(parent == objectNames->end() || (parent.value() & s_ModelSkeletonObjectNameFlag) == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} targets a missing skeleton object"), i);
            return false;
        }
    }

    return true;
}

b8 Model::loadBinary(const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(s_AssetsModelLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ModelHeaderBinary>(binary, s_ModelBinaryMagic, s_ModelLoadBinaryContext, s_ModelAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const ModelHeaderBinary& header = *headerResult;

    if(!IsValidModelHeaderBinary(header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::loadBinary failed: reserved header fields must be zero"));
        return false;
    }
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

    candidate.m_skeletonObjects.reserve(skeletonObjectBinaries.size());
    for(const ModelSkeletonObjectBinary& objectBinary : skeletonObjectBinaries)
        candidate.m_skeletonObjects.push_back(UnpackModelSkeletonObjectBinary(objectBinary));

    candidate.m_staticMeshObjects.reserve(staticMeshObjectBinaries.size());
    for(const ModelStaticMeshObjectBinary& objectBinary : staticMeshObjectBinaries)
        candidate.m_staticMeshObjects.push_back(UnpackModelStaticMeshObjectBinary(objectBinary));

    candidate.m_skinnedMeshObjects.reserve(skinnedMeshObjectBinaries.size());
    for(const ModelSkinnedMeshObjectBinary& objectBinary : skinnedMeshObjectBinaries)
        candidate.m_skinnedMeshObjects.push_back(UnpackModelSkinnedMeshObjectBinary(objectBinary));

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

