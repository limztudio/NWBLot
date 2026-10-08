// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_mesh_liveness.h"

#include "resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SkinnedRuntimeMeshIdentity> ResolveSkinnedRuntimeMeshIdentity(
    const Core::ECS::EntityID entity,
    const RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* const instance
){
    SkinnedRuntimeMeshIdentity identity;
    if(!runtimeMesh.valid())
        return MakeUnexpected(Failure{});

    if(!instance || instance->entity != entity)
        return MakeUnexpected(Failure{});
    if((instance->dirtyFlags & (RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return MakeUnexpected(Failure{});
    NWB_ASSERT(instance->valid());
    NWB_ASSERT(instance->meshlets.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(instance->meshletPrimitiveIndices.size() <= static_cast<usize>(Limit<u32>::s_Max));

    identity.meshKey = DeriveRuntimeResourceName(instance->sourceName, instance->handle.value, instance->editRevision, "skinned_draw");
    identity.version = instance->editRevision;
    NWB_ASSERT(identity.meshKey);
    return identity;
}

Expected<RuntimeMeshDesc> BuildSkinnedRuntimeMeshDesc(
    const Core::ECS::EntityID entity,
    const RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* const instance,
    const bool boundsFresh,
    const bool conesFresh
){
    const auto identity = ResolveSkinnedRuntimeMeshIdentity(entity, runtimeMesh, instance);
    if(!identity)
        return MakeUnexpected(Failure{});
    RuntimeMeshDesc mesh;
    mesh.meshKey = identity->meshKey;
    mesh.version = identity->version;

    mesh.entity = entity;
    mesh.geometryContentRevision = instance->deformationState.contentRevision();
    if(mesh.geometryContentRevision != 0u){
        mesh.localBoundsBuffer = instance->localBoundsBuffer;
        mesh.meshletLocalBoundsBuffer = instance->meshletLocalBoundsBuffer;
    }
    mesh.positionBuffer = instance->skinnedPositionBuffer;
    mesh.normalBuffer = instance->skinnedNormalBuffer;
    mesh.tangentBuffer = instance->skinnedTangentBuffer;
    mesh.uv0Buffer = instance->uv0Buffer;
    mesh.colorBuffer = instance->colorBuffer;
    mesh.meshletDescBuffer = instance->meshletDescBuffer;
    mesh.meshletBoundsBuffer = instance->meshletBoundsBuffer;
    mesh.meshletPositionRefDeltaBuffer = instance->meshletPositionRefDeltaBuffer;
    mesh.meshletAttributeRefDeltaBuffer = instance->meshletAttributeRefDeltaBuffer;
    mesh.meshletLocalVertexRefBuffer = instance->meshletLocalVertexRefBuffer;
    mesh.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    mesh.triangleIndexBuffer = instance->triangleIndexBuffer;
    mesh.attributeBuffer = instance->attributeBuffer;
    mesh.localBounds = instance->localBounds;
    mesh.meshletCount = static_cast<u32>(instance->meshlets.size());
    mesh.meshletPrimitiveIndexCount = static_cast<u32>(instance->meshletPrimitiveIndices.size());
    mesh.dynamicMeshletBoundsFresh = boundsFresh;
    mesh.dynamicMeshletConesFresh = conesFresh;
    NWB_ASSERT(mesh.valid());
    return mesh;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

