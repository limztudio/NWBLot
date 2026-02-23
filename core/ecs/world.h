// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "query.h"
#include "message_bus.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World : NoCopy, public Alloc::ITaskScheduler{
public:
    World(u32 workerThreads = 0, Alloc::CoreAffinity affinity = Alloc::CoreAffinity::Any, usize poolArenaSize = 0);
    ~World();


    // ---- Entity management ----
public:
    Entity createEntity();
    void destroyEntity(Entity entity);
    bool alive(Entity entity)const;
    usize entityCount()const;


    // ---- Component management ----
public:
    template<typename T, typename... Args>
    T& addComponent(Entity entity, Args&&... args){
        auto* pool = assurePool<T>();
        return pool->add(entity, Forward<Args>(args)...);
    }

    template<typename T>
    void removeComponent(Entity entity){
        auto* pool = getPool<T>();
        if(pool)
            pool->remove(entity);
    }

    template<typename T>
    T& getComponent(Entity entity){
        auto* pool = getPool<T>();
        NWB_ASSERT(pool);
        return pool->get(entity);
    }

    template<typename T>
    const T& getComponent(Entity entity)const{
        auto* pool = getPool<T>();
        NWB_ASSERT(pool);
        return pool->get(entity);
    }

    template<typename T>
    bool hasComponent(Entity entity)const{
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


    // ---- View / Query ----
public:
    template<typename... Ts>
    View<Ts...> view(){
        return View<Ts...>(
            std::make_tuple(assurePool<Ts>()...)
        );
    }

    template<typename... Ts>
    View<Ts...> view()const{
        return View<Ts...>(
            std::make_tuple(const_cast<ComponentPool<Ts>*>(getPool<Ts>())...)
        );
    }


    // ---- System management ----
public:
    template<typename T, typename... Args>
    T& addSystem(Args&&... args){
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


    // ---- Message bus ----
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


    // ---- Tick ----
public:
    void tick(float delta);


    // ---- Internal ----
private:
    template<typename T>
    ComponentPool<T>* assurePool(){
        const auto typeId = ComponentType<T>();
        auto itr = m_pools.find(typeId);
        if(itr != m_pools.end())
            return static_cast<ComponentPool<T>*>(itr->second.get());

        auto pool = MakeUnique<ComponentPool<T>>();
        auto* raw = pool.get();
        m_pools.emplace(typeId, Move(pool));
        return raw;
    }

    void destroyEntityComponents(Entity entity);


private:
    Alloc::ThreadPool m_threadPool;
    EntityManager m_entityManager;
    HashMap<ComponentTypeId, UniquePtr<IComponentPool>> m_pools;
    Vector<UniquePtr<ISystem>> m_systems;
    SystemScheduler m_scheduler;
    MessageBus m_messageBus;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

