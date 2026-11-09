// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/shadow_subsystem.adhd.h>
#include <impl/ecs_render/shadow/shadow_pipeline_set.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <global/algorithm.h>
#include <impl/ecs_render/shadow/software_adaptive_shadow_recorder.h>
#include <impl/ecs_render/shadow/light_space_shadow_diagnostics.adhd.h>
#include <impl/ecs_render/shadow/rt_shadow_tasks.h>
#include <impl/ecs_render/shadow/soft_shadow_temporal_merge_recorder.h>
#include <impl/ecs_render/shadow/soft_shadow_resolve_dispatch_builder.h>
#include <impl/ecs_render/shadow/software_transparent_shadow_trace_recorder.h>
#include <impl/ecs_render/shadow/soft_shadow_wavelet.h>
#include <global/range_algorithms.h>
#include <impl/ecs_render/shadow/quality_settings.adhd.h>
#include "hardware_transparent_shadow_state.h"
#include "hardware_transparent_shadow_resources.h"
#include <impl/assets/graphics/shadow/hardware_transparent_binding_slots.h>
#include "shadow_occluder_material_gather.h"
#include "prepared_shadow_material_context.h"
#include <impl/ecs_render/shadow/shadow_material_context_policy.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPipelineSet::ensureShadowPipeline(){
    if(m_shadow.m_shadowPipeline)
        return true;
    if(m_shadow.m_shadowPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    // Trace layouts are push-only; resources come from the global heap.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(ShadowRqSoftPushConstants), Core::ShaderType::Compute, m_shadow.m_shadowBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowRayqueryCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_ShadowRayQuery", m_shadow.m_shadowShader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_shadow.m_shadowShader, m_shadow.m_shadowBindingLayout, true, m_shadow.m_shadowPipeline)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}

b8 ShadowPipelineSet::ensureShadowSoftPipeline(){
    if(m_softShadow.m_shadowSoftPipeline)
        return true;
    if(m_softShadow.m_shadowSoftPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    // Soft and hard traces share their push-only layout.
    if(!m_shadow.m_shadowBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow binding layout missing for the soft RayQuery pipeline"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowRayquerySoftCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_ShadowRayQuerySoft", m_softShadow.m_shadowSoftShader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_softShadow.m_shadowSoftShader, m_shadow.m_shadowBindingLayout, true, m_softShadow.m_shadowSoftPipeline)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}

b8 ShadowPipelineSet::ensureSwShadowPipeline(){
    if(m_shadow.m_swShadowPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software shadows require the initialized global descriptor heap"));
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }

    if(!m_shadow.m_swShadowBindingLayout){
        if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(SwShadowHeapPushConstants), Core::ShaderType::Compute, m_shadow.m_swShadowBindingLayout)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }

        // Compaction uses a persistent counter and UAV-writable indirect-args buffer.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareShadowEdgeCounter))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto swShadowEdgeCounterBufferResult = m_graphics.createBuffer(edgeCounterDesc);
        if(swShadowEdgeCounterBufferResult)
            m_shadow.m_swShadowEdgeCounterBuffer = Move(*swShadowEdgeCounterBufferResult);
        else
            m_shadow.m_swShadowEdgeCounterBuffer.reset();
        if(!swShadowEdgeCounterBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareShadowIndirectArgs))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto swShadowIndirectArgsBufferResult = m_graphics.createBuffer(indirectArgsDesc);
        if(swShadowIndirectArgsBufferResult)
            m_shadow.m_swShadowIndirectArgsBuffer = Move(*swShadowIndirectArgsBufferResult);
        else
            m_shadow.m_swShadowIndirectArgsBuffer.reset();
        if(!swShadowIndirectArgsBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }
    }

    const b8 heapResourcesReady =
        RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_shadow.m_swShadowEdgeCounterBuffer.get(), m_shadow.m_swShadowEdgeCounterHeapHandle)
        && RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_shadow.m_swShadowIndirectArgsBuffer.get(), m_shadow.m_swShadowIndirectArgsHeapHandle)
    ;
    if(!heapResourcesReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-shadow work buffers in the descriptor heap"));
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }

    const b8 passesReady =
        ensureSwShadowPassPipeline(m_shadow.m_swShadowOpaquePrepassShader, m_shadow.m_swShadowOpaquePrepassPipeline, &ShadowSubsystem::swShadowOpaquePrepassCs, "ECSRender_SwShadowOpaquePrepass")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowSoftOpaqueShader, m_shadow.m_swShadowSoftOpaquePipeline, &ShadowSubsystem::swShadowSoftOpaqueCs, "ECSRender_SwShadowSoftOpaque")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentCoarseShader, m_shadow.m_swShadowTransparentCoarsePipeline, &ShadowSubsystem::swShadowTransparentCoarseCs, "ECSRender_SwShadowTransparentCoarse")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentResolveShader, m_shadow.m_swShadowTransparentResolvePipeline, &ShadowSubsystem::swShadowTransparentResolveCs, "ECSRender_SwShadowTransparentResolve")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentClassifyShader, m_shadow.m_swShadowTransparentClassifyPipeline, &ShadowSubsystem::swShadowTransparentClassifyCs, "ECSRender_SwShadowTransparentClassify")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentBuildArgsShader, m_shadow.m_swShadowTransparentBuildArgsPipeline, &ShadowSubsystem::swShadowTransparentBuildargsCs, "ECSRender_SwShadowTransparentBuildArgs")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentIndirectShader, m_shadow.m_swShadowTransparentIndirectPipeline, &ShadowSubsystem::swShadowTransparentIndirectCs, "ECSRender_SwShadowTransparentIndirect")
        && ensureSoftwareTransparentSamplingPipeline()
    ;
    if(!passesReady){
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

b8 ShadowPipelineSet::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* shaderMember, const NameText<char> debugLabel){
    if(pipeline)
        return true;

    if(!m_shaderSystem.loadShader<ComputeShader>(shaderMember, Core::ShaderArchive::s_DefaultVariant, Name(debugLabel), shader))
        return false;
    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_graphics.getDevice(), shader, m_shadow.m_swShadowBindingLayout, false, pipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}

b8 RayTraceMaterialResources::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // CPU-uploaded material context is shared by the exclusive HW/SW backends.
    if(m_shadow.m_shadowInstanceMaterialBuffer && m_shadow.m_shadowInstanceMaterialCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceMaterialBuffer.get(), m_shadow.m_shadowInstanceMaterialHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowInstanceMaterialCapacity, instanceCount, s_ShadowInstanceMaterialInitialCapacity);

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceInstanceMaterial))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialBufferResult = m_graphics.createBuffer(materialBufferDesc);
    if(!materialBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    Core::BufferHandle materialBuffer = Move(*materialBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*materialBuffer.get(), m_shadow.m_shadowInstanceMaterialHeapHandle))
        return false;
    m_shadow.m_shadowInstanceMaterialBuffer = Move(materialBuffer);
    m_shadow.m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

b8 RayTraceMaterialResources::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow tracing needs an instance record for every gathered occluder.
    if(instanceCount == 0u)
        return !m_shadow.m_shadowInstanceBuffer || ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle);
    if(m_shadow.m_shadowInstanceBuffer && m_shadow.m_shadowInstanceCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceInstanceContext))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto instanceBufferResult = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    Core::BufferHandle instanceBuffer = Move(*instanceBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle))
        return false;
    m_shadow.m_shadowInstanceBuffer = Move(instanceBuffer);
    m_shadow.m_shadowInstanceCapacity = capacity;
    return true;
}

b8 RayTraceMaterialResources::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Keep one word so the heap binding remains valid with no transparent occluders.
    const usize requiredByteCount = ResolveMaterialTypedBufferByteCount(byteCount, sizeof(u32));
    if(m_shadow.m_shadowMaterialTypedBuffer && m_shadow.m_shadowMaterialTypedCapacity >= requiredByteCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowMaterialTypedBuffer.get(), m_shadow.m_shadowMaterialTypedHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceMaterialTyped))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialTypedBufferResult = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    Core::BufferHandle materialTypedBuffer = Move(*materialTypedBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*materialTypedBuffer.get(), m_shadow.m_shadowMaterialTypedHeapHandle))
        return false;
    m_shadow.m_shadowMaterialTypedBuffer = Move(materialTypedBuffer);
    m_shadow.m_shadowMaterialTypedCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // White transmittance is the all-lit fallback.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(*targets.shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

RayTracingShadowVisibilityGraphPlanSnapshot RendererRayTracingSystem::snapshotShadowVisibilityGraphPlan(const b8 hardwareShadowSupported)const noexcept{
    const auto& state = m_rayTracingState;
    const b8 hardwareTransparentTrace = hardwareShadowSupported && state.m_softTransparentReady && hardwareTransparentShadowReady();
    const b8 transparentSceneReady = hardwareTransparentTrace ? state.m_tlas && state.m_tlasHeapHandle.valid() : state.m_sceneBvhNodeBuffer && state.m_sceneInstanceBuffer;
    const b8 softTransparentFoldReady =
        state.m_softShadowReady
        && state.m_softShadowSlotMask != 0u
        && softTransparentShadowReady()
        && transparentSceneReady
        && state.m_shadowInstanceMaterialBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceBuffer
    ;
    GraphOwnedAdaptiveShadowPlan adaptivePlan;
    if(!softTransparentShadowReady() && !hardwareShadowSupported && shadowVisibilitySoftwareResourcesPreflighted() && state.m_swShadowEdgeCounterBuffer){
        adaptivePlan.enabled = true;
        adaptivePlan.compact = true;
    }

    return RayTracingShadowVisibilityGraphPlanSnapshot{
        .lightSpace = lightSpaceShadowSnapshot(),
        .adaptivePlan = adaptivePlan,
        .hardwareTransparentTrace = hardwareTransparentTrace,
        .softTransparentFoldReady = softTransparentFoldReady,
        .combinedSoftUpsample = softTransparentFoldReady
            && state.m_softShadowResolve.m_combinedUpsample.m_pipeline
            && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u,
        .combinedSoftWaveletReady = state.m_softShadowResolve.m_combinedWavelet.m_pipeline
            && state.m_softShadowTemporalReady && state.m_softTransparentTemporalReady,
        .combinedSoftTemporalReady = static_cast<b8>(state.m_softShadowResolve.m_combinedTemporal.m_pipeline),
        .softShadowHistoryReadable = SoftShadowTemporalHistoryUsable(
            state.m_softShadowTemporalReady,
            state.m_prevWorldToClipValid,
            state.m_softShadowTemporalSeeded
        ),
        .opaqueTemporalMergeReady = state.m_softShadowTemporalReady,
        .transparentTemporalMergeReady = state.m_softTransparentTemporalReady,
        .historyFrontIsA = state.m_softShadowHistoryFrontIsA != 0u,
    };
}

