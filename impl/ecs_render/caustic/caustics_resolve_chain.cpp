// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustics_resolve_chain.h"

#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
    const CausticsStageTargetNaming naming = hardwareCaustics
        ? CausticsStageTargetNaming{
            .accumulator = Name("render.hardware_caustics.accumulator"),
            .history = Name("render.hardware_caustics.history"),
            .resolveHalf = Name("render.hardware_caustics.resolve_half"),
            .resolveGeometry = Name("render.hardware_caustics.resolve_geometry"),
            .sceneGeometryDomain = Name("render.hardware_caustics.scene_geometry"),
            .sceneGeometryDomainLabel = s_SceneGeometryDomainLabel,
            .importWarnText = NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"),
        }
        : CausticsStageTargetNaming{
            .accumulator = Name("render.software_caustics.accumulator"),
            .history = Name("render.software_caustics.history"),
            .resolveHalf = Name("render.software_caustics.resolve_half"),
            .resolveGeometry = Name("render.software_caustics.resolve_geometry"),
            .sceneGeometryDomain = Name("render.software_caustics.scene_geometry"),
            .sceneGeometryDomainLabel = "Software BVH Scene Geometry",
            .importWarnText = NWB_TEXT("RendererSystem: could not import deferred software-caustics graph resources"),
        };

    const auto causticAccumulatorResult = ImportExternalTexture(m_graph, targets.causticAccumulator, naming.accumulator, s_CausticAccumulatorLabel);
    const auto causticHistoryResult = ImportExternalTexture(m_graph, targets.causticHistory, naming.history, s_CausticHistoryLabel);
    const auto causticResolveHalfResult = ImportExternalTexture(m_graph, targets.causticResolveHalf, naming.resolveHalf, s_CausticResolveHalfLabel);
    const auto causticResolveGeometryResult = ImportExternalTexture(m_graph, targets.causticResolveGeometry, naming.resolveGeometry, s_CausticResolveGeometryLabel);
    const auto sceneGeometryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(naming.sceneGeometryDomain, naming.sceneGeometryDomainLabel));
    if(!causticAccumulatorResult || !causticHistoryResult || !causticResolveHalfResult || !causticResolveGeometryResult || !sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(naming.importWarnText);
        return MakeUnexpected(Failure{});
    }
    return CausticsStageTargets{
        .causticAccumulator = *causticAccumulatorResult,
        .causticHistory = *causticHistoryResult,
        .causticResolveHalf = *causticResolveHalfResult,
        .causticResolveGeometry = *causticResolveGeometryResult,
        .sceneGeometryDomain = *sceneGeometryDomainResult,
    };
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
        .setIdentity(inputs.hardwareCaustics ? Name("render.hardware_caustics.irradiance_clear") : Name("render.software_caustics.irradiance_clear"))
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
            .setIdentity(inputs.hardwareCaustics ? Name("render.hardware_caustics.accumulator_bootstrap_clear") : Name("render.software_caustics.accumulator_bootstrap_clear"))
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
            .setIdentity(inputs.hardwareCaustics ? Name("render.hardware_caustics.accumulator_decay") : Name("render.software_caustics.accumulator_decay"))
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
        .setIdentity(inputs.hardwareCaustics ? Name("render.hardware_caustics.geometry_downsample") : Name("render.software_caustics.geometry_downsample"))
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
        .prepare = { Name("render.hardware_caustics.resolve_prepare"), "Hardware Caustics Resolve Prepare", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task") },
        .wavelets = {{
            { Name("render.hardware_caustics.resolve_wavelet"), "Hardware Caustics Resolve Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task") },
            { Name("render.hardware_caustics.resolve_second_wavelet"), "Hardware Caustics Resolve Second Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task") },
            { Name("render.hardware_caustics.resolve_third_wavelet"), "Hardware Caustics Resolve Third Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task") },
            { Name("render.hardware_caustics.resolve_fourth_wavelet"), "Hardware Caustics Resolve Fourth Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task") },
            { Name("render.hardware_caustics.resolve_fifth_wavelet"), "Hardware Caustics Resolve Fifth Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task") },
        }},
        .upsample = { Name("render.hardware_caustics.resolve_upsample"), "Hardware Caustics Resolve Upsample", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task") },
        .timingClose = { Name("render.hardware_caustics.resolve_timing_close"), "Hardware Caustics Resolve Timing Close", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task") },
    };
    return declare(inputs, naming);
}

Expected<CausticsResolveChainResult> CausticsResolveChainBuilder::declareSoftware(const CausticsResolveChainInputs& inputs){
    const CausticsResolveChainNaming naming{
        .prepare = { Name("render.software_caustics.resolve_prepare"), "Software Caustics Resolve Prepare", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-prepare graph task") },
        .wavelets = {{
            { Name("render.software_caustics.resolve_wavelet"), "Software Caustics Resolve Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics first-wavelet graph task") },
            { Name("render.software_caustics.resolve_second_wavelet"), "Software Caustics Resolve Second Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics second-wavelet graph task") },
            { Name("render.software_caustics.resolve_third_wavelet"), "Software Caustics Resolve Third Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics third-wavelet graph task") },
            { Name("render.software_caustics.resolve_fourth_wavelet"), "Software Caustics Resolve Fourth Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics fourth-wavelet graph task") },
            { Name("render.software_caustics.resolve_fifth_wavelet"), "Software Caustics Resolve Fifth Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics fifth-wavelet graph task") },
        }},
        .upsample = { Name("render.software_caustics.resolve_upsample"), "Software Caustics Resolve Upsample", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-upsample graph task") },
        .timingClose = { Name("render.software_caustics.resolve_timing_close"), "Software Caustics Resolve Timing Close", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve graph task") },
    };
    return declare(inputs, naming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

