// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "entity_id.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        if(m_generations.size() >= static_cast<usize>(ECSDetail::ENTITY_INVALID_INDEX)){
            NWB_ASSERT_MSG(false, NWB_TEXT("EntityManager exceeded maximum entity count"));
            return ENTITY_ID_INVALID;
        }
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
    m_generations[index] = (m_generations[index] + 1) & ECSDetail::ENTITY_GENERATION_MASK;
    m_freeIndices.push_back(index);
    --m_aliveCount;
}


bool EntityManager::alive(EntityID entityId)const{
    const u32 index = entityId.index();
    if(index >= static_cast<u32>(m_generations.size()))
        return false;
    return m_generations[index] == entityId.generation();
}


void EntityManager::clear(){
    m_generations.clear();
    m_freeIndices.clear();
    m_aliveCount = 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

