// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/deferred_graph_private.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_scene_private.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>

#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets/graphics/deferred/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PushConstants{
    u32 resourceSlots = 0u;
    u32 presentationMode = NWB_DEFERRED_PRESENTATION_SDR;
};
static_assert(sizeof(PushConstants) == sizeof(u32) * 2u);


struct DeferredLightingGraphTask{
    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool useLaggedLightingHistory = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return ECSRenderDetail::RecordDeferredGraphTask(
            payload,
            commandList,
            [&](RendererDeferredSystem& deferredSystem, DeferredFrameTargets& targets, Core::CommandList& taskCommandList){
                return deferredSystem.renderDeferredLighting(
                    taskCommandList,
                    targets,
                    payload.useLaggedLightingHistory
                );
            }
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredLightingResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred lighting requires the global descriptor heap"));
        return false;
    }

    if(!m_deferredState.m_sceneShadingBuffer){
        Core::BufferDesc sceneShadingBufferDesc;
        sceneShadingBufferDesc
            .setByteSize(sizeof(ECSRenderDetail::SceneShadingGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(ECSRenderDetail::s_SceneShadingBufferName)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_deferredState.m_sceneShadingBuffer = m_graphics.createBuffer(sceneShadingBufferDesc);
        if(!m_deferredState.m_sceneShadingBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene shading buffer"));
            return false;
        }
    }

    if(!m_deferredState.m_lightBuffer){
        Core::BufferDesc lightBufferDesc;
        lightBufferDesc
            .setByteSize(static_cast<u64>(sizeof(ECSRenderDetail::SceneLightGpuData) * NWB_SCENE_MAX_LIGHTS))
            .setStructStride(sizeof(ECSRenderDetail::SceneLightGpuData))
            .setDebugName(ECSRenderDetail::s_SceneLightBufferName)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_deferredState.m_lightBuffer = m_graphics.createBuffer(lightBufferDesc);
        if(!m_deferredState.m_lightBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene light buffer"));
            return false;
        }
    }

    if(!m_deferredState.m_lightingBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Compute)
        ;
        // The target-generation selector is a UniformBuffer heap entry; the local layout carries its slot plus the
        // effective swap-chain mode so HDR can retain linear values until final presentation.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(__hidden_deferred_lighting::PushConstants)));

        m_deferredState.m_lightingBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_deferredState.m_lightingBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(device, m_deferredState.m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting sampler"));
        return false;
    }

    // The deferred-lighting compute harness includes the cook-generated BXDF dispatch module assembled from every
    // material's `bxdf`. The engine ships no default BXDF and projects do not select a lighting shader -- shading is
    // entirely material-driven (see EmitDeferredBxdfDispatchModule).
    if(!m_shaderSystem.loadShader(
        m_deferredState.m_lightingComputeShader,
        AssetsGraphicsDeferred::s_LightingComputeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_DeferredLightingCS"
    ))
        return false;

    return true;
}

bool RendererDeferredSystem::createDeferredLightingPipeline(){
    if(!createDeferredLightingResources())
        return false;

    if(m_deferredState.m_lightingPipeline)
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_deferredState.m_lightingComputeShader)
        .addBindingLayout(m_deferredState.m_lightingBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    m_deferredState.m_lightingPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_deferredState.m_lightingPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting pipeline"));
        return false;
    }

    return true;
}

Core::GpuTaskId RendererDeferredSystem::declareDeferredLightingTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool useLaggedLightingHistory,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    return graph.addTask<__hidden_deferred_lighting::DeferredLightingGraphTask>(
        desc,
        __hidden_deferred_lighting::DeferredLightingGraphTask::Payload{
            .deferredSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .useLaggedLightingHistory = useLaggedLightingHistory,
        }
    );
}

