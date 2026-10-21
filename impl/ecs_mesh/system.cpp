// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "runtime/mesh_requests.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSystem::MeshSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world)
    : Core::ECS::ISystem(arena)
    , m_world(world)
    , m_runtimeMeshProviders(arena)
{
    readAccess<MeshComponent>();
    readAccess<SkinnedMeshBindingComponent>();
}

Expected<void, Core::CpuTaskAdmissionError::Enum> MeshSystem::update(Core::ECS::World& world, f32 delta)noexcept{
    static_cast<void>(world);
    static_cast<void>(delta);
    return {};
}

MeshComponent* MeshSystem::findMesh(const Core::ECS::EntityID entity)noexcept{
    return m_world.tryGetComponent<MeshComponent>(entity);
}

const MeshComponent* MeshSystem::findMesh(const Core::ECS::EntityID entity)const noexcept{
    return m_world.tryGetComponent<MeshComponent>(entity);
}

Expected<Core::Assets::AssetRef<Mesh>> MeshSystem::resolveMesh(const Core::ECS::EntityID entity)const noexcept{
    const MeshComponent* mesh = findMesh(entity);
    if(!mesh || !mesh->mesh.valid())
        return MakeUnexpected(Failure{});

    return mesh->mesh;
}

Expected<RenderableMeshDesc, RenderableMeshResolution::Enum> MeshSystem::resolveRenderableMeshStatus(const Core::ECS::EntityID entity)const{
    RenderableMeshDesc mesh;

    for(const NotNull<IRuntimeMeshProvider*> provider : m_runtimeMeshProviders){
        const auto runtimeMesh = provider->resolveRuntimeMesh(entity);
        if(runtimeMesh && runtimeMesh->valid()){
            mesh.runtimeMesh = *runtimeMesh;
            mesh.runtime = true;
            return mesh;
        }
    }

    const auto meshAsset = resolveMesh(entity);
    if(!meshAsset){
        if(findMesh(entity) || hasRuntimeMeshBinding(entity))
            return MakeUnexpected(RenderableMeshResolution::Unavailable);
        return MakeUnexpected(RenderableMeshResolution::Absent);
    }

    mesh.mesh = *meshAsset;
    return mesh;
}

bool MeshSystem::hasRuntimeMeshBinding(const Core::ECS::EntityID entity)const{
    if(m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
        return true;
    for(const NotNull<IRuntimeMeshProvider*> provider : m_runtimeMeshProviders){
        if(provider->hasRuntimeMeshBinding(entity))
            return true;
    }
    return false;
}

void MeshSystem::markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests)const{
    for(const NotNull<IRuntimeMeshProvider*> provider : m_runtimeMeshProviders){
        if(requests.complete())
            return;

        provider->markLiveRuntimeMeshes(requests);
    }
}

void MeshSystem::registerRuntimeMeshProvider(IRuntimeMeshProvider& provider){
    if(FindIf(m_runtimeMeshProviders.begin(), m_runtimeMeshProviders.end(), [&provider](const NotNull<IRuntimeMeshProvider*> item)noexcept{ return item.get() == &provider; }) != m_runtimeMeshProviders.end())
        return;

    m_runtimeMeshProviders.push_back(MakeNotNull(&provider));
}

void MeshSystem::unregisterRuntimeMeshProvider(IRuntimeMeshProvider& provider){
    const auto found = FindIf(m_runtimeMeshProviders.begin(), m_runtimeMeshProviders.end(), [&provider](const NotNull<IRuntimeMeshProvider*> item)noexcept{ return item.get() == &provider; });
    if(found == m_runtimeMeshProviders.end())
        return;

    m_runtimeMeshProviders.erase(found);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

