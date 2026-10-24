// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>

#include <global/array.h>
#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<u32 SlotCount>
[[nodiscard]] inline Expected<Array<Core::BufferHandle, SlotCount>> CreateStatisticsReadbackBuffers(Core::GraphicsRuntime& graphics, const usize counterByteSize, const Name (&names)[SlotCount]){
    Array<Core::BufferHandle, SlotCount> buffers;
    for(u32 index = 0u; index < SlotCount; ++index){
        Core::BufferDesc desc;
        desc.setByteSize(counterByteSize).setCpuAccess(Core::CpuAccessMode::Read).setDebugName(names[index]).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::CopyDest);
        auto bufferResult = graphics.createBuffer(desc);
        if(!bufferResult)
            return MakeUnexpected(Failure{});
        buffers[index] = Move(*bufferResult);
    }
    return buffers;
}

template<usize CounterByteSize, u32 SlotCount, typename ControlHandleT>
inline void PollCompletedStatisticsReadback(ControlHandleT& control, Core::GraphicsRuntime& graphics, const Core::BufferHandle (&buffers)[SlotCount], const TStringView mapErrorMessage){
    if(!control)
        return;
    auto& device = graphics.getDevice();
    for(u32 index = 0u; index < SlotCount; ++index){
        if(!buffers[index])
            continue;
        const auto pending = control->pending(index);
        if(!pending || pending->token.deviceGeneration != device.getDeviceGeneration())
            continue;
        const auto& key = pending->key;
        const auto& token = pending->token;
        const Core::GpuPhysicalQueueId physicalQueue{ .index = token.physicalQueueIndex, .deviceGeneration = token.deviceGeneration };
        const Expected<u64> completedValue = device.queueGetCompletedInstance(physicalQueue);
        if(!completedValue || *completedValue < token.value)
            continue;
        const Expected<void*> counters = device.mapBuffer(*buffers[index], Core::CpuAccessMode::Read);
        if(!counters){
            NWB_LOGGER_ERROR(mapErrorMessage);
            control->complete(key, token, nullptr);
            continue;
        }
        u32 completedCounters[CounterByteSize / sizeof(u32)];
        NWB_MEMCPY(completedCounters, sizeof(completedCounters), *counters, sizeof(completedCounters));
        device.unmapBuffer(*buffers[index]);
        control->complete(key, token, completedCounters);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

