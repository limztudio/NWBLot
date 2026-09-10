// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <core/ecs/world.h>
#include <impl/ecs_mesh/runtime/mesh_requests.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caller resolves instances from binding handles; queries retain no GPU buffers.
[[nodiscard]] bool ResolveSkinnedRuntimeMeshIdentity(
    Core::ECS::EntityID entity,
    RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* instance,
    Name& outMeshKey,
    u64& outVersion
);
// Rejection clears outputs; success retains raster and optional RT roles.
[[nodiscard]] bool BuildSkinnedRuntimeMeshDesc(
    Core::ECS::EntityID entity,
    RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* instance,
    bool boundsFresh,
    bool conesFresh,
    RuntimeMeshDesc& outMesh
);

template<typename ResolveIdentity>
void MarkLiveSkinnedRuntimeMeshes(
    Core::ECS::World& world,
    RuntimeMeshRequestSet& requests,
    ResolveIdentity&& resolveIdentity
){
    if(requests.complete())
        return;
    for(auto&& [entity, binding] : world.view<SkinnedMeshBindingComponent>()){
        Name meshKey = NAME_NONE;
        u64 version = 0u;
        if(resolveIdentity(entity, binding, meshKey, version))
            requests.markLive(meshKey, version);
        if(requests.complete())
            return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

