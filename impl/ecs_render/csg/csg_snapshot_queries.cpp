// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_csg/module.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ECSRenderDetail::CsgGraphResourceSnapshot::bindingValid()const noexcept{
    return
        receiverRanges
        && cutters
        && clipContextSlots
        && intervalSampleState
        && receiverRangeHeapHandle.valid()
        && receiverRangeHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && cutterHeapHandle.valid()
        && cutterHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && clipContextSlotsHeapHandle.valid()
        && clipContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && intervalSampleStateHeapHandle.valid()
        && intervalSampleStateHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

b8 ECSRenderDetail::CsgGraphResourceSnapshot::frameReady(const CsgFrameGpuDataView& csgFrameData)const noexcept{
    if(!csgFrameData.hasWork())
        return true;

    return bindingValid() && receiverRangeCapacity >= csgFrameData.receiverRanges.size() && cutterCapacity >= csgFrameData.cutters.size();
}

Expected<u32> ECSRenderDetail::CsgGraphResourceSnapshot::findClipContextHeapSlot()const noexcept{
    if(!clipContextSlotsHeapHandle.valid())
        return MakeUnexpected(Failure{});

    return clipContextSlotsHeapHandle.slot();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

