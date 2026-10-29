// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "statistics.h"
#include "history.h"

#include <impl/ecs_render/shared/statistics_readback_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ReflectionStatisticsReservationKey = StatisticsReservationKey;
using ReflectionStatisticsPendingReadback = StatisticsPendingReadback;


// CPU lifecycle shared by owner and leases; completion follows GPU token completion.
class ReflectionStatisticsState : public StatisticsReadbackStateBase<ReflectionStatistics>{
public:
    using StatisticsReadbackStateBase<ReflectionStatistics>::StatisticsReadbackStateBase;


public:
    void accept(const ReflectionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, b8 hardwareReady, const ReflectionHistoryOutcome* history = nullptr, const ReflectionFeedbackOutcome* feedback = nullptr)noexcept;
    void complete(const ReflectionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const u32* counters)noexcept;
};

using ReflectionStatisticsControl = RefCounter<ReflectionStatisticsState>;
using ReflectionStatisticsControlHandle = RefCountPtr< ReflectionStatisticsControl, ArenaRefDeleter<ReflectionStatisticsControl, Core::Alloc::GlobalArena> >;

[[nodiscard]] Expected<ReflectionStatisticsControlHandle> CreateReflectionStatisticsControl(Core::Alloc::GlobalArena& arena, u16 deviceGeneration)noexcept;

// Destruction releases only an unaccepted reservation.
class ReflectionStatisticsReservation final : public StatisticsReservationBase<ReflectionStatisticsControlHandle, ReflectionStatistics>{
public:
    using StatisticsReservationBase<ReflectionStatisticsControlHandle, ReflectionStatistics>::StatisticsReservationBase;


public:
    void accept(const Core::QueueSubmissionToken& token, b8 hardwareReady, const ReflectionHistoryOutcome* history = nullptr, const ReflectionFeedbackOutcome* feedback = nullptr)noexcept;
};

struct ReflectionStatisticsReadbackSnapshot{
    ReflectionStatisticsControlHandle control;
    Core::BufferHandle buffers[ReflectionStatisticsState::s_SlotCount];
    ReflectionStatistics metadata;
};

class ReflectionStatisticsReadback final : NoCopy{
public:
    ReflectionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept;


public:
    void invalidateResources()noexcept;
    [[nodiscard]] b8 prepareResources();
    void pollCompleted();
    [[nodiscard]] Expected<ReflectionStatistics> tryGetLatestStatistics()const noexcept;
    [[nodiscard]] ReflectionStatisticsReadbackSnapshot snapshot(const ReflectionStatistics& metadata)const noexcept;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    ReflectionStatisticsControlHandle m_control;
    Core::BufferHandle m_buffers[ReflectionStatisticsState::s_SlotCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

