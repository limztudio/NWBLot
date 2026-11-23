// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "arena_memory.h"

#include "allocation_size.h"
#include "sync.h"

#include <cstdlib>
#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ArenaMemoryOwnerRecord{
    Name ownerName = s_NameNone;
    ArenaMemoryStats retiredStats = {};
    ArenaMemoryTracker* liveTrackers = nullptr;
    const ArenaMemoryOwnerRecord* next = nullptr;
    ArenaMemoryOwnerRecord* bucketNext = nullptr;
    mutable Futex mutex;
    ArenaMemorySource::Enum source = ArenaMemorySource::Arena;


public:
    constexpr ArenaMemoryOwnerRecord()noexcept = default;
    constexpr ArenaMemoryOwnerRecord(const Name& owner, const ArenaMemorySource::Enum src)noexcept
        : ownerName(owner)
        , source(src)
    {}

    void attach(ArenaMemoryTracker& tracker)noexcept;
    void retire(ArenaMemoryTracker& tracker)noexcept;
    [[nodiscard]] ArenaMemoryStats snapshot()const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_arena_memory{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_OwnerBucketCount = 256u;
inline constexpr usize s_HeapShardPadBytes = 80u;
inline constexpr usize s_HeapShardMinBytes = 128u;
inline constexpr u32 s_TrackerShardCount = 32u;
inline constexpr usize s_TrackerShardPadBytes = 88u;
inline constexpr usize s_TrackerShardMinBytes = 128u;
static constinit ArenaMemoryOwnerRecord s_HeapBacking{ Name("global/arena_memory/heap_backing"), ArenaMemorySource::HeapBacking };
static constinit Atomic<const ArenaMemoryOwnerRecord*> s_OwnerHead{ &s_HeapBacking };
static MallocMutex s_OwnerMutex;
static Array<Atomic<ArenaMemoryOwnerRecord*>, s_OwnerBucketCount> s_OwnerBuckets = {};

struct HeapMemoryShard{
    Atomic<u64> allocatedBytes{ 0u };
    Atomic<u64> releasedBytes{ 0u };
    Atomic<u64> allocationCount{ 0u };
    Atomic<u64> reallocationCount{ 0u };
    Atomic<u64> deallocationCount{ 0u };
    const HeapMemoryShard* next = nullptr;

    Array<u8, s_HeapShardPadBytes> padding = {};
};
static_assert(sizeof(HeapMemoryShard) >= s_HeapShardMinBytes);

struct TrackerShard{
    Atomic<u64> allocatedBytes{ 0u };
    Atomic<u64> releasedBytes{ 0u };
    Atomic<u64> allocationCount{ 0u };
    Atomic<u64> reallocationCount{ 0u };
    Atomic<u64> deallocationCount{ 0u };

    Array<u8, s_TrackerShardPadBytes> padding = {};
};
static_assert(sizeof(TrackerShard) >= s_TrackerShardMinBytes);

struct TrackerShardTable{
    Array<TrackerShard, s_TrackerShardCount> slots = {};
};

static constinit HeapMemoryShard s_HeapFallback;
static constinit Atomic<const HeapMemoryShard*> s_HeapHead{ &s_HeapFallback };
static constinit Atomic<u64> s_HeapSampledPeak{ 0u };
static constinit thread_local HeapMemoryShard* s_ThreadHeapShard = nullptr;
static constinit Atomic<u32> s_NextTrackerThreadTag{ 0u };
static constinit thread_local u32 s_ThreadTrackerTag = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 ThreadTrackerTag()noexcept{
    u32 tag = s_ThreadTrackerTag;
    if(tag == 0u) [[unlikely]]{
        tag = s_NextTrackerThreadTag.fetch_add(1u, MemoryOrder::relaxed) + 1u;
        if(tag == 0u) [[unlikely]]
            tag = 1u;
        s_ThreadTrackerTag = tag;
    }
    return tag;
}

[[nodiscard]] inline u32 ThreadTrackerSlot(const u32 tag)noexcept{
    return (tag - 1u) & (s_TrackerShardCount - 1u);
}

struct FollowsUsageShardResult{
    TrackerShard* shard = nullptr;
    b8 singleWriter = false;
};

[[nodiscard]] inline FollowsUsageShardResult ResolveFollowsUsageShard(Atomic<u64>& reservedBits, Atomic<u32>& ownerThreadTag)noexcept{
    u64 tableBits = reservedBits.load(MemoryOrder::relaxed);
    if(tableBits == 0u){
        const u32 myTag = ThreadTrackerTag();
        u32 ownerTag = ownerThreadTag.load(MemoryOrder::relaxed);
        if(ownerTag == 0u){
            if(ownerThreadTag.compare_exchange_strong(ownerTag, myTag, MemoryOrder::relaxed, MemoryOrder::relaxed))
                return FollowsUsageShardResult{ .shard = nullptr, .singleWriter = true, };
        }
        if(ownerTag == myTag)
            return FollowsUsageShardResult{ .shard = nullptr, .singleWriter = true, };

        void* const storage = std::malloc(sizeof(TrackerShardTable));
        if(!storage)
            return FollowsUsageShardResult{ .shard = nullptr, .singleWriter = false, };
        auto* const created = new(storage) TrackerShardTable;
        u64 expected = 0u;
        if(!reservedBits.compare_exchange_strong(expected, static_cast<u64>(reinterpret_cast<usize>(created)), MemoryOrder::release, MemoryOrder::acquire)){
            std::free(created);
            tableBits = expected;
        }
        else{
            tableBits = static_cast<u64>(reinterpret_cast<usize>(created));
        }
    }
    else{
        tableBits = reservedBits.load(MemoryOrder::acquire);
    }
    if(tableBits == 0u)
        return FollowsUsageShardResult{ .shard = nullptr, .singleWriter = false, };

    auto* const table = reinterpret_cast<TrackerShardTable*>(static_cast<usize>(tableBits));
    return FollowsUsageShardResult{ .shard = &table->slots[ThreadTrackerSlot(ThreadTrackerTag())], .singleWriter = false, };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AccumulateStats(ArenaMemoryStats& total, const ArenaMemoryStats& contribution)noexcept{
    total.reservedBytes += contribution.reservedBytes;
    total.usedBytes += contribution.usedBytes;
    if(contribution.peakUsedBytes > total.peakUsedBytes)
        total.peakUsedBytes = contribution.peakUsedBytes;
    total.allocationCount += contribution.allocationCount;
    total.reallocationCount += contribution.reallocationCount;
    total.deallocationCount += contribution.deallocationCount;
}

[[nodiscard]] ArenaMemoryOwnerRecord& FindOrCreateOwner(const Name& ownerName){
    const usize bucketIndex = Hasher<Name>{}(ownerName) % s_OwnerBucketCount;
    for(ArenaMemoryOwnerRecord* record = s_OwnerBuckets[bucketIndex].load(MemoryOrder::acquire); record; record = record->bucketNext){
        if(record->ownerName == ownerName)
            return *record;
    }

    ScopedLock lock(s_OwnerMutex);
    ArenaMemoryOwnerRecord* const bucketHead = s_OwnerBuckets[bucketIndex].load(MemoryOrder::relaxed);
    for(ArenaMemoryOwnerRecord* record = bucketHead; record; record = record->bucketNext){
        if(record->ownerName == ownerName)
            return *record;
    }

    void* const storage = std::malloc(sizeof(ArenaMemoryOwnerRecord));
    if(!storage)
        throw AllocationException{};
    auto& record = *new(storage) ArenaMemoryOwnerRecord;
    record.ownerName = ownerName;
    record.next = s_OwnerHead.load(MemoryOrder::relaxed);
    record.bucketNext = bucketHead;
    s_OwnerBuckets[bucketIndex].store(&record, MemoryOrder::release);
    s_OwnerHead.store(&record, MemoryOrder::release);
    return record;
}

[[nodiscard]] HeapMemoryShard& ThreadHeapShard()noexcept{
    HeapMemoryShard* const threadShard = s_ThreadHeapShard;
    if(threadShard)
        return *threadShard;

    void* const storage = std::malloc(sizeof(HeapMemoryShard));
    if(!storage){
        s_ThreadHeapShard = &s_HeapFallback;
        return s_HeapFallback;
    }
    auto& shard = *new(storage) HeapMemoryShard;
    const HeapMemoryShard* head = s_HeapHead.load(MemoryOrder::relaxed);
    do{
        shard.next = head;
    }while(!s_HeapHead.compare_exchange_weak(head, &shard, MemoryOrder::release, MemoryOrder::relaxed));
    s_ThreadHeapShard = &shard;
    return shard;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ArenaMemoryOwnerRecord::attach(ArenaMemoryTracker& tracker)noexcept{
    tracker.m_owner = this;
    tracker.m_next = liveTrackers;
    if(liveTrackers)
        liveTrackers->m_previous = &tracker;
    liveTrackers = &tracker;
}

void ArenaMemoryOwnerRecord::retire(ArenaMemoryTracker& tracker)noexcept{
    __hidden_arena_memory::AccumulateStats(retiredStats, tracker.snapshot());
    if(tracker.m_previous)
        tracker.m_previous->m_next = tracker.m_next;
    else
        liveTrackers = tracker.m_next;
    if(tracker.m_next)
        tracker.m_next->m_previous = tracker.m_previous;
    tracker.m_owner = nullptr;
    tracker.m_previous = nullptr;
    tracker.m_next = nullptr;
}

ArenaMemoryStats ArenaMemoryOwnerRecord::snapshot()const noexcept{
    ArenaMemoryStats stats = retiredStats;
    for(const ArenaMemoryTracker* tracker = liveTrackers; tracker; tracker = tracker->m_next)
        __hidden_arena_memory::AccumulateStats(stats, tracker->snapshot());
    return stats;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ArenaMemoryTracker::ArenaMemoryTracker(const Name& ownerName, const ArenaMemoryReservation::Enum reservation, const b8 deferAttach)
    : m_owner(&__hidden_arena_memory::FindOrCreateOwner(ownerName))
    , m_reservation(reservation)
{
    if(!deferAttach){
        NothrowScopedLock lock(m_owner->mutex);
        m_owner->attach(*this);
        m_attached = true;
    }
}

ArenaMemoryTracker::~ArenaMemoryTracker()noexcept{
    if(m_owner && m_attached){
        auto& owner = *m_owner;
        NothrowScopedLock lock(owner.mutex);
        owner.retire(*this);
        m_attached = false;
    }
    if(m_reservation == ArenaMemoryReservation::FollowsUsage){
        void* const table = reinterpret_cast<void*>(static_cast<usize>(m_reservedBytes.exchange(0u, MemoryOrder::acq_rel)));
        if(table)
            std::free(table);
    }
}

void ArenaMemoryTracker::attachToOwnerSlow()noexcept{
    if(!m_owner)
        return;

    NothrowScopedLock lock(m_owner->mutex);
    if(!m_attached){
        m_owner->attach(*this);
        m_attached = true;
    }
}

void ArenaMemoryTracker::releaseRetainedMemory()noexcept{
    if(m_reservation == ArenaMemoryReservation::FollowsUsage){
        const u64 tableBits = m_reservedBytes.load(MemoryOrder::acquire);
        if(tableBits != 0u){
            auto* const table = reinterpret_cast<__hidden_arena_memory::TrackerShardTable*>(static_cast<usize>(tableBits));
            for(__hidden_arena_memory::TrackerShard& shard : table->slots){
                shard.releasedBytes.store(shard.allocatedBytes.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
                shard.deallocationCount.store(shard.allocationCount.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
            }
        }
        m_usedBytes.store(0u, MemoryOrder::relaxed);
        m_deallocationCount.store(m_allocationCount.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
        return;
    }
    m_reservedBytes.store(0u, MemoryOrder::relaxed);
    m_usedBytes.store(0u, MemoryOrder::relaxed);
    m_deallocationCount.store(m_allocationCount.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
}

void ArenaMemoryTracker::reset(const u64 reservedBytes)noexcept{
    if(m_owner && !m_attached && reservedBytes != 0u)
        attachToOwnerSlow();

    const auto applyReset = [this, reservedBytes]()noexcept{
        releaseRetainedMemory();
        if(m_owner && m_attached)
            __hidden_arena_memory::AccumulateStats(m_owner->retiredStats, snapshot());
        if(m_reservation == ArenaMemoryReservation::FollowsUsage){
            const u64 tableBits = m_reservedBytes.load(MemoryOrder::acquire);
            if(tableBits != 0u){
                auto* const table = reinterpret_cast<__hidden_arena_memory::TrackerShardTable*>(static_cast<usize>(tableBits));
                for(__hidden_arena_memory::TrackerShard& shard : table->slots){
                    shard.allocatedBytes.store(0u, MemoryOrder::relaxed);
                    shard.releasedBytes.store(0u, MemoryOrder::relaxed);
                    shard.allocationCount.store(0u, MemoryOrder::relaxed);
                    shard.reallocationCount.store(0u, MemoryOrder::relaxed);
                    shard.deallocationCount.store(0u, MemoryOrder::relaxed);
                }
            }
        }
        else{
            m_reservedBytes.store(reservedBytes, MemoryOrder::relaxed);
        }
        m_usedBytes.store(0u, MemoryOrder::relaxed);
        m_peakUsedBytes.store(0u, MemoryOrder::relaxed);
        m_allocationCount.store(0u, MemoryOrder::relaxed);
        m_reallocationCount.store(0u, MemoryOrder::relaxed);
        m_deallocationCount.store(0u, MemoryOrder::relaxed);
    };
    if(m_owner && m_attached){
        NothrowScopedLock lock(m_owner->mutex);
        applyReset();
        return;
    }
    applyReset();
}

void ArenaMemoryTracker::recordAllocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    if(m_reservation == ArenaMemoryReservation::FollowsUsage){
        const __hidden_arena_memory::FollowsUsageShardResult resolved = __hidden_arena_memory::ResolveFollowsUsageShard(m_reservedBytes, m_ownerThreadSlot);
        if(resolved.shard){
            resolved.shard->allocatedBytes.fetch_add(bytes, MemoryOrder::relaxed);
            resolved.shard->allocationCount.fetch_add(1u, MemoryOrder::relaxed);
            return;
        }
        if(resolved.singleWriter){
            const u64 usedBytes = m_usedBytes.load(MemoryOrder::relaxed) + bytes;
            m_usedBytes.store(usedBytes, MemoryOrder::relaxed);
            if(usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
                UpdateAtomicMaxRelaxed(m_peakUsedBytes, usedBytes);
            m_allocationCount.store(m_allocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
            return;
        }
    }

    const u64 usedBytes = m_usedBytes.fetch_add(bytes, MemoryOrder::relaxed) + bytes;
    if(usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
        UpdateAtomicMaxRelaxed(m_peakUsedBytes, usedBytes);
    m_allocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

void ArenaMemoryTracker::recordReallocation(const u64 oldBytes, const u64 newBytes)noexcept{
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

    if(m_reservation == ArenaMemoryReservation::FollowsUsage){
        const __hidden_arena_memory::FollowsUsageShardResult resolved = __hidden_arena_memory::ResolveFollowsUsageShard(m_reservedBytes, m_ownerThreadSlot);
        if(resolved.shard){
            if(newBytes > oldBytes)
                resolved.shard->allocatedBytes.fetch_add(newBytes - oldBytes, MemoryOrder::relaxed);
            else if(oldBytes > newBytes)
                resolved.shard->releasedBytes.fetch_add(oldBytes - newBytes, MemoryOrder::relaxed);
            resolved.shard->reallocationCount.fetch_add(1u, MemoryOrder::relaxed);
            return;
        }
        if(resolved.singleWriter){
            u64 usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
            if(newBytes >= oldBytes)
                usedBytes += newBytes - oldBytes;
            else
                usedBytes -= oldBytes - newBytes;
            m_usedBytes.store(usedBytes, MemoryOrder::relaxed);
            if(usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
                UpdateAtomicMaxRelaxed(m_peakUsedBytes, usedBytes);
            m_reallocationCount.store(m_reallocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
            return;
        }
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
    if(usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
        UpdateAtomicMaxRelaxed(m_peakUsedBytes, usedBytes);
    m_reallocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

void ArenaMemoryTracker::recordDeallocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    if(m_reservation == ArenaMemoryReservation::FollowsUsage){
        const __hidden_arena_memory::FollowsUsageShardResult resolved = __hidden_arena_memory::ResolveFollowsUsageShard(m_reservedBytes, m_ownerThreadSlot);
        if(resolved.shard){
            resolved.shard->releasedBytes.fetch_add(bytes, MemoryOrder::relaxed);
            resolved.shard->deallocationCount.fetch_add(1u, MemoryOrder::relaxed);
            return;
        }
        if(resolved.singleWriter){
            m_usedBytes.store(m_usedBytes.load(MemoryOrder::relaxed) - bytes, MemoryOrder::relaxed);
            m_deallocationCount.store(m_deallocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
            return;
        }
    }

    m_usedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
    m_deallocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

ArenaMemoryStats ArenaMemoryTracker::snapshot()const noexcept{
    ArenaMemoryStats stats;
    stats.allocationCount = m_allocationCount.load(MemoryOrder::relaxed);
    stats.reallocationCount = m_reallocationCount.load(MemoryOrder::relaxed);
    stats.deallocationCount = m_deallocationCount.load(MemoryOrder::relaxed);
    if(m_reservation == ArenaMemoryReservation::FollowsUsage){
        const u64 tableBits = m_reservedBytes.load(MemoryOrder::acquire);
        if(tableBits != 0u){
            const auto* const table = reinterpret_cast<const __hidden_arena_memory::TrackerShardTable*>(static_cast<usize>(tableBits));
            u64 shardAlloc = 0u;
            u64 shardRelease = 0u;
            for(const __hidden_arena_memory::TrackerShard& shard : table->slots){
                shardAlloc += shard.allocatedBytes.load(MemoryOrder::relaxed);
                shardRelease += shard.releasedBytes.load(MemoryOrder::relaxed);
                stats.allocationCount += shard.allocationCount.load(MemoryOrder::relaxed);
                stats.reallocationCount += shard.reallocationCount.load(MemoryOrder::relaxed);
                stats.deallocationCount += shard.deallocationCount.load(MemoryOrder::relaxed);
            }
            const i64 netBytes = static_cast<i64>(m_usedBytes.load(MemoryOrder::relaxed)) + static_cast<i64>(shardAlloc) - static_cast<i64>(shardRelease);
            stats.usedBytes = netBytes > 0 ? static_cast<u64>(netBytes) : 0u;
            stats.reservedBytes = stats.usedBytes;
            if(stats.usedBytes > m_peakUsedBytes.load(MemoryOrder::relaxed))
                UpdateAtomicMaxRelaxed(m_peakUsedBytes, stats.usedBytes);
            stats.peakUsedBytes = m_peakUsedBytes.load(MemoryOrder::relaxed);
            return stats;
        }
        stats.usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
        stats.reservedBytes = stats.usedBytes;
        stats.peakUsedBytes = m_peakUsedBytes.load(MemoryOrder::relaxed);
        return stats;
    }
    stats.usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
    stats.reservedBytes = m_reservedBytes.load(MemoryOrder::relaxed);
    stats.peakUsedBytes = m_peakUsedBytes.load(MemoryOrder::relaxed);
    return stats;
}

void ArenaMemoryTracker::recordPeakUsedBytes(const u64 usedBytes)noexcept{
    UpdateAtomicMaxRelaxed(m_peakUsedBytes, usedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const ArenaMemoryOwnerRecord* FirstArenaMemoryOwnerRecord()noexcept{
    return __hidden_arena_memory::s_OwnerHead.load(MemoryOrder::acquire);
}

const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerIdentity(const ArenaMemoryOwnerRecord& record, ArenaMemoryOwnerIdentity& outIdentity)noexcept{
    outIdentity.ownerName = record.ownerName;
    outIdentity.source = record.source;
    return record.next;
}

Name ReadArenaMemoryOwnerName(const ArenaMemoryOwnerRecord& record)noexcept{
    return record.ownerName;
}

const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerRecord(const ArenaMemoryOwnerRecord& record, ArenaMemoryOwnerSnapshot& outSnapshot)noexcept{
    outSnapshot.ownerName = record.ownerName;
    outSnapshot.source = record.source;
    if(record.source == ArenaMemorySource::HeapBacking)
        outSnapshot.stats = HeapBackingMemoryStats();
    else{
        NothrowScopedLock lock(record.mutex);
        outSnapshot.stats = record.snapshot();
    }
    return record.next;
}

void RecordHeapMemoryAllocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    auto& shard = __hidden_arena_memory::ThreadHeapShard();
    if(&shard != &__hidden_arena_memory::s_HeapFallback){
        shard.allocatedBytes.store(shard.allocatedBytes.load(MemoryOrder::relaxed) + bytes, MemoryOrder::relaxed);
        shard.allocationCount.store(shard.allocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
        return;
    }
    shard.allocatedBytes.fetch_add(bytes, MemoryOrder::relaxed);
    shard.allocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

void RecordHeapMemoryReallocation(const u64 oldBytes, const u64 newBytes)noexcept{
    if(oldBytes == 0u && newBytes == 0u)
        return;
    if(oldBytes == 0u){
        RecordHeapMemoryAllocation(newBytes);
        return;
    }
    if(newBytes == 0u){
        RecordHeapMemoryDeallocation(oldBytes);
        return;
    }

    auto& shard = __hidden_arena_memory::ThreadHeapShard();
    if(&shard != &__hidden_arena_memory::s_HeapFallback){
        if(newBytes > oldBytes)
            shard.allocatedBytes.store(shard.allocatedBytes.load(MemoryOrder::relaxed) + (newBytes - oldBytes), MemoryOrder::relaxed);
        else if(oldBytes > newBytes)
            shard.releasedBytes.store(shard.releasedBytes.load(MemoryOrder::relaxed) + (oldBytes - newBytes), MemoryOrder::relaxed);
        shard.reallocationCount.store(shard.reallocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
        return;
    }
    if(newBytes > oldBytes)
        shard.allocatedBytes.fetch_add(newBytes - oldBytes, MemoryOrder::relaxed);
    else if(oldBytes > newBytes)
        shard.releasedBytes.fetch_add(oldBytes - newBytes, MemoryOrder::relaxed);
    shard.reallocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

void RecordHeapMemoryDeallocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    auto& shard = __hidden_arena_memory::ThreadHeapShard();
    if(&shard != &__hidden_arena_memory::s_HeapFallback){
        shard.releasedBytes.store(shard.releasedBytes.load(MemoryOrder::relaxed) + bytes, MemoryOrder::relaxed);
        shard.deallocationCount.store(shard.deallocationCount.load(MemoryOrder::relaxed) + 1u, MemoryOrder::relaxed);
        return;
    }
    shard.releasedBytes.fetch_add(bytes, MemoryOrder::relaxed);
    shard.deallocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

void RecordHeapAndArenaAllocation(const u64 bytes, ArenaMemoryTracker* const tracker)noexcept{
    RecordHeapMemoryAllocation(bytes);
    if(tracker)
        tracker->recordAllocation(bytes);
}

void RecordHeapAndArenaReallocation(const u64 oldBytes, const u64 newBytes, ArenaMemoryTracker* const tracker)noexcept{
    RecordHeapMemoryReallocation(oldBytes, newBytes);
    if(tracker)
        tracker->recordReallocation(oldBytes, newBytes);
}

void RecordHeapAndArenaDeallocation(const u64 bytes, ArenaMemoryTracker* const tracker)noexcept{
    RecordHeapMemoryDeallocation(bytes);
    if(tracker)
        tracker->recordDeallocation(bytes);
}

ArenaMemoryStats HeapBackingMemoryStats()noexcept{
    ArenaMemoryStats stats;
    u64 allocatedBytes = 0u;
    u64 releasedBytes = 0u;
    const auto* shard = __hidden_arena_memory::s_HeapHead.load(MemoryOrder::acquire);
    for(; shard; shard = shard->next){
        allocatedBytes += shard->allocatedBytes.load(MemoryOrder::relaxed);
        releasedBytes += shard->releasedBytes.load(MemoryOrder::relaxed);
        stats.allocationCount += shard->allocationCount.load(MemoryOrder::relaxed);
        stats.reallocationCount += shard->reallocationCount.load(MemoryOrder::relaxed);
        stats.deallocationCount += shard->deallocationCount.load(MemoryOrder::relaxed);
    }
    stats.usedBytes = allocatedBytes >= releasedBytes ? allocatedBytes - releasedBytes : 0u;
    stats.reservedBytes = stats.usedBytes;
    u64 peak = __hidden_arena_memory::s_HeapSampledPeak.load(MemoryOrder::relaxed);
    while(stats.usedBytes > peak){
        if(__hidden_arena_memory::s_HeapSampledPeak.compare_exchange_weak(peak, stats.usedBytes, MemoryOrder::relaxed)){
            peak = stats.usedBytes;
            break;
        }
    }
    stats.peakUsedBytes = peak;
    return stats;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

