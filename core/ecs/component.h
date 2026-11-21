// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "entity_id.h"
#include "type_id.h"

#include <global/expected.h>
#include <global/name.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_COMPONENT_TYPE(componentTypeLiteral) \
    static constexpr ::AStringView s_ComponentTypeText = componentTypeLiteral; \
    inline static constexpr ::Name s_ComponentTypeName = ::Name(componentTypeLiteral); \
    [[nodiscard]] static constexpr const ::Name& ComponentTypeName()noexcept{ \
        return s_ComponentTypeName; \
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewTupleAccess;

inline constexpr u32 s_InvalidDenseIndex = s_EntityIndexMask;

[[nodiscard]] FOUNDATION_INLINE constexpr u32 PackSparseEntry(const EntityID entityId, const u32 denseIndex)noexcept{
    return (entityId.id & ~s_EntityIndexMask) | (denseIndex & s_EntityIndexMask);
}

[[nodiscard]] FOUNDATION_INLINE u32 FindPackedSparseDenseIndex(const u32* const sparseData, const usize sparseSize, const EntityID entityId)noexcept{
    const u32 index = entityId.index();
    if(index >= sparseSize)
        return s_InvalidDenseIndex;

    const u32 packed = sparseData[index];
    const u32 denseIndex = packed & s_EntityIndexMask;
    if(denseIndex == s_InvalidDenseIndex || ((packed ^ entityId.id) >> s_EntityIndexBits) != 0u)
        return s_InvalidDenseIndex;

    return denseIndex;
}

struct SparsePoolView{
    const u32* sparseData = nullptr;
    usize sparseSize = 0u;


    [[nodiscard]] FOUNDATION_INLINE u32 findDenseIndex(const EntityID entityId)const noexcept{
        return FindPackedSparseDenseIndex(sparseData, sparseSize, entityId);
    }
};

template<typename T>
struct PoolSlice{
    const u32* sparseData = nullptr;
    usize sparseSize = 0u;
    T* components = nullptr;


    [[nodiscard]] FOUNDATION_INLINE u32 findDenseIndex(const EntityID entityId)const noexcept{
        return FindPackedSparseDenseIndex(sparseData, sparseSize, entityId);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IComponentPool{
public:
    virtual ~IComponentPool()noexcept = default;


public:
    virtual b8 has(EntityID entityId)const = 0;
    virtual b8 remove(EntityID entityId) = 0;
    virtual void clear() = 0;
    virtual usize size()const = 0;
    virtual u64 mutationVersion()const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ComponentPool final : public IComponentPool{
    friend class World;
    friend struct ECSDetail::ViewTupleAccess;


public:
    explicit ComponentPool(Alloc::GlobalArena& arena)noexcept
        : m_sparse(arena)
        , m_dense(arena)
        , m_components(arena)
        , m_mutationVersion(0u)
    {}


public:
    template<typename... Args>
    T& add(EntityID entityId, Args&&... args){
        const u32 existingDenseIndex = findDenseIndexRaw(entityId);
        if(existingDenseIndex != ECSDetail::s_InvalidDenseIndex)
            return m_components[existingDenseIndex];

        return addUnchecked(entityId, Forward<Args>(args)...);
    }

    inline T& get(EntityID entityId)noexcept{
        return m_components[requireDenseIndex(entityId)];
    }

    inline const T& get(EntityID entityId)const noexcept{
        return m_components[requireDenseIndex(entityId)];
    }

    inline T* tryGet(EntityID entityId)noexcept{
        const u32 denseIndex = findDenseIndexRaw(entityId);
        if(denseIndex == ECSDetail::s_InvalidDenseIndex)
            return nullptr;
        return AddressOf(m_components[denseIndex]);
    }

    inline const T* tryGet(EntityID entityId)const noexcept{
        const u32 denseIndex = findDenseIndexRaw(entityId);
        if(denseIndex == ECSDetail::s_InvalidDenseIndex)
            return nullptr;
        return AddressOf(m_components[denseIndex]);
    }

    inline virtual b8 has(EntityID entityId)const noexcept override{
        return findDenseIndexRaw(entityId) != ECSDetail::s_InvalidDenseIndex;
    }

    virtual b8 remove(EntityID entityId)noexcept(noexcept(DeclVal<T&>() = DeclVal<T&&>()))override{
        const u32 denseIndex = findDenseIndexRaw(entityId);
        if(denseIndex == ECSDetail::s_InvalidDenseIndex)
            return false;

        const u32 index = entityId.index();
        const u32 lastDense = static_cast<u32>(m_dense.size()) - 1;

        if(denseIndex != lastDense){
            const EntityID lastEntityId = m_dense[lastDense];
            m_components[denseIndex] = Move(m_components[lastDense]);
            m_dense[denseIndex] = lastEntityId;
            m_sparse[lastEntityId.index()] = ECSDetail::PackSparseEntry(lastEntityId, denseIndex);
        }

        m_dense.pop_back();
        m_components.pop_back();
        m_sparse[index] = ~0u;
        ++m_mutationVersion;
        return true;
    }

    inline virtual void clear()noexcept override{
        if(!m_dense.empty())
            ++m_mutationVersion;
        m_sparse.clear();
        m_dense.clear();
        m_components.clear();
    }

    inline virtual usize size()const noexcept override{ return m_dense.size(); }

    inline virtual u64 mutationVersion()const noexcept override{ return m_mutationVersion; }


private:
    template<typename... Args>
    T& addUnchecked(EntityID entityId, Args&&... args){
        FOUNDATION_ASSERT(findDenseIndexRaw(entityId) == ECSDetail::s_InvalidDenseIndex);

        const u32 index = entityId.index();
        if(index >= static_cast<u32>(m_sparse.size()))
            m_sparse.resize(static_cast<usize>(index) + 1, ~0u);

        const u32 denseIndex = static_cast<u32>(m_dense.size());
        FOUNDATION_ASSERT(denseIndex < ECSDetail::s_EntityIndexMask);
        m_dense.push_back(entityId);
        ScopeExit rollbackDense([this]()noexcept{ m_dense.pop_back(); });
        m_components.emplace_back(Forward<Args>(args)...);
        m_sparse[index] = ECSDetail::PackSparseEntry(entityId, denseIndex);
        ++m_mutationVersion;
        rollbackDense.release();

        return m_components[denseIndex];
    }

    [[nodiscard]] FOUNDATION_INLINE u32 findDenseIndexRaw(EntityID entityId)const noexcept{
        return ECSDetail::FindPackedSparseDenseIndex(m_sparse.data(), m_sparse.size(), entityId);
    }

    [[nodiscard]] inline u32 requireDenseIndex(EntityID entityId)const noexcept{
        const u32 index = entityId.index();
        FOUNDATION_ASSERT(index < static_cast<u32>(m_sparse.size()));

        const u32 packed = m_sparse[index];
        const u32 denseIndex = packed & ECSDetail::s_EntityIndexMask;
        FOUNDATION_ASSERT(denseIndex != ECSDetail::s_InvalidDenseIndex);
        FOUNDATION_ASSERT(((packed ^ entityId.id) >> ECSDetail::s_EntityIndexBits) == 0u);
        FOUNDATION_ASSERT(denseIndex < static_cast<u32>(m_dense.size()));
        FOUNDATION_ASSERT(m_dense[denseIndex] == entityId);
        return denseIndex;
    }


private:
    Vector<u32, Alloc::GlobalArena> m_sparse;
    Vector<EntityID, Alloc::GlobalArena> m_dense;
    Vector<T, Alloc::GlobalArena> m_components;
    u64 m_mutationVersion;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

