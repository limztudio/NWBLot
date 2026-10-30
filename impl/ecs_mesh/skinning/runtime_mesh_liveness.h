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
[[nodiscard]] Expected<SkinnedRuntimeMeshIdentity> ResolveSkinnedRuntimeMeshIdentity(Core::ECS::EntityID entity, RuntimeMeshHandle runtimeMesh, const MeshSkinningRuntimeInstance* instance);
// Success retains raster and optional RT roles.
[[nodiscard]] Expected<RuntimeMeshDesc> BuildSkinnedRuntimeMeshDesc(Core::ECS::EntityID entity, RuntimeMeshHandle runtimeMesh, const MeshSkinningRuntimeInstance* instance, b8 boundsFresh, b8 conesFresh);

namespace RuntimeMeshLivenessExceptionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ResolveIdentity>
consteval b8 ResolveIdentityIsNothrow()noexcept{
    using Result = decltype(DeclVal<ResolveIdentity&>()(DeclVal<Core::ECS::EntityID&>(), DeclVal<SkinnedMeshBindingComponent&>()));
    using Identity = Decay_T<Result>;
    return
        noexcept(DeclVal<ResolveIdentity&>()(DeclVal<Core::ECS::EntityID&>(), DeclVal<SkinnedMeshBindingComponent&>()))
        && (!IsReference_V<Result> || IsNothrowConstructible_V<Identity, Result>)
        && IsNothrowDestructible_V<Identity>
        && noexcept(static_cast<b8>(DeclVal<const Identity&>()))
        && noexcept(DeclVal<RuntimeMeshRequestSet&>().markLive(DeclVal<const Identity&>()->meshKey, DeclVal<const Identity&>()->version))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ResolveIdentity>
void MarkLiveSkinnedRuntimeMeshes(Core::ECS::World& world, ResolveIdentity&& resolveIdentity, RuntimeMeshRequestSet& requests)noexcept(RuntimeMeshLivenessExceptionDetail::ResolveIdentityIsNothrow<ResolveIdentity>()){
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

