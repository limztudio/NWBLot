// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <core/ecs/world.h>
#include <impl/ecs_mesh/runtime/mesh_requests.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedRuntimeMeshIdentity{
    Name meshKey = s_NameNone;
    u64 version = 0u;
};

// Caller resolves instances from binding handles; queries retain no GPU buffers.
[[nodiscard]] Expected<SkinnedRuntimeMeshIdentity> ResolveSkinnedRuntimeMeshIdentity(
    Core::ECS::EntityID entity,
    RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* instance
);
// Success retains raster and optional RT roles.
[[nodiscard]] Expected<RuntimeMeshDesc> BuildSkinnedRuntimeMeshDesc(
    Core::ECS::EntityID entity,
    RuntimeMeshHandle runtimeMesh,
    const MeshSkinningRuntimeInstance* instance,
    bool boundsFresh,
    bool conesFresh
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
        if(const auto identity = resolveIdentity(entity, binding))
            requests.markLive(identity->meshKey, identity->version);
        if(requests.complete())
            return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

