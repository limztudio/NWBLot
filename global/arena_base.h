// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "arena_c_allocator.h"
#include "assert.h"
#include "arena_memory.h"
#include "generic.h"
#include "name.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArenaBase : NoCopy{
protected:
    explicit ArenaBase(const Name& allocationLog, const ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate)
        : m_memoryStats(allocationLog, reservation)
    {
        NWB_ASSERT_MSG(static_cast<b8>(allocationLog), NWB_TEXT("ArenaBase allocationLog must be a valid name"));
    }


public:
    virtual ~ArenaBase()noexcept = default;


public:
    [[nodiscard]] virtual void* allocate(usize align, usize size) = 0;
    [[nodiscard]] virtual void* reallocate(void* p, usize align, usize size) = 0;
    virtual void deallocate(void* p, usize align, usize size)noexcept = 0;

    template<typename T>
    [[nodiscard]] NWB_INLINE T* allocate(const usize count){
        return ::AllocateArenaTyped<T>(*this, count);
    }

    template<typename T>
    NWB_INLINE void deallocate(void* const p, const usize count)noexcept{
        ::DeallocateArenaTyped<T>(*this, p, count);
    }

    [[nodiscard]] ArenaMemoryStats memoryStats()const noexcept{ return m_memoryStats.snapshot(); }
    [[nodiscard]] NWB_INLINE Name allocationLog()const noexcept{ return m_memoryStats.ownerName(); }


protected:
    ArenaMemoryTracker m_memoryStats;
};


template<typename Arena>
class ArenaBaseT : public ArenaBase{
protected:
    explicit ArenaBaseT(const Name& allocationLog, const ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate)
        : ArenaBase(allocationLog, reservation)
    {}
    ~ArenaBaseT()noexcept override = default;


public:
    using ArenaBase::allocate;
    using ArenaBase::deallocate;

    template<typename T>
    [[nodiscard]] NWB_INLINE T* allocate(usize count)noexcept(noexcept(::AllocateArenaTyped<T>(DeclVal<Arena&>(), count))){
        if constexpr(requires{ Arena::s_MaxAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxAlignSize, "Arena cannot allocate types with alignment greater than s_MaxAlignSize.");

        return ::AllocateArenaTyped<T>(static_cast<Arena&>(*this), count);
    }

    template<typename T>
    NWB_INLINE void deallocate(void* p, usize count)noexcept(noexcept(::DeallocateArenaTyped<T>(DeclVal<Arena&>(), p, count))){
        if constexpr(requires{ Arena::s_MaxAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxAlignSize, "Arena cannot deallocate types with alignment greater than s_MaxAlignSize.");

        ::DeallocateArenaTyped<T>(static_cast<Arena&>(*this), p, count);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

