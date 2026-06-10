// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool ReadVector(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    ValueContainer& outValues,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = ReadBinaryVectorPayload(binary, inOutCursor, count, outValues);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed '{}' payload"), failureContext, label);
    }
    return false;
}

[[nodiscard]] bool ReadComplete(const Core::Assets::AssetBytes& binary, const usize cursor, const tchar* failureContext){
    if(cursor == binary.size())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: trailing bytes detected"), failureContext);
    return false;
}

template<typename ObjectT>
[[nodiscard]] bool NameUniqueInSpan(const ObjectT* objects, const usize count, const Name name){
    for(usize i = 0u; i < count; ++i){
        if(objects[i].name == name)
            return false;
    }
    return true;
}

[[nodiscard]] bool ModelObjectNameUnique(
    const Model::SkeletonObjectVector& skeletonObjects,
    const Model::StaticMeshObjectVector& staticMeshObjects,
    const Model::SkinnedMeshObjectVector& skinnedMeshObjects,
    const Name name,
    const usize skipSkeletonObjectCount,
    const usize skipStaticMeshObjectCount,
    const usize skipSkinnedMeshObjectCount
){
    return NameUniqueInSpan(skeletonObjects.data(), Min(skipSkeletonObjectCount, skeletonObjects.size()), name)
        && NameUniqueInSpan(staticMeshObjects.data(), Min(skipStaticMeshObjectCount, staticMeshObjects.size()), name)
        && NameUniqueInSpan(skinnedMeshObjects.data(), Min(skipSkinnedMeshObjectCount, skinnedMeshObjects.size()), name)
    ;
}

[[nodiscard]] bool ModelSkeletonObjectExists(const Model::SkeletonObjectVector& skeletonObjects, const Name name){
    for(const ModelSkeletonObject& object : skeletonObjects){
        if(object.name == name)
            return true;
    }
    return false;
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
        if(!__hidden_model_runtime::ModelObjectNameUnique(
            m_skeletonObjects,
            m_staticMeshObjects,
            m_skinnedMeshObjects,
            object.name,
            i,
            0u,
            0u
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: duplicate object name in skeleton object {}"), i);
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
        if(!__hidden_model_runtime::ModelObjectNameUnique(
            m_skeletonObjects,
            m_staticMeshObjects,
            m_skinnedMeshObjects,
            object.name,
            m_skeletonObjects.size(),
            i,
            0u
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: duplicate object name in static mesh object {}"), i);
            return false;
        }
        if(!object.parentObject && object.parentJoint){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} has joint parent without object parent"), i);
            return false;
        }
        if(object.parentJoint && !__hidden_model_runtime::ModelSkeletonObjectExists(m_skeletonObjects, object.parentObject)){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} targets a missing skeleton object parent"), i);
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
        if(!__hidden_model_runtime::ModelObjectNameUnique(
            m_skeletonObjects,
            m_staticMeshObjects,
            m_skinnedMeshObjects,
            object.name,
            m_skeletonObjects.size(),
            m_staticMeshObjects.size(),
            i
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: duplicate object name in skinned mesh object {}"), i);
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

    if(!__hidden_model_runtime::ReadVector(
        binary,
        cursor,
        header.skeletonObjectCount,
        skeletonObjectBinaries,
        NWB_TEXT("Model::loadBinary"),
        NWB_TEXT("skeleton objects")
    ))
        return false;
    if(!__hidden_model_runtime::ReadVector(
        binary,
        cursor,
        header.staticMeshObjectCount,
        staticMeshObjectBinaries,
        NWB_TEXT("Model::loadBinary"),
        NWB_TEXT("static mesh objects")
    ))
        return false;
    if(!__hidden_model_runtime::ReadVector(
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

    return __hidden_model_runtime::ReadComplete(binary, cursor, NWB_TEXT("Model::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
