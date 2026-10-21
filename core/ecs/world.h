// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "query.h"
#include "message_bus.h"

#include <global/allocation_size.h>
#include <global/containers.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Entity;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World : NoCopy{
    friend class Entity;


private:
    struct EntityComponentNode{
        ComponentTypeId typeId;
        NotNull<IComponentPool*> pool;
        u32 next;
    };


private:
    using ComponentPoolPtr = GlobalUniquePtr<IComponentPool>;
    using SystemPtr = GlobalUniquePtr<ISystem>;


private:
    struct SystemEntry{
        SystemTypeId typeId;
        SystemPtr system;
    };


private:
    static constexpr u32 s_InvalidEntityComponentNode = Limit<u32>::s_Max;


public:
    World(Alloc::GlobalArena& arena, CpuTaskScheduler& taskScheduler);
    ~World()noexcept(false);


public:
    Entity createEntity();
    [[nodiscard]] Entity entity(EntityID entityId)noexcept;
    void destroyEntity(EntityID entityId);
    usize entityCount()const noexcept{ return m_entityManager.count(); }


public:
    template<typename... Ts>
    View<Ts...> view()noexcept{
        return View<Ts...>(MakeTuple(getPool<Ts>()...));
    }

    template<typename T>
    [[nodiscard]] T* tryGetComponent(EntityID entityId)noexcept{
        auto* pool = getPool<T>();
        return pool ? pool->tryGet(entityId) : nullptr;
    }

    template<typename T>
    [[nodiscard]] const T* tryGetComponent(EntityID entityId)const noexcept{
        const auto* pool = getPool<T>();
        return pool ? pool->tryGet(entityId) : nullptr;
    }

    template<typename T>
    [[nodiscard]] u64 componentMutationVersion()const noexcept{
        const auto* pool = getPool<T>();
        return pool ? pool->mutationVersion() : 0u;
    }


private:
    template<typename T, typename... Args>
    T& addComponent(EntityID entityId, Args&&... args){
        NWB_ASSERT(m_entityManager.alive(entityId));
        ComponentPool<T>& pool = assurePool<T>();
        if(T* existing = pool.tryGet(entityId))
            return *existing;

        const ComponentTypeId typeId = ComponentType<T>();
        addEntityComponentType(entityId, typeId, pool);
        ScopeExit rollbackType([this, entityId, typeId]()noexcept{ removeEntityComponentType(entityId, typeId); });
        T& component = pool.add(entityId, Forward<Args>(args)...);
        rollbackType.release();
        return component;
    }

    template<typename T>
    void removeComponent(EntityID entityId)noexcept(noexcept(DeclVal<ComponentPool<T>&>().remove(entityId))){
        if(!m_entityManager.alive(entityId))
            return;

        auto* pool = getPool<T>();
        if(pool && pool->remove(entityId))
            removeEntityComponentType(entityId, ComponentType<T>());
    }

    template<typename T>
    T& getComponent(EntityID entityId)noexcept{
        NWB_ASSERT(m_entityManager.alive(entityId));
        return requirePool<T>().get(entityId);
    }

    template<typename T>
    const T& getComponent(EntityID entityId)const noexcept{
        NWB_ASSERT(m_entityManager.alive(entityId));
        return requirePool<T>().get(entityId);
    }

    template<typename T>
    bool hasComponent(EntityID entityId)const noexcept{
        auto* pool = getPool<T>();
        return pool ? pool->has(entityId) : false;
    }


public:
    template<typename T, typename... Args>
    T& addSystem(Args&&... args){
        static_assert(IsBaseOf_V<ISystem, T>, "addSystem requires T to derive from ISystem");

        auto ptr = MakeGlobalUnique<T>(m_arena, m_arena, Forward<Args>(args)...);
        if(!ptr)
            throw AllocationException{};
        T& ref = *ptr;
        ContainerDetail::ReserveGrowingCapacity(m_systems, AddSize(m_systems.size(), 1u));
        m_scheduler.addSystem(ref);
        m_systems.push_back(SystemEntry{ SystemType<T>(), Move(ptr) });
        return ref;
    }

    void removeSystem(ISystem& system);

    template<typename T>
    T* getSystem()noexcept{
        const SystemTypeId systemTypeId = SystemType<T>();

        for(auto& system : m_systems){
            if(system.typeId != systemTypeId)
                continue;
            return checked_cast<T*>(system.system.get());
        }
        return nullptr;
    }


