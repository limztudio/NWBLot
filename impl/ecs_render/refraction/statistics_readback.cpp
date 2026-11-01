// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "statistics_readback.h"

#include <impl/assets/graphics/refraction/diagnostics_layout.h>
#include <impl/ecs_render/refraction/types.adhd.h>
#include <impl/ecs_render/shared/statistics_readback_detail.h>

#include <core/alloc/general.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RefractionStatisticsState::accept(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = beginAcceptWithinLock(key, token, hardwareReady);
    if(!slot)
        return;
    slot->metadata.csgHardwareEnabled = slot->metadata.csgHardwareEnabled && hardwareReady;
}

void RefractionStatisticsState::complete(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const Span<const u32> counters)noexcept{
    constexpr usize s_RequiredCounterCount = NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32);
    completeSlot<s_RequiredCounterCount>(
        key,
        token,
        counters,
        [](RefractionStatistics& latest, [[maybe_unused]] const RefractionStatistics& metadata, const Span<const u32> completedCounters)noexcept{
#define NWB_REFRACTION_READ_COUNTER(name, offset) latest.name = completedCounters[offset / sizeof(u32)];
            NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_REFRACTION_READ_COUNTER)
#undef NWB_REFRACTION_READ_COUNTER
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RefractionStatisticsReadback::invalidateResources()noexcept{
    // Renderer invalidation follows the runtime's device join or terminal loss and graph-payload reset.
    // Retire the descriptor only at that boundary; disabling diagnostics keeps pending resources alive.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(m_counterDescriptor.valid() && heap.isInitialized())
        heap.free(m_counterDescriptor);
    m_counterDescriptor = Core::GpuDescriptorHandle::Invalid();
    m_counters = nullptr;
    resetReadbackBuffers();
}

b8 RefractionStatisticsReadback::prepareResources(){
    if(m_counters && m_counterDescriptor.valid() && m_buffers[0])
        return true;
    static constexpr Array<Name, s_StatisticsReadbackSlotCount> s_Names = { s_RefractionStatisticsReadback0Name, s_RefractionStatisticsReadback1Name, s_RefractionStatisticsReadback2Name, };
    Core::BufferDesc counterDesc;
    counterDesc.setByteSize(NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE).setCanHaveUAVs(true).setCanHaveRawViews(true).setDebugName(s_RefractionDiagnosticCountersName).enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess);
    const auto counterResult = m_graphics.createBuffer(counterDesc);
    if(!counterResult)
        return false;
    Core::BufferHandle counters = *counterResult;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    const auto descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!descriptor)
        return false;
    Core::GpuDescriptorRetirementGuard retireDescriptor(heap, *descriptor);
    if(!heap.write(*descriptor, Core::DescriptorWriteItem::RawBufferUav(0u, counters.get())))
        return false;
    if(!ECSRenderDetail::PrepareStatisticsReadbackControlAndBuffers<NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE>(
        s_Names,
        NWB_TEXT("Refraction statistics: failed to allocate readback control"),
        m_arena,
        m_graphics,
        m_control,
        m_buffers
    ))
        return false;
    m_counters = Move(counters);
    m_counterDescriptor = *descriptor;
    retireDescriptor.release();
    return true;
}

void RefractionStatisticsReadback::pollCompleted(){
    ECSRenderDetail::PollCompletedStatisticsReadback<NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE>(m_buffers, NWB_TEXT("Refraction statistics: failed to map a completed readback"), m_control, m_graphics);
}

RefractionStatisticsReadbackSnapshot RefractionStatisticsReadback::snapshot(const RefractionStatistics& metadata)const noexcept{
    RefractionStatisticsReadbackSnapshot result;
    if(!m_control || !m_counters || !m_counterDescriptor.valid() || !m_buffers[0])
        return result;
    populateSnapshot(metadata, result);
    result.counters = m_counters;
    result.counterDescriptor = m_counterDescriptor;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

