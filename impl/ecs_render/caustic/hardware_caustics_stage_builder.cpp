// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hardware_caustics_stage_builder.h"

#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/caustic/caustics_resolve_chain.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
        .setIdentity(Name("render.hardware_caustics.photons"))
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

