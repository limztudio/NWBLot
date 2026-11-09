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
#include <impl/ecs_render/caustic/graph_types.adhd.h>
#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>
#include <global/timer.h>
#include "caustics_resolve_chain.h"
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <core/graphics/backend_selection/backend.h>
#include <global/algorithm.h>
#include "hardware_caustics_stage_builder.h"


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
        .setIdentity(Name(s_RenderNameRenderSoftwareCausticsPhotons))
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


namespace __hidden_caustics_resolve_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolveStageDesc{
    const CausticsResolveStageNaming& naming;
    CausticsResolveStageUses stageUses;
    Core::GpuTaskId& outTask;
    u32 waveletPassIndex = 0u;
    b8 isWavelet = false;
    b8 applyStateSources = false;
    Expected<Core::GpuTaskId> (RendererRayTracingSystem::*declareTask)(const Core::GpuTaskDesc&, const b8&, Core::GpuTaskGraph&, DeferredFrameTargets&) = nullptr;
};

// One activity read of the previous stage and one activity write of this stage.
inline constexpr usize s_ResolveStageCount = NWB_CAUSTIC_RESOLVE_PASS_COUNT + 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CausticsStageTargets> CausticsResolveChainBuilder::importStageTargets(const DeferredFrameTargets& targets, const b8 hardwareCaustics){
    using namespace RendererTaskGraphDetail;
    const CausticsStageTargetNaming naming = [&]()noexcept{
        if(hardwareCaustics){
            const Name accumulator = s_CausticsNameRenderHardwareCausticsAccumulator;
            const Name history = s_CausticsNameRenderHardwareCausticsHistory;
            const Name resolveHalf = s_CausticsNameRenderHardwareCausticsResolveHalf;
            const Name resolveGeometry = s_CausticsNameRenderHardwareCausticsResolveGeometry;
            const Name sceneGeometryDomain = s_CausticsNameRenderHardwareCausticsSceneGeometry;
            return CausticsStageTargetNaming(accumulator, history, resolveHalf, resolveGeometry, sceneGeometryDomain, s_SceneGeometryDomainLabel, NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"));
        }
        const Name accumulator = s_CausticsNameRenderSoftwareCausticsAccumulator;
        const Name history = s_CausticsNameRenderSoftwareCausticsHistory;
        const Name resolveHalf = s_CausticsNameRenderSoftwareCausticsResolveHalf;
        const Name resolveGeometry = s_CausticsNameRenderSoftwareCausticsResolveGeometry;
        const Name sceneGeometryDomain = s_CausticsNameRenderSoftwareCausticsSceneGeometry;
        return CausticsStageTargetNaming(accumulator, history, resolveHalf, resolveGeometry, sceneGeometryDomain, "Software BVH Scene Geometry", NWB_TEXT("RendererSystem: could not import deferred software-caustics graph resources"));
    }();

    const auto causticAccumulatorResult = ImportExternalTexture(m_graph, targets.causticAccumulator, naming.accumulator, s_CausticAccumulatorLabel);
    const auto causticHistoryResult = ImportExternalTexture(m_graph, targets.causticHistory, naming.history, s_CausticHistoryLabel);
    const auto causticResolveHalfResult = ImportExternalTexture(m_graph, targets.causticResolveHalf, naming.resolveHalf, s_CausticResolveHalfLabel);
    const auto causticResolveGeometryResult = ImportExternalTexture(m_graph, targets.causticResolveGeometry, naming.resolveGeometry, s_CausticResolveGeometryLabel);
    const auto sceneGeometryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(naming.sceneGeometryDomain, naming.sceneGeometryDomainLabel));
    if(!causticAccumulatorResult || !causticHistoryResult || !causticResolveHalfResult || !causticResolveGeometryResult || !sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(naming.importWarnText);
        return MakeUnexpected(Failure{});
    }
    return CausticsStageTargets(*causticAccumulatorResult, *causticHistoryResult, *causticResolveHalfResult, *causticResolveGeometryResult, *sceneGeometryDomainResult);
}

