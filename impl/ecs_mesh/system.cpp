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

void MeshSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

MeshComponent* MeshSystem::findMesh(const Core::ECS::EntityID entity){
    return m_world.tryGetComponent<MeshComponent>(entity);
}

const MeshComponent* MeshSystem::findMesh(const Core::ECS::EntityID entity)const{
    return m_world.tryGetComponent<MeshComponent>(entity);
}

Expected<Core::Assets::AssetRef<Mesh>> MeshSystem::resolveMesh(
    const Core::ECS::EntityID entity
)const{
    const MeshComponent* mesh = findMesh(entity);
    if(!mesh || !mesh->mesh.valid())
        return MakeUnexpected(Failure{});

    return mesh->mesh;
}

Expected<RenderableMeshDesc, RenderableMeshResolution::Enum> MeshSystem::resolveRenderableMeshStatus(
    const Core::ECS::EntityID entity
)const{
    RenderableMeshDesc mesh;

    for(IRuntimeMeshProvider* provider : m_runtimeMeshProviders){
        if(!provider)
            continue;

        const auto runtimeMesh = provider->resolveRuntimeMesh(entity);
        if(runtimeMesh && runtimeMesh->valid()){
            mesh.runtimeMesh = *runtimeMesh;
            mesh.runtime = true;
            return mesh;
        }
    }

    const auto meshAsset = resolveMesh(entity);
    if(!meshAsset){
        if(findMesh(entity) || m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
            return MakeUnexpected(RenderableMeshResolution::Unavailable);
        // Only unresolved entities need attachment discovery.
        for(const IRuntimeMeshProvider* provider : m_runtimeMeshProviders){
            if(provider && provider->hasRuntimeMeshBinding(entity))
                return MakeUnexpected(RenderableMeshResolution::Unavailable);
        }
        return MakeUnexpected(RenderableMeshResolution::Absent);
    }

    mesh.mesh = *meshAsset;
    return mesh;
}

void MeshSystem::markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests)const{
    for(IRuntimeMeshProvider* provider : m_runtimeMeshProviders){
        if(requests.complete())
            return;
        if(provider)
            provider->markLiveRuntimeMeshes(requests);
    }
}

void MeshSystem::registerRuntimeMeshProvider(IRuntimeMeshProvider& provider){
    if(
        FindIf(
            m_runtimeMeshProviders.begin(),
            m_runtimeMeshProviders.end(),
            [&provider](IRuntimeMeshProvider* item){ return item == &provider; }
        ) != m_runtimeMeshProviders.end()
    )
        return;

    m_runtimeMeshProviders.push_back(&provider);
}

void MeshSystem::unregisterRuntimeMeshProvider(IRuntimeMeshProvider& provider){
    const auto found = FindIf(
        m_runtimeMeshProviders.begin(),
        m_runtimeMeshProviders.end(),
        [&provider](IRuntimeMeshProvider* item){ return item == &provider; }
    );
    if(found == m_runtimeMeshProviders.end())
        return;

    m_runtimeMeshProviders.erase(found);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

