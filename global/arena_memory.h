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
    explicit ArenaMemoryTracker(const Name& ownerName, ArenaMemoryReservation::Enum reservation = ArenaMemoryReservation::Separate, b8 deferAttach = false);
    ~ArenaMemoryTracker()noexcept;


public:
    FOUNDATION_INLINE void ensureAttached()noexcept{
        if(m_attached || !m_owner)
            return;
        attachToOwnerSlow();
    }
    void releaseRetainedMemory()noexcept;
    void reset(u64 reservedBytes = 0u)noexcept;

    void addReservedBytes(const u64 bytes)noexcept{
        if(bytes != 0u && m_reservation == ArenaMemoryReservation::Separate)
            m_reservedBytes.fetch_add(bytes, MemoryOrder::relaxed);
    }

    void removeReservedBytes(const u64 bytes)noexcept{
        if(bytes != 0u && m_reservation == ArenaMemoryReservation::Separate)
            m_reservedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
    }

    void addReservedBytesSingleThread(const u64 bytes)noexcept{
        if(bytes != 0u && m_reservation == ArenaMemoryReservation::Separate)
            m_reservedBytes.store(m_reservedBytes.load(MemoryOrder::relaxed) + bytes, MemoryOrder::relaxed);
    }

    void removeReservedBytesSingleThread(const u64 bytes)noexcept{
        if(bytes != 0u && m_reservation == ArenaMemoryReservation::Separate)
            m_reservedBytes.store(m_reservedBytes.load(MemoryOrder::relaxed) - bytes, MemoryOrder::relaxed);
    }

    FOUNDATION_INLINE void recordAllocationSingleThread(const u64 bytes)noexcept{
        if(bytes == 0u)
            return;

        const u64 usedBytes = m_usedBytes.load(MemoryOrder::relaxed) + bytes;
        m_usedBytes.store(usedBytes, MemoryOrder::relaxed);
        if(usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
            m_peakUsedBytes.store(usedBytes, MemoryOrder::relaxed);
        m_allocationCount.store(m_allocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
    }

    FOUNDATION_INLINE void recordReallocationSingleThread(const u64 oldBytes, const u64 newBytes)noexcept{
        if(oldBytes == 0u && newBytes == 0u)
            return;
        if(oldBytes == 0u){
            recordAllocationSingleThread(newBytes);
            return;
        }
        if(newBytes == 0u){
            recordDeallocationSingleThread(oldBytes);
            return;
        }

        u64 usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
        if(newBytes >= oldBytes)
            usedBytes += newBytes - oldBytes;
        else
            usedBytes -= oldBytes - newBytes;
        m_usedBytes.store(usedBytes, MemoryOrder::relaxed);
        if(usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
            m_peakUsedBytes.store(usedBytes, MemoryOrder::relaxed);
        m_reallocationCount.store(m_reallocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
    }

    FOUNDATION_INLINE void recordDeallocationSingleThread(const u64 bytes)noexcept{
        if(bytes == 0u)
            return;

        m_usedBytes.store(m_usedBytes.load(MemoryOrder::relaxed) - bytes, MemoryOrder::relaxed);
        m_deallocationCount.store(m_deallocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
    }

    void recordAllocation(u64 bytes)noexcept;
    void recordReallocation(u64 oldBytes, u64 newBytes)noexcept;
    void recordDeallocation(u64 bytes)noexcept;
    [[nodiscard]] ArenaMemoryStats snapshot()const noexcept;
    [[nodiscard]] FOUNDATION_INLINE Name ownerName()const noexcept;


private:
    void attachToOwnerSlow()noexcept;


private:
    ArenaMemoryOwnerRecord* m_owner = nullptr;
    ArenaMemoryTracker* m_previous = nullptr;
    ArenaMemoryTracker* m_next = nullptr;
    Atomic<u64> m_reservedBytes{ 0u };
    Atomic<u64> m_usedBytes{ 0u };
    mutable Atomic<u64> m_peakUsedBytes{ 0u };
    Atomic<u64> m_allocationCount{ 0u };
    Atomic<u64> m_reallocationCount{ 0u };
    Atomic<u64> m_deallocationCount{ 0u };
    ArenaMemoryReservation::Enum m_reservation = ArenaMemoryReservation::Separate;
    b8 m_attached = false;
    Atomic<u32> m_ownerThreadSlot{ 0u };
};
static_assert(sizeof(ArenaMemoryTracker) == 80u);


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

[[nodiscard]] const ArenaMemoryOwnerRecord* FirstArenaMemoryOwnerRecord()noexcept;
[[nodiscard]] const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerIdentity(const ArenaMemoryOwnerRecord& record, ArenaMemoryOwnerIdentity& outIdentity)noexcept;
[[nodiscard]] Name ReadArenaMemoryOwnerName(const ArenaMemoryOwnerRecord& record)noexcept;

FOUNDATION_INLINE Name ArenaMemoryTracker::ownerName()const noexcept{
    return m_owner ? ReadArenaMemoryOwnerName(*m_owner) : s_NameNone;
}
[[nodiscard]] const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerRecord(const ArenaMemoryOwnerRecord& record, ArenaMemoryOwnerSnapshot& outSnapshot)noexcept;

void RecordHeapMemoryAllocation(u64 bytes)noexcept;
void RecordHeapMemoryReallocation(u64 oldBytes, u64 newBytes)noexcept;
void RecordHeapMemoryDeallocation(u64 bytes)noexcept;
void RecordHeapAndArenaAllocation(u64 bytes, ArenaMemoryTracker* tracker)noexcept;
void RecordHeapAndArenaReallocation(u64 oldBytes, u64 newBytes, ArenaMemoryTracker* tracker)noexcept;
void RecordHeapAndArenaDeallocation(u64 bytes, ArenaMemoryTracker* tracker)noexcept;
[[nodiscard]] ArenaMemoryStats HeapBackingMemoryStats()noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArenaMemoryOwnerCursor{
public:
    [[nodiscard]] b8 next(ArenaMemoryOwnerSnapshot& snapshot)noexcept{
        if(!m_record)
            return false;
        m_record = ReadArenaMemoryOwnerRecord(*m_record, snapshot);
        return true;
    }

private:
    const ArenaMemoryOwnerRecord* m_record = FirstArenaMemoryOwnerRecord();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

