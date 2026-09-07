// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_ModelAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<ModelAssetCodec>);


static constexpr u8 s_SkeletonObjectName = 1u;
static constexpr u8 s_DuplicateObjectName = 2u;
using ObjectNameMap = HashMap<Name, u8, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Model::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(m_skeletonObjects.empty() && m_staticMeshObjects.empty() && m_skinnedMeshObjects.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: model has no objects"));
        return false;
    }

    usize objectCount = 0u;
    for(const usize count : { m_skeletonObjects.size(), m_staticMeshObjects.size(), m_skinnedMeshObjects.size() }){
        if(count > Limit<usize>::s_Max - objectCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: total object count overflows"));
            return false;
        }
        objectCount += count;
    }
    Optional<__hidden_model_runtime::ObjectNameMap> objectNames;
    // One object is necessarily unique and a mesh cannot have a separate skeleton parent in that model.
    // Larger models build the complete name index once, preserving validation order across all object kinds.
    if(objectCount > 1u){
        if(objectCount > Limit<usize>::s_Max / 2u){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: name index capacity overflows"));
            return false;
        }
        // The map's default 0.5 load factor needs two buckets per possible distinct name.
        // Construct the final bucket array directly so scratch storage never needs a reserve-time rehash.
        objectNames.emplace(objectCount * 2u, scratchArena);
        const auto indexObjects = [&objectNames](const auto& objects, const u8 flags){
            for(const auto& object : objects){
                const auto inserted = objectNames->try_emplace(object.name, flags);
                if(!inserted.second)
                    inserted.first.value() |= __hidden_model_runtime::s_DuplicateObjectName;
            }
        };
        indexObjects(m_skeletonObjects, __hidden_model_runtime::s_SkeletonObjectName);
        indexObjects(m_staticMeshObjects, 0u);
        indexObjects(m_skinnedMeshObjects, 0u);
    }
    const auto nameIsUnique = [&objectNames](const Name name){
        return !objectNames || (objectNames->at(name) & __hidden_model_runtime::s_DuplicateObjectName) == 0u;
    };

    for(usize i = 0u; i < m_skeletonObjects.size(); ++i){
        const ModelSkeletonObject& object = m_skeletonObjects[i];
        if(!object.name || !object.skeleton.valid()){
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
        if(!object.name || !object.mesh.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: static mesh object {} is incomplete"), i);
            return false;
        }
        if(object.material.name() && !object.material.valid()){
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
            bool parentExists = object.parentObject == object.name;
            if(objectNames){
                const auto parent = objectNames->find(object.parentObject);
                if(parent != objectNames->end() && (parent.value() & __hidden_model_runtime::s_SkeletonObjectName) != 0u)
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
        if(!object.name || !object.mesh.valid() || !object.skin.valid() || !object.skeletonObject){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} is incomplete"), i);
            return false;
        }
        if(object.material.name() && !object.material.valid()){
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
        if(parent == objectNames->end() || (parent.value() & __hidden_model_runtime::s_SkeletonObjectName) == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Model::validatePayload failed: skinned mesh object {} targets a missing skeleton object"), i);
            return false;
        }
    }

    return true;
}

bool Model::loadBinary(const Core::Assets::AssetBytes& binary){
    Core::Alloc::ScratchArena scratchArena(AssetsModelArenaScope::s_LoadBinaryArena);
    m_skeletonObjects.clear();
    m_staticMeshObjects.clear();
    m_skinnedMeshObjects.clear();

    usize cursor = 0u;
    ModelBinaryPayload::ModelHeaderBinary header;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        cursor,
        header,
        ModelBinaryPayload::s_ModelMagic,
        NWB_TEXT("Model::loadBinary"),
        NWB_TEXT("model")
    ))
        return false;

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
        && validatePayload(scratchArena)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

