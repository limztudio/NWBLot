// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/ecs_mesh/components.adhd.h>
#include <impl/ecs_mesh/runtime/mesh_requests.h>
#include <impl/ecs_mesh/skinning/submission_state.adhd.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caller resolves instances from binding handles; queries retain no GPU buffers.
[[nodiscard]] inline Expected<SkinnedRuntimeMeshIdentity> ResolveSkinnedRuntimeMeshIdentity(const Core::ECS::EntityID entity, const RuntimeMeshHandle runtimeMesh, const MeshSkinningRuntimeInstance* const instance){
    if(!instance)
        return MakeUnexpected(Failure{});
    const Expected<SkinnedRuntimeMeshIdentity> identity = TryResolveSkinnedRuntimeMeshIdentity(entity, runtimeMesh, instance->entity, instance->dirtyFlags, instance->sourceName, instance->handle.value, instance->editRevision);
    if(!identity)
        return MakeUnexpected(Failure{});
    NWB_ASSERT(instance->valid());
    NWB_ASSERT(instance->meshlets.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(instance->meshletPrimitiveIndices.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(identity->meshKey);
    return *identity;
}
// Success retains raster and optional RT roles.
[[nodiscard]] inline Expected<RuntimeMeshDesc> BuildSkinnedRuntimeMeshDesc(const Core::ECS::EntityID entity, const RuntimeMeshHandle runtimeMesh, const MeshSkinningRuntimeInstance* const instance, const b8 boundsFresh, const b8 conesFresh){
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
    static_cast<RuntimeMeshBuffers&>(mesh) = instance->runtimeMeshBuffers();
    mesh.triangleIndexBuffer = instance->triangleIndexBuffer;
    mesh.attributeBuffer = instance->attributeBuffer;
    mesh.solidTriangleWords = instance->solidTriangleWords;
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

