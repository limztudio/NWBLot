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

static constinit HeapMemoryShard s_HeapFallback;
static constinit Atomic<const HeapMemoryShard*> s_HeapHead{ &s_HeapFallback };
static constinit Atomic<u64> s_HeapSampledPeak{ 0u };
static constinit thread_local HeapMemoryShard* s_ThreadHeapShard = nullptr;


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


ArenaMemoryTracker::ArenaMemoryTracker(const Name& ownerName, const ArenaMemoryReservation::Enum reservation)
    : m_reservation(reservation)
{
    auto& owner = __hidden_arena_memory::FindOrCreateOwner(ownerName);
    NothrowScopedLock lock(owner.mutex);
    owner.attach(*this);
}

ArenaMemoryTracker::~ArenaMemoryTracker()noexcept{
    if(!m_owner)
        return;

    auto& owner = *m_owner;
    NothrowScopedLock lock(owner.mutex);
    owner.retire(*this);
}

void ArenaMemoryTracker::releaseRetainedMemory()noexcept{
    m_reservedBytes.store(0u, MemoryOrder::relaxed);
    m_usedBytes.store(0u, MemoryOrder::relaxed);
    m_deallocationCount.store(m_allocationCount.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
}

void ArenaMemoryTracker::reset(const u64 reservedBytes)noexcept{
    const auto applyReset = [this, reservedBytes]()noexcept{
        releaseRetainedMemory();
        if(m_owner)
            __hidden_arena_memory::AccumulateStats(m_owner->retiredStats, snapshot());
        m_reservedBytes.store(reservedBytes, MemoryOrder::relaxed);
        m_usedBytes.store(0u, MemoryOrder::relaxed);
        m_peakUsedBytes.store(0u, MemoryOrder::relaxed);
        m_allocationCount.store(0u, MemoryOrder::relaxed);
        m_reallocationCount.store(0u, MemoryOrder::relaxed);
        m_deallocationCount.store(0u, MemoryOrder::relaxed);
    };
    if(m_owner){
        NothrowScopedLock lock(m_owner->mutex);
        applyReset();
        return;
    }
    applyReset();
}

void ArenaMemoryTracker::recordAllocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    const u64 usedBytes = m_usedBytes.fetch_add(bytes, MemoryOrder::relaxed) + bytes;
    recordPeakUsedBytes(usedBytes);
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

void ArenaMemoryTracker::recordDeallocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    m_usedBytes.fetch_sub(bytes, MemoryOrder::relaxed);
    m_deallocationCount.fetch_add(1u, MemoryOrder::relaxed);
}

ArenaMemoryStats ArenaMemoryTracker::snapshot()const noexcept{
    ArenaMemoryStats stats;
    stats.usedBytes = m_usedBytes.load(MemoryOrder::relaxed);
    stats.reservedBytes = m_reservation == ArenaMemoryReservation::FollowsUsage ? stats.usedBytes : m_reservedBytes.load(MemoryOrder::relaxed);
    stats.peakUsedBytes = m_peakUsedBytes.load(MemoryOrder::relaxed);
    stats.allocationCount = m_allocationCount.load(MemoryOrder::relaxed);
    stats.reallocationCount = m_reallocationCount.load(MemoryOrder::relaxed);
    stats.deallocationCount = m_deallocationCount.load(MemoryOrder::relaxed);
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

