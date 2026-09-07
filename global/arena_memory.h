// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"
#include "generic.h"
#include "name.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArenaMemorySource{
    enum Enum : u8{
        Arena,
        HeapBacking,
    };
};

namespace ArenaMemoryReservation{
    enum Enum : u8{
        Separate,
        FollowsUsage,
    };
};

struct ArenaMemoryStats{
    u64 reservedBytes = 0u;
    u64 usedBytes = 0u;
    u64 peakUsedBytes = 0u;
    u64 allocationCount = 0u;
    u64 reallocationCount = 0u;
    u64 deallocationCount = 0u;
};

struct ArenaMemoryOwnerRecord;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArenaMemoryTracker final : NoCopy{
    friend struct ArenaMemoryOwnerRecord;


public:
    constexpr ArenaMemoryTracker()noexcept = default;
    explicit ArenaMemoryTracker(
        const Name& ownerName,
        ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate
    );
    ~ArenaMemoryTracker();


public:
    void releaseRetainedMemory()noexcept;
    void reset(u64 reservedBytes = 0u);

    void addReservedBytes(const u64 bytes){
        if(bytes != 0u)
            m_reservedBytes.fetch_add(bytes, MemoryOrder::relaxed);
    }

    void removeReservedBytes(const u64 bytes)noexcept{
        if(bytes != 0u)
            m_reservedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
    }

    // Direct arenas derive reservation from usage during capture. Pools track their reservation separately.
    // Allocation hot paths update only this arena's usage, peak and operation counters.
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
        if(newBytes >= oldBytes){
            const u64 growth = newBytes - oldBytes;
            usedBytes = m_usedBytes.fetch_add(growth, MemoryOrder::relaxed) + growth;
        }
        else{
            const u64 shrink = oldBytes - newBytes;
            usedBytes = m_usedBytes.fetch_sub(shrink, MemoryOrder::relaxed) - shrink;
        }
        recordPeakUsedBytes(usedBytes);
        m_reallocationCount.fetch_add(1u, MemoryOrder::relaxed);
    }

    void recordDeallocation(const u64 bytes)noexcept{
        if(bytes == 0u)
            return;

        m_usedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
        m_deallocationCount.fetch_add(1u, MemoryOrder::relaxed);
    }

    [[nodiscard]] ArenaMemoryStats snapshot()const{
        ArenaMemoryStats stats;
        stats.usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
        stats.reservedBytes = m_reservation == ArenaMemoryReservation::FollowsUsage
            ? stats.usedBytes
            : m_reservedBytes.load(MemoryOrder::relaxed)
        ;
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
    ArenaMemoryReservation::Enum m_reservation = ArenaMemoryReservation::Separate;
    ArenaMemoryOwnerRecord* m_owner = nullptr;
    ArenaMemoryTracker* m_previous = nullptr;
    ArenaMemoryTracker* m_next = nullptr;
    Atomic<u64> m_reservedBytes{ 0u };
    Atomic<u64> m_usedBytes{ 0u };
    Atomic<u64> m_peakUsedBytes{ 0u };
    Atomic<u64> m_allocationCount{ 0u };
    Atomic<u64> m_reallocationCount{ 0u };
    Atomic<u64> m_deallocationCount{ 0u };
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ArenaMemoryOwnerSnapshot{
    Name ownerName = NAME_NONE;
    ArenaMemorySource::Enum source = ArenaMemorySource::Arena;
    ArenaMemoryStats stats;
};

// Records live for the process lifetime. Each read protects the live arena census against construction and
// destruction, adding retired history exactly once. Owner peak is the largest individual arena highwater.
// Traversal captures the current head, so report allocations cannot extend its own traversal indefinitely.
[[nodiscard]] const ArenaMemoryOwnerRecord* FirstArenaMemoryOwnerRecord()noexcept;
[[nodiscard]] const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerRecord(
    const ArenaMemoryOwnerRecord& record,
    ArenaMemoryOwnerSnapshot& outSnapshot
)noexcept;

// Raw heap activity is an inclusive backing total, separate from named arenas. Per-thread cumulative counters
// support frees on another thread and retain exited-thread history. Heap peak is the highest sampled usage.
void RecordHeapMemoryAllocation(u64 bytes)noexcept;
void RecordHeapMemoryReallocation(u64 oldBytes, u64 newBytes)noexcept;
void RecordHeapMemoryDeallocation(u64 bytes)noexcept;
[[nodiscard]] ArenaMemoryStats HeapBackingMemoryStats()noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