Expected<CausticsStagePrefixResult> CausticsResolveChainBuilder::declarePrefixTasks(const CausticsStagePrefixInputs& inputs){
    using namespace RendererTaskGraphDetail;
    CausticsStagePrefixResult result{};

    Core::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    // Allow physical queue balancing for the initial packet; successors retain its selected queue.
    EnableSameFamilyComputeEffectRouting(irradianceClearScheduling, false);
    EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling);

    Core::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(inputs.hardwareCaustics ? Name(s_CausticsNameRenderHardwareCausticsIrradianceClear) : Name(s_CausticsNameRenderSoftwareCausticsIrradianceClear))
        .setMarkerLabel(inputs.hardwareCaustics ? "Hardware Caustics Irradiance Clear" : "Software Caustics Irradiance Clear")
        .setScheduling(irradianceClearScheduling)
        .setDependencies(inputs.initialDependencies)
        .setExternalDependencies(inputs.externalDependencies)
        .setExternalStateSources(inputs.irradianceClearStateSources)
    ;
    Core::GpuClearTextureTaskDesc irradianceClear;
    irradianceClear.destination = inputs.causticIrradiance;
    irradianceClear.subresources = ECSRenderDetail::s_FramebufferSubresources;
    irradianceClear.valueType = Core::GpuClearTextureTaskValueType::Float;
    irradianceClear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
    const auto irradianceClearTaskResult = m_graph.addClearTextureTask(irradianceClearDesc, irradianceClear);
    if(!irradianceClearTaskResult){
        NWB_LOGGER_WARNING(
            inputs.hardwareCaustics
                ? NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics irradiance clear")
                : NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics irradiance clear")
        );
        return MakeUnexpected(Failure{});
    }
    const Core::GpuTaskId irradianceClearTask = *irradianceClearTaskResult;
    result.causticIrradianceClearTask = irradianceClearTask;
    result.photonDependencyTask = irradianceClearTask;
    result.graphOwnsAccumulatorBootstrapClear = !inputs.causticAccumulatorInitialized;

    if(result.graphOwnsAccumulatorBootstrapClear){
        Core::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling;
        accumulatorBootstrapClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accumulatorBootstrapClearScheduling.allowPacketMerge = true;
        accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
        accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
        EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
        Core::GpuTaskDesc accumulatorBootstrapClearDesc;
        accumulatorBootstrapClearDesc
            .setIdentity(inputs.hardwareCaustics ? Name(s_CausticsNameRenderHardwareCausticsAccumulatorBootstrapClear) : Name(s_CausticsNameRenderSoftwareCausticsAccumulatorBootstrapClear))
            .setMarkerLabel(inputs.hardwareCaustics ? "Hardware Caustics Accumulator Bootstrap Clear" : "Software Caustics Accumulator Bootstrap Clear")
            .setScheduling(accumulatorBootstrapClearScheduling)
            .setDependencies({ &irradianceClearTask, 1u })
            .setExternalStateSources(inputs.accumulatorStateSources)
        ;
        Core::GpuClearTextureTaskDesc accumulatorBootstrapClear;
        accumulatorBootstrapClear.destination = inputs.causticAccumulator;
        accumulatorBootstrapClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
        accumulatorBootstrapClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
        accumulatorBootstrapClear.uintValue = Core::UIntColor(0u);
        const auto accumulatorBootstrapClearTaskResult = m_graph.addClearTextureTask(accumulatorBootstrapClearDesc, accumulatorBootstrapClear);
        if(!accumulatorBootstrapClearTaskResult){
            NWB_LOGGER_WARNING(
                inputs.hardwareCaustics
                    ? NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator bootstrap clear")
                    : NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics accumulator bootstrap clear")
            );
            return MakeUnexpected(Failure{});
        }
        result.causticAccumulatorBootstrapClearTask = *accumulatorBootstrapClearTaskResult;
        result.photonDependencyTask = result.causticAccumulatorBootstrapClearTask;
    }
    else{
        Core::GpuTaskSchedulingHint accumulatorDecayScheduling;
        accumulatorDecayScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accumulatorDecayScheduling.allowPacketMerge = true;
        accumulatorDecayScheduling.mergeWithPrevious = true;
        accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorDecayScheduling);
        EnableCrossFamilyComputeEffectRouting(accumulatorDecayScheduling);
        const Array<Core::GpuTaskResourceUse, 1u> accumulatorDecayUses = {
            ReadWriteTextureUse(inputs.causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc accumulatorDecayDesc;
        accumulatorDecayDesc
            .setIdentity(inputs.hardwareCaustics ? Name(s_CausticsNameRenderHardwareCausticsAccumulatorDecay) : Name(s_CausticsNameRenderSoftwareCausticsAccumulatorDecay))
            .setMarkerLabel(inputs.hardwareCaustics ? "Hardware Caustics Accumulator Decay" : "Software Caustics Accumulator Decay")
            .setScheduling(accumulatorDecayScheduling)
            .setDependencies({ &result.photonDependencyTask, 1u })
            .setExternalStateSources(inputs.accumulatorStateSources)
            .setResourceUses(accumulatorDecayUses)
        ;
        const auto accumulatorDecayTaskResult = m_raytracingSystem.declareCausticAccumulatorDecayTask(
            m_graph,
            accumulatorDecayDesc,
            inputs.meshViewSnapshot,
            inputs.shadowPreparationReady,
            inputs.hardwareCaustics,
            inputs.targets,
            inputs.timingTicket,
            inputs.photonTiming
        );
        if(!accumulatorDecayTaskResult){
            NWB_LOGGER_WARNING(
                inputs.hardwareCaustics
                    ? NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator decay")
                    : NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics accumulator decay")
            );
            return MakeUnexpected(Failure{});
        }
        result.causticAccumulatorDecayTask = *accumulatorDecayTaskResult;
        result.photonDependencyTask = result.causticAccumulatorDecayTask;
    }
    return result;
}

