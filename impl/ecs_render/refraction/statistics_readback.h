// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "statistics.h"

#include <impl/ecs_render/shared/statistics_readback_state.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using RefractionStatisticsReservationKey = StatisticsReservationKey;
using RefractionStatisticsPendingReadback = StatisticsPendingReadback;


// CPU lifecycle shared by owner and leases; completion follows GPU token completion.
class RefractionStatisticsState : public StatisticsReadbackStateBase<RefractionStatistics>{
public:
    using StatisticsReadbackStateBase<RefractionStatistics>::StatisticsReadbackStateBase;


public:
    void accept(const RefractionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, b8 hardwareReady)noexcept;
    void complete(const RefractionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const u32* counters)noexcept;
};

using RefractionStatisticsControl = RefCounter<RefractionStatisticsState>;
using RefractionStatisticsControlHandle = RefCountPtr< RefractionStatisticsControl, ArenaRefDeleter<RefractionStatisticsControl, Core::Alloc::GlobalArena> >;

[[nodiscard]] Expected<RefractionStatisticsControlHandle> CreateRefractionStatisticsControl(Core::Alloc::GlobalArena& arena, u16 deviceGeneration)noexcept;

// Destruction releases only an unaccepted reservation.
class RefractionStatisticsReservation final : public StatisticsReservationBase<RefractionStatisticsControlHandle, RefractionStatistics>{
public:
    using StatisticsReservationBase<RefractionStatisticsControlHandle, RefractionStatistics>::StatisticsReservationBase;


public:
    void accept(const Core::QueueSubmissionToken& token, b8 hardwareReady)noexcept;
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
    [[nodiscard]] b8 prepareResources();
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

