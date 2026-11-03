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

    const Expected<usize> objectCountResult = ResolveModelObjectCount(m_skeletonObjects.size(), m_staticMeshObjects.size(), m_skinnedMeshObjects.size());
    if(!objectCountResult)
        return false;
    const usize objectCount = *objectCountResult;
    Optional<__hidden_model_runtime::ObjectNameMap> objectNames;
    // One object is necessarily unique and a mesh cannot have a separate skeleton parent in that model.
    // Larger models build the complete name index once, preserving validation order across all object kinds.
    if(objectCount > 1u){
        // Construct the final bucket array at the map's default 0.5 load factor so scratch storage never needs a reserve-time rehash.
        objectNames.emplace(objectCount * 2u, scratchArena);
        const auto indexObjects = [&objectNames](const auto& objects, const u8 flags){
            for(const auto& object : objects){
                const auto inserted = objectNames->try_emplace(object.name, flags);
                if(!inserted.second)
                    MarkDuplicateModelObjectNameFlags(inserted.first.value());
            }
        };
        indexObjects(m_skeletonObjects, s_ModelSkeletonObjectNameFlag);
        indexObjects(m_staticMeshObjects, 0u);
        indexObjects(m_skinnedMeshObjects, 0u);
    }
    const auto nameIsUnique = [&objectNames](const Name name)noexcept{
        return !objectNames || !IsModelObjectFlaggedDuplicate(objectNames->at(name));
    };

    for(usize i = 0u; i < m_skeletonObjects.size(); ++i){
        const ModelSkeletonObject& object = m_skeletonObjects[i];
        if(!ValidateModelSkeletonObject(object, i, nameIsUnique(object.name)))
            return false;
    }

    for(usize i = 0u; i < m_staticMeshObjects.size(); ++i){
        const ModelStaticMeshObject& object = m_staticMeshObjects[i];
        b8 foundParent = false;
        u8 parentFlags = 0u;
        if(object.parentObject && objectNames){
            const auto parent = objectNames->find(object.parentObject);
            foundParent = parent != objectNames->end();
            if(foundParent)
                parentFlags = parent.value();
        }
        b8 parentExists = false;
        b8 parentIsSkeleton = false;
        ResolveModelStaticMeshParentState(object.name, object.parentObject, foundParent, parentFlags, parentExists, parentIsSkeleton);
        if(!ValidateModelStaticMeshObject(object, i, nameIsUnique(object.name), parentExists, parentIsSkeleton))
            return false;
    }

    for(usize i = 0u; i < m_skinnedMeshObjects.size(); ++i){
        const ModelSkinnedMeshObject& object = m_skinnedMeshObjects[i];
        b8 foundParent = false;
        u8 parentFlags = 0u;
        if(objectNames){
            const auto parent = objectNames->find(object.skeletonObject);
            foundParent = parent != objectNames->end();
            if(foundParent)
                parentFlags = parent.value();
        }
        if(!ValidateModelSkinnedMeshObject(object, i, nameIsUnique(object.name), ResolveModelSkinnedMeshTargetState(foundParent, parentFlags)))
            return false;
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

