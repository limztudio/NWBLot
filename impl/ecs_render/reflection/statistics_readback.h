// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "statistics.h"
#include "history.h"

#include <core/alloc/global.h>
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


struct ReflectionStatisticsReservationKey{
    u64 sequence = 0u;
    u64 generation = 0u;
    u32 slot = 0u;

    [[nodiscard]] bool valid()const noexcept{ return sequence != 0u && generation != 0u; }
};

// CPU lifecycle shared by owner and leases; completion follows GPU token completion.
class ReflectionStatisticsState : NoCopy{
private:
    struct Slot{
        ReflectionStatistics metadata;
        bool reserved = false;
        bool inFlight = false;
    };

public:
    static constexpr u32 s_SlotCount = 3u;

    explicit ReflectionStatisticsState(u16 deviceGeneration);
    // Owner joins GPU work before invalidation; stale callbacks cannot affect next epoch.
    void reset(u16 deviceGeneration)noexcept;
    [[nodiscard]] ReflectionStatisticsReservationKey reserve(const ReflectionStatistics& metadata)noexcept;
    void discard(const ReflectionStatisticsReservationKey& key)noexcept;
    void accept(
        const ReflectionStatisticsReservationKey& key,
        const Core::QueueSubmissionToken& token,
        bool hardwareReady,
        const ReflectionHistoryOutcome* history = nullptr,
        const ReflectionFeedbackOutcome* feedback = nullptr
    )noexcept;
    [[nodiscard]] bool pending(
        u32 slot,
        ReflectionStatisticsReservationKey& outKey,
        Core::QueueSubmissionToken& outToken
    )const noexcept;
    void complete(
        const ReflectionStatisticsReservationKey& key,
        const Core::QueueSubmissionToken& token,
        const u32* counters
    )noexcept;
    [[nodiscard]] bool tryGetLatestStatistics(ReflectionStatistics& outStatistics)const noexcept;

private:
    [[nodiscard]] Slot* matchingSlot(const ReflectionStatisticsReservationKey& key)noexcept;

private:
    Slot m_slots[s_SlotCount];
    ReflectionStatistics m_latest;
    u64 m_generation = 1u;
    u64 m_nextSequence = 1u;
    mutable Futex m_mutex;
    u16 m_deviceGeneration = 0u;
};

using ReflectionStatisticsControl = RefCounter<ReflectionStatisticsState>;
using ReflectionStatisticsControlHandle = RefCountPtr<
    ReflectionStatisticsControl, ArenaRefDeleter<ReflectionStatisticsControl, Core::Alloc::GlobalArena>
>;

[[nodiscard]] ReflectionStatisticsControlHandle CreateReflectionStatisticsControl(
    Core::Alloc::GlobalArena& arena,
    u16 deviceGeneration
);

// Destruction releases only an unaccepted reservation.
class ReflectionStatisticsReservation final : NoCopy{
public:
    ReflectionStatisticsReservation(ReflectionStatisticsControlHandle control, const ReflectionStatistics& metadata);
    ReflectionStatisticsReservation(ReflectionStatisticsReservation&& other)noexcept;
    ~ReflectionStatisticsReservation()noexcept;
    ReflectionStatisticsReservation& operator=(ReflectionStatisticsReservation&& other)noexcept;

    [[nodiscard]] bool valid()const noexcept{ return m_key.valid(); }
    [[nodiscard]] u32 slotIndex()const noexcept{ return m_key.slot; }
    void accept(
        const Core::QueueSubmissionToken& token,
        bool hardwareReady,
        const ReflectionHistoryOutcome* history = nullptr,
        const ReflectionFeedbackOutcome* feedback = nullptr
    )noexcept;
    void discard()noexcept;

private:
    ReflectionStatisticsControlHandle m_control;
    ReflectionStatisticsReservationKey m_key;
};

struct ReflectionStatisticsReadbackSnapshot{
    ReflectionStatisticsControlHandle control;
    Core::BufferHandle buffers[ReflectionStatisticsState::s_SlotCount];
    ReflectionStatistics metadata;
};

class ReflectionStatisticsReadback final : NoCopy{
public:
    ReflectionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics);
    void invalidateResources();
    [[nodiscard]] bool prepareResources();
    void pollCompleted();
    [[nodiscard]] bool tryGetLatestStatistics(ReflectionStatistics& outStatistics)const;
    [[nodiscard]] ReflectionStatisticsReadbackSnapshot snapshot(const ReflectionStatistics& metadata)const;

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    ReflectionStatisticsControlHandle m_control;
    Core::BufferHandle m_buffers[ReflectionStatisticsState::s_SlotCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

