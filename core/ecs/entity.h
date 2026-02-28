// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "world.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Entity{
    friend class World;


public:
    [[nodiscard]] bool alive()const{ return m_world.alive(m_entity); }

    void destroy(){ m_world.destroyEntity(m_entity); }


public:
    template<typename T, typename... Args>
    T& addComponent(Args&&... args){
        return m_world.addComponent<T>(m_entity, Forward<Args>(args)...);
    }

    template<typename T>
    void removeComponent(){
        m_world.removeComponent<T>(m_entity);
    }

    template<typename T>
    T& getComponent(){
        return m_world.getComponent<T>(m_entity);
    }

    template<typename T>
    const T& getComponent()const{
        return m_world.getComponent<T>(m_entity);
    }

    template<typename T>
    bool hasComponent()const{
        return m_world.hasComponent<T>(m_entity);
    }


private:
    Entity(World& world, EntityID entityId)
        : m_world(world)
        , m_entity(entityId)
    {}


private:
    World& m_world;
    EntityID m_entity;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

