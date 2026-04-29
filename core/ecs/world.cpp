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
    , m_entityComponentHeads(EntityComponentHeadAllocator(m_arena))
    , m_entityComponentNodes(EntityComponentNodeAllocator(m_arena))
    , m_freeEntityComponentNode(s_InvalidEntityComponentNode)
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
    m_entityComponentHeads.clear();
    m_entityComponentNodes.clear();
    m_freeEntityComponentNode = s_InvalidEntityComponentNode;
    m_entityManager.clear();
}


void World::destroyEntityComponents(EntityID entityId){
    const usize index = static_cast<usize>(entityId.index());
    if(index >= m_entityComponentHeads.size())
        return;

    u32 nodeIndex = m_entityComponentHeads[index];
    m_entityComponentHeads[index] = s_InvalidEntityComponentNode;
    while(nodeIndex != s_InvalidEntityComponentNode){
        const u32 nextNode = m_entityComponentNodes[nodeIndex].next;
        auto itr = m_pools.find(m_entityComponentNodes[nodeIndex].typeId);
        if(itr != m_pools.end())
            itr.value()->remove(entityId);
        releaseEntityComponentNode(nodeIndex);
        nodeIndex = nextNode;
    }
}


void World::addEntityComponentType(EntityID entityId, ComponentTypeId typeId){
    ensureEntityComponentHead(entityId);

    const usize index = static_cast<usize>(entityId.index());
    u32& headNode = m_entityComponentHeads[index];
#if NWB_OCCUR_ASSERT
    for(u32 nodeIndex = headNode; nodeIndex != s_InvalidEntityComponentNode; nodeIndex = m_entityComponentNodes[nodeIndex].next)
        NWB_ASSERT(m_entityComponentNodes[nodeIndex].typeId != typeId);
#endif

    headNode = acquireEntityComponentNode(typeId, headNode);
}


void World::removeEntityComponentType(EntityID entityId, ComponentTypeId typeId){
    const usize index = static_cast<usize>(entityId.index());
    if(index >= m_entityComponentHeads.size())
        return;

    u32* nextNodeSlot = &m_entityComponentHeads[index];
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

    NWB_ASSERT(m_entityComponentNodes.size() < static_cast<usize>(Limit<u32>::s_Max));
    const u32 nodeIndex = static_cast<u32>(m_entityComponentNodes.size());
    m_entityComponentNodes.push_back(EntityComponentNode{ typeId, nextNode });
    return nodeIndex;
}


void World::releaseEntityComponentNode(u32 nodeIndex){
    NWB_ASSERT(nodeIndex < m_entityComponentNodes.size());
    m_entityComponentNodes[nodeIndex].next = m_freeEntityComponentNode;
    m_freeEntityComponentNode = nodeIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

