// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "entity_id.h"


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


namespace __hidden_ecs{
template<typename... Ts>
struct ViewIterator;
};

template<typename... Ts>
class View;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IComponentPool{
public:
    virtual ~IComponentPool() = default;


public:
    virtual bool has(EntityID entityId)const = 0;
    virtual void remove(EntityID entityId) = 0;
    virtual void clear() = 0;
    virtual usize size()const = 0;
    virtual ComponentTypeId typeId()const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ComponentPool : public IComponentPool{
    template<typename... Ts>
    friend struct __hidden_ecs::ViewIterator;
    template<typename... Ts>
    friend class View;


public:
    using SparseAllocator = Alloc::CustomAllocator<u32>;
    using EntityIdAllocator = Alloc::CustomAllocator<EntityID>;
    using ComponentAllocator = Alloc::CustomAllocator<T>;


public:
    explicit ComponentPool(Alloc::CustomArena& arena)
        : m_typeId(ComponentType<T>())
        , m_sparse(SparseAllocator(arena))
        , m_dense(EntityIdAllocator(arena))
        , m_components(ComponentAllocator(arena))
    {}


public:
    template<typename... Args>
    T& add(EntityID entityId, Args&&... args){
        NWB_ASSERT(!has(entityId));

        const u32 index = entityId.index();

        if(index >= static_cast<u32>(m_sparse.size()))
            m_sparse.resize(static_cast<usize>(index) + 1, ~0u);

        m_sparse[index] = static_cast<u32>(m_dense.size());
        m_dense.push_back(entityId);
        m_components.emplace_back(Forward<Args>(args)...);

        return m_components.back();
    }

    inline T& get(EntityID entityId){
        NWB_ASSERT(has(entityId));
        return m_components[m_sparse[entityId.index()]];
    }
    inline const T& get(EntityID entityId)const{
        NWB_ASSERT(has(entityId));
        return m_components[m_sparse[entityId.index()]];
    }

    inline virtual bool has(EntityID entityId)const override{
        const u32 index = entityId.index();
        if(index >= static_cast<u32>(m_sparse.size()))
            return false;
        const u32 dense = m_sparse[index];
        if(dense >= static_cast<u32>(m_dense.size()))
            return false;
        return m_dense[dense] == entityId;
    }

    virtual void remove(EntityID entityId)override{
        if(!has(entityId))
            return;

        const u32 index = entityId.index();
        const u32 denseIndex = m_sparse[index];
        const u32 lastDense = static_cast<u32>(m_dense.size()) - 1;

        if(denseIndex != lastDense){
            // Swap-and-pop
            const EntityID lastEntityId = m_dense[lastDense];
            m_dense[denseIndex] = lastEntityId;
            m_components[denseIndex] = Move(m_components[lastDense]);
            m_sparse[lastEntityId.index()] = denseIndex;
        }

        m_dense.pop_back();
        m_components.pop_back();
        m_sparse[index] = ~0u;
    }

    inline virtual void clear()override{
        m_sparse.clear();
        m_dense.clear();
        m_components.clear();
    }

    inline virtual usize size()const override{ return m_dense.size(); }
    inline virtual ComponentTypeId typeId()const override{ return m_typeId; }

private:
    ComponentTypeId m_typeId;
    Vector<u32, SparseAllocator> m_sparse;
    Vector<EntityID, EntityIdAllocator> m_dense;
    Vector<T, ComponentAllocator> m_components;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

