// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "query.h"
#include "message_bus.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World : NoCopy, public Alloc::ITaskScheduler{
private:
    using PoolMapAllocator = Alloc::CustomAllocator<Pair<const ComponentTypeId, UniquePtr<IComponentPool>>>;
    using SystemVectorAllocator = Alloc::CustomAllocator<UniquePtr<ISystem>>;


public:
    World(
        Alloc::CustomArena& arena,
        Alloc::ThreadPool& threadPool
    );
    ~World();


public:
    Entity createEntity();
    void destroyEntity(Entity entity);
    bool alive(Entity entity)const;
    usize entityCount()const;

    inline Alloc::CustomArena& arena(){ return m_arena; }
    inline const Alloc::CustomArena& arena()const{ return m_arena; }


public:
    template<typename T, typename... Args>
    T& addComponent(Entity entity, Args&&... args){
        NWB_ASSERT(m_entityManager.alive(entity));
        auto* pool = assurePool<T>();
        return pool->add(entity, Forward<Args>(args)...);
    }

    template<typename T>
    void removeComponent(Entity entity){
        if(!m_entityManager.alive(entity))
            return;

        auto* pool = getPool<T>();
        if(pool)
            pool->remove(entity);
    }

    template<typename T>
    T& getComponent(Entity entity){
        NWB_ASSERT(m_entityManager.alive(entity));
        auto* pool = getPool<T>();
        NWB_ASSERT(pool);
        return pool->get(entity);
    }

    template<typename T>
    const T& getComponent(Entity entity)const{
        NWB_ASSERT(m_entityManager.alive(entity));
        auto* pool = getPool<T>();
        NWB_ASSERT(pool);
        return pool->get(entity);
    }

    template<typename T>
    bool hasComponent(Entity entity)const{
        if(!m_entityManager.alive(entity))
            return false;

        auto* pool = getPool<T>();
        return pool ? pool->has(entity) : false;
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
        m_scheduler.addSystem(ptr.get());
        m_systems.push_back(Move(ptr));
        return ref;
    }

    void removeSystem(ISystem* system);

    template<typename T>
    T* getSystem(){
        for(auto& s : m_systems){
            T* casted = dynamic_cast<T*>(s.get());
            if(casted)
                return casted;
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


private:
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

    void destroyEntityComponents(Entity entity);


private:
    Alloc::CustomArena& m_arena;

    EntityManager m_entityManager;
    HashMap<ComponentTypeId, UniquePtr<IComponentPool>, Hasher<ComponentTypeId>, EqualTo<ComponentTypeId>, PoolMapAllocator> m_pools;
    Vector<UniquePtr<ISystem>, SystemVectorAllocator> m_systems;
    SystemScheduler m_scheduler;
    MessageBus m_messageBus;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

