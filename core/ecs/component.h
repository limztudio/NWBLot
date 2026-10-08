// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "entity_id.h"
#include "type_id.h"

#include <global/expected.h>


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
inline ComponentTypeId ComponentType()noexcept{
    return ECSDetail::TypeCounter<ECSDetail::ComponentTypeTag>::Id<Decay_T<T>>();
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
    virtual u64 mutationVersion()const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ComponentPool final : public IComponentPool{
    friend struct ECSDetail::ViewTupleAccess;


private:
    [[nodiscard]] inline Expected<u32> findDenseIndex(EntityID entityId)const noexcept{
        const u32 index = entityId.index();
        if(index >= static_cast<u32>(m_sparse.size()))
            return MakeUnexpected(Failure{});
        const u32 denseIndex = m_sparse[index];
        if(denseIndex >= static_cast<u32>(m_dense.size()))
            return MakeUnexpected(Failure{});
        if(m_dense[denseIndex] != entityId)
            return MakeUnexpected(Failure{});

        return denseIndex;
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
    explicit ComponentPool(Alloc::GlobalArena& arena)
        : m_sparse(arena)
        , m_dense(arena)
        , m_components(arena)
        , m_mutationVersion(0u)
    {}


public:
    template<typename... Args>
    T& add(EntityID entityId, Args&&... args){
        if(const auto existingDenseIndex = findDenseIndex(entityId))
            return m_components[*existingDenseIndex];

        const u32 index = entityId.index();

        if(index >= static_cast<u32>(m_sparse.size()))
            m_sparse.resize(static_cast<usize>(index) + 1, ~0u);

        const u32 denseIndex = static_cast<u32>(m_dense.size());
        m_sparse[index] = denseIndex;
        m_dense.push_back(entityId);
        m_components.emplace_back(Forward<Args>(args)...);
        ++m_mutationVersion;

        return m_components[denseIndex];
    }

    inline T& get(EntityID entityId){
        return m_components[requireDenseIndex(entityId)];
    }
    inline const T& get(EntityID entityId)const{
        return m_components[requireDenseIndex(entityId)];
    }
    inline T* tryGet(EntityID entityId)noexcept{
        const auto denseIndex = findDenseIndex(entityId);
        if(!denseIndex)
            return nullptr;
        return &m_components[*denseIndex];
    }
    inline const T* tryGet(EntityID entityId)const noexcept{
        const auto denseIndex = findDenseIndex(entityId);
        if(!denseIndex)
            return nullptr;
        return &m_components[*denseIndex];
    }

    inline virtual bool has(EntityID entityId)const override{
        return findDenseIndex(entityId).has_value();
    }

    virtual bool remove(EntityID entityId)override{
        const auto resolvedDenseIndex = findDenseIndex(entityId);
        if(!resolvedDenseIndex)
            return false;
        const u32 denseIndex = *resolvedDenseIndex;

        const u32 index = entityId.index();
        const u32 lastDense = static_cast<u32>(m_dense.size()) - 1;

        if(denseIndex != lastDense){
            const EntityID lastEntityId = m_dense[lastDense];
            m_dense[denseIndex] = lastEntityId;
            m_components[denseIndex] = Move(m_components[lastDense]);
            m_sparse[lastEntityId.index()] = denseIndex;
        }

        m_dense.pop_back();
        m_components.pop_back();
        m_sparse[index] = ~0u;
        ++m_mutationVersion;
        return true;
    }

    inline virtual void clear()override{
        if(!m_dense.empty())
            ++m_mutationVersion;
        m_sparse.clear();
        m_dense.clear();
        m_components.clear();
    }

    inline virtual usize size()const override{ return m_dense.size(); }
    inline virtual u64 mutationVersion()const override{ return m_mutationVersion; }

private:
    Vector<u32, Alloc::GlobalArena> m_sparse;
    Vector<EntityID, Alloc::GlobalArena> m_dense;
    Vector<T, Alloc::GlobalArena> m_components;
    u64 m_mutationVersion;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

