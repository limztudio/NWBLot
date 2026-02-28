// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "query.h"
#include "message_bus.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Entity;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World : NoCopy, public Alloc::ITaskScheduler{
    friend class Entity;


private:
    using PoolMapAllocator = Alloc::CustomAllocator<Pair<const ComponentTypeId, UniquePtr<IComponentPool>>>;

    struct SystemEntry{
        SystemTypeId typeId;
        UniquePtr<ISystem> system;
    };

    using SystemVectorAllocator = Alloc::CustomAllocator<SystemEntry>;


public:
    World(Alloc::CustomArena& arena, Alloc::ThreadPool& threadPool);
    ~World();


public:
    Entity createEntity();
    usize entityCount()const;


public:
    template<typename... Ts>
    View<Ts...> view(){
        return View<Ts...>(
            MakeTuple(assurePool<Ts>()...)
        );
    }

    template<typename... Ts>
    View<Ts...> view()const{
        return View<Ts...>(
            MakeTuple(const_cast<ComponentPool<Ts>*>(getPool<Ts>())...)
        );
    }


public:
    template<typename T, typename... Args>
    T& addSystem(Args&&... args){
        static_assert(IsBaseOf_V<ISystem, T>, "addSystem requires T to derive from ISystem");

        auto ptr = MakeUnique<T>(Forward<Args>(args)...);
        T& ref = *ptr;
        m_scheduler.addSystem(ref);
        m_systems.push_back(SystemEntry{ SystemType<T>(), Move(ptr) });
        return ref;
    }

    void removeSystem(ISystem& system);

    template<typename T>
    T* getSystem(){
        const SystemTypeId systemTypeId = SystemType<T>();

        for(auto& system : m_systems){
            if(system.typeId != systemTypeId)
                continue;
            return static_cast<T*>(system.system.get());
        }
        return nullptr;
    }


public:
    template<typename T>
    void postMessage(const T& message){
        m_messageBus.post<T>(message);
    }

    template<typename T>
    void postMessage(T&& message){
        m_messageBus.post<T>(Move(message));
    }

    template<typename T, typename... Args>
    void emplaceMessage(Args&&... args){
        m_messageBus.emplace<T>(Forward<Args>(args)...);
    }

    template<typename T, typename Func>
    void consumeMessages(Func&& func)const{
        m_messageBus.consume<T>(Forward<Func>(func));
    }

    template<typename T>
    usize messageCount()const{
        return m_messageBus.messageCount<T>();
    }

    void swapMessageBuffers(){ m_messageBus.swapBuffers(); }
    void clearMessages(){ m_messageBus.clear(); }


public:
    void tick(f32 delta);
    void clear();


private:
    void destroyEntity(EntityID entityId);
    bool alive(EntityID entityId)const;

    template<typename T, typename... Args>
    T& addComponent(EntityID entityId, Args&&... args){
        NWB_ASSERT(m_entityManager.alive(entityId));
        auto* pool = assurePool<T>();
        return pool->add(entityId, Forward<Args>(args)...);
    }

    template<typename T>
    void removeComponent(EntityID entityId){
        if(!m_entityManager.alive(entityId))
            return;

        auto* pool = getPool<T>();
        if(pool)
            pool->remove(entityId);
    }

    template<typename T>
    T& getComponent(EntityID entityId){
        NWB_ASSERT(m_entityManager.alive(entityId));
        auto& pool = requirePool<T>();
        return pool->get(entityId);
    }

    template<typename T>
    const T& getComponent(EntityID entityId)const{
        NWB_ASSERT(m_entityManager.alive(entityId));
        const auto& pool = requirePool<T>();
        return pool->get(entityId);
    }

    template<typename T>
    bool hasComponent(EntityID entityId)const{
        if(!m_entityManager.alive(entityId))
            return false;

        auto* pool = getPool<T>();
        return pool ? pool->has(entityId) : false;
    }

    template<typename T>
    ComponentPool<T>* getPool(){
        const auto typeId = ComponentType<T>();
        auto itr = m_pools.find(typeId);
        if(itr == m_pools.end())
            return nullptr;
        return static_cast<ComponentPool<T>*>(itr->second.get());
    }

    template<typename T>
    const ComponentPool<T>* getPool()const{
        const auto typeId = ComponentType<T>();
        auto itr = m_pools.find(typeId);
        if(itr == m_pools.end())
            return nullptr;
        return static_cast<const ComponentPool<T>*>(itr->second.get());
    }

    template<typename T>
    ComponentPool<T>* assurePool(){
        const auto typeId = ComponentType<T>();
        auto itr = m_pools.find(typeId);
        if(itr != m_pools.end())
            return static_cast<ComponentPool<T>*>(itr->second.get());

        auto pool = MakeUnique<ComponentPool<T>>(m_arena);
        auto* raw = pool.get();
        m_pools.emplace(typeId, Move(pool));
        return raw;
    }
    template<typename T>
    ComponentPool<T>& requirePool(){
        auto* pool = getPool<T>();
        NWB_ASSERT(pool);
        return *pool;
    }

    template<typename T>
    const ComponentPool<T>& requirePool()const{
        auto* pool = getPool<T>();
        NWB_ASSERT(pool);
        return *pool;
    }

    void destroyEntityComponents(EntityID entityId);


private:
    Alloc::CustomArena& m_arena;

    EntityManager m_entityManager;
    HashMap<ComponentTypeId, UniquePtr<IComponentPool>, Hasher<ComponentTypeId>, EqualTo<ComponentTypeId>, PoolMapAllocator> m_pools;
    Vector<SystemEntry, SystemVectorAllocator> m_systems;
    SystemScheduler m_scheduler;
    MessageBus m_messageBus;
};


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

