// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"
#include "renderer_raytracing_state.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareCausticResources(
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    DeferredFrameTargets& targets,
    const bool hardware
){
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !meshView.buffer
        || !meshView.heapHandle.valid()
        || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        meshView.heapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        if(hardware)
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require complete deferred bindless frame resources"));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady =
        ensureRayTraceMaterialContextSlotsHeapHandle()
        && (hardware ? ensureCausticRtPipeline() : ensureSwCausticPipeline())
    ;
    // Prepare shared resolve and temporal resources even when the producer is unavailable.
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticResolvePipeline()
    ;
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || ensureCausticAccumulatorDecayPipeline()
    ;
    return producerReady && resolveReady && temporalReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

