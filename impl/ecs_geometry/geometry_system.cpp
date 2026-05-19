// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_system.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GeometrySystem::GeometrySystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world)
    : Core::ECS::ISystem(arena)
    , m_world(world)
    , m_runtimeGeometryProviders(arena)
{
    readAccess<GeometryComponent>();
}

void GeometrySystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

GeometryComponent* GeometrySystem::findGeometry(const Core::ECS::EntityID entity){
    return m_world.tryGetComponent<GeometryComponent>(entity);
}

const GeometryComponent* GeometrySystem::findGeometry(const Core::ECS::EntityID entity)const{
    return m_world.tryGetComponent<GeometryComponent>(entity);
}

bool GeometrySystem::resolveGeometry(
    const Core::ECS::EntityID entity,
    Core::Assets::AssetRef<Geometry>& outGeometry
)const{
    outGeometry = Core::Assets::AssetRef<Geometry>{};

    const GeometryComponent* geometry = findGeometry(entity);
    if(!geometry || !geometry->geometry.valid())
        return false;

    outGeometry = geometry->geometry;
    return true;
}

bool GeometrySystem::resolveRenderableGeometry(
    const Core::ECS::EntityID entity,
    RenderableGeometryDesc& outGeometry
)const{
    outGeometry = RenderableGeometryDesc{};

    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(!provider)
            continue;

        RuntimeGeometryDesc runtimeGeometry;
        if(provider->resolveRuntimeGeometry(entity, runtimeGeometry) && runtimeGeometry.valid()){
            outGeometry.runtimeGeometry = runtimeGeometry;
            outGeometry.runtime = true;
            return true;
        }
    }

    Core::Assets::AssetRef<Geometry> geometry;
    if(!resolveGeometry(entity, geometry))
        return false;

    outGeometry.geometry = geometry;
    return outGeometry.valid();
}

bool GeometrySystem::containsRuntimeGeometry(const Name& geometryKey, const u64 version)const{
    if(!geometryKey)
        return false;

    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(provider && provider->containsRuntimeGeometry(geometryKey, version))
            return true;
    }
    return false;
}

void GeometrySystem::registerRuntimeGeometryProvider(IRuntimeGeometryProvider& provider){
    if(
        FindIf(
            m_runtimeGeometryProviders.begin(),
            m_runtimeGeometryProviders.end(),
            [&provider](IRuntimeGeometryProvider* item){ return item == &provider; }
        ) != m_runtimeGeometryProviders.end()
    )
        return;

    m_runtimeGeometryProviders.push_back(&provider);
}

void GeometrySystem::unregisterRuntimeGeometryProvider(IRuntimeGeometryProvider& provider){
    const auto found = FindIf(
        m_runtimeGeometryProviders.begin(),
        m_runtimeGeometryProviders.end(),
        [&provider](IRuntimeGeometryProvider* item){ return item == &provider; }
    );
    if(found == m_runtimeGeometryProviders.end())
        return;

    m_runtimeGeometryProviders.erase(found);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

