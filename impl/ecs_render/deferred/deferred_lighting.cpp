// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/caustic/rt_caustic_state.h>
#include <impl/ecs_render/deferred/deferred_graph_private.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_scene_private.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(DeferredLightingPushConstants) == sizeof(u32) * 2u);


struct DeferredLightingGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // The deferred system, the frame targets, and the timing ticket are required: the pass always records against this frame's targets.
    struct Payload{
        RendererDeferredSystem& deferredSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        b8 useLaggedLightingHistory = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){

        return ECSRenderDetail::RecordDeferredGraphTask(payload, commandList, [&](RendererDeferredSystem& deferredSystem, DeferredFrameTargets& targets, Core::CommandList& taskCommandList){
            return deferredSystem.renderDeferredLighting(taskCommandList, targets, payload.useLaggedLightingHistory);
        });
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneShadingBufferUploads RendererDeferredSystem::prepareSceneShadingBufferUploads(const f32 fallbackAspectRatio, const RayTracingLightingClassificationInput& rayTracingInput)noexcept{
    NWB_ASSERT(m_deferredState.m_sceneShadingBuffer);
    NWB_ASSERT(m_deferredState.m_lightBuffer);
    SceneShadingBufferUploads uploads{};

    Array<f32, NWB_SCENE_MAX_LIGHTS> causticLightImportance{};
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(m_world, uploads.lightData, causticLightImportance);

    // Rank caustic lights into params.w; needs a refractive instance.
    const u32 causticLightCount = ECSRenderDetail::ResolveCausticLights(
        Span<const f32>(causticLightImportance.data(), lightCount),
        rayTracingInput.refractiveInstanceCount,
        Span<SceneLightGpuData>(uploads.lightData.data(), lightCount)
    );
    uploads.lightingClassification.causticLightCount = causticLightCount;
    // Bitmask of shadow slots holding a light.
    uploads.lightingClassification.softShadowSlotMask = ResolveSoftShadowSlotMask(Span<const SceneLightGpuData>(uploads.lightData.data(), lightCount));

    const usize lightByteCount = static_cast<usize>(lightCount) * sizeof(SceneLightGpuData);
    NWB_ASSERT(lightByteCount <= m_deferredState.m_lightGpuData.size());
    const b8 lightDataUnchanged = m_deferredState.m_lightGpuDataValid && m_deferredState.m_lightGpuDataCount == lightCount && NWB_MEMCMP(m_deferredState.m_lightGpuData.data(), uploads.lightData.data(), lightByteCount) == 0;
    // A zero-light scene has nothing to copy; still record the empty mirror.
    uploads.lightUploadRequired = !lightDataUnchanged && lightByteCount != 0u;
    uploads.lightCount = lightCount;

    uploads.sceneShadingState = ECSRenderDetail::ResolveSceneShadingState(fallbackAspectRatio, lightCount, m_world);
    uploads.sceneShadingUploadRequired = !(m_deferredState.m_sceneShadingGpuDataValid && NWB_MEMCMP(m_deferredState.m_sceneShadingGpuData.data(), &uploads.sceneShadingState, sizeof(uploads.sceneShadingState)) == 0);
    return uploads;
}

void RendererDeferredSystem::confirmSceneShadingBufferUploads(
    const Span<const SceneLightGpuData> lights,
    const b8 lightUploadRequired,
    const SceneShadingGpuData& sceneShadingState,
    const b8 sceneShadingUploadRequired
)noexcept{
    const u32 lightCount = static_cast<u32>(lights.size());
    const usize lightByteCount = lights.size() * sizeof(SceneLightGpuData);
    // Commit the empty mirror on accept so later diffs stay correct.
    if(lightUploadRequired || lightCount == 0u){
        if(lightByteCount != 0u)
            NWB_MEMCPY(m_deferredState.m_lightGpuData.data(), m_deferredState.m_lightGpuData.size(), lights.data(), lightByteCount);
        m_deferredState.m_lightGpuDataCount = lightCount;
        m_deferredState.m_lightGpuDataValid = true;
    }
    if(sceneShadingUploadRequired){
        NWB_MEMCPY(m_deferredState.m_sceneShadingGpuData.data(), m_deferredState.m_sceneShadingGpuData.size(), &sceneShadingState, sizeof(sceneShadingState));
        m_deferredState.m_sceneShadingGpuDataValid = true;
    }
}

b8 RendererDeferredSystem::createDeferredLightingResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred lighting requires the global descriptor heap"));
        return false;
    }

    if(!m_deferredState.m_sceneShadingBuffer){
        Core::BufferDesc sceneShadingBufferDesc;
        sceneShadingBufferDesc
            .setByteSize(sizeof(SceneShadingGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(s_SceneShadingBufferName)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto sceneShadingBufferResult = m_graphics.createBuffer(sceneShadingBufferDesc);
        if(sceneShadingBufferResult)
            m_deferredState.m_sceneShadingBuffer = Move(*sceneShadingBufferResult);
        else
            m_deferredState.m_sceneShadingBuffer.reset();
        if(!sceneShadingBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene shading buffer"));
            return false;
        }
    }

    if(!m_deferredState.m_lightBuffer){
        Core::BufferDesc lightBufferDesc;
        lightBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneLightGpuData) * NWB_SCENE_MAX_LIGHTS))
            .setStructStride(sizeof(SceneLightGpuData))
            .setDebugName(s_SceneLightBufferName)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto lightBufferResult = m_graphics.createBuffer(lightBufferDesc);
        if(lightBufferResult)
            m_deferredState.m_lightBuffer = Move(*lightBufferResult);
        else
            m_deferredState.m_lightBuffer.reset();
        if(!lightBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene light buffer"));
            return false;
        }
    }

    // Local layout carries the heap slot plus the swap-chain mode.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(DeferredLightingPushConstants), Core::ShaderType::Compute, m_deferredState.m_lightingBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding layout"));
        return false;
    }

    if(!ECSRenderDetail::CreateClampSampler(device, false, m_deferredState.m_sampler)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting sampler"));
        return false;
    }

    // Shading is material-driven via the generated BXDF dispatch.
    if(!m_shaderSystem.loadShader<ComputeShader>(&DeferredSubsystem::lightingCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredLightingCS", m_deferredState.m_lightingComputeShader))
        return false;

    return true;
}

b8 RendererDeferredSystem::createDeferredLightingPipeline(){
    if(!createDeferredLightingResources())
        return false;

    if(m_deferredState.m_lightingPipeline)
        return true;

    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_graphics.getDevice(), m_deferredState.m_lightingComputeShader, m_deferredState.m_lightingBindingLayout, false, m_deferredState.m_lightingPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting pipeline"));
        return false;
    }

    return true;
}

