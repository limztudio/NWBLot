// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "statistics_readback.h"

#include <impl/assets/graphics/refraction/diagnostics_layout.h>
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

    Slot* slot = matchingSlot(key);
    if(!slot || !slot->reserved || slot->inFlight)
        return;
    slot->reserved = false;
    slot->inFlight = true;
    slot->metadata.acceptedToken = token;
    slot->metadata.hardwareReady = hardwareReady;
    slot->metadata.csgHardwareEnabled = slot->metadata.csgHardwareEnabled && hardwareReady;
}

void RefractionStatisticsState::complete(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const u32* counters)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = beginCompleteWithinLock(key, token);
    if(!slot)
        return;
    if(counters && slot->metadata.sequence > m_latest.sequence){
        m_latest = slot->metadata;
#define NWB_REFRACTION_READ_COUNTER(name, offset) m_latest.name = counters[offset / sizeof(u32)];
        NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_REFRACTION_READ_COUNTER)
#undef NWB_REFRACTION_READ_COUNTER
    }
    *slot = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RefractionStatisticsControlHandle> CreateRefractionStatisticsControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration)noexcept{
    return CreateStatisticsControl<RefractionStatisticsControl>(arena, deviceGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RefractionStatisticsReservation::accept(const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
    if(m_control && m_key.valid())
        m_control->accept(m_key, token, hardwareReady);
    m_key = {};
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RefractionStatisticsReadback::RefractionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept
    : m_arena(arena)
    , m_graphics(graphics)
{}

void RefractionStatisticsReadback::invalidateResources()noexcept{
    // Renderer invalidation follows the runtime's device join or terminal loss and graph-payload reset.
    // Retire the descriptor only at that boundary; disabling diagnostics keeps pending resources alive.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(m_counterDescriptor.valid() && heap.isInitialized())
        heap.free(m_counterDescriptor);
    m_counterDescriptor = Core::GpuDescriptorHandle::Invalid();
    m_counters = nullptr;
    if(m_control)
        m_control->reset(0u);
    for(Core::BufferHandle& buffer : m_buffers)
        buffer = nullptr;
}

b8 RefractionStatisticsReadback::prepareResources(){
    if(m_counters && m_counterDescriptor.valid() && m_buffers[0])
        return true;
    const u16 deviceGeneration = m_graphics.getDevice().getDeviceGeneration();
    if(!m_control){
        auto controlResult = CreateRefractionStatisticsControl(m_arena, deviceGeneration);
        if(!controlResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Refraction statistics: failed to allocate readback control"));
            return false;
        }
        m_control = Move(*controlResult);
    }
    static constexpr Name s_Names[] = { Name("engine/refraction/statistics_readback_0"), Name("engine/refraction/statistics_readback_1"), Name("engine/refraction/statistics_readback_2"), };
    Core::BufferDesc counterDesc;
    counterDesc.setByteSize(NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE).setCanHaveUAVs(true).setCanHaveRawViews(true).setDebugName(Name("engine/refraction/diagnostic_counters")).enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess);
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
    auto buffers = ECSRenderDetail::CreateStatisticsReadbackBuffers(m_graphics, NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE, s_Names);
    if(!buffers)
        return false;
    m_control->reset(deviceGeneration);
    m_counters = Move(counters);
    m_counterDescriptor = *descriptor;
    for(u32 index = 0u; index < RefractionStatisticsState::s_SlotCount; ++index)
        m_buffers[index] = Move((*buffers)[index]);
    retireDescriptor.release();
    return true;
}

void RefractionStatisticsReadback::pollCompleted(){
    ECSRenderDetail::PollCompletedStatisticsReadback<NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE>(m_control, m_graphics, m_buffers, NWB_TEXT("Refraction statistics: failed to map a completed readback"));
}

Expected<RefractionStatistics> RefractionStatisticsReadback::tryGetLatestStatistics()const noexcept{
    if(!m_control)
        return MakeUnexpected(Failure{});
    return m_control->tryGetLatestStatistics();
}

RefractionStatisticsReadbackSnapshot RefractionStatisticsReadback::snapshot(const RefractionStatistics& metadata)const noexcept{
    RefractionStatisticsReadbackSnapshot result;
    if(!m_control || !m_counters || !m_counterDescriptor.valid() || !m_buffers[0])
        return result;
    result.control = m_control;
    result.counters = m_counters;
    result.counterDescriptor = m_counterDescriptor;
    result.metadata = metadata;
    for(u32 index = 0u; index < RefractionStatisticsState::s_SlotCount; ++index)
        result.buffers[index] = m_buffers[index];
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

