// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"
#include "renderer_raytracing_state.h"

#include <impl/assets_graphics/refraction/names.h>
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
static_assert(sizeof(RefractionPushConstants) == 9u * sizeof(u32));

static RefractionPushConstants MakePushConstants(const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)noexcept{
    RefractionPushConstants push;
    push.width = targets.width;
    push.height = targets.height;
    push.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
    push.materialContextSlotsHeapSlot = resources.materialContextSlotsHeapSlot;
    push.viewHeapSlot = resources.viewHeapSlot;
    push.opaqueReflectionSlot = resources.opaqueReflectionSlot;
    push.refractionEnabled = resources.refractionEnabled ? 1u : 0u;
    push.tileListSlot = targets.avboit.refractionTileListDescriptor.slot();
    push.tileArgumentsSlot = targets.avboit.refractionTileArgumentsDescriptor.slot();
    return push;
}


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
        auto refractionBindingLayoutResult = device.createBindingLayout(desc);
        if(refractionBindingLayoutResult)
            m_rayTracingState.m_refractionBindingLayout = Move(*refractionBindingLayoutResult);
        else
            m_rayTracingState.m_refractionBindingLayout.reset();
        if(!refractionBindingLayoutResult)
            return false;
    }

    if(!m_rayTracingState.m_refractionClassifyTilesPipeline){
        if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_refractionClassifyTilesShader, AssetsGraphicsRefraction::s_ClassifyTilesShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_RefractionClassifyTiles"))
            return false;
        Core::ComputePipelineDesc desc;
        desc.setComputeShader(m_rayTracingState.m_refractionClassifyTilesShader).addBindingLayout(m_rayTracingState.m_refractionBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto pipeline = device.createComputePipeline(desc);
        if(!pipeline)
            return false;
        m_rayTracingState.m_refractionClassifyTilesPipeline = Move(*pipeline);
    }
    if(!m_graphics.gpuTiming().prepareScopeQueries(RendererGpuTimingScope::s_RefractionClassifyTiles.identity, device, 32u))
        return false;

    const bool csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    const auto ensurePipeline = [&](const bool hardware) -> bool{
        Core::ComputePipelineHandle& pipeline = hardware ? (csg ? m_rayTracingState.m_refractionCsgHwPipeline : m_rayTracingState.m_refractionHwPipeline) : m_rayTracingState.m_refractionScreenPipeline;
        Core::ShaderHandle& shader = hardware ? (csg ? m_rayTracingState.m_refractionCsgHwShader : m_rayTracingState.m_refractionHwShader) : m_rayTracingState.m_refractionScreenShader;
        bool& failed = hardware ? (csg ? m_rayTracingState.m_refractionCsgHwPipelineFailed : m_rayTracingState.m_refractionHwPipelineFailed) : m_rayTracingState.m_refractionScreenPipelineFailed;
        if(pipeline)
            return true;
        if(failed)
            return false;
        if(!m_shaderSystem.loadShader<ComputeShader>(
            shader,
            hardware ? AssetsGraphicsRefraction::s_HwResolveShaderName : AssetsGraphicsRefraction::s_ScreenResolveShaderName,
            hardware ? (csg ? s_RayTracingCsgEnabledVariant : s_RayTracingCsgDisabledVariant) : Core::ShaderArchive::s_DefaultVariant,
            hardware ? "ECSRender_RefractionHw" : "ECSRender_RefractionScreen"
        )){
            failed = true;
            return false;
        }
        Core::ComputePipelineDesc desc;
        desc.setComputeShader(shader).addBindingLayout(m_rayTracingState.m_refractionBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        if(hardware)
            desc.addBindingLayout(heap.getAccelStructLayout());
        auto pipelineResult = device.createComputePipeline(desc);
        if(pipelineResult)
            pipeline = Move(*pipelineResult);
        else
            pipeline.reset();
        if(!pipelineResult){
            failed = true;
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: created refraction resolve pipeline ({})"), hardware ? NWB_TEXT("hardware ray query") : NWB_TEXT("screen space"));
        return true;
    };

    // A separate shader keeps ray-tracing capabilities off unsupported devices.
    if(!ensurePipeline(false))
        return false;
    const bool hardwareReady = m_rayTracingState.m_refractionHardwareTracingEnabled && shadowVisibilityHardwareSupported()
        && m_shadowVisibilityTraceResourcesPreflighted && m_shadowVisibilityBackendPipelinePreflighted
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
        && heap.hasAccelStructLayout()
        && m_rayTracingState.m_tlas && m_rayTracingState.m_tlasHeapHandle.valid()
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer;
    if(hardwareReady && ensureRayTraceMaterialContextSlotsHeapHandle() && ensurePipeline(true))
        m_rayTracingState.m_refractionUseHardwareTrace = true;
    return true;
}

void RendererRayTracingSystem::setRefractionHardwareTracingEnabled(const bool enabled)noexcept{
    m_rayTracingState.m_refractionHardwareTracingEnabled = enabled;
}

RayTracingRefractionGraphResources RendererRayTracingSystem::snapshotRefractionGraphResources()const noexcept{
    RayTracingRefractionGraphResources resources;
    const ECSRenderDetail::MeshViewBufferSnapshot view = m_meshSystem.meshViewBufferSnapshot();
    if(!view.bindingValid())
        return resources;
    resources.viewBuffer = view.buffer;
    resources.screenFallbackPipeline = m_rayTracingState.m_refractionScreenPipeline;
    resources.classifyTilesPipeline = m_rayTracingState.m_refractionClassifyTilesPipeline;
    resources.viewHeapSlot = view.heapHandle.slot();
    resources.usesHardwareTrace = m_rayTracingState.m_refractionUseHardwareTrace;
    resources.pipeline = resources.usesHardwareTrace ? (m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_refractionCsgHwPipeline : m_rayTracingState.m_refractionHwPipeline) : m_rayTracingState.m_refractionScreenPipeline;
    if(resources.usesHardwareTrace){
        resources.sceneTlas = m_rayTracingState.m_tlas;
        resources.tlasHeapHandle = m_rayTracingState.m_tlasHeapHandle;
        resources.materialContextSlotsBuffer = m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer;
        resources.materialContextSlotsHeapSlot = m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
    }
    return resources;
}

bool RendererRayTracingSystem::recordRefractionTileClassification(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const{
    if(!resources.valid() || !targets.bindless.valid() || !targets.avboit.refractionTileListDescriptor.valid() || !targets.avboit.refractionTileArgumentsDescriptor.valid() || targets.width == 0u || targets.height == 0u)
        return false;
    const auto push = __hidden_rt_refraction::MakePushConstants(targets, resources);
    Core::ComputeState state;
    state.setPipeline(resources.classifyTilesPipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.classifyTilesPipeline);
    commandList.setPushConstants(&push, sizeof(push));
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_RefractionClassifyTiles, m_graphics.getDevice(), commandList);

    commandList.dispatch((targets.width + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE, (targets.height + NWB_REFRACTION_GROUP_SIZE - 1u) / NWB_REFRACTION_GROUP_SIZE, 1u);
    return true;
}

bool RendererRayTracingSystem::recordRefractionResolve(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const{
    if(!resources.valid() || !targets.bindless.valid() || targets.width == 0u || targets.height == 0u)
        return false;

    // The graph owns transitions and frozen declarations; this records without rebuilds.
    const auto push = __hidden_rt_refraction::MakePushConstants(targets, resources);
    Core::ComputeState state;
    state.setPipeline(resources.pipeline.get()).setIndirectParams(targets.avboit.refractionTileArguments.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.pipeline.get(), resources.tlasHeapHandle);
    commandList.setPushConstants(&push, sizeof(push));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_RefractionResolve, m_graphics.getDevice(), commandList);

        commandList.dispatchIndirect(0u);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