Expected<u32> RendererRayTracingSystem::renderGpuBvhShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    if(!m_rayTracingState.m_sceneBvhNodeBuffer || m_rayTracingState.m_sceneBvhInstanceCount == 0u)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_swShadowOpaquePrepassPipeline || m_rayTracingState.m_swShadowMeshCount == 0u)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowCoarseTransmittanceStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.transparentSoftHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-shadow heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Subsequent visibility passes read/write this UAV in place.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);

    const auto makePush = [&]()noexcept{
        return MakeSwShadowHeapPushConstants(
            m_rayTracingState.m_sceneBvhInstanceCount,
            targets.bindless.slotsBufferDescriptor.slot(),
            m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
            targets.bindless.shadowVisibilityStorage.slot(),
            targets.bindless.shadowCoarseTransmittanceStorage.slot(),
            targets.bindless.shadowSoftHalfAStorage.slot(),
            targets.bindless.transparentSoftHalfStorage.slot(),
            m_rayTracingState.m_swShadowEdgeCounterHeapHandle.slot(),
            m_rayTracingState.m_swShadowEdgeListHeapHandle.slot(),
            m_rayTracingState.m_swShadowIndirectArgsHeapHandle.slot(),
            targets.shadowReceiverFactor
        );
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);

    // Skip the fallback after a soft transparent fold.
    b8 softTransparentRan = false;

    // Soft upsample overwrites the opaque prepass.
    const b8 softWillRun = m_rayTracingState.m_softShadowReady && m_rayTracingState.m_softShadowSlotMask != 0u;
    if(!softWillRun){
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants opaquePush = makePush();
        opaquePush.width = targets.width;
        opaquePush.height = targets.height;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowOpaquePrepassPipeline));
        heap.bindCompute(commandList, *m_rayTracingState.m_swShadowOpaquePrepassPipeline.get());
        commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
    }

    // The transparent pass reads and multiplies the opaque mask in place.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    // Soft opaque resolve replaces the full-resolution mask.
    if(softWillRun){
        const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, groupSize);
        const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, groupSize);

        // Advance the primary producer's jitter sequence once.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        // Resolve reads the soft trace and geometry scratch in place.
        enableSoftShadowResolveBarriers(commandList, targets);

        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            SwShadowHeapPushConstants softTracePush = makePush();
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.frameIndex = frameIndex;
            softTracePush.softSampleCount = softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP;
            if(lightSpace && lightSpace->ready){
                NWB_ASSERT(splitSoftTransparentFold);
                if(!RecordLightSpaceResolve(*lightSpace, frameIndex, softTracePush.softSampleCount, targets.bindless.shadowSoftHalfAStorage.slot(), false, commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA))
                    return MakeUnexpected(Failure{});
                LightSpaceShadowLogReporter{}.reportCsgDispatch(
                    (lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u,
                    false,
                    lightSpace->push.instanceCount,
                    m_lightSpaceShadow.m_csgDispatchLogged
                );
                LightSpaceShadowLogReporter{}.reportShadowMapDispatch(m_lightSpaceShadow.m_dispatchLogged);
            }
            else{
                commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowSoftOpaquePipeline));
                heap.bindCompute(commandList, *m_rayTracingState.m_swShadowSoftOpaquePipeline.get());
                commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }
        }

        // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence.
        // Unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        if(splitSoftTransparentFold){
            // The transparent tail records later, so preserve the normal route's one-shot traversal diagnostic first.
            reportSoftwareShadowTraversal(targets);
            return producedFrameIndex;
        }
        softTransparentRan = m_rayTracingState.m_softTransparentReady;
    }

    if(!softTransparentRan)
        SoftwareAdaptiveShadowRecorder::Record(graphOwnedAdaptivePlan, m_rayTracingState, commandList, targets, heap);

    reportSoftwareShadowTraversal(targets);
    return producedFrameIndex;
}

Expected<u32> RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return MakeUnexpected(Failure{});
    return renderGpuBvhShadowVisibility(commandList, targets, deferredLightingResources, true, graphOwnsOpaqueTemporalMergeEntryStates, true, nullptr, lightSpace);
}

void RendererRayTracingSystem::enableSoftShadowResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
    // Temporal merge writes history before later reads.
    if(m_rayTracingState.m_softShadowTemporalReady){
        commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
    }
}

void RendererRayTracingSystem::dispatchPrimarySoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const b8 splitOpaqueSoftResolve,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates
){
    // Split resolve declares the trace fence; the direct route retains its UAV fence.
    if(!splitOpaqueSoftResolve){
        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        true,
        !splitOpaqueSoftResolve,
        !splitSoftTransparentFold,
        !splitSoftTransparentFold,
        false,
        false,
        false,
        graphOwnsOpaqueTemporalMergeEntryStates,
        false,
        !splitOpaqueSoftResolve,
        false
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()noexcept{
    if(m_preparedShadowMaterialContext.publishAcceptedUpload(m_rayTracingState))
        clearPreparedShadowMaterialContext();
}

// The frozen software traversal indexes this context, so a new context always drops it.
b8 RendererRayTracingSystem::capturePreparedShadowMaterialContext(
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const u64 hash,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> instances,
    const Span<const u8> materialTypedBytes
){
    if(!m_preparedShadowMaterialContext.capture(m_rayTracingState, route, staticScene, hash, instanceMaterials, instances, materialTypedBytes))
        return false;
    m_preparedSceneSwBvhTraversal.clear();
    return true;
}

b8 RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse(const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    m_preparedSceneSwBvhTraversal.clear();
    return m_preparedShadowMaterialContext.captureCacheReuse(m_rayTracingState, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
}

void RendererRayTracingSystem::clearPreparedShadowMaterialContext()noexcept{
    m_preparedShadowMaterialContext.clear();
    m_preparedSceneSwBvhTraversal.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityOpaqueTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& prepared,
    const b8 hardwareShadowSupported,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    b8& opaqueProduced,
    u32& opaqueFrameIndex
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
            .lightSpace = lightSpace ? *lightSpace : LightSpaceShadowSnapshot{},
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .prepared = prepared,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityOpaqueFirstWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32& opaqueFrameIndex,
    const b8 hardwareShadowSupported,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 deferUpsample,
    const b8 deferWavelet,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    b8& opaqueProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
            .deferUpsample = deferUpsample,
            .deferWavelet = deferWavelet,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityOpaqueResolveTailTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32& opaqueFrameIndex,
    const b8 hardwareShadowSupported,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    b8& opaqueProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
        });
}

Expected<u32> RendererRayTracingSystem::renderShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return MakeUnexpected(Failure{});
    return renderShadowVisibility(commandList, targets, deferredLightingResources, true, graphOwnsOpaqueTemporalMergeEntryStates, true, lightSpace);
}

b8 RendererRayTracingSystem::renderSoftOpaqueShadowResolvePhase(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 hardwareShadowSupported,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const SoftShadowOpaqueResolvePhase::Enum phase
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u || (phase != SoftShadowOpaqueResolvePhase::TemporalAndWavelet && !m_rayTracingState.m_softShadowTemporalReady))
        return false;
    const u32 groupSize = hardwareShadowSupported ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE) : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, groupSize);
    const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, groupSize);
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        false,
        true,
        false,
        false,
        true,
        false,
        false,
        graphOwnsOpaqueTemporalMergeEntryStates,
        false,
        false,
        true,
        false,
        false,
        false,
        phase
    );
    return true;
}

b8 RendererRayTracingSystem::renderSoftOpaqueShadowResolveTail(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const u32 frameIndex, const b8 hardwareShadowSupported){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const u32 groupSize = hardwareShadowSupported ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE) : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, groupSize);
    const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, groupSize);
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, false, false, false, false, true, false, false, false, false, true, false);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingShadowVisibilityTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;

    payload.opaqueProduced = false;
    payload.opaqueFrameIndex = 0u;
    // A retry must never retain a timestamp whose producer command list is about to be replaced.
    if(payload.shadowVisibilityTiming.has_value()){
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
    }
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().discardTiming();
        payload.asyncTiming.reset();
    }

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(queue->queueClass == Core::CommandQueue::Compute)
        payload.asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncShadow, payload.graphics.getDevice(), commandList);
    payload.shadowVisibilityTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, payload.graphics.getDevice(), commandList);

    b8 opaqueRecorded = false;
    if(payload.prepared){
        const auto opaqueFrame = payload.hardwareShadowSupported
            ? payload.raytracingSystem.renderShadowVisibilityOpaque(commandList, payload.targets, payload.deferredLightingResources, payload.graphOwnsOpaqueTemporalMergeEntryStates, &payload.lightSpace)
            : payload.raytracingSystem.renderGpuBvhShadowVisibilityOpaque(commandList, payload.targets, payload.deferredLightingResources, payload.graphOwnsOpaqueTemporalMergeEntryStates, &payload.lightSpace)
        ;
        opaqueRecorded = opaqueFrame.has_value();
        if(opaqueFrame)
            payload.opaqueFrameIndex = *opaqueFrame;
    }
    if(!opaqueRecorded){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility"));
        payload.raytracingSystem.clearShadowVisibility(commandList, payload.targets);
        // Later callbacks declare this output as UAV. Realign the tracker after the typed fallback clear.
        commandList.setTextureState(payload.targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
        if(payload.asyncTiming.has_value()){
            payload.asyncTiming.value().discardTiming();
            payload.asyncTiming.reset();
        }
        return true;
    }

    payload.opaqueProduced = true;
    // Both timestamp ranges end in the terminal fold callback, but their debug markers must close before this graph task's marker closes.
    const b8 visibilityMarkerFinished = Core::FinishSplitGpuTimingMarker(&payload.shadowVisibilityTiming);
    b8 asyncMarkerFinished = true;
    if(payload.asyncTiming.has_value())
        asyncMarkerFinished = Core::FinishSplitGpuTimingMarker(&payload.asyncTiming);
    if(!asyncMarkerFinished || !visibilityMarkerFinished){
        Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
        Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
        return false;
    }
    return true;
}

