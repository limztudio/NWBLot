// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_csg_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgState::invalidateResources(){
    m_clipBindingLayout.reset();
    m_intervalPeelBindingLayout.reset();
    m_receiverSpanBuildBindingLayout.reset();
    m_intervalCombineBindingLayout.reset();
    m_intervalPeelComputeShader.reset();
    m_receiverSpanBuildComputeShader.reset();
    m_intervalCombineComputeShader.reset();
    m_intervalCapFillPixelShader.reset();
    m_intervalPeelPipeline.reset();
    m_receiverSpanBuildPipeline.reset();
    m_intervalCombinePipeline.reset();
    m_intervalCapFillPipeline.reset();
    m_receiverRangeBuffer.reset();
    m_cutterBuffer.reset();
    m_clipContextSlotsBuffer.reset();
    m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_intervalSampleStateBuffer.reset();
    m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_frameStateCacheSignature = CsgFrameStateCacheSignature{};
    m_frameStateCache = CsgFrameState{};
    m_receiverRangeBufferCapacity = 0u;
    m_cutterBufferCapacity = 0u;
    m_frameStateCacheValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

