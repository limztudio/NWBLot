// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "statistics.h"

#include <core/alloc/global.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>

#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RefractionStatisticsReservationKey{
    u64 sequence = 0u;
    u64 generation = 0u;
    u32 slot = 0u;


public:
    [[nodiscard]] bool valid()const noexcept{ return sequence != 0u && generation != 0u; }
};

struct RefractionStatisticsPendingReadback{
    RefractionStatisticsReservationKey key;
    Core::QueueSubmissionToken token;
};


// CPU lifecycle shared by owner and leases; completion follows GPU token completion.
class RefractionStatisticsState : NoCopy{
private:
    struct Slot{
        RefractionStatistics metadata;
        bool reserved = false;
        bool inFlight = false;
    };


public:
    static constexpr u32 s_SlotCount = 3u;


public:
    explicit RefractionStatisticsState(u16 deviceGeneration)noexcept;


public:
    // Owner joins GPU work before invalidation; stale callbacks cannot affect next epoch.
    void reset(u16 deviceGeneration)noexcept;
    [[nodiscard]] RefractionStatisticsReservationKey reserve(const RefractionStatistics& metadata)noexcept;
    void discard(const RefractionStatisticsReservationKey& key)noexcept;
    void accept(const RefractionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, bool hardwareReady)noexcept;
    [[nodiscard]] Expected<RefractionStatisticsPendingReadback> pending(u32 slot)const noexcept;
    void complete(const RefractionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const u32* counters)noexcept;
    [[nodiscard]] Expected<RefractionStatistics> tryGetLatestStatistics()const noexcept;

private:
    [[nodiscard]] Slot* matchingSlot(const RefractionStatisticsReservationKey& key)noexcept;


private:
    Slot m_slots[s_SlotCount];
    RefractionStatistics m_latest;
    u64 m_generation = 1u;
    u64 m_nextSequence = 1u;
    mutable Futex m_mutex;
    u16 m_deviceGeneration = 0u;
};

using RefractionStatisticsControl = RefCounter<RefractionStatisticsState>;
using RefractionStatisticsControlHandle = RefCountPtr< RefractionStatisticsControl, ArenaRefDeleter<RefractionStatisticsControl, Core::Alloc::GlobalArena> >;

[[nodiscard]] Expected<RefractionStatisticsControlHandle> CreateRefractionStatisticsControl(Core::Alloc::GlobalArena& arena, u16 deviceGeneration)noexcept;

// Destruction releases only an unaccepted reservation.
class RefractionStatisticsReservation final : NoCopy{
public:
    RefractionStatisticsReservation(RefractionStatisticsControlHandle control, const RefractionStatistics& metadata)noexcept;
    RefractionStatisticsReservation(RefractionStatisticsReservation&& other)noexcept;
    ~RefractionStatisticsReservation()noexcept;


public:
    RefractionStatisticsReservation& operator=(RefractionStatisticsReservation&& other)noexcept;


public:
    [[nodiscard]] bool valid()const noexcept{ return m_key.valid(); }

    [[nodiscard]] u32 slotIndex()const noexcept{ return m_key.slot; }
    void accept(const Core::QueueSubmissionToken& token, bool hardwareReady)noexcept;
    void discard()noexcept;


private:
    RefractionStatisticsControlHandle m_control;
    RefractionStatisticsReservationKey m_key;
};

struct RefractionStatisticsReadbackSnapshot{
    RefractionStatisticsControlHandle control;
    Core::BufferHandle counters;
    Core::GpuDescriptorHandle counterDescriptor = Core::GpuDescriptorHandle::Invalid();
    Core::BufferHandle buffers[RefractionStatisticsState::s_SlotCount];
    RefractionStatistics metadata;
};

class RefractionStatisticsReadback final : NoCopy{
public:
    RefractionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept;


public:
    void invalidateResources()noexcept;
    [[nodiscard]] bool prepareResources();
    void pollCompleted();
    [[nodiscard]] Expected<RefractionStatistics> tryGetLatestStatistics()const noexcept;
    [[nodiscard]] RefractionStatisticsReadbackSnapshot snapshot(const RefractionStatistics& metadata)const noexcept;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RefractionStatisticsControlHandle m_control;
    Core::BufferHandle m_counters;
    Core::GpuDescriptorHandle m_counterDescriptor = Core::GpuDescriptorHandle::Invalid();
    Core::BufferHandle m_buffers[RefractionStatisticsState::s_SlotCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

