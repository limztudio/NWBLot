// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "entity_id.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void* ECSArenaAlloc(usize size){
    return Alloc::CoreAlloc(size, "NWB::Core::ECS::EntityManager::ECSArenaAlloc");
}

static void ECSArenaFree(void* ptr){
    Alloc::CoreFree(ptr, "NWB::Core::ECS::EntityManager::ECSArenaFree");
}

static void* ECSArenaAllocAligned(usize size, usize align){
    return Alloc::CoreAllocAligned(size, align, "NWB::Core::ECS::EntityManager::ECSArenaAllocAligned");
}

static void ECSArenaFreeAligned(void* ptr){
    Alloc::CoreFreeAligned(ptr, "NWB::Core::ECS::EntityManager::ECSArenaFreeAligned");
}

static Alloc::CustomArena& DefaultECSArena(){
    static Alloc::CustomArena s_Arena(
        &ECSArenaAlloc,
        &ECSArenaFree,
        &ECSArenaAllocAligned,
        &ECSArenaFreeAligned
    );
    return s_Arena;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EntityManager::EntityManager()
    : EntityManager(__hidden_ecs::DefaultECSArena())
{}
EntityManager::EntityManager(Alloc::CustomArena& arena)
    : m_generations(GenerationAllocator(arena))
    , m_freeIndices(FreeIndexAllocator(arena))
    , m_aliveCount(0)
{}
EntityManager::~EntityManager()
{}


EntityID EntityManager::create(){
    u32 index;

    if(!m_freeIndices.empty()){
        index = m_freeIndices.front();
        m_freeIndices.pop_front();
    }
    else{
        index = static_cast<u32>(m_generations.size());
        m_generations.push_back(0);
    }

    ++m_aliveCount;
    return EntityID(index, m_generations[index]);
}


void EntityManager::destroy(EntityID entityId){
    if(!alive(entityId))
        return;

    const u32 index = entityId.index();
    m_generations[index] = (m_generations[index] + 1) & __hidden_ecs::ENTITY_GENERATION_MASK;
    m_freeIndices.push_back(index);
    --m_aliveCount;
}


bool EntityManager::alive(EntityID entityId)const{
    const u32 index = entityId.index();
    if(index >= static_cast<u32>(m_generations.size()))
        return false;
    return m_generations[index] == entityId.generation();
}


usize EntityManager::count()const{
    return m_aliveCount;
}

void EntityManager::clear(){
    m_generations.clear();
    m_freeIndices.clear();
    m_aliveCount = 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

