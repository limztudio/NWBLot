// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/caustic/caustics_graph_coordinator.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/caustic/caustics_resolve_chain.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CausticsGraphCoordinator::declareDeferredSoftwareCausticsTask(
    const CausticsGraphContext& context,
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
        || !context.shadowVisibilityTask.valid()
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

    const ECSRenderDetail::MeshViewBufferSnapshot meshViewBufferSnapshot = context.mesh.meshViewBufferSnapshot();
    if(context.rayTracing.hasCausticWork(meshViewBufferSnapshot) && !opticalScene.valid())
        return false;

    CausticsResolveChainBuilder resolveChainBuilder(context.graph, context.rayTracing);
    const auto stageTargetsResult = resolveChainBuilder.importStageTargets(deferredTargets, false);
    if(!stageTargetsResult)
        return false;
    const CausticsStageTargets stageTargets = *stageTargetsResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> photonResourceUses{ scratchArena };
    const b8 softwareTraceGeometryStatesGraphOwned = softwareTraceGeometrySet.valid();
    photonResourceUses.reserve(
        s_SoftwarePhotonResourceUseCapacity
        + (rayTracingResources.csgTraceContextBuffer ? 1u : 0u)
        + rayTracingResources.csgTraceBoundsBuffers.size()
        + (softwareTraceGeometryStatesGraphOwned ? 0u : softwareTraceGeometryResources.size())
    );
    photonResourceUses.push_back(ReadTextureUse(worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource));
    // Declared layout must match the shader read.
    photonResourceUses.push_back(ReadTextureUse(depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource));
    photonResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    photonResourceUses.push_back(ReadWriteTextureUse(stageTargets.causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess));
    photonResourceUses.push_back(ReadUse(stageTargets.sceneGeometryDomain));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const auto resourceResult = ImportExternalBuffer(context.graph, buffer, identity, label);
        if(!resourceResult)
            return false;
        photonResourceUses.push_back(ReadUse(*resourceResult, state));
        return true;
    };
    if(opticalScene.valid())
        photonResourceUses.push_back(ReadUse(opticalScene));
    const b8 optionalResourcesImported =
        appendOptionalReadBuffer(rayTracingResources.causticEmissionTargetBuffer, Name(s_SoftwareCausticEmissionTargetsName), s_CausticEmissionTargetsLabel, Core::ResourceStates::ShaderResource)
        && appendOptionalReadBuffer(meshViewBufferSnapshot.buffer, Name(s_MeshViewName), s_MeshViewLabel, Core::ResourceStates::ConstantBuffer)
        && RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(
            rayTracingResources,
            context.graph,
            photonResourceUses
        )
        && RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
            rayTracingResources.csgTraceContextBuffer,
            rayTracingResources.csgTraceBoundsBuffers,
            context.graph,
            photonResourceUses
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
    const RendererFramePipelineDetail::TraceResourceSetUses photonResourceSetUses = RendererFramePipelineDetail::MakeTraceResourceSetUses(softwareTraceGeometrySet, traceMaterialSampledTextureSet);

    const Array<Core::GpuTaskExternalStateSource, 1u> scratchStateSources = {
        Core::GpuTaskExternalStateSource{ .states = context.causticsComputeState.source(), },
    };
    const Span<const Core::GpuTaskExternalStateSource> scratchStateSourceSpan = context.causticsComputeState.valid()
        ? Span<const Core::GpuTaskExternalStateSource>(scratchStateSources)
        : Span<const Core::GpuTaskExternalStateSource>{};
    const Array<Core::GpuTaskExternalStateSource, 1u> irradianceReturnStateSources = {
        Core::GpuTaskExternalStateSource{ .states = context.irradianceReturnState.source(), .applicableConsumerQueueClass = Core::CommandQueue::Compute, },
    };
    const Span<const Core::GpuTaskExternalStateSource> irradianceReturnStateSourceSpan = context.irradianceReturnState.valid()
        ? Span<const Core::GpuTaskExternalStateSource>(irradianceReturnStateSources)
        : Span<const Core::GpuTaskExternalStateSource>{};
    const Array<Core::GpuTaskId, 1u> shadowVisibilityDependency = { context.shadowVisibilityTask };

    const auto prefixTasksResult = resolveChainBuilder.declarePrefixTasks(CausticsStagePrefixInputs{
        .targets = deferredTargets,
        .meshViewSnapshot = meshViewBufferSnapshot,
        .shadowPreparationReady = context.shadowPrepared,
        .timingTicket = timingTicket,
        .photonTiming = causticPhotonTiming,
        .initialDependencies = shadowVisibilityDependency,
        .externalDependencies = {},
        .irradianceClearStateSources = irradianceReturnStateSourceSpan,
        .accumulatorStateSources = scratchStateSourceSpan,
        .causticIrradiance = causticIrradiance,
        .causticAccumulator = stageTargets.causticAccumulator,
        .causticAccumulatorInitialized = rayTracingResources.causticAccumulatorInitialized,
        .hardwareCaustics = false,
    });
    if(!prefixTasksResult)
        return false;
    m_deferredCausticIrradianceClearTask = prefixTasksResult->causticIrradianceClearTask;
    m_deferredCausticAccumulatorBootstrapClearTask = prefixTasksResult->causticAccumulatorBootstrapClearTask;
    m_deferredCausticAccumulatorDecayTask = prefixTasksResult->causticAccumulatorDecayTask;

    Core::GpuTaskSchedulingHint causticsScheduling;
    causticsScheduling.cost = Core::GpuTaskCostHint::Large;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    // Stages stay serial successors in one timing packet.
    causticsScheduling.allowMergeAcrossConsumerFrontier = true;
    EnableSameFamilyComputeEffectRouting(causticsScheduling);
    EnableCrossFamilyComputeEffectRouting(causticsScheduling);

    Core::GpuTaskDesc photonDesc;
    photonDesc
        .setIdentity(Name("render.software_caustics.photons"))
        .setMarkerLabel("Software Caustic Photons")
        .setScheduling(causticsScheduling)
        .setDependencies({ &prefixTasksResult->photonDependencyTask, 1u })
        .setResourceUses(photonResourceUses)
        .setResourceSetUses({ photonResourceSetUses.useCount != 0u ? photonResourceSetUses.uses.data() : nullptr, photonResourceSetUses.useCount })
    ;
    const auto deferredCausticPhotonTaskResult = context.rayTracing.declareSoftwareCausticsTask(
        context.graph,
        photonDesc,
        deferredLightingResources,
        meshViewBufferSnapshot,
        context.shadowPrepared,
        prefixTasksResult->graphOwnsAccumulatorBootstrapClear,
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

    const auto geometryAndResolveResult = resolveChainBuilder.declareGeometryAndResolve(CausticsGeometryAndResolveInputs{
        .targets = deferredTargets,
        .producerDispatched = m_deferredCausticProducerDispatched,
        .timingTicket = timingTicket,
        .resolveTiming = causticResolveTiming,
        .stageTargets = stageTargets,
        .worldPosition = worldPosition,
        .depth = depth,
        .causticIrradiance = causticIrradiance,
        .photonTask = m_deferredCausticPhotonTask,
        .baseScheduling = causticsScheduling,
        .geometryStateSources = scratchStateSourceSpan,
        .resolveStateSources = scratchStateSourceSpan,
        .hardwareCaustics = false,
    });
    if(!geometryAndResolveResult){
        m_deferredCausticGeometryTask = {};
        return false;
    }
    m_deferredCausticGeometryTask = geometryAndResolveResult->causticGeometryTask;
    m_deferredCausticResolvePrepareTask = geometryAndResolveResult->causticResolvePrepareTask;
    m_deferredCausticResolveWaveletTasks = geometryAndResolveResult->causticResolveWaveletTasks;
    m_deferredCausticResolveUpsampleTask = geometryAndResolveResult->causticResolveUpsampleTask;
    m_deferredSoftwareCausticsTask = geometryAndResolveResult->causticsTask;
    return true;
}


void CausticsGraphCoordinator::resetDeferredCausticsTaskState()noexcept{
    m_deferredSoftwareCausticsTask = {};
    m_deferredCausticIrradianceClearTask = {};
    m_deferredCausticAccumulatorBootstrapClearTask = {};
    m_deferredCausticAccumulatorDecayTask = {};
    m_deferredCausticPhotonTask = {};
    m_deferredCausticGeometryTask = {};
    m_deferredCausticResolvePrepareTask = {};
    m_deferredCausticResolveWaveletTasks = {};
    m_deferredCausticResolveUpsampleTask = {};
    m_deferredCausticProducerDispatched = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

