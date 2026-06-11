// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateModelAssetCodec(){
    return MakeUnique<ModelAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_ModelAssetCodecAutoRegistrar(&CreateModelAssetCodec);


template<typename ObjectT>
[[nodiscard]] bool NameUniqueInSpan(const ObjectT* objects, const usize count, const Name name){
    for(usize i = 0u; i < count; ++i){
        if(objects[i].name == name)
            return false;
    }
    return true;
}

[[nodiscard]] bool ModelSkeletonObjectExists(const Model::SkeletonObjectVector& skeletonObjects, const Name name){
    for(const ModelSkeletonObject& object : skeletonObjects){
        if(object.name == name)
            return true;
    }
    return false;
}

[[nodiscard]] bool ModelStaticMeshObjectExists(const Model::StaticMeshObjectVector& staticMeshObjects, const Name name){
    for(const ModelStaticMeshObject& object : staticMeshObjects){
        if(object.name == name)
            return true;
    }
    return false;
}

[[nodiscard]] bool ModelSkinnedMeshObjectExists(const Model::SkinnedMeshObjectVector& skinnedMeshObjects, const Name name){
    for(const ModelSkinnedMeshObject& object : skinnedMeshObjects){
        if(object.name == name)
            return true;
    }
    return false;
}

[[nodiscard]] bool ModelObjectNameIsUnique(
    const Model::SkeletonObjectVector& skeletonObjects,
    const Model::StaticMeshObjectVector& staticMeshObjects,
    const Model::SkinnedMeshObjectVector& skinnedMeshObjects,
    const Name name
){
    u32 count = 0u;
    for(const ModelSkeletonObject& object : skeletonObjects)
        count += object.name == name ? 1u : 0u;
    for(const ModelStaticMeshObject& object : staticMeshObjects)
        count += object.name == name ? 1u : 0u;
    for(const ModelSkinnedMeshObject& object : skinnedMeshObjects)
        count += object.name == name ? 1u : 0u;
    return count == 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Model::validatePayload()const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(m_skeletonObjects.empty() && m_staticMeshObjects.empty() && m_skinnedMeshObjects.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: model has no objects"));
        return false;
    }

    for(usize i = 0u; i < m_skeletonObjects.size(); ++i){
        const ModelSkeletonObject& object = m_skeletonObjects[i];
        if(!object.name || !object.skeleton.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skeleton object {} is incomplete"), i);
            return false;
        }
        if(!__hidden_model_runtime::NameUniqueInSpan(m_skeletonObjects.data(), i, object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: duplicate object name in skeleton object {}"), i);
            return false;
        }
        if(!__hidden_model_runtime::ModelObjectNameIsUnique(m_skeletonObjects, m_staticMeshObjects, m_skinnedMeshObjects, object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skeleton object {} name is duplicated in the model"), i);
            return false;
        }
    }

    for(usize i = 0u; i < m_staticMeshObjects.size(); ++i){
        const ModelStaticMeshObject& object = m_staticMeshObjects[i];
        if(!object.name || !object.mesh.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} is incomplete"), i);
            return false;
        }
        if(object.material.name() && !object.material.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} has invalid material reference"), i);
            return false;
        }
        if(!__hidden_model_runtime::NameUniqueInSpan(m_staticMeshObjects.data(), i, object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: duplicate object name in static mesh object {}"), i);
            return false;
        }
        if(!__hidden_model_runtime::ModelObjectNameIsUnique(m_skeletonObjects, m_staticMeshObjects, m_skinnedMeshObjects, object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} name is duplicated in the model"), i);
            return false;
        }
        if(!object.parentObject && object.parentJoint){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} has joint parent without object parent"), i);
            return false;
        }
        if(object.parentObject && !__hidden_model_runtime::ModelSkeletonObjectExists(m_skeletonObjects, object.parentObject)){
            if(__hidden_model_runtime::ModelStaticMeshObjectExists(m_staticMeshObjects, object.parentObject)
                || __hidden_model_runtime::ModelSkinnedMeshObjectExists(m_skinnedMeshObjects, object.parentObject)
            ){
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
        if(!object.name || !object.mesh.valid() || !object.skin.valid() || !object.skeletonObject){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} is incomplete"), i);
            return false;
        }
        if(object.material.name() && !object.material.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} has invalid material reference"), i);
            return false;
        }
        if(!__hidden_model_runtime::NameUniqueInSpan(m_skinnedMeshObjects.data(), i, object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: duplicate object name in skinned mesh object {}"), i);
            return false;
        }
        if(!__hidden_model_runtime::ModelObjectNameIsUnique(m_skeletonObjects, m_staticMeshObjects, m_skinnedMeshObjects, object.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} name is duplicated in the model"), i);
            return false;
        }
        if(!__hidden_model_runtime::ModelSkeletonObjectExists(m_skeletonObjects, object.skeletonObject)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} targets a missing skeleton object"), i);
            return false;
        }
    }

    return true;
}

