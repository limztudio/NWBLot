// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "entity_id.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_entity_id{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_FreeEntityFlag = static_cast<u16>(1u << ECSDetail::s_EntityGenerationBits);
static_assert(s_FreeEntityFlag > ECSDetail::s_EntityGenerationMask);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EntityManager::EntityManager(Alloc::GlobalArena& arena)
    : m_generations(arena)
    , m_freeIndices(arena)
    , m_nextEntityIndex(0u)
    , m_aliveCount(0)
{}


EntityID EntityManager::create(){
    u32 index;

    if(!m_freeIndices.empty()){
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
        m_generations[index] = static_cast<u16>(m_generations[index] & ECSDetail::s_EntityGenerationMask);
    }
    else{
        if(m_nextEntityIndex >= ECSDetail::s_EntityInvalidIndex){
            NWB_ASSERT_MSG(false, NWB_TEXT("EntityManager exceeded maximum entity count"));
            return s_InvalidEntityId;
        }
        index = m_nextEntityIndex;
        if(index == m_generations.size())
            m_generations.push_back(0u);
        else
            m_generations[index] = static_cast<u16>(m_generations[index] & ECSDetail::s_EntityGenerationMask);
        ++m_nextEntityIndex;
    }

    ++m_aliveCount;
    return EntityID(index, static_cast<u32>(m_generations[index]));
}


void EntityManager::destroy(EntityID entityId){
    if(!alive(entityId))
        return;

    destroyAlive(entityId);
}


void EntityManager::destroyAlive(EntityID entityId){
    NWB_ASSERT(alive(entityId));

    const u32 index = entityId.index();
    m_freeIndices.push_back(index);
    m_generations[index] = static_cast<u16>(((m_generations[index] + 1u) & ECSDetail::s_EntityGenerationMask) | __hidden_entity_id::s_FreeEntityFlag);
    --m_aliveCount;
}


bool EntityManager::alive(EntityID entityId)const noexcept{
    const u32 index = entityId.index();
    if(index >= static_cast<u32>(m_generations.size()))
        return false;
    return static_cast<u32>(m_generations[index]) == entityId.generation();
}


void EntityManager::clear()noexcept{
    for(u32 index = 0u; index < m_nextEntityIndex; ++index){
        u16& generation = m_generations[index];
        if((generation & __hidden_entity_id::s_FreeEntityFlag) == 0u)
            generation = static_cast<u16>((generation + 1u) & ECSDetail::s_EntityGenerationMask);
        generation = static_cast<u16>(generation | __hidden_entity_id::s_FreeEntityFlag);
    }
    m_freeIndices.clear();
    m_nextEntityIndex = 0u;
    m_aliveCount = 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

