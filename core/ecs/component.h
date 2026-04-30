// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "entity_id.h"
#include "type_id.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ComponentTypeId = usize;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewTupleAccess;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline ComponentTypeId ComponentType(){
    return ECSDetail::TypeCounter<ECSDetail::ComponentTypeTag>::id<Decay_T<T>>();
}

template<typename... Ts>
class View;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IComponentPool{
public:
    virtual ~IComponentPool() = default;


public:
    virtual bool has(EntityID entityId)const = 0;
    virtual bool remove(EntityID entityId) = 0;
    virtual void clear() = 0;
    virtual usize size()const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ComponentPool final : public IComponentPool{
    friend struct ECSDetail::ViewTupleAccess;


public:
    using SparseAllocator = Alloc::CustomAllocator<u32>;
    using EntityIdAllocator = Alloc::CustomAllocator<EntityID>;
    using ComponentAllocator = Alloc::CustomAllocator<T>;


private:
    [[nodiscard]] inline bool findDenseIndex(EntityID entityId, u32& outDenseIndex)const{
        const u32 index = entityId.index();
        if(index >= static_cast<u32>(m_sparse.size()))
            return false;
        const u32 denseIndex = m_sparse[index];
        if(denseIndex >= static_cast<u32>(m_dense.size()))
            return false;
        if(m_dense[denseIndex] != entityId)
            return false;

        outDenseIndex = denseIndex;
        return true;
    }

    [[nodiscard]] inline u32 requireDenseIndex(EntityID entityId)const{
        const u32 index = entityId.index();
        NWB_ASSERT(index < static_cast<u32>(m_sparse.size()));

        const u32 denseIndex = m_sparse[index];
        NWB_ASSERT(denseIndex < static_cast<u32>(m_dense.size()));
        NWB_ASSERT(m_dense[denseIndex] == entityId);
        return denseIndex;
    }


public:
    explicit ComponentPool(Alloc::CustomArena& arena)
        : m_sparse(SparseAllocator(arena))
        , m_dense(EntityIdAllocator(arena))
        , m_components(ComponentAllocator(arena))
    {}


public:
    template<typename... Args>
    T& add(EntityID entityId, Args&&... args){
        u32 existingDenseIndex = 0u;
        if(findDenseIndex(entityId, existingDenseIndex))
            return m_components[existingDenseIndex];

        const u32 index = entityId.index();

        if(index >= static_cast<u32>(m_sparse.size()))
            m_sparse.resize(static_cast<usize>(index) + 1, ~0u);

        const u32 denseIndex = static_cast<u32>(m_dense.size());
        m_sparse[index] = denseIndex;
        m_dense.push_back(entityId);
        m_components.emplace_back(Forward<Args>(args)...);

        return m_components[denseIndex];
    }

    inline T& get(EntityID entityId){
        return m_components[requireDenseIndex(entityId)];
    }
    inline const T& get(EntityID entityId)const{
        return m_components[requireDenseIndex(entityId)];
    }
    inline T* tryGet(EntityID entityId){
        u32 denseIndex = 0;
        if(!findDenseIndex(entityId, denseIndex))
            return nullptr;
        return &m_components[denseIndex];
    }
    inline const T* tryGet(EntityID entityId)const{
        u32 denseIndex = 0;
        if(!findDenseIndex(entityId, denseIndex))
            return nullptr;
        return &m_components[denseIndex];
    }

    inline virtual bool has(EntityID entityId)const override{
        u32 denseIndex = 0;
        return findDenseIndex(entityId, denseIndex);
    }

    virtual bool remove(EntityID entityId)override{
        u32 denseIndex = 0;
        if(!findDenseIndex(entityId, denseIndex))
            return false;

        const u32 index = entityId.index();
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
        return true;
    }

    inline virtual void clear()override{
        m_sparse.clear();
        m_dense.clear();
        m_components.clear();
    }

    inline virtual usize size()const override{ return m_dense.size(); }

private:
    Vector<u32, SparseAllocator> m_sparse;
    Vector<EntityID, EntityIdAllocator> m_dense;
    Vector<T, ComponentAllocator> m_components;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

