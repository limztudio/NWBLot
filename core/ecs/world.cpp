// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "world.h"

#include <global/bit.h>
#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_world{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_WorldTaskProfileName("cpu.task.ecs.world");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


World::World(Alloc::GlobalArena& arena, CpuTaskScheduler& taskScheduler)
    : m_arena(arena)
    , m_tasks(taskScheduler, taskScheduler.registerProfileLabel(__hidden_world::s_WorldTaskProfileName))
    , m_entityManager(m_arena)
    , m_entityComponentMasks(m_arena)
    , m_entityComponentHeads(m_arena)
    , m_entityComponentNodes(m_arena)
    , m_pools(m_arena)
    , m_systems(m_arena)
    , m_scheduler(m_arena)
    , m_messageBus(m_arena)
    , m_freeEntityComponentNode(s_InvalidEntityComponentNode)
{}

World::~World()noexcept(false){
    b8 joined = false;
    ScopeExit retireOwned([this, &joined]()noexcept{
        if(!joined)
            m_tasks.drain();
        m_messageBus.discardForDestruction();
        m_scheduler.clear();
        while(!m_systems.empty())
            m_systems.pop_back();
        m_pools.clear();
    });
    if(UncaughtExceptionCount() == 0){
        m_tasks.wait();
        joined = true;
    }
}


void World::destroyEntity(EntityID entityId){
    if(!m_entityManager.alive(entityId))
        return;

    destroyEntityComponents(entityId);
    m_entityManager.destroyAlive(entityId);
}


Expected<void, CpuTaskAdmissionError::Enum> World::tick(f32 delta){
    m_tasks.wait();
    m_messageBus.swapBuffers();
    return m_scheduler.execute(*this, delta);
}

void World::clear(){
    if(UncaughtExceptionCount() > 0)
        m_tasks.drain();
    else
        m_tasks.wait();
    m_messageBus.clear();
    m_scheduler.clear();
    while(!m_systems.empty())
        m_systems.pop_back();
    m_pools.clear();
    m_entityComponentMasks.clear();
    m_entityComponentHeads.clear();
    m_entityComponentNodes.clear();
    m_freeEntityComponentNode = s_InvalidEntityComponentNode;
    m_entityManager.clear();
}


void World::destroyEntityComponents(EntityID entityId){
    const usize index = static_cast<usize>(entityId.index());
    if(index < m_entityComponentMasks.size()){
        u64& mask = m_entityComponentMasks[index];
        while(mask != 0u){
            const u32 typeId = static_cast<u32>(CountTrailingZeros(mask));
            FOUNDATION_ASSERT(static_cast<usize>(typeId) < m_pools.size() && m_pools[typeId]);
            m_pools[typeId]->remove(entityId);
            mask &= mask - 1u;
        }
    }

    if(index >= m_entityComponentHeads.size())
        return;

    u32 nodeIndex = m_entityComponentHeads[index];
    while(nodeIndex != s_InvalidEntityComponentNode){
        EntityComponentNode& node = m_entityComponentNodes[nodeIndex];
        const u32 nextNode = node.next;
        FOUNDATION_ASSERT(node.typeId < m_pools.size() && m_pools[node.typeId]);
        m_pools[node.typeId]->remove(entityId);
        m_entityComponentHeads[index] = nextNode;
        releaseEntityComponentNode(nodeIndex);
        nodeIndex = nextNode;
    }
}

void World::addEntityComponentTypeUncheckedSlow(EntityID entityId, ComponentTypeId typeId){
    const usize index = static_cast<usize>(entityId.index());
    if(typeId < s_FastComponentMaskBits){
        m_entityComponentMasks.resize(index + 1u, 0u);
        m_entityComponentMasks[index] |= (1ull << typeId);
        return;
    }

    ensureEntityComponentHead(entityId);
    u32& headNode = m_entityComponentHeads[index];
    headNode = acquireEntityComponentNode(typeId, headNode);
}

void World::removeEntityComponentTypeOverflow(const usize index, ComponentTypeId typeId)noexcept{
    if(index >= m_entityComponentHeads.size())
        return;

    auto nextNodeSlot = MakeNotNull(&m_entityComponentHeads[index]);
    while(*nextNodeSlot != s_InvalidEntityComponentNode){
        EntityComponentNode& node = m_entityComponentNodes[*nextNodeSlot];
        if(node.typeId == typeId){
            const u32 removedNodeIndex = *nextNodeSlot;
            *nextNodeSlot = node.next;
            releaseEntityComponentNode(removedNodeIndex);
            return;
        }
        nextNodeSlot = &node.next;
    }
}

void World::ensureEntityComponentHead(EntityID entityId){
    const usize index = static_cast<usize>(entityId.index());
    if(index >= m_entityComponentHeads.size())
        m_entityComponentHeads.resize(index + 1u, s_InvalidEntityComponentNode);
}

u32 World::acquireEntityComponentNode(ComponentTypeId typeId, u32 nextNode){
    if(m_freeEntityComponentNode != s_InvalidEntityComponentNode){
        const u32 nodeIndex = m_freeEntityComponentNode;
        EntityComponentNode& node = m_entityComponentNodes[nodeIndex];
        m_freeEntityComponentNode = node.next;
        node = EntityComponentNode{ typeId, nextNode };
        return nodeIndex;
    }

    FOUNDATION_ASSERT(m_entityComponentNodes.size() < static_cast<usize>(Limit<u32>::s_Max));
    const u32 nodeIndex = static_cast<u32>(m_entityComponentNodes.size());
    m_entityComponentNodes.push_back(EntityComponentNode{ typeId, nextNode });
    return nodeIndex;
}

void World::releaseEntityComponentNode(u32 nodeIndex)noexcept{
    FOUNDATION_ASSERT(nodeIndex < m_entityComponentNodes.size());
    m_entityComponentNodes[nodeIndex].next = m_freeEntityComponentNode;
    m_freeEntityComponentNode = nodeIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