bool RendererDeferredSystem::prepareSceneShadingBufferUploads(
    const f32 fallbackAspectRatio,
    const RayTracingLightingClassificationInput& rayTracingInput,
    ECSRenderDetail::SceneLightGpuData* const outLightData,
    const usize lightDataCapacity,
    u32& outLightCount,
    RayTracingLightingClassification& outRayTracingClassification,
    bool& outLightUploadRequired,
    ECSRenderDetail::SceneShadingGpuData& outSceneShadingState,
    bool& outSceneShadingUploadRequired
){
    NWB_ASSERT(m_deferredState.m_sceneShadingBuffer);
    NWB_ASSERT(m_deferredState.m_lightBuffer);
    outLightCount = 0u;
    outRayTracingClassification = {};
    outLightUploadRequired = false;
    outSceneShadingUploadRequired = false;
    if(!outLightData || lightDataCapacity < NWB_SCENE_MAX_LIGHTS)
        return false;

    f32 causticLightImportance[NWB_SCENE_MAX_LIGHTS];
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(
        m_world,
        outLightData,
        causticLightImportance,
        NWB_SCENE_MAX_LIGHTS
    );

    // Caustic-light classification: rank the opted-in directional/spot lights and assign a caustic slot into
    // each chosen light's params.w, gated on the scene holding at least one refractive instance gathered earlier by
    // ray-tracing preflight and passed through the root-owned frame contract.
    const u32 causticLightCount = ECSRenderDetail::ResolveCausticLights(
        outLightData,
        causticLightImportance,
        lightCount,
        rayTracingInput.refractiveInstanceCount
    );
    outRayTracingClassification.causticLightCount = causticLightCount;
    // Soft opaque shadow (all light types): record which shadow slots hold a light (params.z >= 0), regardless of type.
    // The soft path traces + denoises + upsamples exactly these slots (once per set bit): a directional light softens by
    // its constant angular radius, a point/spot light by the distance-dependent cone its source sphere subtends -- both
    // handled inside the trace, so every slot light is soft. A light can land on any slot index (the slot allocator
    // ranks by importance, not type), so this is a scattered bitmask, not a contiguous range.
    u32 softShadowSlotMask = 0u;
    for(u32 i = 0u; i < lightCount; ++i){
        const f32 slot = outLightData[i].params.z;
        if(slot >= 0.f){
            const u32 slotIndex = static_cast<u32>(slot);
            if(slotIndex < NWB_SCENE_SHADOW_SLOT_COUNT)
                softShadowSlotMask |= (1u << slotIndex);
        }
    }
    outRayTracingClassification.softShadowSlotMask = softShadowSlotMask;

    const usize lightByteCount = static_cast<usize>(lightCount) * sizeof(ECSRenderDetail::SceneLightGpuData);
    NWB_ASSERT(lightByteCount <= sizeof(m_deferredState.m_lightGpuData));
    const bool lightDataUnchanged =
        m_deferredState.m_lightGpuDataValid
        && m_deferredState.m_lightGpuDataCount == lightCount
        && NWB_MEMCMP(m_deferredState.m_lightGpuData, outLightData, lightByteCount) == 0
    ;
    // A zero-light scene has no copyable payload. The graph still transitions the buffer for a later SRV use,
    // while acceptance records the empty CPU mirror below.
    outLightUploadRequired = !lightDataUnchanged && lightByteCount != 0u;
    outLightCount = lightCount;

    outSceneShadingState = ECSRenderDetail::ResolveSceneShadingState(m_world, fallbackAspectRatio, lightCount);
    outSceneShadingUploadRequired = !(
        m_deferredState.m_sceneShadingGpuDataValid
        && NWB_MEMCMP(
            m_deferredState.m_sceneShadingGpuData,
            &outSceneShadingState,
            sizeof(outSceneShadingState)
        ) == 0
    );
    return true;
}

void RendererDeferredSystem::confirmSceneShadingBufferUploads(
    const ECSRenderDetail::SceneLightGpuData* const lightData,
    const u32 lightCount,
    const bool lightUploadRequired,
    const ECSRenderDetail::SceneShadingGpuData& sceneShadingState,
    const bool sceneShadingUploadRequired
){
    const usize lightByteCount = static_cast<usize>(lightCount) * sizeof(ECSRenderDetail::SceneLightGpuData);
    // A zero-light frame has no blob to upload, but it must still commit its empty CPU mirror once its dependent
    // prefix packet accepts. Otherwise a transition from a nonempty list would be treated as changed forever.
    if(lightUploadRequired || lightCount == 0u){
        NWB_ASSERT(lightData || lightByteCount == 0u);
        if(lightByteCount != 0u){
            NWB_MEMCPY(
                m_deferredState.m_lightGpuData,
                sizeof(m_deferredState.m_lightGpuData),
                lightData,
                lightByteCount
            );
        }
        m_deferredState.m_lightGpuDataCount = lightCount;
        m_deferredState.m_lightGpuDataValid = true;
    }
    if(sceneShadingUploadRequired){
        NWB_MEMCPY(
            m_deferredState.m_sceneShadingGpuData,
            sizeof(m_deferredState.m_sceneShadingGpuData),
            &sceneShadingState,
            sizeof(sceneShadingState)
        );
        m_deferredState.m_sceneShadingGpuDataValid = true;
    }
}

bool RendererDeferredSystem::renderDeferredLighting(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool useLaggedLightingHistory
){
    NWB_ASSERT(m_deferredState.m_lightingPipeline);

    const DeferredLaggedLightingHistoryResources* const laggedHistory = useLaggedLightingHistory
        ? &targets.laggedLightingHistory
        : nullptr
    ;
    if(useLaggedLightingHistory && (!laggedHistory || !laggedHistory->valid()))
        return false;

    const Core::GpuDescriptorHandle resourceSlots = laggedHistory
        ? laggedHistory->slotsBufferDescriptor
        : targets.bindless.slotsBufferDescriptor
    ;

    // The compiled task graph owns every packet-boundary bindless transition, including the lagged-history variant.
    // This thunk keeps only its descriptor upload and native lighting commands.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredLighting, m_graphics.getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(m_deferredState.m_lightingPipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_deferredState.m_lightingPipeline);
    const __hidden_deferred_lighting::PushConstants pushConstants{
        resourceSlots.slot(),
        m_graphics.isHDR10OutputActive()
            ? NWB_DEFERRED_PRESENTATION_HDR10
            : NWB_DEFERRED_PRESENTATION_SDR
    };
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    const u32 groupCountX = (targets.width + NWB_DEFERRED_LIGHTING_GROUP_SIZE - 1u) / NWB_DEFERRED_LIGHTING_GROUP_SIZE;
    const u32 groupCountY = (targets.height + NWB_DEFERRED_LIGHTING_GROUP_SIZE - 1u) / NWB_DEFERRED_LIGHTING_GROUP_SIZE;
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