public:
    template<typename T>
    void postMessage(const T& message){
        using MessageT = Decay_T<T>;
        m_messageBus.post<MessageT>(message);
    }

    template<typename T>
    void postMessage(T&& message){
        using MessageT = Decay_T<T>;
        m_messageBus.post<MessageT>(Forward<T>(message));
    }

    template<typename T, typename... Args>
    void emplaceMessage(Args&&... args){
        using MessageT = Decay_T<T>;
        m_messageBus.emplace<MessageT>(Forward<Args>(args)...);
    }

    template<typename T, typename Func>
    void consumeMessages(Func&& func)const noexcept(noexcept(DeclVal<Func&>()(DeclVal<const Decay_T<T>&>()))){
        using MessageT = Decay_T<T>;
        m_messageBus.consume<MessageT>(Forward<Func>(func));
    }

    template<typename T>
    usize messageCount()const noexcept{
        using MessageT = Decay_T<T>;
        return m_messageBus.messageCount<MessageT>();
    }

    void swapMessageBuffers(){ m_messageBus.swapBuffers(); }
    void clearMessages(){ m_messageBus.clear(); }


public:
    [[nodiscard]] CpuTaskScope& taskScope()noexcept{ return m_tasks; }
    [[nodiscard]] CpuTaskScheduler& taskScheduler()noexcept{ return m_tasks.scheduler(); }

    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> tick(f32 delta);
    void clear();


private:
    bool alive(EntityID entityId)const noexcept{ return m_entityManager.alive(entityId); }

    template<typename T>
    ComponentPool<T>* getPool()noexcept{
        const auto typeId = ComponentType<T>();
        if(typeId >= m_pools.size())
            return nullptr;
        return checked_cast<ComponentPool<T>*>(m_pools[typeId].get());
    }

    template<typename T>
    const ComponentPool<T>* getPool()const noexcept{
        const auto typeId = ComponentType<T>();
        if(typeId >= m_pools.size())
            return nullptr;
        return checked_cast<const ComponentPool<T>*>(m_pools[typeId].get());
    }

    template<typename T>
    ComponentPool<T>& assurePool(){
        const auto typeId = ComponentType<T>();
        if(typeId >= m_pools.size())
            m_pools.resize(typeId + 1u);

        auto& pool = m_pools[typeId];
        if(!pool){
            pool = MakeGlobalUnique<ComponentPool<T>>(m_arena, m_arena);
            if(!pool)
                throw AllocationException{};
        }
        return *checked_cast<ComponentPool<T>*>(pool.get());
    }
    template<typename T>
    ComponentPool<T>& requirePool()noexcept{
        auto& pool = *MakeNotNull(getPool<T>());
        return pool;
    }

    template<typename T>
    const ComponentPool<T>& requirePool()const noexcept{
        auto& pool = *MakeNotNull(getPool<T>());
        return pool;
    }

    void destroyEntityComponents(EntityID entityId);
    void addEntityComponentType(EntityID entityId, ComponentTypeId typeId, IComponentPool& pool);
    void removeEntityComponentType(EntityID entityId, ComponentTypeId typeId)noexcept;
    void ensureEntityComponentHead(EntityID entityId);
    u32 acquireEntityComponentNode(ComponentTypeId typeId, IComponentPool& pool, u32 nextNode);
    void releaseEntityComponentNode(u32 nodeIndex)noexcept;


private:
    Alloc::GlobalArena& m_arena;
    CpuTaskScope m_tasks;

    EntityManager m_entityManager;
    Vector<u32, Alloc::GlobalArena> m_entityComponentHeads;
    Vector<EntityComponentNode, Alloc::GlobalArena> m_entityComponentNodes;
    Vector<ComponentPoolPtr, Alloc::GlobalArena> m_pools;
    Vector<SystemEntry, Alloc::GlobalArena> m_systems;
    SystemScheduler m_scheduler;
    MessageBus m_messageBus;
    u32 m_freeEntityComponentNode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

