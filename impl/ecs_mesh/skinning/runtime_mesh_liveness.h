// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/ecs_mesh/runtime/mesh_requests.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedRuntimeMeshIdentity{
    Name meshKey = s_NameNone;
    u64 version = 0u;
};

// Caller resolves instances from binding handles; queries retain no GPU buffers.
[[nodiscard]] Expected<SkinnedRuntimeMeshIdentity> ResolveSkinnedRuntimeMeshIdentity(Core::ECS::EntityID entity, RuntimeMeshHandle runtimeMesh, const MeshSkinningRuntimeInstance* instance);
// Success retains raster and optional RT roles.
[[nodiscard]] Expected<RuntimeMeshDesc> BuildSkinnedRuntimeMeshDesc(Core::ECS::EntityID entity, RuntimeMeshHandle runtimeMesh, const MeshSkinningRuntimeInstance* instance, bool boundsFresh, bool conesFresh);

namespace RuntimeMeshLivenessExceptionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ResolveIdentity>
consteval bool ResolveIdentityIsNothrow()noexcept{
    using Result = decltype(DeclVal<ResolveIdentity&>()(DeclVal<Core::ECS::EntityID&>(), DeclVal<SkinnedMeshBindingComponent&>()));
    using Identity = Decay_T<Result>;
    return
        noexcept(DeclVal<ResolveIdentity&>()(DeclVal<Core::ECS::EntityID&>(), DeclVal<SkinnedMeshBindingComponent&>()))
        && (!IsReference_V<Result> || IsNothrowConstructible_V<Identity, Result>)
        && IsNothrowDestructible_V<Identity>
        && noexcept(static_cast<bool>(DeclVal<const Identity&>()))
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