void ShadowVisibilityOpaqueGraphTask::Discarded(Payload& payload)noexcept{
    payload.opaqueProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueFirstWaveletGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.opaqueResolveTiming.has_value()){
        payload.opaqueResolveTiming.value().discardTiming();
        payload.opaqueResolveTiming.reset();
    }
    payload.opaqueResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueResolve, payload.graphics.getDevice(), commandList);
    if(payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(
        commandList,
        payload.targets,
        payload.deferredLightingResources,
        payload.opaqueFrameIndex,
        payload.hardwareShadowSupported,
        payload.graphOwnsOpaqueTemporalMergeEntryStates,
        payload.deferWavelet ? SoftShadowOpaqueResolvePhase::TemporalOnly : SoftShadowOpaqueResolvePhase::TemporalAndWavelet
    )){
        if(payload.deferUpsample){
            payload.opaqueResolveTiming.value().finishTiming(commandList);
            payload.opaqueResolveTiming.reset();
            return true;
        }
        return Core::FinishSplitGpuTimingMarker(&payload.opaqueResolveTiming);
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow first wavelet failed; retaining all-lit visibility"));
    RayTracingShadowVisibilityTaskDetail::RecoverOpaqueSoftShadowAllLit(payload, commandList);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueResolveTailGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(!payload.opaqueResolveTiming.has_value())
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.raytracingSystem.renderSoftOpaqueShadowResolveTail(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.hardwareShadowSupported)){
        payload.opaqueResolveTiming.value().finishTiming(commandList);
        payload.opaqueResolveTiming.reset();
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow resolve tail failed; retaining all-lit visibility"));
    RayTracingShadowVisibilityTaskDetail::RecoverOpaqueSoftShadowAllLit(payload, commandList);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftTraceGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    payload.transparentTraceProduced = false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.raytracingSystem.renderSoftTransparentShadowTrace(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, true, &payload.lightSpace)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility"));
        return true;
    }
    payload.transparentTraceProduced = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftTemporalMergeGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    const auto recoverOpaqueTemporal = [&](){
        if(!payload.combinedTemporal)
            return true;
        return payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.hardwareShadowSupported, true, SoftShadowOpaqueResolvePhase::TemporalOnly);
    };
    if(!payload.transparentTraceProduced)
        return recoverOpaqueTemporal();
    if(payload.transparentResolveTiming.has_value()){
        payload.transparentResolveTiming.value().discardTiming();
        payload.transparentResolveTiming.reset();
    }
    payload.transparentResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, payload.graphics.getDevice(), commandList);
    const b8 merged = payload.combinedTemporal
        ? payload.raytracingSystem.renderSoftShadowCombinedTemporalMerge(commandList, payload.targets)
        : payload.raytracingSystem.renderSoftTransparentShadowTemporalMerge(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.graphOwnsTransparentTemporalMergeEntryStates)
    ;
    if(merged)
        return Core::FinishSplitGpuTimingMarker(&payload.transparentResolveTiming);

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow temporal merge failed; preserving opaque visibility"));
    payload.transparentTraceProduced = false;
    payload.transparentResolveTiming.value().discardTiming();
    payload.transparentResolveTiming.reset();
    return recoverOpaqueTemporal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftFirstWaveletGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(payload.combinedWavelet){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(payload.transparentTraceProduced && payload.transparentResolveTiming.has_value()){
            if(payload.raytracingSystem.renderSoftShadowCombinedWavelet(commandList, payload.targets))
                return true;
        }
        // Opaque temporal merge already completed. Recover only its first wavelet, without blending history twice.
        payload.transparentTraceProduced = false;
        Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
        return payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, true, true, SoftShadowOpaqueResolvePhase::WaveletOnly);
    }
    if(!payload.transparentTraceProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.startsTransparentResolveTiming){
        if(payload.transparentResolveTiming.has_value()){
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
        payload.transparentResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, payload.graphics.getDevice(), commandList);
    }
    else if(!payload.transparentResolveTiming.has_value()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope; preserving opaque visibility"));
        payload.transparentTraceProduced = false;
        return true;
    }

    if(payload.raytracingSystem.renderSoftTransparentShadowFirstWavelet(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.graphOwnsTransparentWaveletInputBoundary)){
        if(payload.startsTransparentResolveTiming && !Core::FinishSplitGpuTimingMarker(&payload.transparentResolveTiming))
            return false;
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow first wavelet failed; preserving opaque visibility"));
    payload.transparentTraceProduced = false;
    if(payload.transparentResolveTiming.has_value()){
        payload.transparentResolveTiming.value().discardTiming();
        payload.transparentResolveTiming.reset();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftFoldGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(!payload.shadowVisibilityTiming.has_value() || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value()))
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.transparentTraceProduced){
        if(!payload.transparentResolveTiming.has_value())
            return false;
        const b8 resolved = payload.combinedUpsample
            ? payload.raytracingSystem.renderSoftShadowTerminalUpsample(commandList, payload.targets, payload.deferredLightingResources, true)
            : payload.raytracingSystem.renderSoftTransparentShadowFold(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex)
        ;
        if(resolved){
            payload.transparentResolveTiming.value().finishTiming(commandList);
            payload.transparentResolveTiming.reset();
        }
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility"));
            payload.transparentTraceProduced = false;
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
    }else{
        if(payload.transparentResolveTiming.has_value()){
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace or first wavelet failed; preserving opaque visibility"));
    }

    // Fusion deferred the opaque upsample, so a failed transparent phase must still publish opaque visibility.
    if(payload.combinedUpsample && !payload.transparentTraceProduced){
        if(!payload.raytracingSystem.renderSoftShadowTerminalUpsample(commandList, payload.targets, payload.deferredLightingResources, false))
            return false;
    }

    payload.shadowVisibilityTiming.value().finishTiming(commandList);
    payload.shadowVisibilityTiming.reset();
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().finishTiming(commandList);
        payload.asyncTiming.reset();
    }
    return true;
}

void ShadowTransparentSoftFoldGraphTask::Discarded(Payload& payload)noexcept{
    payload.transparentTraceProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
    payload.raytracingSystem.discardSoftShadowTemporalHistory();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const GraphOwnedAdaptiveShadowPlan& adaptivePlan = payload.graphOwnedAdaptivePlan;
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan = adaptivePlan.enabled ? &adaptivePlan : nullptr;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    Optional<Core::GpuTimingMeasure> asyncTiming;
    if(queue->queueClass == Core::CommandQueue::Compute)
        asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncShadow, payload.graphics.getDevice(), commandList);

    b8 shadowVisibilityWritten = false;
    if(payload.prepared && payload.hardwareShadowSupported){
        shadowVisibilityWritten = payload.raytracingSystem.renderShadowVisibility(commandList, payload.targets, payload.deferredLightingResources).has_value();
        if(!shadowVisibilityWritten)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
    }
    else if(payload.prepared)
        shadowVisibilityWritten = payload.raytracingSystem.renderGpuBvhShadowVisibility(commandList, payload.targets, payload.deferredLightingResources, false, false, false, graphOwnedAdaptivePlan).has_value();
    // The preceding typed white clear remains the all-lit result when no producer records.

    if(asyncTiming){
        asyncTiming->finishTiming(commandList);
        asyncTiming.reset();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& prepared,
    const b8 hardwareShadowSupported,
    const GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .prepared = prepared,
            .graphOwnedAdaptivePlan = graphOwnedAdaptivePlan,
            .hardwareShadowSupported = hardwareShadowSupported,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftTemporalMergeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const b8 graphOwnsTransparentTemporalMergeEntryStates,
    const b8 combinedTemporal,
    const b8 hardwareShadowSupported,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphOwnsTransparentTemporalMergeEntryStates = graphOwnsTransparentTemporalMergeEntryStates,
            .combinedTemporal = combinedTemporal,
            .hardwareShadowSupported = hardwareShadowSupported,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftFirstWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const b8 graphOwnsTransparentWaveletInputBoundary,
    const b8 startsTransparentResolveTiming,
    const b8 combinedWavelet,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphOwnsTransparentWaveletInputBoundary = graphOwnsTransparentWaveletInputBoundary,
            .startsTransparentResolveTiming = startsTransparentResolveTiming,
            .combinedWavelet = combinedWavelet,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftFoldTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const b8 combinedUpsample,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .combinedUpsample = combinedUpsample,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftTraceTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const LightSpaceShadowSnapshot* const lightSpace,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .lightSpace = lightSpace ? *lightSpace : LightSpaceShadowSnapshot{},
            .timingTicket = timingTicket,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .transparentTraceProduced = transparentTraceProduced,
        });
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowTrace(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 graphOwnsOpaqueToTransparentBoundary,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const b8 composeCsg = lightSpace && lightSpace->ready && (lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_HW_COMPOSE) != 0u;
    const b8 replaceTransparent = composeCsg && (lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_NO_ORDINARY_TRANSPARENT) != 0u;
    if(lightSpace && lightSpace->ready && (!composeCsg || replaceTransparent)){
        NWB_ASSERT(graphOwnsOpaqueToTransparentBoundary);
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTrace, m_graphics.getDevice(), commandList);

        const u32 sampleCount = transparentShadowSampleCount();
        if(!RecordLightSpaceResolve(*lightSpace, frameIndex, sampleCount, targets.bindless.transparentSoftHalfStorage.slot(), true, commandList, m_graphics.getDevice().getDescriptorHeap(), m_graphics.gpuTiming(), *targets.transparentSoftHalf))
            return false;
        reportTransparentShadowSampling(sampleCount);
        return true;
    }
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, groups.x, groups.y, false, false, true, false, false, graphOwnsOpaqueToTransparentBoundary, false, false, false, false, false);
    if(composeCsg){
        commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
        commandList.commitBarriers();
        if(!RecordLightSpaceResolve(
            *lightSpace, frameIndex, transparentShadowSampleCount(),
            targets.bindless.transparentSoftHalfStorage.slot(), true,
            commandList, m_graphics.getDevice().getDescriptorHeap(), m_graphics.gpuTiming(), *targets.transparentSoftHalf
        ))
            return false;
    }
    if(!hardwareTransparentShadowReady())
        reportSoftwareShadowTraversal(targets);
    return true;
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowTemporalMerge(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 graphOwnsTransparentTemporalMergeEntryStates
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || !m_rayTracingState.m_softTransparentTemporalReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        groups.x,
        groups.y,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        graphOwnsTransparentTemporalMergeEntryStates,
        false,
        false,
        false,
        false,
        true
    );
    return true;
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowFirstWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 graphOwnsTransparentWaveletInputBoundary
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, groups.x, groups.y, false, false, false, true, false, false, graphOwnsTransparentWaveletInputBoundary, false, false, false, false, false, true, false);
    return true;
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowFold(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const u32 frameIndex){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, groups.x, groups.y, false, false, false, false, false, false, false, false, false, false, false, true);
    return true;
}