Expected<Core::GpuTaskId> RendererDeferredSystem::declareDeferredLightingTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8 useLaggedLightingHistory, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket){
    return graph.addTask<__hidden_deferred_lighting::DeferredLightingGraphTask>(
        desc,
        __hidden_deferred_lighting::DeferredLightingGraphTask::Payload{ .deferredSystem = *this, .targets = targets, .timingTicket = timingTicket, .useLaggedLightingHistory = useLaggedLightingHistory, }
    );
}

b8 RendererDeferredSystem::renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets, const b8 useLaggedLightingHistory){
    NWB_ASSERT(m_deferredState.m_lightingPipeline);

    const DeferredLaggedLightingHistoryResources* const laggedHistory = useLaggedLightingHistory ? &targets.laggedLightingHistory : nullptr;
    if(useLaggedLightingHistory && (!laggedHistory || !laggedHistory->valid()))
        return false;

    const Core::GpuDescriptorHandle resourceSlots = laggedHistory ? laggedHistory->slotsBufferDescriptor : targets.bindless.slotsBufferDescriptor;

    // Boundary transitions are graph-owned; this thunk holds upload + dispatch.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredLighting, m_graphics.getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(m_deferredState.m_lightingPipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_deferredState.m_lightingPipeline);
    const DeferredLightingPushConstants pushConstants = ResolveDeferredLightingPushConstants(resourceSlots.slot(), m_graphics.isHDR10OutputActive());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    const u32 groupCountX = (targets.width + NWB_DEFERRED_LIGHTING_GROUP_SIZE - 1u) / NWB_DEFERRED_LIGHTING_GROUP_SIZE;
    const u32 groupCountY = (targets.height + NWB_DEFERRED_LIGHTING_GROUP_SIZE - 1u) / NWB_DEFERRED_LIGHTING_GROUP_SIZE;
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

