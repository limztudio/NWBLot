// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_system.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GeometrySystem::GeometrySystem(Core::Alloc::CustomArena& arena, Core::ECS::World& world)
    : Core::ECS::ISystem(arena)
    , m_world(world)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

