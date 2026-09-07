// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_mesh_liveness.h"

#include "resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveSkinnedRuntimeMeshIdentity(
    const Core::ECS::EntityID entity,
    const RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* const instance,
    Name& outMeshKey,
    u64& outVersion
){
    outMeshKey = NAME_NONE;
    outVersion = 0u;
    if(!runtimeMesh.valid())
        return false;

    if(!instance || instance->entity != entity)
        return false;
    if((instance->dirtyFlags & (RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return false;
    NWB_ASSERT(instance->valid());
    NWB_ASSERT(instance->meshlets.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(instance->meshletPrimitiveIndices.size() <= static_cast<usize>(Limit<u32>::s_Max));

    outMeshKey = DeriveRuntimeResourceName(instance->sourceName, instance->handle.value, instance->editRevision, "skinned_draw");
    outVersion = instance->editRevision;
    NWB_ASSERT(outMeshKey);
    return true;
}

bool BuildSkinnedRuntimeMeshDesc(
    const Core::ECS::EntityID entity,
    const RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* const instance,
    const bool boundsFresh,
    const bool conesFresh,
    RuntimeMeshDesc& outMesh
){
    outMesh = RuntimeMeshDesc{};
    if(!ResolveSkinnedRuntimeMeshIdentity(entity, runtimeMesh, instance, outMesh.meshKey, outMesh.version))
        return false;

    outMesh.entity = entity;
    outMesh.positionBuffer = instance->skinnedPositionBuffer;
    outMesh.normalBuffer = instance->skinnedNormalBuffer;
    outMesh.tangentBuffer = instance->skinnedTangentBuffer;
    outMesh.uv0Buffer = instance->uv0Buffer;
    outMesh.colorBuffer = instance->colorBuffer;
    outMesh.meshletDescBuffer = instance->meshletDescBuffer;
    outMesh.meshletBoundsBuffer = instance->meshletBoundsBuffer;
    outMesh.meshletPositionRefDeltaBuffer = instance->meshletPositionRefDeltaBuffer;
    outMesh.meshletAttributeRefDeltaBuffer = instance->meshletAttributeRefDeltaBuffer;
    outMesh.meshletLocalVertexRefBuffer = instance->meshletLocalVertexRefBuffer;
    outMesh.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    outMesh.triangleIndexBuffer = instance->triangleIndexBuffer;
    outMesh.attributeBuffer = instance->attributeBuffer;
    outMesh.localBounds = instance->localBounds;
    outMesh.meshletCount = static_cast<u32>(instance->meshlets.size());
    outMesh.meshletPrimitiveIndexCount = static_cast<u32>(instance->meshletPrimitiveIndices.size());
    outMesh.dynamicMeshletBoundsFresh = boundsFresh;
    outMesh.dynamicMeshletConesFresh = conesFresh;
    NWB_ASSERT(outMesh.valid());
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

