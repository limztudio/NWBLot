// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "arena_memory.h"

#include "sync.h"

#include <cstdlib>
#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ArenaMemoryOwnerRecord{
    Name ownerName = NAME_NONE;
    ArenaMemorySource::Enum source = ArenaMemorySource::Arena;
    ArenaMemoryStats retiredStats = {};
    ArenaMemoryTracker* liveTrackers = nullptr;
    const ArenaMemoryOwnerRecord* next = nullptr;
    ArenaMemoryOwnerRecord* bucketNext = nullptr;

    void attach(ArenaMemoryTracker& tracker)noexcept;
    void retire(ArenaMemoryTracker& tracker)noexcept;
    [[nodiscard]] ArenaMemoryStats snapshot()const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_arena_memory{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_OwnerBucketCount = 256u;
constinit ArenaMemoryOwnerRecord s_HeapBacking{
    .ownerName = Name("core/alloc/heap_backing"),
    .source = ArenaMemorySource::HeapBacking,
};
constinit Atomic<const ArenaMemoryOwnerRecord*> s_OwnerHead{ &s_HeapBacking };
MallocMutex s_OwnerMutex;
ArenaMemoryOwnerRecord* s_OwnerBuckets[s_OwnerBucketCount] = {};

struct HeapMemoryShard{
    Atomic<u64> allocatedBytes{ 0u };
    Atomic<u64> releasedBytes{ 0u };
    Atomic<u64> allocationCount{ 0u };
    Atomic<u64> reallocationCount{ 0u };
    Atomic<u64> deallocationCount{ 0u };
    const HeapMemoryShard* next = nullptr;

    // Separate different threads' writable counters even when CRT allocations have only ordinary alignment.
    u8 padding[80u] = {};
};
static_assert(sizeof(HeapMemoryShard) >= 128u);

constinit HeapMemoryShard s_HeapFallback;
constinit Atomic<const HeapMemoryShard*> s_HeapHead{ &s_HeapFallback };
constinit Atomic<u64> s_HeapSampledPeak{ 0u };
constinit thread_local HeapMemoryShard* s_ThreadHeapShard = nullptr;

void AccumulateStats(ArenaMemoryStats& total, const ArenaMemoryStats& contribution)noexcept{
    total.reservedBytes += contribution.reservedBytes;
    total.usedBytes += contribution.usedBytes;
    if(contribution.peakUsedBytes > total.peakUsedBytes)
        total.peakUsedBytes = contribution.peakUsedBytes;
    total.allocationCount += contribution.allocationCount;
    total.reallocationCount += contribution.reallocationCount;
    total.deallocationCount += contribution.deallocationCount;
}

// Called with the lifecycle mutex held. Records use the uninstrumented CRT and remain available for late
// static destruction and transient-arena history. Registration cannot recurse through global new.
[[nodiscard]] ArenaMemoryOwnerRecord& FindOrCreateOwner(const Name& ownerName){
    const usize bucketIndex = Hasher<Name>{}(ownerName) % s_OwnerBucketCount;
    for(ArenaMemoryOwnerRecord* record = s_OwnerBuckets[bucketIndex]; record; record = record->bucketNext){
        if(record->ownerName == ownerName)
            return *record;
    }

    void* const storage = std::malloc(sizeof(ArenaMemoryOwnerRecord));
    if(!storage)
        throw std::bad_alloc{};
    auto* const record = new(storage) ArenaMemoryOwnerRecord;
    record->ownerName = ownerName;
    record->next = s_OwnerHead.load(MemoryOrder::relaxed);
    record->bucketNext = s_OwnerBuckets[bucketIndex];
    s_OwnerBuckets[bucketIndex] = record;
    s_OwnerHead.store(record, MemoryOrder::release);
    return *record;
}

[[nodiscard]] HeapMemoryShard& ThreadHeapShard()noexcept{
    HeapMemoryShard* const threadShard = s_ThreadHeapShard;
    if(threadShard)
        return *threadShard;

    // The shard has no TLS destructor and outlives its writer, preserving cross-thread frees and exited-thread
    // counters. Allocation failure uses the permanent atomic fallback without failing an otherwise valid free.
    void* const storage = std::malloc(sizeof(HeapMemoryShard));
    if(!storage){
        s_ThreadHeapShard = &s_HeapFallback;
        return s_HeapFallback;
    }
    auto* const shard = new(storage) HeapMemoryShard;
    const HeapMemoryShard* head = s_HeapHead.load(MemoryOrder::relaxed);
    do{
        shard->next = head;
    }while(!s_HeapHead.compare_exchange_weak(head, shard, MemoryOrder::release, MemoryOrder::relaxed));
    s_ThreadHeapShard = shard;
    return *shard;
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
    // Publish only after every counter and link member has been initialized. The same lock protects readers
    // until retirement has copied the final counters and detached this tracker before member destruction.
    ScopedLock lock(__hidden_arena_memory::s_OwnerMutex);
    __hidden_arena_memory::FindOrCreateOwner(ownerName).attach(*this);
}

ArenaMemoryTracker::~ArenaMemoryTracker(){
    if(!m_owner)
        return;

    ScopedLock lock(__hidden_arena_memory::s_OwnerMutex);
    m_owner->retire(*this);
}

void ArenaMemoryTracker::releaseRetainedMemory()noexcept{
    m_reservedBytes.store(0u, MemoryOrder::relaxed);
    m_usedBytes.store(0u, MemoryOrder::relaxed);
    m_deallocationCount.store(m_allocationCount.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
}

void ArenaMemoryTracker::reset(const u64 reservedBytes){
    ScopedLock lock(__hidden_arena_memory::s_OwnerMutex);
    releaseRetainedMemory();
    if(m_owner)
        __hidden_arena_memory::AccumulateStats(m_owner->retiredStats, snapshot());
    m_reservedBytes.store(reservedBytes, MemoryOrder::relaxed);
    m_usedBytes.store(0u, MemoryOrder::relaxed);
    m_peakUsedBytes.store(0u, MemoryOrder::relaxed);
    m_allocationCount.store(0u, MemoryOrder::relaxed);
    m_reallocationCount.store(0u, MemoryOrder::relaxed);
    m_deallocationCount.store(0u, MemoryOrder::relaxed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const ArenaMemoryOwnerRecord* FirstArenaMemoryOwnerRecord()noexcept{
    return __hidden_arena_memory::s_OwnerHead.load(MemoryOrder::acquire);
}

const ArenaMemoryOwnerRecord* ReadArenaMemoryOwnerRecord(
    const ArenaMemoryOwnerRecord& record,
    ArenaMemoryOwnerSnapshot& outSnapshot
)noexcept{
    outSnapshot.ownerName = record.ownerName;
    outSnapshot.source = record.source;
    if(record.source == ArenaMemorySource::HeapBacking)
        outSnapshot.stats = HeapBackingMemoryStats();
    else{
        ScopedLock lock(__hidden_arena_memory::s_OwnerMutex);
        outSnapshot.stats = record.snapshot();
    }
    return record.next;
}

void RecordHeapMemoryAllocation(const u64 bytes)noexcept{
    if(bytes == 0u)
        return;

    auto& shard = __hidden_arena_memory::ThreadHeapShard();
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
    // Writers continue during capture; a cross-thread free can be observed before its allocation shard.
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

