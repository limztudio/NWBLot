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
    explicit ArenaMemoryTracker(const Name& ownerName, ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate);
    ~ArenaMemoryTracker()noexcept;


public:
    void releaseRetainedMemory()noexcept;
    void reset(u64 reservedBytes = 0u)noexcept;

    void addReservedBytes(const u64 bytes)noexcept{
        if(bytes != 0u)
            m_reservedBytes.fetch_add(bytes, MemoryOrder::relaxed);
    }

    void removeReservedBytes(const u64 bytes)noexcept{
        if(bytes != 0u)
            m_reservedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
    }

    // Direct arenas derive reservation from usage; hot paths update local counters only.
    void recordAllocation(u64 bytes)noexcept;
    void recordReallocation(u64 oldBytes, u64 newBytes)noexcept;
    void recordDeallocation(u64 bytes)noexcept;
    [[nodiscard]] ArenaMemoryStats snapshot()const noexcept;
    [[nodiscard]] NWB_INLINE Name ownerName()const noexcept;


private:
    void recordPeakUsedBytes(u64 usedBytes)noexcept;


private:
    ArenaMemoryOwnerRecord* m_owner = nullptr;
    ArenaMemoryTracker* m_previous = nullptr;
    ArenaMemoryTracker* m_next = nullptr;
    Atomic<u64> m_reservedBytes{ 0u };
    Atomic<u64> m_usedBytes{ 0u };
    Atomic<u64> m_peakUsedBytes{ 0u };
    Atomic<u64> m_allocationCount{ 0u };
    Atomic<u64> m_reallocationCount{ 0u };
    Atomic<u64> m_deallocationCount{ 0u };
    ArenaMemoryReservation::Enum m_reservation = ArenaMemoryReservation::Separate;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ArenaMemoryOwnerIdentity{
    Name ownerName = s_NameNone;
    ArenaMemorySource::Enum source = ArenaMemorySource::Arena;
};

struct ArenaMemoryOwnerSnapshot{
    Name ownerName = s_NameNone;
    ArenaMemoryStats stats;
    ArenaMemorySource::Enum source = ArenaMemorySource::Arena;
};

// Records live for process lifetime; traversal captures head to stay bounded.
[[nodiscard]] const ArenaMemoryOwnerRecord* FirstArenaMemoryOwnerRecord()noexcept;
// Identity and successor are immutable after publication; this read does not sample counters or lock live trackers.
[[nodiscard]] const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerIdentity(const ArenaMemoryOwnerRecord& record, ArenaMemoryOwnerIdentity& outIdentity)noexcept;

NWB_INLINE Name ArenaMemoryTracker::ownerName()const noexcept{
    if(!m_owner)
        return s_NameNone;
    ArenaMemoryOwnerIdentity identity;
    (void)ReadArenaMemoryOwnerIdentity(*m_owner, identity);
    return identity.ownerName;
}
[[nodiscard]] const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerRecord(const ArenaMemoryOwnerRecord& record, ArenaMemoryOwnerSnapshot& outSnapshot)noexcept;

// Raw heap total is separate from named arenas; per-thread counters retain exited history.
void RecordHeapMemoryAllocation(u64 bytes)noexcept;
void RecordHeapMemoryReallocation(u64 oldBytes, u64 newBytes)noexcept;
void RecordHeapMemoryDeallocation(u64 bytes)noexcept;
[[nodiscard]] ArenaMemoryStats HeapBackingMemoryStats()noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