void RendererRayTracingSystem::reportSoftwareShadowTraversal(const DeferredFrameTargets& targets){
    LogSoftwareShadowTraversalDispatchIfNeeded(
        targets.width,
        targets.height,
        m_rayTracingState.m_sceneBvhInstanceCount,
        m_rayTracingState.m_swShadowDispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> RendererRayTracingSystem::renderShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    NWB_ASSERT(deferredLightingResources.valid());
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_tlas || !m_rayTracingState.m_shadowPipeline)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !m_rayTracingState.m_tlasHeapHandle.valid()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow trace heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Hardware tracing shares the half-resolution soft-shadow resolve when available.
    if(m_rayTracingState.m_softShadowReady && m_rayTracingState.m_shadowSoftPipeline && m_rayTracingState.m_softShadowSlotMask != 0u){
        const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));


        // Resolve reuses the trace outputs as UAV/SRV scratch.
        enableSoftShadowResolveBarriers(commandList, targets);

        // Advance the jitter sequence once for the primary shadow producer.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            Core::ComputeState softState;
            softState.setPipeline(m_rayTracingState.m_shadowSoftPipeline.get());
            commandList.setComputeState(softState);
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowSoftPipeline.get(), m_rayTracingState.m_tlasHeapHandle);

            const ShadowRqSoftPushConstants softPush = MakeShadowRqSoftPushConstants(
                targets.width,
                targets.height,
                frameIndex,
                targets.bindless.gbufferWorldPosition.slot(),
                targets.bindless.gbufferNormal.slot(),
                targets.bindless.gbufferDepth.slot(),
                targets.bindless.slotsBufferDescriptor.slot(),
                targets.bindless.shadowSoftHalfAStorage.slot(),
                softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.shadowReceiverFactor
            );
            commandList.setPushConstants(&softPush, sizeof(softPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        if(lightSpace && lightSpace->ready){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            if(!RecordLightSpaceResolve(
                *lightSpace,
                frameIndex, softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.bindless.shadowSoftHalfAStorage.slot(), false,
                commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA
            ))
                return MakeUnexpected(Failure{});
            LightSpaceShadowLogReporter{}.reportCsgDispatch(
                true,
                true,
                lightSpace->push.instanceCount,
                m_lightSpaceShadow.m_csgDispatchLogged
            );
        }

        // Split resolve declares the trace fence; unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        return producedFrameIndex;
    }

    // Full-resolution inline-RayQuery fallback.

    {
        Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

        Core::ComputeState shadowState;
        shadowState.setPipeline(m_rayTracingState.m_shadowPipeline.get());
        commandList.setComputeState(shadowState);
        heap.bindCompute(commandList, *m_rayTracingState.m_shadowPipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;
        const ShadowRqPushConstants shadowPush = MakeShadowRqPushConstants(
            frameIndex,
            targets.bindless.gbufferWorldPosition.slot(),
            targets.bindless.gbufferNormal.slot(),
            targets.bindless.gbufferDepth.slot(),
            targets.bindless.slotsBufferDescriptor.slot(),
            targets.bindless.shadowVisibilityStorage.slot()
        );
        commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
        commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), 1u);
    }
    return producedFrameIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const b8 dispatchOpaqueGeometry,
    const b8 dispatchOpaqueResolve,
    const b8 dispatchTransparentTrace,
    const b8 dispatchTransparentResolve,
    const b8 graphOwnsOpaqueGeometryToResolveBoundary,
    const b8 graphOwnsOpaqueToTransparentBoundary,
    const b8 graphOwnsTransparentTraceToResolveBoundary,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 graphOwnsTransparentTemporalMergeEntryStates,
    const b8 dispatchOpaqueResolveTail,
    const b8 graphOwnsOpaqueTraceToFirstWaveletBoundary,
    const b8 dispatchTransparentResolveTail,
    const b8 splitTransparentResolve,
    const b8 dispatchTransparentTemporalMerge,
    const SoftShadowOpaqueResolvePhase::Enum opaquePhase
){
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.sceneShading, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftGeometryStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfBStorage, Core::GpuDescriptorClass::StorageImage)
    )
        return;

    const u32 softHalfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 softHalfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);

    if(dispatchOpaqueGeometry){
        // Graph declares downsample inputs; later soft-shadow transitions stay local.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);

        {
            Core::GpuTimingMeasure geometryTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowGeometryDownsample, m_graphics.getDevice(), commandList);

            const ShadowGeometryDownsamplePushConstants geometryPush = MakeShadowGeometryDownsamplePushConstants(
                targets.width,
                targets.height,
                softHalfWidth,
                softHalfHeight,
                targets.bindless.gbufferWorldPosition.slot(),
                targets.bindless.gbufferNormal.slot(),
                targets.bindless.gbufferDepth.slot(),
                targets.bindless.shadowSoftGeometryStorage.slot(),
                targets.bindless.sceneShading.slot(),
                targets.shadowReceiverFactor
            );
            commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_shadowGeometryDownsamplePipeline));
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowGeometryDownsamplePipeline.get());
            commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }
    }

    if(!dispatchOpaqueResolve && !dispatchOpaqueResolveTail && !dispatchTransparentTrace && !dispatchTransparentResolve && !dispatchTransparentResolveTail && !dispatchTransparentTemporalMerge)
        return;

    const u32 slotRangeCount = ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask);
    if(slotRangeCount == 0u)
        return;

    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const b8 opaqueTemporalActive = m_rayTracingState.m_softShadowTemporalReady;
    const b8 temporalHistoryReadable = softShadowTemporalHistoryUsable();

    SoftShadowTemporalMergeRecorder mergeRecorder(
        m_rayTracingState, slotRangeCount, softHalfWidth, softHalfHeight, softGroupsX, softGroupsY, temporalHistoryReadable,
        commandList, targets, heap
    );

    if(dispatchOpaqueResolve || dispatchOpaqueResolveTail){
        const ShadowReprojectMergeHeapResources opaqueMerge = frontIsA
            ? ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot(),
                targets.bindless.shadowHistBStorage.slot(), targets.bindless.shadowMomentsBStorage.slot()
            }
            : ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot(),
                targets.bindless.shadowHistAStorage.slot(), targets.bindless.shadowMomentsAStorage.slot()
            }
        ;
        // Merge writes the NEXT pair; the wavelet must read that same pair, never stale A.
        if(dispatchOpaqueResolve && opaqueTemporalActive && opaquePhase != SoftShadowOpaqueResolvePhase::WaveletOnly){
            Core::GpuTimingMeasure opaqueTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTemporal, m_graphics.getDevice(), commandList);

            // The graph owns the selected history/moments plus stable previous-geometry/world reads.
            // The geometry downsample above still needs this callback's local UAV-to-SRV transition before the opaque merge samples it.
            mergeRecorder.record(opaqueMerge, graphOwnsOpaqueTraceToFirstWaveletBoundary, graphOwnsOpaqueGeometryToResolveBoundary, graphOwnsOpaqueTemporalMergeEntryStates, graphOwnsOpaqueTemporalMergeEntryStates);
        }

        if(opaquePhase != SoftShadowOpaqueResolvePhase::TemporalOnly){
            // The first wavelet reads this frame's trace or temporal merge directly, preserving its exact filtering input.
            const OpaqueSoftShadowResolveOptions opaqueOptions{
                .frontIsA = frontIsA,
                .temporalActive = opaqueTemporalActive,
                .graphOwnsGeometryEntry = graphOwnsOpaqueGeometryToResolveBoundary,
                .graphOwnsTraceToFirstWavelet = graphOwnsOpaqueTraceToFirstWaveletBoundary,
                .dispatchResolve = dispatchOpaqueResolve,
                .dispatchResolveTail = dispatchOpaqueResolveTail,
                .phase = opaquePhase,
            };
            const SoftShadowResolveDispatch opaqueDispatch = SoftShadowResolveDispatchBuilder::Opaque(opaqueOptions, m_rayTracingState, targets);
            if(dispatchOpaqueResolve && dispatchOpaqueResolveTail){
                Core::GpuTimingMeasure opaqueResolveTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueResolve, m_graphics.getDevice(), commandList);

                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch);
            }
            else if(dispatchOpaqueResolve)
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch, true, false);
            else
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch, false, true);
        }
    }

    if(dispatchTransparentTrace && m_rayTracingState.m_softTransparentReady){
        {
            Core::GpuTimingMeasure transparentTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTrace, m_graphics.getDevice(), commandList);

            if(hardwareTransparentShadowReady())
                dispatchHardwareTransparentShadow(commandList, targets, frameIndex);
            else{
                const SoftwareTransparentShadowTraceInputs traceInputs{
                    .frameIndex = frameIndex,
                    .sampleCount = transparentShadowSampleCount(),
                    .groupsX = softGroupsX,
                    .groupsY = softGroupsY,
                    .graphOwnsOpaqueToTransparentBoundary = graphOwnsOpaqueToTransparentBoundary,
                    .temporalHistoryReadable = temporalHistoryReadable,
                    .historyFrontIsA = frontIsA,
                };
                SoftwareTransparentShadowTraceRecorder::Record(traceInputs, m_rayTracingState, commandList, targets, heap);
                reportTransparentShadowSampling(traceInputs.sampleCount);
            }
        }
    }

    const b8 dispatchTransparentWavelet = dispatchTransparentResolve || dispatchTransparentResolveTail;
    const b8 dispatchTransparentMerge = dispatchTransparentTemporalMerge || (dispatchTransparentResolve && !splitTransparentResolve);
    if((dispatchTransparentMerge || dispatchTransparentWavelet) && m_rayTracingState.m_softTransparentReady){
        const b8 transparentTemporalActive = m_rayTracingState.m_softTransparentTemporalReady;
        const ShadowReprojectMergeHeapResources transparentMerge = frontIsA
            ? ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot(),
                targets.bindless.transparentHistBStorage.slot(), targets.bindless.transparentMomentsBStorage.slot()
            }
            : ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot(),
                targets.bindless.transparentHistAStorage.slot(), targets.bindless.transparentMomentsAStorage.slot()
            }
        ;
        // Keep the RGB wavelet's temporal variance paired with the history just emitted by the transparent merge.
        if(dispatchTransparentMerge && transparentTemporalActive){
            Core::GpuTimingMeasure transparentTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTemporal, m_graphics.getDevice(), commandList);

            mergeRecorder.record(transparentMerge, graphOwnsTransparentTraceToResolveBoundary, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates);
            m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
        }

        if(dispatchTransparentWavelet){
            const TransparentSoftShadowResolveOptions transparentOptions{
                .frontIsA = frontIsA,
                .temporalActive = transparentTemporalActive,
                .dispatchResolve = dispatchTransparentResolve,
                .dispatchResolveTail = dispatchTransparentResolveTail,
                .splitResolve = splitTransparentResolve,
                .dispatchTemporalMerge = dispatchTransparentTemporalMerge,
                .graphOwnsTraceToResolve = graphOwnsTransparentTraceToResolveBoundary,
            };
            const SoftShadowResolveDispatch transparentDispatch = SoftShadowResolveDispatchBuilder::Transparent(transparentOptions, m_rayTracingState, targets);
            if(dispatchTransparentResolve && !splitTransparentResolve){
                Core::GpuTimingMeasure transparentResolveTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, m_graphics.getDevice(), commandList);

                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch);
            }
            else if(dispatchTransparentResolve)
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch, true, false);
            else
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch, false, true);
        }
    }

    // Do not mutate the target-generation handles while the sibling caustics and surfel-GI workers can still validate targets.bindless.
    // RendererFramePipeline finalizes this pending CPU-side swap only after its complete ordered Graphics submission succeeds.
    if(((dispatchTransparentResolve && !splitTransparentResolve) || dispatchTransparentResolveTail) && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::softShadowTemporalHistoryUsable()const noexcept{
    return SoftShadowTemporalHistoryUsable(m_rayTracingState.m_softShadowTemporalReady, m_rayTracingState.m_prevWorldToClipValid, m_rayTracingState.m_softShadowTemporalSeeded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_softshadow_pipelines{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 EnsureResolveChannel(
    const Core::BindingLayoutHandle& bindingLayout,
    const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* member,
    RendererShaderSystem& shaderSystem,
    Core::GraphicsBackend::Device& device,
    SoftShadowResolveChannelState& channel
){
    struct StageRequest{
        AStringView m_variant;
        SoftShadowResolveStageState& m_state;
    };
    const Array<StageRequest, 2u> stages = {{
        { s_ShadowResolveWaveletVariant, channel.m_wavelet },
        { s_ShadowResolveUpsampleVariant, channel.m_upsample },
    }};
    static_assert(NWB_SHADOW_RESOLVE_STAGE_WAVELET == 1u && NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE == 2u);
    for(const StageRequest& stage : stages){
        if(stage.m_state.m_pipeline)
            continue;
        if(
            !shaderSystem.loadShader<ComputeShader>(member, stage.m_variant, "ECSRender_SoftShadowResolve", stage.m_state.m_shader)
            || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, stage.m_state.m_shader, bindingLayout, false, stage.m_state.m_pipeline)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create specialized shadow resolve pipeline"));
            channel.m_failed = true;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept{
    if(!m_rayTracingState.m_softShadowTemporalHistoryAdvancePending)
        return;

    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    swapSoftShadowTemporalHistory(targets);
}

void RendererRayTracingSystem::discardSoftShadowTemporalHistory()noexcept{
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    m_rayTracingState.m_transparentShadowSamplingHistory.discardPending();
}

b8 ShadowPipelineSet::ensureSoftShadowResolvePipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_scalar.m_wavelet.m_pipeline && resolve.m_scalar.m_upsample.m_pipeline)
        return true;
    if(resolve.m_scalar.m_failed)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized())
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(ShadowResolvePushConstants), Core::ShaderType::Compute, resolve.m_bindingLayout)){
        resolve.m_scalar.m_failed = true;
        return false;
    }
    return __hidden_softshadow_pipelines::EnsureResolveChannel(resolve.m_bindingLayout, &ShadowSubsystem::shadowResolveCs, m_shaderSystem, device, resolve.m_scalar);
}

b8 ShadowPipelineSet::ensureShadowGeometryDownsamplePipeline(){
    if(m_softShadow.m_shadowGeometryDownsamplePipeline)
        return true;
    if(m_softShadow.m_shadowGeometryDownsamplePipelineFailed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowGeometryDownsampleCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_ShadowGeometryDownsample",
        sizeof(ShadowGeometryDownsamplePushConstants),
        false,
        m_softShadow.m_shadowGeometryDownsampleBindingLayout,
        m_softShadow.m_shadowGeometryDownsampleShader,
        m_softShadow.m_shadowGeometryDownsamplePipeline
    )){
        m_softShadow.m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

b8 ShadowPipelineSet::ensureSoftTransparentResolvePipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_rgb.m_wavelet.m_pipeline && resolve.m_rgb.m_upsample.m_pipeline)
        return true;
    if(resolve.m_rgb.m_failed || !resolve.m_bindingLayout)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized())
        return false;
    return __hidden_softshadow_pipelines::EnsureResolveChannel(resolve.m_bindingLayout, &ShadowSubsystem::shadowResolveRgbCs, m_shaderSystem, device, resolve.m_rgb);
}

