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
    , m_entityComponentTypes(EntityComponentTypeVectorAllocator(m_arena))
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
    m_scheduler.clear();
    m_systems.clear();
    m_pools.clear();
    m_entityComponentTypes.clear();
    m_entityManager.clear();
}


void World::destroyEntityComponents(EntityID entityId){
    const usize index = static_cast<usize>(entityId.index());
    if(index >= m_entityComponentTypes.size())
        return;

    auto& componentTypes = m_entityComponentTypes[index];
    for(ComponentTypeId typeId : componentTypes){
        auto itr = m_pools.find(typeId);
        if(itr != m_pools.end())
            itr.value()->remove(entityId);
    }
    componentTypes.clear();
}


void World::addEntityComponentType(EntityID entityId, ComponentTypeId typeId){
    const usize index = static_cast<usize>(entityId.index());
    while(index >= m_entityComponentTypes.size())
        m_entityComponentTypes.emplace_back(EntityComponentTypeAllocator(m_arena));

    auto& componentTypes = m_entityComponentTypes[index];
    NWB_ASSERT(
        FindIf(componentTypes.begin(), componentTypes.end(),
            [typeId](ComponentTypeId iterTypeId){ return iterTypeId == typeId; }
        ) == componentTypes.end()
    );
    componentTypes.push_back(typeId);
}


void World::removeEntityComponentType(EntityID entityId, ComponentTypeId typeId){
    const usize index = static_cast<usize>(entityId.index());
    if(index >= m_entityComponentTypes.size())
        return;

    auto& componentTypes = m_entityComponentTypes[index];
    auto itr = FindIf(componentTypes.begin(), componentTypes.end(),
        [typeId](ComponentTypeId iterTypeId){ return iterTypeId == typeId; }
    );
    if(itr != componentTypes.end())
        componentTypes.erase(itr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

