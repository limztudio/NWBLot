// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "entity.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EntityManager::EntityManager()
    : m_aliveCount(0)
{}
EntityManager::~EntityManager()
{}


Entity EntityManager::create(){
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
    return Entity(index, m_generations[index]);
}

void EntityManager::destroy(Entity entity){
    if(!alive(entity))
        return;

    const u32 index = entity.index();
    m_generations[index] = (m_generations[index] + 1) & __hidden_ecs::ENTITY_GENERATION_MASK;
    m_freeIndices.push_back(index);
    --m_aliveCount;
}

bool EntityManager::alive(Entity entity)const{
    const u32 index = entity.index();
    if(index >= static_cast<u32>(m_generations.size()))
        return false;
    return m_generations[index] == entity.generation();
}

usize EntityManager::count()const{
    return m_aliveCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

