// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


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
    static constexpr usize s_LogCapacity = 128;


protected:
    explicit ArenaBase(const char* allocationLog)
        : m_log{}
    {
        NWB_ASSERT_MSG(allocationLog, NWB_TEXT("ArenaBase allocationLog must be non-null"));

        const char* source = allocationLog ? allocationLog : "";
        usize i = 0;
        for(; i + 1 < LengthOf(m_log) && source[i] != '\0'; ++i)
            m_log[i] = source[i];
        m_log[i] = '\0';
    }
    ~ArenaBase() = default;

protected:
    [[nodiscard]] inline const char* log()const{ return m_log; }


private:
    char m_log[s_LogCapacity];
};


template<typename Arena>
class ArenaBaseT : public ArenaBase{
protected:
    explicit ArenaBaseT(const char* allocationLog)
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