// Both trace backends publish the same independent channel textures and use the same optional resolve pipelines.
void ShadowPipelineSet::prepareSoftCombinedResolvePipelines(){
    if(!m_softShadow.m_softShadowReady || !m_softShadow.m_softTransparentReady)
        return;
    if(!ensureSoftCombinedUpsamplePipeline())
        m_shadow.m_softShadowResolve.m_combinedUpsampleFailed = true;
    if(!ensureSoftCombinedWaveletPipeline())
        m_shadow.m_softShadowResolve.m_combinedWaveletFailed = true;
    if(!ensureSoftCombinedTemporalPipeline())
        m_shadow.m_softShadowResolve.m_combinedTemporalFailed = true;
}

void RendererRayTracingSystem::dispatchSoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 slotStart,
    const u32 slotCount,
    const SoftShadowResolveDispatch& dispatch,
    const b8 dispatchFirstWavelet,
    const b8 dispatchTail
){
    NWB_ASSERT(dispatch.waveletPipeline && dispatch.upsamplePipeline);
    NWB_ASSERT(dispatch.visibilityTexture);
    const u32 halfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 halfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    const auto runPass = [&](
        const SoftShadowResolvePassResources& resources, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY, const b8 graphOwnsInputColorState = false, const b8 graphOwnsOutputState = false
    ){
        NWB_ASSERT(resources.inputColorTexture && resources.momentsTexture && resources.outputTexture);
        switch(stage){
            case ShadowResolveStage::Wavelet:
                if(!dispatch.graphOwnsWaveletGeometryEntryState)
                    commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(dispatch.temporalMomentsValid && !dispatch.graphOwnsWaveletMomentsEntryState)
                    commandList.setTextureState(resources.momentsTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(resources.outputTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(resources.outputTexture, true);
                break;
            case ShadowResolveStage::Upsample:
                if(!dispatch.graphOwnsUpsampleStaticEntryStates){
                    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
                }
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(dispatch.visibilityTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(dispatch.visibilityTexture, true);
                break;
        }
        commandList.commitBarriers();

        const ShadowResolvePushConstants push = MakeShadowResolvePassPushConstants(
            targets.shadowReceiverFactor,
            targets.width,
            targets.height,
            halfWidth,
            halfHeight,
            stepWidth,
            stage,
            slotStart,
            slotCount,
            dispatch.temporalMomentsValid,
            dispatch.fold,
            bindless.shadowSoftGeometry.slot(),
            bindless.gbufferDepth.slot(),
            bindless.gbufferWorldPosition.slot(),
            bindless.gbufferNormal.slot(),
            resources.inputColor,
            resources.moments,
            resources.outputStorage,
            dispatch.visibilityStorage,
            dispatch.sceneShading
        );

        Core::ComputePipeline& pipeline = stage == ShadowResolveStage::Upsample ? *dispatch.upsamplePipeline : *dispatch.waveletPipeline;
        Core::ComputeState state;
        state.setPipeline(&pipeline);
        commandList.setComputeState(state);
        heap.bindCompute(commandList, pipeline);
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    static_assert((NWB_SHADOW_RESOLVE_PASS_COUNT % 2) == 1, "opaque resolve pass count must be odd");
    static_assert((NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT % 2) == 1, "transparent resolve pass count must be odd");
    NWB_ASSERT(dispatch.waveletPassCount != 0u && (dispatch.waveletPassCount % 2u) == 1u);
    NWB_ASSERT(dispatchFirstWavelet || dispatchTail);

    if(dispatchFirstWavelet)
        runPass(dispatch.firstWaveletResources, 1u, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY, dispatch.graphOwnsFirstWaveletInputState, dispatch.graphOwnsFirstWaveletOutputState);
    if(!dispatchTail)
        return;

    b8 sourceIsHalfA = dispatch.firstWaveletWritesHalfA;
    for(u32 pass = 1u; pass < dispatch.waveletPassCount; ++pass){
        const SoftShadowResolvePassResources& nextWaveletResources = sourceIsHalfA ? dispatch.outputHalfBResources : dispatch.outputHalfAResources;
        runPass(nextWaveletResources, 1u << pass, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        sourceIsHalfA = !sourceIsHalfA;
    }
    NWB_ASSERT(dispatch.upsampleResources.inputColorTexture == (dispatch.waveletPassCount == 1u ? dispatch.firstWaveletResources : (sourceIsHalfA ? dispatch.outputHalfAResources : dispatch.outputHalfBResources)).outputTexture);
    runPass(dispatch.upsampleResources, 1u, ShadowResolveStage::Upsample, fullGroupsX, fullGroupsY, dispatch.graphOwnsUpsampleInputColorEntryState, dispatch.graphOwnsUpsampleVisibilityOutputState);
}

b8 ShadowPipelineSet::ensureShadowReprojectMergePipeline(){
    if(m_softShadow.m_shadowReprojectMergePipeline)
        return true;
    if(m_softShadow.m_shadowReprojectMergePipelineFailed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowReprojectMergeCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SoftShadowReprojectMerge",
        sizeof(ShadowReprojectMergePushConstants),
        false,
        m_softShadow.m_shadowReprojectMergeBindingLayout,
        m_softShadow.m_shadowReprojectMergeShader,
        m_softShadow.m_shadowReprojectMergePipeline
    )){
        m_softShadow.m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    return true;
}

void RendererRayTracingSystem::swapSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept{
    if(!m_rayTracingState.m_softShadowTemporalReady)
        return;

    const auto acceptedWorldToClip = m_meshSystem.snapshotAcceptedMeshViewWorldToClip();
    if(acceptedWorldToClip){
        m_rayTracingState.m_prevWorldToClip = *acceptedWorldToClip;
        m_rayTracingState.m_prevWorldToClipValid = true;
    }
    if(m_rayTracingState.m_softTransparentTemporalReady && !hardwareTransparentShadowReady())
        m_rayTracingState.m_softwareTransparentSampling.m_history.accept();
    else
        m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_transparentShadowSamplingHistory.accept();
    m_rayTracingState.m_softShadowTemporalSeeded = true;
    Swap(targets.shadowSoftGeometry, targets.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometry, targets.bindless.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometryStorage, targets.bindless.shadowSoftGeometryPrevStorage);
    m_rayTracingState.m_softShadowHistoryFrontIsA ^= 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowCombinedTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(
        !resolve.m_combinedTemporal.m_pipeline
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !m_rayTracingState.m_softShadowTemporalReady || !m_rayTracingState.m_softTransparentTemporalReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const Array<Core::Texture*, 4> outputs = {
        frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get(),
        frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get(),
        frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get(),
        frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get(),
    };
    // This graph-only pass inherits both raw inputs and all four selected history/moment output states.
    for(Core::Texture* const output : outputs)
        commandList.setEnableUavBarriersForTexture(output, true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowCombinedTemporalPushConstants push = MakeShadowCombinedTemporalPushConstants(
        LoadFloat(m_rayTracingState.m_prevWorldToClip),
        targets.shadowReceiverFactor,
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        softShadowTemporalHistoryUsable(),
        bindless.shadowSoftGeometry.slot(),
        bindless.shadowSoftGeometryPrev.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.shadowSoftHalfA.slot(),
        frontIsA ? bindless.shadowHistA.slot() : bindless.shadowHistB.slot(),
        frontIsA ? bindless.shadowMomentsA.slot() : bindless.shadowMomentsB.slot(),
        frontIsA ? bindless.shadowHistBStorage.slot() : bindless.shadowHistAStorage.slot(),
        frontIsA ? bindless.shadowMomentsBStorage.slot() : bindless.shadowMomentsAStorage.slot(),
        bindless.transparentSoftHalf.slot(),
        frontIsA ? bindless.transparentHistA.slot() : bindless.transparentHistB.slot(),
        frontIsA ? bindless.transparentMomentsA.slot() : bindless.transparentMomentsB.slot(),
        frontIsA ? bindless.transparentHistBStorage.slot() : bindless.transparentHistAStorage.slot(),
        frontIsA ? bindless.transparentMomentsBStorage.slot() : bindless.transparentMomentsAStorage.slot()
    );
    Core::ComputeState state;
    state.setPipeline(resolve.m_combinedTemporal.m_pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resolve.m_combinedTemporal.m_pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowCombinedTemporal, m_graphics.getDevice(), commandList);

    commandList.dispatch(
        DivideUp(DivideUp(targets.width, targets.shadowReceiverFactor), static_cast<u32>(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE)),
        DivideUp(DivideUp(targets.height, targets.shadowReceiverFactor), static_cast<u32>(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE)),
        1u
    );
    m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedTemporalPipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedTemporal.m_pipeline)
        return true;
    if(resolve.m_combinedTemporalFailed)
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowReprojectMergeCombinedCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CombinedShadowTemporal",
        sizeof(ShadowCombinedTemporalPushConstants),
        false,
        resolve.m_combinedTemporalBindingLayout,
        resolve.m_combinedTemporal.m_shader,
        resolve.m_combinedTemporal.m_pipeline
    )){
        resolve.m_combinedTemporalFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowTerminalUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const b8 transparentReady){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const Core::ComputePipelineHandle& pipeline = transparentReady ? resolve.m_combinedUpsample.m_pipeline : resolve.m_scalar.m_upsample.m_pipeline;
    if(
        !pipeline
        || !m_rayTracingState.m_softShadowReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
        || NWB_SHADOW_RESOLVE_PASS_COUNT != 1u
        || NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT != 1u
        || !deferredLightingResources.valid()
        || !targets.bindless.valid()
        || !targets.depth
        || !targets.worldPosition
        || !targets.normal
        || !targets.shadowSoftHalfB
        || !targets.shadowSoftGeometry
        || !targets.shadowVisibility
        || (transparentReady && (!m_rayTracingState.m_softTransparentReady || !targets.shadowSoftHalfA))
    )
        return false;

    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowResolvePushConstants push = MakeShadowTerminalUpsamplePushConstants(
        targets.width,
        targets.height,
        DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor)),
        DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor)),
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        bindless.shadowSoftGeometry.slot(),
        bindless.gbufferDepth.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.gbufferNormal.slot(),
        transparentReady ? bindless.shadowSoftHalfA.slot() : bindless.shadowSoftHalfB.slot(),
        bindless.shadowVisibilityStorage.slot(),
        bindless.shadowVisibilityStorage.slot(),
        bindless.sceneShading.slot(),
        bindless.shadowSoftHalfB.slot(),
        targets.shadowReceiverFactor
    );
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), 1u);
    // The original terminal fold is the only successful history-advance endpoint. Opaque recovery does not advance it.
    if(transparentReady && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedUpsamplePipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedUpsample.m_pipeline)
        return true;
    if(resolve.m_combinedUpsampleFailed || !resolve.m_bindingLayout)
        return false;
    auto& device = m_graphics.getDevice();
    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowResolveCombinedCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CombinedShadowUpsample", resolve.m_combinedUpsample.m_shader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, resolve.m_combinedUpsample.m_shader, resolve.m_bindingLayout, false, resolve.m_combinedUpsample.m_pipeline)
    ){
        resolve.m_combinedUpsampleFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SoftShadowCombinedWaveletInputs::valid()const noexcept{
    const Array<const Core::Texture*, 7> textures = { opaqueHistory, opaqueMoments, transparentHistory, transparentMoments, geometry, opaqueOutput, transparentOutput };
    return AllValidAndDistinct(Span<const Core::Texture* const>(textures.data(), textures.size()), [](const Core::Texture* texture)noexcept{ return texture != nullptr; }, [](const Core::Texture* current, const Core::Texture* previous)noexcept{ return current == previous; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowCombinedWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    if(
        !CanCombineSoftShadowWavelets(
            static_cast<b8>(resolve.m_combinedUpsample.m_pipeline),
            static_cast<b8>(resolve.m_combinedWavelet.m_pipeline),
            m_rayTracingState.m_softShadowTemporalReady,
            m_rayTracingState.m_softTransparentTemporalReady,
            NWB_SHADOW_RESOLVE_PASS_COUNT,
            NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
        )
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    // This graph-only phase inherits both history/moments reads and both distinct output UAV states.
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
    commandList.commitBarriers();
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowCombinedWaveletPushConstants push = MakeShadowCombinedWaveletPushConstants(
        targets.width,
        targets.height,
        DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor)),
        DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor)),
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        bindless.shadowSoftGeometry.slot(),
        frontIsA ? bindless.transparentHistB.slot() : bindless.transparentHistA.slot(),
        frontIsA ? bindless.transparentMomentsB.slot() : bindless.transparentMomentsA.slot(),
        bindless.shadowSoftHalfAStorage.slot(),
        frontIsA ? bindless.shadowHistB.slot() : bindless.shadowHistA.slot(),
        frontIsA ? bindless.shadowMomentsB.slot() : bindless.shadowMomentsA.slot(),
        bindless.shadowSoftHalfBStorage.slot(),
        targets.shadowReceiverFactor
    );
    Core::ComputeState state;
    state.setPipeline(resolve.m_combinedWavelet.m_pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resolve.m_combinedWavelet.m_pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(DivideUp(push.resolve.halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), DivideUp(push.resolve.halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), 1u);
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedWaveletPipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedWavelet.m_pipeline)
        return true;
    if(resolve.m_combinedWaveletFailed)
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowResolveWaveletCombinedCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CombinedShadowWavelet",
        sizeof(ShadowCombinedWaveletPushConstants),
        false,
        resolve.m_combinedWaveletBindingLayout,
        resolve.m_combinedWavelet.m_shader,
        resolve.m_combinedWavelet.m_pipeline
    )){
        resolve.m_combinedWaveletFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept{
    if(!ValidateShadowQualitySettings(settings))
        return false;
    if(SameShadowQualitySettings(m_shadowQualitySettings, settings))
        return true;
    ApplyShadowQualitySettingsChange(
        settings,
        m_shadowQualitySettings,
        m_temporalOneShadowSamplingLogged,
        m_rayTracingState.m_transparentShadowSamplingHistory,
        m_rayTracingState.m_softShadowTemporalSeeded,
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending
    );
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

b8 ShadowPipelineSet::ensureSoftwareTransparentSamplingPipeline(){
    auto& sampling = m_shadow.m_softwareTransparentSampling;
    if(sampling.m_pipelines[0] && sampling.m_pipelines[1])
        return true;
    auto& device = m_graphics.getDevice();
    const Array<AStringView, 2u> variants = { s_SwTransparentAdaptiveDisabledVariant, s_SwTransparentAdaptiveEnabledVariant };
    for(u32 index = 0u; index < variants.size(); ++index){
        if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
            m_arena,
            device,
            m_shaderSystem,
            &ShadowSubsystem::swShadowTransparentSoftCs,
            variants[index],
            Name(s_SoftwareShadowShaderScope),
            sizeof(SoftwareTransparentSamplingPush),
            false,
            sampling.m_layout,
            sampling.m_shaders[index],
            sampling.m_pipelines[index]
        ))
            return false;
    }
    return true;
}

