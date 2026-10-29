// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/api.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameGpuDataView;

namespace ECSRenderDetail{
    struct CsgGraphResourceSnapshot{
        Core::BufferHandle receiverRanges;
        Core::BufferHandle cutters;
        Core::BufferHandle clipContextSlots;
        Core::BufferHandle intervalSampleState;
        Core::GraphicsPipelineHandle refractionCapFillPipeline;
        usize receiverRangeCapacity = 0u;
        usize cutterCapacity = 0u;
        Core::GpuDescriptorHandle receiverRangeHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle cutterHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::Invalid();


    public:
        [[nodiscard]] b8 bindingValid()const noexcept;
        [[nodiscard]] b8 frameReady(const CsgFrameGpuDataView& csgFrameData)const noexcept;
        [[nodiscard]] Expected<u32> findClipContextHeapSlot()const noexcept;
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

