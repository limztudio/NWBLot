// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/software_caustics_resolve_chain.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::declareDeferredSoftwareCausticsTask(
    const b8 hardwareCaustics,
    const DeferredLightingGraphResources& deferredLightingResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId causticIrradiance,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId opticalScene,
    const Span<const Core::GpuGraphResourceId> softwareTraceGeometryResources,
    const Core::GpuGraphResourceSetId softwareTraceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming
){
    using namespace RendererTaskGraphDetail;

    resetDeferredCausticsTaskState();

    // Direct successor of Shadow Visibility; Compute family is optional.
    if(
        hardwareCaustics
        || !m_deferredShadowVisibilityTask.valid()
        || !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !causticIrradiance.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
    )
        return false;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const auto causticAccumulatorResult = importTexture(deferredTargets.causticAccumulator, Name("render.software_caustics.accumulator"), RendererFramePipelineDetail::s_CausticAccumulatorLabel);
    const auto causticHistoryResult = importTexture(deferredTargets.causticHistory, Name("render.software_caustics.history"), RendererFramePipelineDetail::s_CausticHistoryLabel);
    const auto causticResolveHalfResult = importTexture(deferredTargets.causticResolveHalf, Name("render.software_caustics.resolve_half"), RendererFramePipelineDetail::s_CausticResolveHalfLabel);
    const auto causticResolveGeometryResult = importTexture(deferredTargets.causticResolveGeometry, Name("render.software_caustics.resolve_geometry"), RendererFramePipelineDetail::s_CausticResolveGeometryLabel);
    const auto sceneGeometryDomainResult = m_deferredLightingTaskGraph.importHazardDomain(HazardDomainDesc(Name("render.software_caustics.scene_geometry"), "Software BVH Scene Geometry"));
    if(!worldPosition.valid() || !depth.valid() || !causticAccumulatorResult || !causticHistoryResult || !causticResolveHalfResult || !causticResolveGeometryResult || !sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred software-caustics graph resources"));
        return false;
    }
    const Core::GpuGraphResourceId causticAccumulator = *causticAccumulatorResult;
    const Core::GpuGraphResourceId causticHistory = *causticHistoryResult;
    const Core::GpuGraphResourceId causticResolveHalf = *causticResolveHalfResult;
    const Core::GpuGraphResourceId causticResolveGeometry = *causticResolveGeometryResult;
    const Core::GpuGraphResourceId sceneGeometryDomain = *sceneGeometryDomainResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> photonResourceUses{ scratchArena };
    const b8 softwareTraceGeometryStatesGraphOwned = softwareTraceGeometrySet.valid();
    constexpr usize s_PhotonResourceUseCapacity = 20u;
    photonResourceUses.reserve(s_PhotonResourceUseCapacity + (rayTracingResources.csgTraceContextBuffer ? 1u : 0u) + rayTracingResources.csgTraceBoundsBuffers.size() + (softwareTraceGeometryStatesGraphOwned ? 0u : softwareTraceGeometryResources.size()));
    photonResourceUses.push_back(ReadTextureUse(worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource));
    // Declared layout must match the shader read.
    photonResourceUses.push_back(ReadTextureUse(depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource));
    photonResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    photonResourceUses.push_back(ReadWriteTextureUse(causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess));
    photonResourceUses.push_back(ReadUse(sceneGeometryDomain));

    // Downsample runs after the producer; ping-pong stays in resolve.
    const Core::GpuTaskResourceUse geometryResourceUses[] = {
        ReadTextureUse(worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };

    // Prepare writes the first ping-pong target; wavelets own the sequence.
    constexpr b8 s_CausticResolvePrepareWritesHalf = (NWB_CAUSTIC_RESOLVE_PASS_COUNT % 2u) == 0u;
    const Core::GpuGraphResourceId oddWaveletSource = s_CausticResolvePrepareWritesHalf ? causticResolveHalf : causticHistory;
    const Core::GpuGraphResourceId oddWaveletDest = s_CausticResolvePrepareWritesHalf ? causticHistory : causticResolveHalf;
    const Core::GpuTaskResourceUse resolvePrepareResourceUses[] = {
        ReadTextureUse(causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(oddWaveletSource, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse resolveOddWaveletResourceUses[] = {
        ReadTextureUse(causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(oddWaveletSource, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(oddWaveletDest, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse resolveEvenWaveletResourceUses[] = {
        ReadTextureUse(causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(oddWaveletDest, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(oddWaveletSource, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };

    // Upsample takes the fifth-wavelet handoff; timing-close carries no use.
    const Core::GpuTaskResourceUse resolveUpsampleResourceUses[] = {
        ReadTextureUse(worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(causticResolveHalf, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(causticIrradiance, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const auto resourceResult = importBuffer(buffer, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        photonResourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const ECSRenderDetail::MeshViewBufferSnapshot meshViewBufferSnapshot = m_meshSystem.meshViewBufferSnapshot();
    if(m_raytracingSystem.hasCausticWork(meshViewBufferSnapshot) && !opticalScene.valid())
        return false;
    if(opticalScene.valid())
        photonResourceUses.push_back(ReadUse(opticalScene));
    const b8 optionalResourcesImported =
        appendOptionalReadBuffer(rayTracingResources.causticEmissionTargetBuffer, Name(RendererFramePipelineDetail::s_SoftwareCausticEmissionTargetsName), RendererFramePipelineDetail::s_CausticEmissionTargetsLabel, Core::ResourceStates::ShaderResource)
        && appendOptionalReadBuffer(meshViewBufferSnapshot.buffer, Name(RendererFramePipelineDetail::s_MeshViewName), RendererFramePipelineDetail::s_MeshViewLabel, Core::ResourceStates::ConstantBuffer)
        && RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(
            rayTracingResources,
            importBuffer,
            [&](const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state)noexcept{
                photonResourceUses.push_back(ReadUse(resource, state));
            }
        )
        && RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
            rayTracingResources.csgTraceContextBuffer,
            rayTracingResources.csgTraceBoundsBuffers,
            m_deferredLightingTaskGraph,
            [&](const Core::GpuGraphResourceId resource)noexcept{ photonResourceUses.push_back(ReadUse(resource)); }
        );
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred software-caustics dynamic resource"));
        return false;
    }
    photonResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    photonResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid())
        photonResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!softwareTraceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : softwareTraceGeometryResources){
            if(!resource.valid())
                return false;
            photonResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    const Core::GpuTaskResourceSetUse softwareTraceGeometrySetUse{ .resourceSet = softwareTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{ .resourceSet = traceMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    constexpr usize s_PhotonResourceSetUseCapacity = 2u;
    Core::GpuTaskResourceSetUse photonResourceSetUses[s_PhotonResourceSetUseCapacity] = {};
    usize photonResourceSetUseCount = 0u;
    if(softwareTraceGeometryStatesGraphOwned)
        photonResourceSetUses[photonResourceSetUseCount++] = softwareTraceGeometrySetUse;
    if(traceMaterialSampledTextureSet.valid())
        photonResourceSetUses[photonResourceSetUseCount++] = traceMaterialSampledTextureSetUse;

    const Core::GpuTaskExternalStateSource scratchStateSources[] = { Core::GpuTaskExternalStateSource{ .states = m_causticsComputePersistentState.source(), }, };
    const usize scratchStateSourceCount = m_causticsComputePersistentState.valid() ? LengthOf(scratchStateSources) : 0u;
    const Core::GpuTaskExternalStateSource irradianceReturnStateSources[] = { Core::GpuTaskExternalStateSource{ .states = m_causticIrradianceReturnState.source(), .applicableConsumerQueueClass = Core::CommandQueue::Compute, }, };
    const usize irradianceReturnStateSourceCount = m_causticIrradianceReturnState.valid() ? LengthOf(irradianceReturnStateSources) : 0u;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    const Core::GpuTaskId shadowVisibilityDependency[] = { m_deferredShadowVisibilityTask };

    // Black is the no-producer result; merge the producer into this packet.
    Core::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    // Allow physical queue balancing for the initial packet; successors retain its selected queue.
    EnableSameFamilyComputeEffectRouting(irradianceClearScheduling, false);
    EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling);
    Core::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("render.software_caustics.irradiance_clear"))
        .setMarkerLabel("Software Caustics Irradiance Clear")
        .setScheduling(irradianceClearScheduling)
        .setDependencies(shadowVisibilityDependency, LengthOf(shadowVisibilityDependency))
        .setExternalStateSources(irradianceReturnStateSources, irradianceReturnStateSourceCount)
    ;
    Core::GpuClearTextureTaskDesc irradianceClear;
    irradianceClear.destination = causticIrradiance;
    irradianceClear.subresources = ECSRenderDetail::s_FramebufferSubresources;
    irradianceClear.valueType = Core::GpuClearTextureTaskValueType::Float;
    irradianceClear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
    const auto irradianceClearTaskResult = m_deferredLightingTaskGraph.addClearTextureTask(irradianceClearDesc, irradianceClear);
    if(!irradianceClearTaskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics irradiance clear"));
        return false;
    }
    const Core::GpuTaskId irradianceClearTask = *irradianceClearTaskResult;
    m_deferredCausticIrradianceClearTask = irradianceClearTask;

    Core::GpuTaskId causticsDependency = irradianceClearTask;
    const b8 graphOwnsAccumulatorBootstrapClear = !rayTracingResources.causticAccumulatorInitialized;
    if(graphOwnsAccumulatorBootstrapClear){
        Core::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling;
        accumulatorBootstrapClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accumulatorBootstrapClearScheduling.allowPacketMerge = true;
        accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
        // Keep the clear in the accepted producer packet.
        accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
        EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
        Core::GpuTaskDesc accumulatorBootstrapClearDesc;
        accumulatorBootstrapClearDesc
            .setIdentity(Name("render.software_caustics.accumulator_bootstrap_clear"))
            .setMarkerLabel("Software Caustics Accumulator Bootstrap Clear")
            .setScheduling(accumulatorBootstrapClearScheduling)
            .setDependencies(&irradianceClearTask, 1u)
            .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        ;
        Core::GpuClearTextureTaskDesc accumulatorBootstrapClear;
        accumulatorBootstrapClear.destination = causticAccumulator;
        accumulatorBootstrapClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
        accumulatorBootstrapClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
        accumulatorBootstrapClear.uintValue = Core::UIntColor(0u);
        const auto accumulatorBootstrapClearTaskResult = m_deferredLightingTaskGraph.addClearTextureTask(accumulatorBootstrapClearDesc, accumulatorBootstrapClear);
        if(!accumulatorBootstrapClearTaskResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics accumulator bootstrap clear"));
            return false;
        }
        const Core::GpuTaskId accumulatorBootstrapClearTask = *accumulatorBootstrapClearTaskResult;
        m_deferredCausticAccumulatorBootstrapClearTask = accumulatorBootstrapClearTask;
        causticsDependency = accumulatorBootstrapClearTask;
    }

    const b8 graphOwnsAccumulatorDecay = rayTracingResources.causticAccumulatorInitialized;
    if(graphOwnsAccumulatorDecay){
        Core::GpuTaskSchedulingHint accumulatorDecayScheduling;
        accumulatorDecayScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accumulatorDecayScheduling.allowPacketMerge = true;
        accumulatorDecayScheduling.mergeWithPrevious = true;
        // Keep decay in the accepted producer packet.
        accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorDecayScheduling);
        EnableCrossFamilyComputeEffectRouting(accumulatorDecayScheduling);
        const Core::GpuTaskResourceUse accumulatorDecayUses[] = { ReadWriteTextureUse(causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess), };
        Core::GpuTaskDesc accumulatorDecayDesc;
        accumulatorDecayDesc
            .setIdentity(Name("render.software_caustics.accumulator_decay"))
            .setMarkerLabel("Software Caustics Accumulator Decay")
            .setScheduling(accumulatorDecayScheduling)
            .setDependencies(&causticsDependency, 1u)
            .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
            .setResourceUses(accumulatorDecayUses, LengthOf(accumulatorDecayUses))
        ;
        const auto accumulatorDecayTaskResult = m_raytracingSystem.declareCausticAccumulatorDecayTask(
            m_deferredLightingTaskGraph,
            accumulatorDecayDesc,
            meshViewBufferSnapshot,
            m_shadowPreparationOutcome.ready,
            false,
            deferredTargets,
            timingTicket,
            causticPhotonTiming
        );
        if(!accumulatorDecayTaskResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics accumulator decay"));
            return false;
        }
        const Core::GpuTaskId accumulatorDecayTask = *accumulatorDecayTaskResult;
        m_deferredCausticAccumulatorDecayTask = accumulatorDecayTask;
        causticsDependency = accumulatorDecayTask;
    }

    Core::GpuTaskSchedulingHint causticsScheduling = scheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    // Stages stay serial successors in one timing packet.
    causticsScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc photonDesc;
    photonDesc
        .setIdentity(Name("render.software_caustics.photons"))
        .setMarkerLabel("Software Caustic Photons")
        .setScheduling(causticsScheduling)
        .setDependencies(&causticsDependency, 1u)
        .setResourceUses(photonResourceUses.data(), photonResourceUses.size())
        .setResourceSetUses(photonResourceSetUseCount != 0u ? photonResourceSetUses : nullptr, photonResourceSetUseCount)
    ;
    const auto deferredCausticPhotonTaskResult = m_raytracingSystem.declareSoftwareCausticsTask(
        m_deferredLightingTaskGraph,
        photonDesc,
        deferredLightingResources,
        meshViewBufferSnapshot,
        m_shadowPreparationOutcome.ready,
        graphOwnsAccumulatorBootstrapClear,
        deferredTargets,
        timingTicket,
        causticPhotonTiming,
        m_deferredCausticProducerDispatched
    );
    if(!deferredCausticPhotonTaskResult){
        m_deferredCausticPhotonTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics photon graph task"));
        return false;
    }
    m_deferredCausticPhotonTask = *deferredCausticPhotonTaskResult;

    Core::GpuTaskSchedulingHint geometryScheduling = causticsScheduling;
    geometryScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc geometryDesc;
    geometryDesc
        .setIdentity(Name("render.software_caustics.geometry_downsample"))
        .setMarkerLabel("Software Caustics Geometry Downsample")
        .setScheduling(geometryScheduling)
        .setDependencies(&m_deferredCausticPhotonTask, 1u)
        .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        .setResourceUses(geometryResourceUses, LengthOf(geometryResourceUses))
    ;
    const auto deferredCausticGeometryTaskResult = m_raytracingSystem.declareCausticGeometryDownsampleTask(m_deferredLightingTaskGraph, geometryDesc, m_deferredCausticProducerDispatched, deferredTargets, timingTicket, causticResolveTiming);
    if(!deferredCausticGeometryTaskResult){
        m_deferredCausticGeometryTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics geometry graph task"));
        return false;
    }
    m_deferredCausticGeometryTask = *deferredCausticGeometryTaskResult;

    SoftwareCausticsResolveChainBuilder resolveChainBuilder(m_deferredLightingTaskGraph, m_raytracingSystem);
    SoftwareCausticsResolveChainInputs resolveChainInputs(deferredTargets, m_deferredCausticProducerDispatched, timingTicket, causticResolveTiming);
    resolveChainInputs.geometryTask = m_deferredCausticGeometryTask;
    resolveChainInputs.baseScheduling = geometryScheduling;
    resolveChainInputs.prepare = resolvePrepareResourceUses;
    resolveChainInputs.wavelet = resolveOddWaveletResourceUses;
    resolveChainInputs.secondWavelet = resolveEvenWaveletResourceUses;
    resolveChainInputs.thirdWavelet = resolveOddWaveletResourceUses;
    resolveChainInputs.fourthWavelet = resolveEvenWaveletResourceUses;
    resolveChainInputs.fifthWavelet = resolveOddWaveletResourceUses;
    resolveChainInputs.upsample = resolveUpsampleResourceUses;
    resolveChainInputs.stateSources = { scratchStateSources, scratchStateSourceCount };
    auto resolveChainResult = resolveChainBuilder.declare(resolveChainInputs);
    if(!resolveChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve chain"));
        return false;
    }
    m_deferredCausticResolvePrepareTask = resolveChainResult->causticResolvePrepareTask;
    m_deferredCausticResolveWaveletTask = resolveChainResult->causticResolveWaveletTask;
    m_deferredCausticResolveSecondWaveletTask = resolveChainResult->causticResolveSecondWaveletTask;
    m_deferredCausticResolveThirdWaveletTask = resolveChainResult->causticResolveThirdWaveletTask;
    m_deferredCausticResolveFourthWaveletTask = resolveChainResult->causticResolveFourthWaveletTask;
    m_deferredCausticResolveFifthWaveletTask = resolveChainResult->causticResolveFifthWaveletTask;
    m_deferredCausticResolveUpsampleTask = resolveChainResult->causticResolveUpsampleTask;
    m_deferredSoftwareCausticsTask = resolveChainResult->softwareCausticsTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

