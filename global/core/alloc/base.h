
#pragma once


#include "global.h"

#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ArenaMemoryStats{
    u64 reservedBytes = 0u;
    u64 usedBytes = 0u;
    u64 peakUsedBytes = 0u;
    u64 allocationCount = 0u;
    u64 reallocationCount = 0u;
    u64 deallocationCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArenaMemoryTracker final : NoCopy{
public:
    void reset(const u64 reservedBytes = 0u){
        m_reservedBytes.store(reservedBytes, MemoryOrder::relaxed);
        m_usedBytes.store(0u, MemoryOrder::relaxed);
        m_peakUsedBytes.store(0u, MemoryOrder::relaxed);
        m_allocationCount.store(0u, MemoryOrder::relaxed);
        m_reallocationCount.store(0u, MemoryOrder::relaxed);
        m_deallocationCount.store(0u, MemoryOrder::relaxed);
    }

    void addReservedBytes(const u64 bytes){
        if(bytes == 0u)
            return;

        m_reservedBytes.fetch_add(bytes, MemoryOrder::relaxed);
    }

    void removeReservedBytes(const u64 bytes){
        if(bytes == 0u)
            return;

        m_reservedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
    }

    void recordAllocation(const u64 bytes){
        if(bytes == 0u)
            return;

        const u64 usedBytes = m_usedBytes.fetch_add(bytes, MemoryOrder::relaxed) + bytes;
        recordPeakUsedBytes(usedBytes);
        m_allocationCount.fetch_add(1u, MemoryOrder::relaxed);
    }

    void recordReallocation(const u64 oldBytes, const u64 newBytes){
        if(oldBytes == 0u && newBytes == 0u)
            return;
        if(oldBytes == 0u){
            recordAllocation(newBytes);
            return;
        }
        if(newBytes == 0u){
            recordDeallocation(oldBytes);
            return;
        }

        u64 usedBytes = 0u;
        if(newBytes >= oldBytes)
            usedBytes = m_usedBytes.fetch_add(newBytes - oldBytes, MemoryOrder::relaxed) + (newBytes - oldBytes);
        else
            usedBytes = m_usedBytes.fetch_sub(oldBytes - newBytes, MemoryOrder::relaxed) - (oldBytes - newBytes);
        recordPeakUsedBytes(usedBytes);
        m_reallocationCount.fetch_add(1u, MemoryOrder::relaxed);
    }

    void recordDeallocation(const u64 bytes){
        if(bytes == 0u)
            return;

        m_usedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
        m_deallocationCount.fetch_add(1u, MemoryOrder::relaxed);
    }

    [[nodiscard]] ArenaMemoryStats snapshot()const{
        ArenaMemoryStats stats;
        stats.reservedBytes = m_reservedBytes.load(MemoryOrder::relaxed);
        stats.usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
        stats.peakUsedBytes = m_peakUsedBytes.load(MemoryOrder::relaxed);
        stats.allocationCount = m_allocationCount.load(MemoryOrder::relaxed);
        stats.reallocationCount = m_reallocationCount.load(MemoryOrder::relaxed);
        stats.deallocationCount = m_deallocationCount.load(MemoryOrder::relaxed);
        return stats;
    }


private:
    void recordPeakUsedBytes(const u64 usedBytes){
        u64 peakBytes = m_peakUsedBytes.load(MemoryOrder::relaxed);
        while(usedBytes > peakBytes){
            if(m_peakUsedBytes.compare_exchange_weak(peakBytes, usedBytes, MemoryOrder::relaxed))
                return;
        }
    }


private:
    Atomic<u64> m_reservedBytes{ 0u };
    Atomic<u64> m_usedBytes{ 0u };
    Atomic<u64> m_peakUsedBytes{ 0u };
    Atomic<u64> m_allocationCount{ 0u };
    Atomic<u64> m_reallocationCount{ 0u };
    Atomic<u64> m_deallocationCount{ 0u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArenaBase : NoCopy{
protected:
    explicit ArenaBase(const Name& allocationLog)
        : m_name(allocationLog)
    {
        NWB_ASSERT_MSG(static_cast<bool>(m_name), NWB_TEXT("ArenaBase allocationLog must be a valid name"));
    }
    ~ArenaBase() = default;


public:
    [[nodiscard]] ArenaMemoryStats memoryStats()const{ return m_memoryStats.snapshot(); }


protected:
    [[nodiscard]] inline const Name& name()const{ return m_name; }
    // Non-resolving: the allocator must NOT run the symbol resolver (it allocates from a GlobalArena whose allocate()
    // logs its own name -> re-entrancy / the opt stack overflow). c_str() resolves; logText() never does.
    [[nodiscard]] inline const char* log()const{ return m_name.logText(); }


protected:
    ArenaMemoryTracker m_memoryStats;

private:
    Name m_name;
};


template<typename Arena>
class ArenaBaseT : public ArenaBase{
protected:
    explicit ArenaBaseT(const Name& allocationLog)
        : ArenaBase(allocationLog)
    {}
    ~ArenaBaseT() = default;


public:
    template<typename T>
    inline T* allocate(usize count){
        if constexpr(requires{ Arena::s_MaxAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxAlignSize, "Arena cannot allocate types with alignment greater than s_MaxAlignSize.");

        return AllocDetail::AllocateTyped<T>(static_cast<Arena&>(*this), count);
    }

    template<typename T>
    inline void deallocate(void* p, usize count){
        if constexpr(requires{ Arena::s_MaxAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxAlignSize, "Arena cannot deallocate types with alignment greater than s_MaxAlignSize.");

        AllocDetail::DeallocateTyped<T>(static_cast<Arena&>(*this), p, count);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

