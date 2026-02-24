// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "world.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void* ECSArenaAlloc(usize size){
    return Alloc::CoreAlloc(size, "NWB::Core::ECS::World::ECSArenaAlloc");
}

static void ECSArenaFree(void* ptr){
    Alloc::CoreFree(ptr, "NWB::Core::ECS::World::ECSArenaFree");
}

static void* ECSArenaAllocAligned(usize size, usize align){
    return Alloc::CoreAllocAligned(size, align, "NWB::Core::ECS::World::ECSArenaAllocAligned");
}

static void ECSArenaFreeAligned(void* ptr){
    Alloc::CoreFreeAligned(ptr, "NWB::Core::ECS::World::ECSArenaFreeAligned");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Alloc::CustomArena> World::createOwnedArena(){
    return MakeUnique<Alloc::CustomArena>(
        &__hidden_ecs::ECSArenaAlloc,
        &__hidden_ecs::ECSArenaFree,
        &__hidden_ecs::ECSArenaAllocAligned,
        &__hidden_ecs::ECSArenaFreeAligned
    );
}


World::World(u32 workerThreads, Alloc::CoreAffinity affinity, usize poolArenaSize, Alloc::CustomArena* arena)
    : Alloc::ITaskScheduler(m_threadPool)
    , m_ownedArena(arena ? UniquePtr<Alloc::CustomArena>() : World::createOwnedArena())
    , m_arena(arena ? arena : m_ownedArena.get())
    , m_threadPool(
        (workerThreads > 0) ? workerThreads : Alloc::QueryCoreCount(affinity),
        affinity,
        poolArenaSize
    )
    , m_entityManager(*m_arena)
    , m_pools(0, Hasher<ComponentTypeId>(), EqualTo<ComponentTypeId>(), PoolMapAllocator(*m_arena))
    , m_systems(SystemVectorAllocator(*m_arena))
    , m_scheduler(*m_arena)
    , m_messageBus(*m_arena)
{}
World::~World()
{
    m_threadPool.wait();
    m_messageBus.clear();
    m_systems.clear();
    m_pools.clear();
}


Entity World::createEntity(){
    return m_entityManager.create();
}


void World::destroyEntity(Entity entity){
    if(!m_entityManager.alive(entity))
        return;

    destroyEntityComponents(entity);
    m_entityManager.destroy(entity);
}


bool World::alive(Entity entity)const{
    return m_entityManager.alive(entity);
}


usize World::entityCount()const{
    return m_entityManager.count();
}


void World::removeSystem(ISystem* system){
    m_scheduler.removeSystem(system);

    auto itr = FindIf(m_systems.begin(), m_systems.end(),
        [system](const UniquePtr<ISystem>& ptr){ return ptr.get() == system; }
    );
    if(itr != m_systems.end())
        m_systems.erase(itr);
}


void World::tick(f32 delta){
    m_messageBus.swapBuffers();
    m_scheduler.execute(*this, delta);
}


void World::destroyEntityComponents(Entity entity){
    for(auto& [typeId, pool] : m_pools){
        if(pool->has(entity))
            pool->remove(entity);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