Expected<CausticsGeometryAndResolveResult> CausticsResolveChainBuilder::declareGeometryAndResolve(const CausticsGeometryAndResolveInputs& inputs){
    using namespace RendererTaskGraphDetail;
    const Array<Core::GpuTaskResourceUse, 3u> geometryResourceUses = {
        ReadTextureUse(inputs.worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(inputs.stageTargets.causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };

    // Prepare writes the parity-selected ping-pong target; wavelets own the alternating sequence.
    constexpr b8 s_CausticResolvePrepareWritesHalf = (NWB_CAUSTIC_RESOLVE_PASS_COUNT % 2u) == 0u;
    const Core::GpuGraphResourceId oddWaveletSource = s_CausticResolvePrepareWritesHalf ? inputs.stageTargets.causticResolveHalf : inputs.stageTargets.causticHistory;
    const Core::GpuGraphResourceId oddWaveletDest = s_CausticResolvePrepareWritesHalf ? inputs.stageTargets.causticHistory : inputs.stageTargets.causticResolveHalf;
    const Array<Core::GpuTaskResourceUse, 3u> resolvePrepareResourceUses = {
        ReadTextureUse(inputs.stageTargets.causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.stageTargets.causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(oddWaveletSource, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    const Array<Core::GpuTaskResourceUse, 3u> resolveOddWaveletResourceUses = {
        ReadTextureUse(inputs.stageTargets.causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(oddWaveletSource, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(oddWaveletDest, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    const Array<Core::GpuTaskResourceUse, 3u> resolveEvenWaveletResourceUses = {
        ReadTextureUse(inputs.stageTargets.causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(oddWaveletDest, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(oddWaveletSource, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    const Array<Core::GpuTaskResourceUse, 5u> resolveUpsampleResourceUses = {
        ReadTextureUse(inputs.worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.stageTargets.causticResolveHalf, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.stageTargets.causticResolveGeometry, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        WriteTextureUse(inputs.causticIrradiance, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };

    Core::GpuTaskSchedulingHint geometryScheduling = inputs.baseScheduling;
    geometryScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc geometryDesc;
    geometryDesc
        .setIdentity(inputs.hardwareCaustics ? Name(s_CausticsNameRenderHardwareCausticsGeometryDownsample) : Name(s_CausticsNameRenderSoftwareCausticsGeometryDownsample))
        .setMarkerLabel(inputs.hardwareCaustics ? "Hardware Caustics Geometry Downsample" : "Software Caustics Geometry Downsample")
        .setScheduling(geometryScheduling)
        .setDependencies({ &inputs.photonTask, 1u })
        .setExternalStateSources(inputs.geometryStateSources)
        .setResourceUses(geometryResourceUses)
    ;
    const auto causticGeometryTaskResult = m_raytracingSystem.declareCausticGeometryDownsampleTask(
        m_graph,
        geometryDesc,
        inputs.producerDispatched,
        inputs.targets,
        inputs.timingTicket,
        inputs.resolveTiming
    );
    if(!causticGeometryTaskResult){
        NWB_LOGGER_WARNING(
            inputs.hardwareCaustics
                ? NWB_TEXT("RendererSystem: could not declare hardware-caustics geometry graph task")
                : NWB_TEXT("RendererSystem: could not declare deferred software-caustics geometry graph task")
        );
        return MakeUnexpected(Failure{});
    }

    CausticsResolveChainInputs resolveChainInputs(inputs.targets, inputs.producerDispatched, inputs.timingTicket, inputs.resolveTiming);
    resolveChainInputs.geometryTask = *causticGeometryTaskResult;
    resolveChainInputs.baseScheduling = geometryScheduling;
    resolveChainInputs.prepare = resolvePrepareResourceUses;
    for(usize index = 0u; index < NWB_CAUSTIC_RESOLVE_PASS_COUNT; ++index)
        resolveChainInputs.wavelets[index] = (index % 2u == 0u) ? CausticsResolveStageUses(resolveOddWaveletResourceUses) : CausticsResolveStageUses(resolveEvenWaveletResourceUses);
    resolveChainInputs.upsample = resolveUpsampleResourceUses;
    resolveChainInputs.stateSources = inputs.resolveStateSources;

    const auto resolveChainResult = inputs.hardwareCaustics ? declareHardware(resolveChainInputs) : declareSoftware(resolveChainInputs);
    if(!resolveChainResult){
        NWB_LOGGER_WARNING(
            inputs.hardwareCaustics
                ? NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve chain")
                : NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve chain")
        );
        return MakeUnexpected(Failure{});
    }

    CausticsGeometryAndResolveResult result{};
    static_cast<CausticsResolveChainResult&>(result) = *resolveChainResult;
    result.causticGeometryTask = *causticGeometryTaskResult;
    return result;
}

Expected<CausticsResolveChainResult> CausticsResolveChainBuilder::declare(const CausticsResolveChainInputs& inputs, const CausticsResolveChainNaming& naming){
    using namespace RendererTaskGraphDetail;
    CausticsResolveChainResult result{};
    if(!inputs.geometryTask.valid())
        return MakeUnexpected(Failure{});

    const CausticResolveActivitySnapshot activity = m_raytracingSystem.causticResolveActivitySnapshot(inputs.targets);
    Array<Core::GpuGraphResourceId, s_CausticResolveActivityBufferCount> activityResources{};
    if(activity.valid()){
        for(u32 index = 0u; index < LengthOf(activityResources); ++index){
            Core::GpuGraphResourceDesc desc = BufferResourceDesc(s_CausticResolveActivityBufferNames[index], "caustic resolve activity");
            desc.setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common);
            const auto activityResourceResult = m_graph.importBuffer(activity.buffers[index], desc);
            if(!activityResourceResult)
                return MakeUnexpected(Failure{});
            activityResources[index] = *activityResourceResult;
        }
    }

    const Array<__hidden_caustics_resolve_chain::ResolveStageDesc, __hidden_caustics_resolve_chain::s_ResolveStageCount> stages = {{
        { naming.prepare, inputs.prepare, result.causticResolvePrepareTask, 0u, false, true, &RendererRayTracingSystem::declareCausticResolvePrepareTask },
        { naming.wavelets[0u], inputs.wavelets[0u], result.causticResolveWaveletTasks[0u], 0u, true, true, nullptr },
        { naming.wavelets[1u], inputs.wavelets[1u], result.causticResolveWaveletTasks[1u], 1u, true, false, nullptr },
        { naming.wavelets[2u], inputs.wavelets[2u], result.causticResolveWaveletTasks[2u], 2u, true, false, nullptr },
        { naming.wavelets[3u], inputs.wavelets[3u], result.causticResolveWaveletTasks[3u], 3u, true, false, nullptr },
        { naming.wavelets[4u], inputs.wavelets[4u], result.causticResolveWaveletTasks[4u], 4u, true, false, nullptr },
        { naming.upsample, inputs.upsample, result.causticResolveUpsampleTask, 0u, false, false, &RendererRayTracingSystem::declareCausticResolveUpsampleTask },
    }};
    Array<Core::GpuTaskResourceUse, s_CausticMaxStageResourceUses + s_CausticActivityUsesPerStage> activityUses{};
    Core::GpuTaskSchedulingHint stageScheduling = inputs.baseScheduling;
    stageScheduling.mergeWithPrevious = true;
    Core::GpuTaskId previousTask = inputs.geometryTask;
    for(usize stageIndex = 0u; stageIndex < LengthOf(stages); ++stageIndex){
        const __hidden_caustics_resolve_chain::ResolveStageDesc& stage = stages[stageIndex];
        Core::GpuTaskDesc stageDesc;
        stageDesc.setIdentity(stage.naming.identity).setMarkerLabel(stage.naming.label).setScheduling(stageScheduling).setDependencies({ &previousTask, 1u }).setResourceUses(stage.stageUses);
        if(activity.valid()){
            if(stage.stageUses.size() > s_CausticMaxStageResourceUses)
                return MakeUnexpected(Failure{});
            usize activityUseCount = 0u;
            for(const Core::GpuTaskResourceUse& use : stage.stageUses)
                activityUses[activityUseCount++] = use;
            // Prepare publishes A; every wavelet alternates masks with its quantized color output.
            if(stageIndex > 0u)
                activityUses[activityUseCount++] = ReadUse(activityResources[(stageIndex - 1u) % s_CausticResolveActivityBufferCount], Core::ResourceStates::ShaderResource);
            if(stageIndex <= NWB_CAUSTIC_RESOLVE_PASS_COUNT)
                activityUses[activityUseCount++] = WriteUse(activityResources[stageIndex % s_CausticResolveActivityBufferCount], Core::ResourceStates::UnorderedAccess);
            stageDesc.setResourceUses({ activityUses.data(), activityUseCount });
        }
        if(!inputs.applyStateSourcesToPrepareOnly || stage.applyStateSources)
            stageDesc.setExternalStateSources(inputs.stateSources);
        const Expected<Core::GpuTaskId> stageTask = stage.isWavelet
            ? m_raytracingSystem.declareCausticResolveWaveletTask(stageDesc, stage.waveletPassIndex, inputs.producerDispatched, m_graph, inputs.targets)
            : (m_raytracingSystem.*(stage.declareTask))(stageDesc, inputs.producerDispatched, m_graph, inputs.targets);
        if(!stageTask){
            NWB_LOGGER_WARNING(stage.naming.warnText);
            return MakeUnexpected(stageTask.error());
        }
        stage.outTask = *stageTask;
        previousTask = stage.outTask;
    }

    Core::GpuTaskDesc timingCloseDesc;
    timingCloseDesc.setIdentity(naming.timingClose.identity).setMarkerLabel(naming.timingClose.label).setScheduling(stageScheduling).setDependencies({ &result.causticResolveUpsampleTask, 1u });
    const auto causticsTaskResult = m_raytracingSystem.declareCausticResolveTask(m_graph, timingCloseDesc, inputs.producerDispatched, inputs.timingTicket, inputs.resolveTiming);
    if(!causticsTaskResult){
        NWB_LOGGER_WARNING(naming.timingClose.warnText);
        return MakeUnexpected(Failure{});
    }
    result.causticsTask = *causticsTaskResult;
    return result;
}

Expected<CausticsResolveChainResult> CausticsResolveChainBuilder::declareHardware(const CausticsResolveChainInputs& inputs){
    const CausticsResolveChainNaming naming{
        .prepare = { Name(s_CausticsNameRenderHardwareCausticsResolvePrepare), "Hardware Caustics Resolve Prepare", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task") },
        .wavelets = {{
            { Name(s_CausticsNameRenderHardwareCausticsResolveWavelet), "Hardware Caustics Resolve Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task") },
            { Name(s_CausticsNameRenderHardwareCausticsResolveSecondWavelet), "Hardware Caustics Resolve Second Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task") },
            { Name(s_CausticsNameRenderHardwareCausticsResolveThirdWavelet), "Hardware Caustics Resolve Third Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task") },
            { Name(s_CausticsNameRenderHardwareCausticsResolveFourthWavelet), "Hardware Caustics Resolve Fourth Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task") },
            { Name(s_CausticsNameRenderHardwareCausticsResolveFifthWavelet), "Hardware Caustics Resolve Fifth Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task") },
        }},
        .upsample = { Name(s_CausticsNameRenderHardwareCausticsResolveUpsample), "Hardware Caustics Resolve Upsample", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task") },
        .timingClose = { Name(s_CausticsNameRenderHardwareCausticsResolveTimingClose), "Hardware Caustics Resolve Timing Close", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task") },
    };
    return declare(inputs, naming);
}

Expected<CausticsResolveChainResult> CausticsResolveChainBuilder::declareSoftware(const CausticsResolveChainInputs& inputs){
    const CausticsResolveChainNaming naming{
        .prepare = { Name(s_CausticsNameRenderSoftwareCausticsResolvePrepare), "Software Caustics Resolve Prepare", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-prepare graph task") },
        .wavelets = {{
            { Name(s_CausticsNameRenderSoftwareCausticsResolveWavelet), "Software Caustics Resolve Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics first-wavelet graph task") },
            { Name(s_CausticsNameRenderSoftwareCausticsResolveSecondWavelet), "Software Caustics Resolve Second Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics second-wavelet graph task") },
            { Name(s_CausticsNameRenderSoftwareCausticsResolveThirdWavelet), "Software Caustics Resolve Third Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics third-wavelet graph task") },
            { Name(s_CausticsNameRenderSoftwareCausticsResolveFourthWavelet), "Software Caustics Resolve Fourth Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics fourth-wavelet graph task") },
            { Name(s_CausticsNameRenderSoftwareCausticsResolveFifthWavelet), "Software Caustics Resolve Fifth Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics fifth-wavelet graph task") },
        }},
        .upsample = { Name(s_CausticsNameRenderSoftwareCausticsResolveUpsample), "Software Caustics Resolve Upsample", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-upsample graph task") },
        .timingClose = { Name(s_CausticsNameRenderSoftwareCausticsResolveTimingClose), "Software Caustics Resolve Timing Close", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve graph task") },
    };
    return declare(inputs, naming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<HardwareCausticsStageResult> HardwareCausticsStageBuilder::declare(const HardwareCausticsStageInputs& inputs, Core::Alloc::ScratchArena& scratchArena){
    using namespace RendererTaskGraphDetail;
    HardwareCausticsStageResult result{};
    if(!inputs.declaresHardwareCaustics)
        return result;
    if(
        !inputs.worldPosition.valid()
        || !inputs.depth.valid()
        || !inputs.currentCausticIrradiance.valid()
        || !inputs.currentBindlessSlots.valid()
        || !inputs.sceneShading.valid()
        || !inputs.lights.valid()
        || !inputs.graphicsPrefixTask.valid()
    )
        return MakeUnexpected(Failure{});
    if(m_raytracingSystem.hasHwCausticWork(inputs.meshViewSnapshot) && !inputs.opticalScene.valid())
        return MakeUnexpected(Failure{});

    CausticsResolveChainBuilder resolveChainBuilder(m_graph, m_raytracingSystem);
    const auto stageTargetsResult = resolveChainBuilder.importStageTargets(inputs.targets, true);
    if(!stageTargetsResult)
        return MakeUnexpected(Failure{});
    const CausticsStageTargets stageTargets = *stageTargetsResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwarePhotonResourceUses{ scratchArena };
    const b8 hardwareTraceGeometryStatesGraphOwned = inputs.hardwareTraceGeometrySet.valid();
    const RendererFramePipelineDetail::TraceResourceSetUses hardwarePhotonResourceSetUses = RendererFramePipelineDetail::MakeTraceResourceSetUses(inputs.hardwareTraceGeometrySet, inputs.traceMaterialSampledTextureSet);
    hardwarePhotonResourceUses.reserve(
        s_HardwarePhotonResourceUseCapacity
        + (inputs.rayTracingResources.csgTraceContextBuffer ? 1u : 0u)
        + inputs.rayTracingResources.csgTraceBoundsBuffers.size()
        + (hardwareTraceGeometryStatesGraphOwned ? 0u : inputs.hardwareTraceGeometryResources.size())
    );
    hardwarePhotonResourceUses.push_back(ReadTextureUse(inputs.worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource));
    hardwarePhotonResourceUses.push_back(ReadTextureUse(inputs.depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource));
    hardwarePhotonResourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    hardwarePhotonResourceUses.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    hardwarePhotonResourceUses.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(inputs.opticalScene.valid())
        hardwarePhotonResourceUses.push_back(ReadUse(inputs.opticalScene));
    hardwarePhotonResourceUses.push_back(ReadUse(stageTargets.sceneGeometryDomain));
    hardwarePhotonResourceUses.push_back(ReadWriteTextureUse(stageTargets.causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const auto resourceResult = ImportExternalBuffer(m_graph, buffer, identity, label);
        if(!resourceResult)
            return false;
        hardwarePhotonResourceUses.push_back(ReadUse(*resourceResult, state));
        return true;
    };
    b8 optionalResourcesImported =
        appendOptionalReadBuffer(inputs.meshViewSnapshot.buffer, Name(s_MeshViewName), s_MeshViewLabel, Core::ResourceStates::ConstantBuffer)
        && appendOptionalReadBuffer(
            inputs.rayTracingResources.shadowInstanceMaterialBuffer,
            Name(s_SceneShadowInstanceMaterialName),
            s_SceneShadowInstanceMaterialLabel,
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(s_SceneShadowMaterialTypedName), s_SceneShadowMaterialTypedLabel, Core::ResourceStates::ShaderResource)
        && appendOptionalReadBuffer(inputs.rayTracingResources.shadowInstanceBuffer, Name(s_SceneShadowInstancesName), s_SceneShadowInstancesLabel, Core::ResourceStates::ShaderResource)
        && appendOptionalReadBuffer(inputs.rayTracingResources.causticEmissionTargetBuffer, Name(s_HardwareCausticEmissionTargetsName), s_CausticEmissionTargetsLabel, Core::ResourceStates::ShaderResource)
    ;
    if(inputs.materialContextSlots.valid())
        hardwarePhotonResourceUses.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    // Candidate normal and seam checks read the frozen position and index streams.
    for(const Core::GpuGraphResourceId resource : inputs.hardwareTraceGeometryResources){
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid prepared hardware-caustics geometry resource"));
            return MakeUnexpected(Failure{});
        }
        if(!hardwareTraceGeometryStatesGraphOwned)
            hardwarePhotonResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    if(inputs.rayTracingResources.sceneTlas){
        const auto tlas = m_graph.importAccelStruct(
            inputs.rayTracingResources.sceneTlas,
            AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel)
                .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
        );
        optionalResourcesImported = optionalResourcesImported && tlas.has_value();
        if(tlas.has_value())
            hardwarePhotonResourceUses.push_back(ReadUse(*tlas, Core::ResourceStates::AccelStructRead));
    }
    optionalResourcesImported = optionalResourcesImported && RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
        inputs.rayTracingResources.csgTraceContextBuffer,
        inputs.rayTracingResources.csgTraceBoundsBuffers,
        m_graph,
        hardwarePhotonResourceUses
    );
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
        return MakeUnexpected(Failure{});
    }

    const Array<Core::GpuTaskId, 1u> hardwareDependencies = { inputs.graphicsPrefixTask };
    const Span<const Core::GpuExternalCompletionId> hardwareExternalDependencies = inputs.features.laggedLightingHistoryWriterWaitPending
        ? Span<const Core::GpuExternalCompletionId>(&inputs.historyWriterDrainCompletion, 1u)
        : Span<const Core::GpuExternalCompletionId>{};
    const Array<Core::GpuTaskExternalStateSource, 1u> accumulatorStateSources = {
        Core::GpuTaskExternalStateSource{ .states = inputs.accumulatorPersistentState.source(), },
    };
    const Span<const Core::GpuTaskExternalStateSource> accumulatorStateSourceSpan = inputs.accumulatorPersistentState.valid()
        ? Span<const Core::GpuTaskExternalStateSource>(accumulatorStateSources)
        : Span<const Core::GpuTaskExternalStateSource>{};

    const auto prefixTasksResult = resolveChainBuilder.declarePrefixTasks(CausticsStagePrefixInputs{
        .targets = inputs.targets,
        .meshViewSnapshot = inputs.meshViewSnapshot,
        .shadowPreparationReady = inputs.shadowPreparationReady,
        .timingTicket = inputs.timingTicket,
        .photonTiming = inputs.photonTiming,
        .initialDependencies = hardwareDependencies,
        .externalDependencies = hardwareExternalDependencies,
        .irradianceClearStateSources = {},
        .accumulatorStateSources = accumulatorStateSourceSpan,
        .causticIrradiance = inputs.currentCausticIrradiance,
        .causticAccumulator = stageTargets.causticAccumulator,
        .causticAccumulatorInitialized = inputs.rayTracingResources.causticAccumulatorInitialized,
        .hardwareCaustics = true,
    });
    if(!prefixTasksResult)
        return MakeUnexpected(Failure{});
    result.causticIrradianceClearTask = prefixTasksResult->causticIrradianceClearTask;
    result.causticAccumulatorBootstrapClearTask = prefixTasksResult->causticAccumulatorBootstrapClearTask;
    result.causticAccumulatorDecayTask = prefixTasksResult->causticAccumulatorDecayTask;

    Core::GpuTaskSchedulingHint hardwareCausticsScheduling;
    hardwareCausticsScheduling.cost = Core::GpuTaskCostHint::Large;
    hardwareCausticsScheduling.forceSubmissionBoundary = false;
    hardwareCausticsScheduling.allowPacketMerge = true;
    hardwareCausticsScheduling.mergeWithPrevious = true;
    // Keep photon/geometry/resolve stages in one Hardware Caustics timing chain.
    hardwareCausticsScheduling.allowMergeAcrossConsumerFrontier = true;
    EnableSameFamilyComputeEffectRouting(hardwareCausticsScheduling);
    EnableCrossFamilyComputeEffectRouting(hardwareCausticsScheduling);

    Core::GpuTaskDesc hardwarePhotonDesc;
    hardwarePhotonDesc
        .setIdentity(Name(s_RenderNameRenderHardwareCausticsPhotons))
        .setMarkerLabel("Hardware Caustic Photons")
        .setScheduling(hardwareCausticsScheduling)
        .setDependencies({ &prefixTasksResult->photonDependencyTask, 1u })
        .setResourceUses(hardwarePhotonResourceUses)
        .setResourceSetUses({ hardwarePhotonResourceSetUses.useCount != 0u ? hardwarePhotonResourceSetUses.uses.data() : nullptr, hardwarePhotonResourceSetUses.useCount })
    ;
    const auto causticPhotonTaskResult = m_raytracingSystem.declareHardwareCausticsTask(
        m_graph,
        hardwarePhotonDesc,
        inputs.lightingResources,
        inputs.meshViewSnapshot,
        inputs.shadowPreparationReady,
        prefixTasksResult->graphOwnsAccumulatorBootstrapClear,
        inputs.targets,
        inputs.timingTicket,
        inputs.photonTiming,
        inputs.producerDispatched
    );
    if(!causticPhotonTaskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics photon graph task"));
        return MakeUnexpected(Failure{});
    }
    result.causticPhotonTask = *causticPhotonTaskResult;

    const auto geometryAndResolveResult = resolveChainBuilder.declareGeometryAndResolve(CausticsGeometryAndResolveInputs{
        .targets = inputs.targets,
        .producerDispatched = inputs.producerDispatched,
        .timingTicket = inputs.timingTicket,
        .resolveTiming = inputs.resolveTiming,
        .stageTargets = stageTargets,
        .worldPosition = inputs.worldPosition,
        .depth = inputs.depth,
        .causticIrradiance = inputs.currentCausticIrradiance,
        .photonTask = result.causticPhotonTask,
        .baseScheduling = hardwareCausticsScheduling,
        .geometryStateSources = {},
        .resolveStateSources = {},
        .hardwareCaustics = true,
    });
    if(!geometryAndResolveResult)
        return MakeUnexpected(Failure{});
    result.causticGeometryTask = geometryAndResolveResult->causticGeometryTask;
    result.causticResolvePrepareTask = geometryAndResolveResult->causticResolvePrepareTask;
    result.causticResolveWaveletTasks = geometryAndResolveResult->causticResolveWaveletTasks;
    result.causticResolveUpsampleTask = geometryAndResolveResult->causticResolveUpsampleTask;
    result.hardwareCausticsTask = geometryAndResolveResult->causticsTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
