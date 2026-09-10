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

bool MeshSystem::resolveMesh(
    const Core::ECS::EntityID entity,
    Core::Assets::AssetRef<Mesh>& outMesh
)const{
    outMesh = Core::Assets::AssetRef<Mesh>{};

    const MeshComponent* mesh = findMesh(entity);
    if(!mesh || !mesh->mesh.valid())
        return false;

    outMesh = mesh->mesh;
    return true;
}

bool MeshSystem::resolveRenderableMesh(
    const Core::ECS::EntityID entity,
    RenderableMeshDesc& outMesh
)const{
    return resolveRenderableMeshStatus(entity, outMesh) == RenderableMeshResolution::Ready;
}

RenderableMeshResolution::Enum MeshSystem::resolveRenderableMeshStatus(
    const Core::ECS::EntityID entity,
    RenderableMeshDesc& outMesh)const{
    outMesh = RenderableMeshDesc{};

    for(IRuntimeMeshProvider* provider : m_runtimeMeshProviders){
        if(!provider)
            continue;

        RuntimeMeshDesc runtimeMesh;
        if(provider->resolveRuntimeMesh(entity, runtimeMesh) && runtimeMesh.valid()){
            outMesh.runtimeMesh = runtimeMesh;
            outMesh.runtime = true;
            return RenderableMeshResolution::Ready;
        }
    }

    Core::Assets::AssetRef<Mesh> mesh;
    if(!resolveMesh(entity, mesh)){
        if(findMesh(entity) || m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
            return RenderableMeshResolution::Unavailable;
        // Only unresolved entities need attachment discovery.
        for(const IRuntimeMeshProvider* provider : m_runtimeMeshProviders){
            if(provider && provider->hasRuntimeMeshBinding(entity))
                return RenderableMeshResolution::Unavailable;
        }
        return RenderableMeshResolution::Absent;
    }

    outMesh.mesh = mesh;
    return RenderableMeshResolution::Ready;
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

