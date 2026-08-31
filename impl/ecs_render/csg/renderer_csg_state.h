// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/pipeline.h>

#include <impl/ecs_csg/frame_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameStateCacheSignature{
    u64 contentHash = 0u;
    u64 shapeRegistryRevision = 0u;

    friend bool operator==(const CsgFrameStateCacheSignature& lhs, const CsgFrameStateCacheSignature& rhs){
        return lhs.contentHash == rhs.contentHash
            && lhs.shapeRegistryRevision == rhs.shapeRegistryRevision;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgState final : NoCopy{
    friend class RendererCsgSystem;

public:
    RendererCsgState() = default;


private:
    void invalidateResources();


private:
    // CSG layouts are push-only; resources use the global heap.
    Core::BindingLayoutHandle m_clipBindingLayout;
    Core::BindingLayoutHandle m_intervalPeelBindingLayout;
    Core::BindingLayoutHandle m_receiverSpanBuildBindingLayout;
    Core::BindingLayoutHandle m_intervalCombineBindingLayout;
    Core::ShaderHandle m_intervalPeelComputeShader;
    Core::ShaderHandle m_receiverSpanBuildComputeShader;
    Core::ShaderHandle m_intervalCombineComputeShader;
    Core::ShaderHandle m_intervalCapFillPixelShader;
    Core::ComputePipelineHandle m_intervalPeelPipeline;
    Core::ComputePipelineHandle m_receiverSpanBuildPipeline;
    Core::ComputePipelineHandle m_intervalCombinePipeline;
    Core::GraphicsPipelineHandle m_intervalCapFillPipeline;
    Core::BufferHandle m_receiverRangeBuffer;
    Core::BufferHandle m_cutterBuffer;
    Core::BufferHandle m_clipContextSlotsBuffer;
    Core::BufferHandle m_intervalSampleStateBuffer;
    CsgFrameStateCacheSignature m_frameStateCacheSignature;
    CsgFrameState m_frameStateCache;
    usize m_receiverRangeBufferCapacity = 0u;
    usize m_cutterBufferCapacity = 0u;
    Core::GpuDescriptorHandle m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::invalid();
    bool m_frameStateCacheValid = false;
};
static_assert(sizeof(RendererCsgState) == 328u, "RendererCsgState should keep its compact CPU-only layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