bool Model::loadBinary(const Core::Assets::AssetBytes& binary){
    m_skeletonObjects.clear();
    m_staticMeshObjects.clear();
    m_skinnedMeshObjects.clear();

    usize cursor = 0u;
    ModelBinaryPayload::ModelHeaderBinary header;
    if(!ReadPOD(binary, cursor, header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::loadBinary failed: malformed header"));
        return false;
    }
    if(header.magic != ModelBinaryPayload::s_ModelMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::loadBinary failed: invalid model asset format; recook required"));
        return false;
    }

    Core::Assets::AssetVector<ModelBinaryPayload::ModelSkeletonObjectBinary> skeletonObjectBinaries(m_skeletonObjects.get_allocator().arena());
    Core::Assets::AssetVector<ModelBinaryPayload::ModelStaticMeshObjectBinary> staticMeshObjectBinaries(m_staticMeshObjects.get_allocator().arena());
    Core::Assets::AssetVector<ModelBinaryPayload::ModelSkinnedMeshObjectBinary> skinnedMeshObjectBinaries(m_skinnedMeshObjects.get_allocator().arena());

    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.skeletonObjectCount,
        skeletonObjectBinaries,
        NWB_TEXT("Model::loadBinary"),
        NWB_TEXT("skeleton objects")
    ))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.staticMeshObjectCount,
        staticMeshObjectBinaries,
        NWB_TEXT("Model::loadBinary"),
        NWB_TEXT("static mesh objects")
    ))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.skinnedMeshObjectCount,
        skinnedMeshObjectBinaries,
        NWB_TEXT("Model::loadBinary"),
        NWB_TEXT("skinned mesh objects")
    ))
        return false;

    m_skeletonObjects.reserve(skeletonObjectBinaries.size());
    for(const ModelBinaryPayload::ModelSkeletonObjectBinary& objectBinary : skeletonObjectBinaries){
        ModelSkeletonObject object;
        object.name = Name(objectBinary.nameHash);
        object.skeleton.virtualPath = Name(objectBinary.skeletonNameHash);
        object.transform = objectBinary.transform;
        m_skeletonObjects.push_back(object);
    }

    m_staticMeshObjects.reserve(staticMeshObjectBinaries.size());
    for(const ModelBinaryPayload::ModelStaticMeshObjectBinary& objectBinary : staticMeshObjectBinaries){
        ModelStaticMeshObject object;
        object.name = Name(objectBinary.nameHash);
        object.mesh.virtualPath = Name(objectBinary.meshNameHash);
        object.material.virtualPath = Name(objectBinary.materialNameHash);
        object.parentObject = Name(objectBinary.parentObjectNameHash);
        object.parentJoint = Name(objectBinary.parentJointNameHash);
        object.transform = objectBinary.transform;
        m_staticMeshObjects.push_back(object);
    }

    m_skinnedMeshObjects.reserve(skinnedMeshObjectBinaries.size());
    for(const ModelBinaryPayload::ModelSkinnedMeshObjectBinary& objectBinary : skinnedMeshObjectBinaries){
        ModelSkinnedMeshObject object;
        object.name = Name(objectBinary.nameHash);
        object.mesh.virtualPath = Name(objectBinary.meshNameHash);
        object.skin.virtualPath = Name(objectBinary.skinNameHash);
        object.material.virtualPath = Name(objectBinary.materialNameHash);
        object.skeletonObject = Name(objectBinary.skeletonObjectNameHash);
        object.transform = objectBinary.transform;
        m_skinnedMeshObjects.push_back(object);
    }

    return Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Model::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

