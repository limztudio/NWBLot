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
    explicit ArenaBase(
        const Name& allocationLog,
        const ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate)
        : m_memoryStats(allocationLog, reservation)
    {
        NWB_ASSERT_MSG(static_cast<bool>(allocationLog), NWB_TEXT("ArenaBase allocationLog must be a valid name"));
    }
    ~ArenaBase() = default;


public:
    [[nodiscard]] ArenaMemoryStats memoryStats()const{ return m_memoryStats.snapshot(); }


protected:
    ArenaMemoryTracker m_memoryStats;
};


template<typename Arena>
class ArenaBaseT : public ArenaBase{
protected:
    explicit ArenaBaseT(
        const Name& allocationLog,
        const ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate)
        : ArenaBase(allocationLog, reservation)
    {}
    ~ArenaBaseT() = default;


public:
    template<typename T>
    inline T* allocate(usize count){
        if constexpr(requires{ Arena::s_MaxAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxAlignSize, "Arena cannot allocate types with alignment greater than s_MaxAlignSize.");

        return ::AllocateArenaTyped<T>(static_cast<Arena&>(*this), count);
    }

    template<typename T>
    inline void deallocate(void* p, usize count){
        if constexpr(requires{ Arena::s_MaxAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxAlignSize, "Arena cannot deallocate types with alignment greater than s_MaxAlignSize.");

        ::DeallocateArenaTyped<T>(static_cast<Arena&>(*this), p, count);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

