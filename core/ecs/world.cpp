// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "world.h"
#include "entity.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


World::World(Alloc::CustomArena& arena, Alloc::ThreadPool& threadPool)
    : Alloc::ITaskScheduler(threadPool)
    , m_arena(arena)
    , m_entityManager(m_arena)
    , m_pools(0, Hasher<ComponentTypeId>(), EqualTo<ComponentTypeId>(), PoolMapAllocator(m_arena))
    , m_systems(SystemVectorAllocator(m_arena))
    , m_scheduler(m_arena)
    , m_messageBus(m_arena)
{}
World::~World()
{
    clear();
}


Entity World::createEntity(){
    return Entity(*this, m_entityManager.create());
}


void World::destroyEntity(EntityID entityId){
    if(!m_entityManager.alive(entityId))
        return;

    destroyEntityComponents(entityId);
    m_entityManager.destroy(entityId);
}


bool World::alive(EntityID entityId)const{
    return m_entityManager.alive(entityId);
}


usize World::entityCount()const{
    return m_entityManager.count();
}


void World::removeSystem(ISystem& system){
    m_scheduler.removeSystem(system);

    auto itr = FindIf(m_systems.begin(), m_systems.end(),
        [&system](const SystemEntry& entry){ return entry.system.get() == &system; }
    );
    if(itr != m_systems.end())
        m_systems.erase(itr);
}


void World::tick(f32 delta){
    m_messageBus.swapBuffers();
    m_scheduler.execute(*this, delta);
}

void World::clear(){
    taskPool().wait();
    m_messageBus.clear();

    for(auto& system : m_systems)
        m_scheduler.removeSystem(*system.system);
    m_systems.clear();

    m_pools.clear();
    m_entityManager.clear();
}


void World::destroyEntityComponents(EntityID entityId){
    for(auto& [typeId, pool] : m_pools){
        if(pool->has(entityId))
            pool->remove(entityId);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

