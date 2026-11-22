// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "query.h"
#include "message_bus.h"

#include <global/allocation_size.h>
#include <global/containers.h>
#include <global/not_null.h>
#include <global/optional.h>
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
    static constexpr usize s_FastComponentMaskBits = 64u;
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
        const Array<ComponentTypeId, sizeof...(Ts)> typeIds = { ComponentType<Ts>()... };
        const usize poolCount = m_pools.size();
        auto pools = [&]<usize... Is>(IndexSequence<Is...>)noexcept{
            return MakeTuple((typeIds[Is] < poolCount ? checked_cast<ComponentPool<Ts>*>(m_pools[typeIds[Is]].get()) : nullptr)...);
        }(IndexSequenceFor<Ts...>{});
        if constexpr(sizeof...(Ts) >= 3u){
            u64 requiredMask = 0u;
            b8 allFastMaskBits = true;
            for(const ComponentTypeId typeId : typeIds){
                if(typeId < s_FastComponentMaskBits)
                    requiredMask |= (1ull << typeId);
                else
                    allFastMaskBits = false;
            }
            if(allFastMaskBits)
                return View<Ts...>(Move(pools), m_entityComponentMasks.data(), m_entityComponentMasks.size(), requiredMask);
        }
        return View<Ts...>(Move(pools));
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
    [[nodiscard]] Optional<NotNull<T*>> borrowComponent(EntityID entityId)noexcept{
        if(T* component = tryGetComponent<T>(entityId))
            return MakeNotNull(component);
        return {};
    }

    template<typename T>
    [[nodiscard]] Optional<NotNull<const T*>> borrowComponent(EntityID entityId)const noexcept{
        if(const T* component = tryGetComponent<T>(entityId))
            return MakeNotNull(component);
        return {};
    }

    template<typename T>
    [[nodiscard]] u64 componentMutationVersion()const noexcept{
        const auto* pool = getPool<T>();
        return pool ? pool->mutationVersion() : 0u;
    }


public:
    template<typename T>
    [[nodiscard]] T makeSystemResource()const{
        return T(m_arena);
    }

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

    template<typename T>
    [[nodiscard]] Optional<NotNull<T*>> borrowSystem()noexcept{
        if(auto* system = getSystem<T>())
            return MakeNotNull(system);
        return {};
    }

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


public:
    template<typename T, typename... Args>
    T& addComponent(EntityID entityId, Args&&... args){
        FOUNDATION_ASSERT(m_entityManager.alive(entityId));
        ComponentPool<T>& pool = assurePool<T>();
        if(T* existing = pool.tryGet(entityId))
            return *existing;

        const ComponentTypeId typeId = ComponentType<T>();
        addEntityComponentTypeUnchecked(entityId, typeId);
        ScopeExit rollbackType([this, entityId, typeId]()noexcept{ removeEntityComponentType(entityId, typeId); });
        T& component = pool.addUnchecked(entityId, Forward<Args>(args)...);
        rollbackType.release();
        return component;
    }

    template<typename T>
    void removeComponent(EntityID entityId)noexcept(noexcept(DeclVal<ComponentPool<T>&>().remove(entityId))){
        auto* pool = getPool<T>();
        if(pool && pool->remove(entityId))
            removeEntityComponentType(entityId, ComponentType<T>());
    }

    template<typename T>
    T& getComponent(EntityID entityId)noexcept{
        FOUNDATION_ASSERT(m_entityManager.alive(entityId));
        return requirePool<T>().get(entityId);
    }

    template<typename T>
    const T& getComponent(EntityID entityId)const noexcept{
        FOUNDATION_ASSERT(m_entityManager.alive(entityId));
        return requirePool<T>().get(entityId);
    }

    template<typename T>
    b8 hasComponent(EntityID entityId)const noexcept{
        auto* pool = getPool<T>();
        return pool ? pool->has(entityId) : false;
    }

    b8 alive(EntityID entityId)const noexcept{ return m_entityManager.alive(entityId); }


private:
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
    FOUNDATION_INLINE void addEntityComponentTypeUnchecked(const EntityID entityId, const ComponentTypeId typeId){
        if(typeId < s_FastComponentMaskBits){
            const usize index = static_cast<usize>(entityId.index());
            if(index < m_entityComponentMasks.size()){
                m_entityComponentMasks[index] |= (1ull << typeId);
                return;
            }
        }
        addEntityComponentTypeUncheckedSlow(entityId, typeId);
    }
    FOUNDATION_INLINE void removeEntityComponentType(const EntityID entityId, const ComponentTypeId typeId)noexcept{
        const usize index = static_cast<usize>(entityId.index());
        if(typeId < s_FastComponentMaskBits){
            if(index < m_entityComponentMasks.size())
                m_entityComponentMasks[index] &= ~(1ull << typeId);
            return;
        }
        removeEntityComponentTypeOverflow(index, typeId);
    }
    void addEntityComponentTypeUncheckedSlow(EntityID entityId, ComponentTypeId typeId);
    void removeEntityComponentTypeOverflow(usize index, ComponentTypeId typeId)noexcept;
    void ensureEntityComponentHead(EntityID entityId);
    u32 acquireEntityComponentNode(ComponentTypeId typeId, u32 nextNode);
    void releaseEntityComponentNode(u32 nodeIndex)noexcept;


private:
    Alloc::GlobalArena& m_arena;
    CpuTaskScope m_tasks;

    EntityManager m_entityManager;
    Vector<u64, Alloc::GlobalArena> m_entityComponentMasks;
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

