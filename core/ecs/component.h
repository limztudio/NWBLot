// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "entity.h"

#include <typeindex>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ComponentTypeId = usize;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TypeIdGenerator{
public:
    template<typename T>
    static ComponentTypeId id(){
        static const ComponentTypeId value = s_NextId++;
        return value;
    }


private:
    inline static Atomic<ComponentTypeId> s_NextId{ 0 };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline ComponentTypeId ComponentType(){
    return __hidden_ecs::TypeIdGenerator::id<Decay_T<T>>();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IComponentPool{
public:
    virtual ~IComponentPool() = default;


public:
    virtual bool has(Entity entity)const = 0;
    virtual void remove(Entity entity) = 0;
    virtual void clear() = 0;
    virtual usize size()const = 0;
    virtual ComponentTypeId typeId()const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ComponentPool : public IComponentPool{
public:
    ComponentPool()
        : m_typeId(ComponentType<T>())
    {}


public:
    template<typename... Args>
    T& add(Entity entity, Args&&... args){
        NWB_ASSERT(!has(entity));

        const u32 index = entity.index();

        if(index >= static_cast<u32>(m_sparse.size()))
            m_sparse.resize(static_cast<usize>(index) + 1, ~0u);

        m_sparse[index] = static_cast<u32>(m_dense.size());
        m_dense.push_back(entity);
        m_components.emplace_back(Forward<Args>(args)...);

        return m_components.back();
    }

    inline T& get(Entity entity){
        NWB_ASSERT(has(entity));
        return m_components[m_sparse[entity.index()]];
    }
    inline const T& get(Entity entity)const{
        NWB_ASSERT(has(entity));
        return m_components[m_sparse[entity.index()]];
    }

    inline bool has(Entity entity)const override{
        const u32 index = entity.index();
        if(index >= static_cast<u32>(m_sparse.size()))
            return false;
        const u32 dense = m_sparse[index];
        if(dense >= static_cast<u32>(m_dense.size()))
            return false;
        return m_dense[dense] == entity;
    }

    void remove(Entity entity)override{
        if(!has(entity))
            return;

        const u32 index = entity.index();
        const u32 denseIndex = m_sparse[index];
        const u32 lastDense = static_cast<u32>(m_dense.size()) - 1;

        if(denseIndex != lastDense){
            // Swap-and-pop
            const Entity lastEntity = m_dense[lastDense];
            m_dense[denseIndex] = lastEntity;
            m_components[denseIndex] = Move(m_components[lastDense]);
            m_sparse[lastEntity.index()] = denseIndex;
        }

        m_dense.pop_back();
        m_components.pop_back();
        m_sparse[index] = ~0u;
    }

    inline void clear()override{
        m_sparse.clear();
        m_dense.clear();
        m_components.clear();
    }

    inline usize size()const override{ return m_dense.size(); }
    inline ComponentTypeId typeId()const override{ return m_typeId; }


public:
    inline Entity* entities(){ return m_dense.data(); }
    inline const Entity* entities()const{ return m_dense.data(); }

    inline T* components(){ return m_components.data(); }
    inline const T* components()const{ return m_components.data(); }

    inline Vector<Entity>& denseEntities(){ return m_dense; }
    inline const Vector<Entity>& denseEntities()const{ return m_dense; }

    inline Vector<T>& denseComponents(){ return m_components; }
    inline const Vector<T>& denseComponents()const{ return m_components; }


private:
    ComponentTypeId m_typeId;
    Vector<u32> m_sparse;
    Vector<Entity> m_dense;
    Vector<T> m_components;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