u32 RendererRayTracingSystem::transparentShadowSampleCount()const noexcept{
    const b8 historyUsable = m_rayTracingState.m_softTransparentTemporalReady && softShadowTemporalHistoryUsable() && m_rayTracingState.m_transparentShadowSamplingHistory.covers(m_rayTracingState.m_softShadowSlotMask);
    return ResolveTransparentShadowSampleCount(m_shadowQualitySettings, historyUsable);
}

void RendererRayTracingSystem::reportTransparentShadowSampling(const u32 sampleCount){
    LogTransparentShadowSamplingIfNeeded(sampleCount, hardwareTransparentShadowReady(), m_temporalOneShadowSamplingLogged);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(HardwareTransparentShadowPushConstants) == NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES);
static_assert(offsetof(HardwareTransparentShadowPushConstants, receiverFactor) == s_HwTransparentReceiverFactorByteOffset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 HardwareTransparentShadowResources::prepareHardwareTransparentShadowResources(DeferredFrameTargets& targets){
    auto& state = m_state;
    state.m_ready = false;
    if(state.m_pipelineFailed)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout())
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(HardwareTransparentShadowPushConstants), Core::ShaderType::Compute, state.m_bindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow binding layout"));
        state.m_pipelineFailed = true;
        return false;
    }
    const Array<const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::*, 3u> shaderMembers = {
        &ShadowSubsystem::hardwareTransparentGatherCs,
        &ShadowSubsystem::hardwareTransparentEvaluateCs,
        &ShadowSubsystem::hardwareTransparentOverflowCs,
    };
    for(u32 i = 0u; i < LengthOf(state.m_pipelines); ++i){
        if(state.m_pipelines[i])
            continue;
        if(!m_shaderSystem.loadShader<ComputeShader>(shaderMembers[i], Core::ShaderArchive::s_DefaultVariant, "ECSRender_HardwareTransparentShadow", state.m_shaders[i])){
            state.m_pipelineFailed = true;
            return false;
        }
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, state.m_shaders[i], state.m_bindingLayout, true, state.m_pipelines[i])){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow pipeline {}"), i);
            state.m_pipelineFailed = true;
            return false;
        }
    }

    u32 pixels = 0u;
    Array<u64, 3u> sizes{};
    if(!ValidateHardwareTransparentShadowScratchExtent(
        targets.width,
        targets.height,
        targets.shadowReceiverFactor,
        NWB_HW_TRANSPARENT_WORDS_PER_RAY,
        NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS,
        pixels,
        sizes
    ))
        return false;
    if(pixels > state.m_pixelsCapacity){
        const Array<Name, 3u> names = { Name(s_HardwareShadowCrossings), Name(s_HardwareShadowOverflowList), Name(s_HardwareShadowOverflowArgs), };
        Array<Core::BufferHandle, s_HwTransparentScratchBufferCount> buffers{};
        Array<Core::GpuDescriptorHandle, s_HwTransparentScratchBufferCount> descriptors{};
        Core::GpuDescriptorRetirementGuard retireUnpublished(heap, Span<const Core::GpuDescriptorHandle>(descriptors));
        for(u32 i = 0u; i < buffers.size(); ++i){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizes[i])
                .setStructStride(sizeof(u32))
                .setCanHaveUAVs(true)
                .setIsDrawIndirectArgs(i == s_HwTransparentOverflowArgsBufferIndex)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(names[i])
                .enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess)
            ;
            auto buffersResult = m_graphics.createBuffer(desc);
            if(buffersResult)
                buffers[i] = Move(*buffersResult);
            else
                buffers[i].reset();
            if(!buffersResult){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
            const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *buffers[i], Core::GpuDescriptorClass::StorageBuffer, true);
            if(!descriptor){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
            descriptors[i] = *descriptor;
        }
        RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
        state.m_crossingsBuffer = Move(buffers[s_HwTransparentCrossingsBufferIndex]);
        state.m_overflowListBuffer = Move(buffers[s_HwTransparentOverflowListBufferIndex]);
        state.m_overflowArgsBuffer = Move(buffers[s_HwTransparentOverflowArgsBufferIndex]);
        state.m_crossingsHeapHandle = descriptors[s_HwTransparentCrossingsBufferIndex];
        state.m_overflowListHeapHandle = descriptors[s_HwTransparentOverflowListBufferIndex];
        state.m_overflowArgsHeapHandle = descriptors[s_HwTransparentOverflowArgsBufferIndex];
        for(auto& handle : descriptors)
            handle = Core::GpuDescriptorHandle::Invalid();
        state.m_pixelsCapacity = static_cast<u32>(pixels);
    }
    state.m_ready = true;
    return true;
}

