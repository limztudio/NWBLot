// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"
#include "renderer_raytracing_state.h"

#include <impl/assets/graphics/refraction/names.h>
#include <impl/assets/graphics/refraction/push_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_refraction{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RefractionPushConstants{
#define NWB_REFRACTION_CPU_FIELD(name, defaultValue) u32 name = defaultValue;
    NWB_REFRACTION_PUSH_CONSTANTS_FIELDS(NWB_REFRACTION_CPU_FIELD)
#undef NWB_REFRACTION_CPU_FIELD
};
static_assert(sizeof(RefractionPushConstants) == 5u * sizeof(u32));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareRefractionResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    m_rayTracingState.m_refractionUseHardwareTrace = false;
    if(!heap.isInitialized())
        return false;

    if(!m_rayTracingState.m_refractionBindingLayout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute);
        desc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_rt_refraction::RefractionPushConstants)));
        m_rayTracingState.m_refractionBindingLayout = device.createBindingLayout(desc);
        if(!m_rayTracingState.m_refractionBindingLayout)
            return false;
    }

    const auto ensurePipeline = [&](const bool hardware) -> bool{
        Core::ComputePipelineHandle& pipeline = hardware
            ? m_rayTracingState.m_refractionHwPipeline : m_rayTracingState.m_refractionScreenPipeline;
        Core::ShaderHandle& shader = hardware
            ? m_rayTracingState.m_refractionHwShader : m_rayTracingState.m_refractionScreenShader;
        bool& failed = hardware
            ? m_rayTracingState.m_refractionHwPipelineFailed : m_rayTracingState.m_refractionScreenPipelineFailed;
        if(pipeline)
            return true;
        if(failed)
            return false;
        if(!m_shaderSystem.loadShader(
            shader,
            hardware ? AssetsGraphicsRefraction::s_HwResolveShaderName : AssetsGraphicsRefraction::s_ScreenResolveShaderName,
            hardware ? AStringView("NWB_BINDLESS_TLAS=1") : Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            hardware ? "ECSRender_RefractionHw" : "ECSRender_RefractionScreen"
        )){
            failed = true;
            return false;
        }
        Core::ComputePipelineDesc desc;
        desc.setComputeShader(shader)
            .addBindingLayout(m_rayTracingState.m_refractionBindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout());
        if(hardware)
            desc.addBindingLayout(heap.getAccelStructLayout());
        pipeline = device.createComputePipeline(desc);
        if(!pipeline){
            failed = true;
            return false;
        }
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created refraction resolve pipeline ({})"),
            hardware ? NWB_TEXT("hardware ray query") : NWB_TEXT("screen space"));
        return true;
    };

    // A separately compiled shader keeps RayQuery/SPIR-V ray-tracing capabilities off unsupported devices.
    if(!ensurePipeline(false))
        return false;
    const bool hardwareReady = m_rayTracingState.m_refractionHardwareTracingEnabled && shadowVisibilityHardwareSupported()
        && m_shadowVisibilityTraceResourcesPreflighted && m_shadowVisibilityBackendPipelinePreflighted
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
        && heap.hasAccelStructLayout()
        && m_rayTracingState.m_tlas && m_rayTracingState.m_tlasHeapHandle.valid()
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer;
    if(hardwareReady && ensureCausticMaterialContextSlotsHeapHandle() && ensurePipeline(true))
        m_rayTracingState.m_refractionUseHardwareTrace = true;
    return true;
}

void RendererRayTracingSystem::setRefractionHardwareTracingEnabled(const bool enabled)noexcept{
    m_rayTracingState.m_refractionHardwareTracingEnabled = enabled;
}

RayTracingRefractionGraphResources RendererRayTracingSystem::snapshotRefractionGraphResources()const{
    RayTracingRefractionGraphResources resources;
    const ECSRenderDetail::MeshViewBufferSnapshot view = m_meshSystem.meshViewBufferSnapshot();
    if(!view.bindingValid())
        return resources;
    resources.viewBuffer = view.buffer;
    resources.screenFallbackPipeline = m_rayTracingState.m_refractionScreenPipeline;
    resources.viewHeapSlot = view.heapHandle.slot();
    resources.usesHardwareTrace = m_rayTracingState.m_refractionUseHardwareTrace;
    resources.pipeline = resources.usesHardwareTrace
        ? m_rayTracingState.m_refractionHwPipeline : m_rayTracingState.m_refractionScreenPipeline;
    if(resources.usesHardwareTrace){
        resources.sceneTlas = m_rayTracingState.m_tlas;
        resources.tlasHeapHandle = m_rayTracingState.m_tlasHeapHandle;
        resources.materialContextSlotsBuffer = m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer;
        resources.materialContextSlotsHeapSlot = m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.slot();
    }
    return resources;
}

bool RendererRayTracingSystem::recordRefractionResolve(
    Core::CommandList& commandList,
    const DeferredFrameTargets& targets,
    const RayTracingRefractionGraphResources& resources
)const{
    if(!resources.valid() || !targets.bindless.valid() || targets.width == 0u || targets.height == 0u)
        return false;

    // The graph owns all transitions and the frozen TLAS/geometry/material declarations. In particular this method
    // neither builds an acceleration structure nor recompiles/switches a pipeline while recording a packet.
    __hidden_rt_refraction::RefractionPushConstants push;
    push.width = targets.width;
    push.height = targets.height;
    push.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
    push.materialContextSlotsHeapSlot = resources.materialContextSlotsHeapSlot;
    push.viewHeapSlot = resources.viewHeapSlot;
    Core::ComputeState state;
    state.setPipeline(resources.pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.pipeline.get(), resources.tlasHeapHandle);
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(
        (targets.width + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE,
        (targets.height + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE,
        1u
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

