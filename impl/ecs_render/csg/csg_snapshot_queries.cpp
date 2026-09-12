// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>

#include <impl/assets/graphics/csg/constants.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/ecs_csg/module.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ECSRenderDetail::CsgGraphResourceSnapshot::bindingValid()const noexcept{
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

bool ECSRenderDetail::CsgGraphResourceSnapshot::frameReady(const CsgFrameGpuData& csgFrameData)const noexcept{
    if(!csgFrameData.hasWork())
        return true;

    return
        bindingValid()
        && receiverRangeCapacity >= csgFrameData.receiverRanges.size()
        && cutterCapacity >= csgFrameData.cutters.size()
    ;
}

bool ECSRenderDetail::CsgGraphResourceSnapshot::findClipContextHeapSlot(u32& outHeapSlot)const noexcept{
    outHeapSlot = 0u;
    if(!clipContextSlotsHeapHandle.valid())
        return false;

    outHeapSlot = clipContextSlotsHeapHandle.slot();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