void HardwareTransparentShadowResources::releaseHardwareTransparentShadowResources(){
    auto& state = m_state;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
    state = HardwareTransparentShadowState{};
}

b8 RendererRayTracingSystem::hardwareTransparentShadowReady()const noexcept{
    return m_shadowVisibilityHardwareSupported && m_rayTracingState.m_hardwareTransparentShadow.m_ready;
}

void RendererRayTracingSystem::dispatchHardwareTransparentShadow(Core::CommandList& commandList, DeferredFrameTargets& targets, const u32 frameIndex){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    NWB_ASSERT(state.m_ready);
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor));
    const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor));
    const u32 groupsX = DivideUp(halfWidth, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 groupsY = DivideUp(halfHeight, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 sampleCount = transparentShadowSampleCount();
    HardwareTransparentShadowPushConstants push = ResolveHardwareTransparentShadowPushConstants(
        targets.width,
        targets.height,
        frameIndex,
        sampleCount,
        targets.bindless.slotsBufferDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        state.m_crossingsHeapHandle.slot(),
        state.m_overflowListHeapHandle.slot(),
        state.m_overflowArgsHeapHandle.slot(),
        targets.bindless.transparentSoftHalfStorage.slot(),
        targets.shadowReceiverFactor
    );
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) == 0u)
            continue;
        push.lightSlot = slot;
        for(u32 sample = 0u; sample < sampleCount; ++sample){
            push.sampleIndex = sample;
            const Array<u32, NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS> emptyArgs = { 0u, 1u, 1u, 0u };
            if(!commandList.tryWriteBuffer(*state.m_overflowArgsBuffer, emptyArgs.data(), sizeof(emptyArgs))){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reset hardware transparent-shadow continuation arguments"));
                return;
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            const auto bind = [&](const u32 stage, const b8 indirect){
                Core::ComputeState compute;
                compute.setPipeline(state.m_pipelines[stage].get());
                if(indirect)
                    compute.setIndirectParams(state.m_overflowArgsBuffer.get());
                commandList.setComputeState(compute);
                heap.bindCompute(commandList, *state.m_pipelines[stage], m_rayTracingState.m_tlasHeapHandle);
                commandList.setPushConstants(&push, sizeof(push));
            };
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentGather, device, commandList);

                bind(s_HwTransparentGatherStage, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::IndirectArgument | Core::ResourceStates::ShaderResource);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentEvaluate, device, commandList);

                bind(s_HwTransparentEvaluateStage, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            // Fast evaluation and continuation write disjoint pixels, but the next sample reads their accumulated result.
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentContinuation, device, commandList);

                bind(s_HwTransparentOverflowStage, true);
                commandList.dispatchIndirect(0u);
            }
        }
    }
    if(m_rayTracingState.m_softShadowSlotMask != 0u)
        reportTransparentShadowSampling(sampleCount);
    // The trace task exports scratch in its declared UAV state, including the indirect-argument allocation.
    commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    LogHardwareTransparentShadowDispatchIfNeeded(
        targets.width,
        targets.height,
        m_rayTracingState.m_tlasInstanceCount,
        state.m_dispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowOccluderMaterialGather::resolve(const Core::ECS::EntityID entity, const Core::Assets::AssetRef<Material>& material, const Scene::TransformComponent* const transform, const u32 meshInstanceIndex, NwbRtInstanceMaterialGpu& outInstanceMaterial, InstanceGpuData& outShadowInstance, b8* const outSceneHasTransparentOccluder){
    outInstanceMaterial = {};
    outShadowInstance = {};
    MaterialSurfaceInfo* materialInfo = nullptr;
    if(const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(material); materialInfoResult){
        materialInfo = materialInfoResult->get();
        if(materialInfo->transparent && outSceneHasTransparentOccluder)
            *outSceneHasTransparentOccluder = true;
        // Trace dispatcher reads Texture2D fields via non-uniform slots. Retain resolved handles in preflight.
        const b8 sampledTexturesReady = materialInfo->surfaceDispatchId == Limit<u32>::s_Max || m_sampledTextureCollector.collect(
            *materialInfo,
            [this](const MaterialSurfaceInfo& surface, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
                return AppendPreparedMaterialSurfaceSampledTextures(surface, m_materialSystem.materialState().m_resourceState, pending);
            }
        );
        if(!sampledTexturesReady)
            return false;
        const auto materialContext = m_materialSystem.appendShadowOccluderMaterialContext(entity, *materialInfo, transform, m_shadowMaterialTypedBytes, m_shadowConstantTypedRanges, m_shadowMutableTypedRanges);
        if(!materialContext)
            return false;
        outShadowInstance = materialContext->instance;
        outInstanceMaterial = ResolveInstanceShadowMaterialData(
            materialInfo->surfaceDispatchId,
            materialInfo->shadingModelId,
            materialInfo->transparent,
            materialInfo->refractive,
            materialContext->constantByteOffset,
            meshInstanceIndex
        );
    }
    if(!materialInfo || materialInfo->surfaceDispatchId == Limit<u32>::s_Max)
        m_contentComplete = false;
    // An unavailable hook need not hide a boundary. Unknown/untransparent surfaces cannot use the outside shortcut.
    if(!materialInfo || (materialInfo->transparent && materialInfo->surfaceDispatchId == Limit<u32>::s_Max))
        m_opticalScene.markIncomplete();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(InstanceGpuData) == s_InstanceGpuDataByteSize, "Shadow instance payload size must match the mesh GPU layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedShadowMaterialContext::StorageReady(const RendererRayTracingState& state, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    return
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceMaterialCapacity >= instanceMaterialCount
        && state.m_shadowInstanceCapacity >= instanceCount
        && state.m_shadowMaterialTypedCapacity >= materialTypedByteCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceMaterialHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_shadowMaterialTypedHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}


b8 PreparedShadowMaterialContext::capture(
    const RendererRayTracingState& state,
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const u64 hash,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> instances,
    const Span<const u8> materialTypedBytes
){
    const usize instanceMaterialCount = instanceMaterials.size();
    const usize instanceCount = instances.size();
    const usize materialTypedByteCount = materialTypedBytes.size();
    const b8 validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
    if(
        route == PreparedShadowMaterialContextRoute::None
        || instanceMaterialCount == 0u
        || instanceCount == 0u
        || materialTypedByteCount == 0u
        || !validStorage
    )
        return false;

    clear();
    ShadowMaterialContextPolicy().captureBytes(
        instanceMaterials,
        instances,
        materialTypedBytes,
        m_instanceMaterialBytes,
        m_instanceBytes,
        m_materialTypedBytes
    );
    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = route;
    m_static = staticScene;
    m_ready = true;
    m_uploadRequired = true;
    return true;
}

b8 PreparedShadowMaterialContext::captureCacheReuse(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    clear();
    if(instanceMaterialCount == 0u || instanceCount == 0u || materialTypedByteCount == 0u)
        return false;

    const b8 validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
    if(!state.m_swShadowMaterialContextHashValid || state.m_swShadowMaterialContextHash != hash || !validStorage)
        return false;

    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = PreparedShadowMaterialContextRoute::Software;
    m_static = true;
    m_ready = true;
    m_uploadRequired = false;
    return true;
}

void PreparedShadowMaterialContext::clear()noexcept{
    m_instanceMaterialBytes.clear();
    m_instanceBytes.clear();
    m_materialTypedBytes.clear();
    m_instanceMaterialBuffer = nullptr;
    m_instanceBuffer = nullptr;
    m_materialTypedBuffer = nullptr;
    m_instanceMaterialHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_materialTypedHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceMaterialCount = 0u;
    m_instanceCount = 0u;
    m_materialTypedUploadBytes = 0u;
    m_instanceMaterialCapacity = 0u;
    m_instanceCapacity = 0u;
    m_materialTypedCapacity = 0u;
    m_hash = 0u;
    m_route = PreparedShadowMaterialContextRoute::None;
    m_static = false;
    m_ready = false;
    m_uploadRequired = false;
}

Expected<PreparedShadowMaterialContextUploads> PreparedShadowMaterialContext::retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const{
    PreparedShadowMaterialContextUploads result;
    if(!m_ready)
        return result;

    if(!ShadowMaterialContextPolicy().validateRetainState(
        m_route,
        m_instanceMaterialCount,
        m_instanceCount,
        m_materialTypedUploadBytes,
        storageMatches(state),
        state.m_shadowInstanceMaterialCapacity,
        m_instanceMaterialCapacity,
        state.m_shadowInstanceCapacity,
        m_instanceCapacity,
        state.m_shadowMaterialTypedCapacity,
        m_materialTypedCapacity,
        StorageReady(state, m_instanceMaterialCount, m_instanceCount, m_materialTypedUploadBytes),
        m_uploadRequired,
        m_static,
        state.m_swShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        m_hash,
        state.m_sceneBvhInstanceCount,
        m_instanceMaterialBytes.size(),
        m_instanceBytes.size(),
        m_materialTypedBytes.size()
    ))
        return MakeUnexpected(Failure{});
    if(!m_uploadRequired)
        return result;

    const auto instanceMaterialResult = graph.copyUploadData(m_instanceMaterialBytes.data(), m_instanceMaterialBytes.size(), alignof(NwbRtInstanceMaterialGpu));
    const auto instanceResult = graph.copyUploadData(m_instanceBytes.data(), m_instanceBytes.size(), alignof(InstanceGpuData));
    const auto materialTypedResult = graph.copyUploadData(m_materialTypedBytes.data(), m_materialTypedBytes.size(), alignof(u32));
    if(!(instanceMaterialResult && instanceResult && materialTypedResult))
        return MakeUnexpected(Failure{});
    result.instanceMaterial = *instanceMaterialResult;
    result.instance = *instanceResult;
    result.materialTyped = *materialTypedResult;
    return result;
}

b8 PreparedShadowMaterialContext::publishAcceptedUpload(RendererRayTracingState& state)const noexcept{
    return ShadowMaterialContextPolicy().publishAcceptedUpload(
        m_ready,
        m_uploadRequired,
        m_route,
        m_hash,
        m_static,
        state.m_hwShadowMaterialContextHash,
        state.m_hwShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        state.m_swShadowMaterialContextHashValid
    );
}

b8 PreparedShadowMaterialContext::storageMatches(const RendererRayTracingState& state)const noexcept{
    return
        state.m_shadowInstanceMaterialBuffer.get() == m_instanceMaterialBuffer.get()
        && state.m_shadowInstanceBuffer.get() == m_instanceBuffer.get()
        && state.m_shadowMaterialTypedBuffer.get() == m_materialTypedBuffer.get()
        && state.m_shadowInstanceMaterialHeapHandle == m_instanceMaterialHeapHandle
        && state.m_shadowInstanceHeapHandle == m_instanceHeapHandle
        && state.m_shadowMaterialTypedHeapHandle == m_materialTypedHeapHandle
    ;
}

b8 PreparedShadowMaterialContext::softwareSnapshotCovers(const usize sceneInstanceCount)const noexcept{
    return ShadowMaterialContextPolicy().softwareSnapshotCovers(m_ready, m_route, m_instanceMaterialCount, m_instanceCount, sceneInstanceCount);
}

b8 PreparedShadowMaterialContext::softwareSnapshotAccepted(const RendererRayTracingState& state)const noexcept{
    return ShadowMaterialContextPolicy().softwareSnapshotAccepted(
        m_uploadRequired,
        m_static,
        state.m_swShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        m_hash,
        state.m_sceneBvhInstanceCount,
        m_instanceCount
    );
}

void PreparedShadowMaterialContext::captureStorage(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    m_instanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    m_instanceBuffer = state.m_shadowInstanceBuffer;
    m_materialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    m_instanceMaterialHeapHandle = state.m_shadowInstanceMaterialHeapHandle;
    m_instanceHeapHandle = state.m_shadowInstanceHeapHandle;
    m_materialTypedHeapHandle = state.m_shadowMaterialTypedHeapHandle;
    m_instanceMaterialCount = instanceMaterialCount;
    m_instanceCount = instanceCount;
    m_materialTypedUploadBytes = materialTypedByteCount;
    m_instanceMaterialCapacity = state.m_shadowInstanceMaterialCapacity;
    m_instanceCapacity = state.m_shadowInstanceCapacity;
    m_materialTypedCapacity = state.m_shadowMaterialTypedCapacity;
    m_hash = hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MergePreparedShadowMaterialSampledTextures(const Span<const Core::TextureHandle> sampledTextures, MaterialSampledTextureCollector<Core::Alloc::GlobalArena>& collector){
    for(const Core::TextureHandle& texture : sampledTextures){
        if(!texture || !texture->getCreationDescription().name)
            return false;
        collector.append(texture);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
