// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::declareDeferredSoftwareCausticsTask(
    const bool hardwareCaustics,
    DeferredFrameTargets& deferredTargets,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId causticIrradiance,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const softwareTraceGeometryResources,
    const usize softwareTraceGeometryResourceCount,
    const Core::GpuGraphResourceSetId softwareTraceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming
){
    using namespace RendererTaskGraphDetail;

    m_deferredSoftwareCausticsTask = {};
    m_deferredCausticIrradianceClearTask = {};
    m_deferredCausticAccumulatorBootstrapClearTask = {};
    m_deferredCausticAccumulatorNonTemporalClearTask = {};
    m_deferredCausticAccumulatorDecayTask = {};
    m_deferredCausticPhotonTask = {};
    m_deferredCausticGeometryTask = {};
    m_deferredCausticResolvePrepareTask = {};
    m_deferredCausticResolveWaveletTask = {};
    m_deferredCausticResolveSecondWaveletTask = {};
    m_deferredCausticResolveThirdWaveletTask = {};
    m_deferredCausticResolveFourthWaveletTask = {};
    m_deferredCausticResolveFifthWaveletTask = {};
    m_deferredCausticResolveUpsampleTask = {};
    m_deferredCausticProducerDispatched = false;

    // This remains the direct successor of Shadow Visibility in the deferred graph. A distinct Compute family is
    // optional; on other devices the compiler routes the same packet through Graphics.
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
        || (softwareTraceGeometryResourceCount != 0u && !softwareTraceGeometryResources)
    )
        return false;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId causticAccumulator = importTexture(
        deferredTargets.causticAccumulator,
        Name("render.software_caustics.accumulator"),
        "Caustic Accumulator"
    );
    const Core::GpuGraphResourceId causticHistory = importTexture(
        deferredTargets.causticHistory,
        Name("render.software_caustics.history"),
        "Caustic History"
    );
    const Core::GpuGraphResourceId causticResolveHalf = importTexture(
        deferredTargets.causticResolveHalf,
        Name("render.software_caustics.resolve_half"),
        "Caustic Resolve Half"
    );
    const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
        deferredTargets.causticResolveGeometry,
        Name("render.software_caustics.resolve_geometry"),
        "Caustic Resolve Geometry"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(
            Name("render.software_caustics.scene_geometry"),
            "Software BVH Scene Geometry"
        )
    );
    if(
        !worldPosition.valid()
        || !depth.valid()
        || !causticAccumulator.valid()
        || !causticHistory.valid()
        || !causticResolveHalf.valid()
        || !causticResolveGeometry.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred software-caustics graph resources"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> photonResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> geometryResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolvePrepareResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveSecondWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveThirdWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveFourthWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveFifthWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveUpsampleResourceUses{ scratchArena };
    const bool softwareTraceGeometryStatesGraphOwned = softwareTraceGeometrySet.valid();
    photonResourceUses.reserve(20u + (
        softwareTraceGeometryStatesGraphOwned
            ? 0u
            : softwareTraceGeometryResourceCount
    ));
    geometryResourceUses.reserve(3u);
    resolvePrepareResourceUses.reserve(3u);
    resolveWaveletResourceUses.reserve(3u);
    resolveSecondWaveletResourceUses.reserve(3u);
    resolveThirdWaveletResourceUses.reserve(3u);
    resolveFourthWaveletResourceUses.reserve(3u);
    resolveFifthWaveletResourceUses.reserve(3u);
    resolveUpsampleResourceUses.reserve(5u);
    photonResourceUses.push_back(ReadTextureUse(
        worldPosition,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    // Software caustics samples the bindless depth image, so its declared layout must match the shader read.
    photonResourceUses.push_back(ReadTextureUse(
        depth,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    photonResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    photonResourceUses.push_back(ReadWriteTextureUse(
        causticAccumulator,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::ResourceStates::UnorderedAccess
    ));
    photonResourceUses.push_back(ReadUse(sceneGeometryDomain));

    // Geometry downsample begins only after the selected photon producer. It writes the fresh cache before wavelet
    // resolve reads it; dynamic ping-pong transitions remain inside the latter callback.
    geometryResourceUses.push_back(ReadTextureUse(
        worldPosition,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    geometryResourceUses.push_back(ReadTextureUse(
        depth,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    geometryResourceUses.push_back(WriteTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::UnorderedAccess
    ));

    // Prepare consumes both immutable graph-produced inputs, then writes the parity-selected first ping-pong target.
    // It does not sample the ping-pong input for this stage; the five fixed wavelet passes own that alternating
    // read/write sequence, including their exact UAV-to-SRV handoffs before the native upsample tail begins.
    constexpr bool s_CausticResolvePrepareWritesHalf = (NWB_CAUSTIC_RESOLVE_PASS_COUNT % 2u) == 0u;
    resolvePrepareResourceUses.push_back(ReadTextureUse(
        causticAccumulator,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolvePrepareResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveWaveletResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveSecondWaveletResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveThirdWaveletResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveFourthWaveletResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveFifthWaveletResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    if(s_CausticResolvePrepareWritesHalf){
        resolvePrepareResourceUses.push_back(WriteTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveWaveletResourceUses.push_back(WriteTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveSecondWaveletResourceUses.push_back(ReadTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveSecondWaveletResourceUses.push_back(WriteTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveThirdWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveThirdWaveletResourceUses.push_back(WriteTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveFourthWaveletResourceUses.push_back(ReadTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveFourthWaveletResourceUses.push_back(WriteTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveFifthWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveFifthWaveletResourceUses.push_back(WriteTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    else{
        resolvePrepareResourceUses.push_back(WriteTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveWaveletResourceUses.push_back(ReadTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveWaveletResourceUses.push_back(WriteTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveSecondWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveSecondWaveletResourceUses.push_back(WriteTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveThirdWaveletResourceUses.push_back(ReadTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveThirdWaveletResourceUses.push_back(WriteTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveFourthWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveFourthWaveletResourceUses.push_back(WriteTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        resolveFifthWaveletResourceUses.push_back(ReadTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        resolveFifthWaveletResourceUses.push_back(WriteTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }

    // Upsample receives the exact fifth-wavelet UAV-to-SRV handoff, so normal graph recording does not repeat that
    // native state bridge. The following timing-close callback intentionally carries no resource use.
    resolveUpsampleResourceUses.push_back(ReadTextureUse(
        worldPosition,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveUpsampleResourceUses.push_back(ReadTextureUse(
        depth,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveUpsampleResourceUses.push_back(ReadTextureUse(
        causticResolveHalf,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveUpsampleResourceUses.push_back(ReadTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    ));
    resolveUpsampleResourceUses.push_back(WriteTextureUse(
        causticIrradiance,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::UnorderedAccess
    ));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        photonResourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const ECSRenderDetail::MeshViewBufferSnapshot meshViewBufferSnapshot = m_meshSystem.meshViewBufferSnapshot();
    const bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            Name("render.software_caustics.emission_targets"),
            "Caustic Emission Targets",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            meshViewBufferSnapshot.buffer,
            Name("render.deferred.mesh_view"),
            "Mesh View",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource
        )
    ;
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred software-caustics dynamic resource"));
        return false;
    }
    photonResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    photonResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid())
        photonResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!softwareTraceGeometryStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex){
            const Core::GpuGraphResourceId resource = softwareTraceGeometryResources[resourceIndex];
            if(!resource.valid())
                return false;
            photonResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    const Core::GpuTaskResourceSetUse softwareTraceGeometrySetUse{
        .resourceSet = softwareTraceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{
        .resourceSet = traceMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse photonResourceSetUses[2u] = {};
    usize photonResourceSetUseCount = 0u;
    if(softwareTraceGeometryStatesGraphOwned)
        photonResourceSetUses[photonResourceSetUseCount++] = softwareTraceGeometrySetUse;
    if(traceMaterialSampledTextureSet.valid())
        photonResourceSetUses[photonResourceSetUseCount++] = traceMaterialSampledTextureSetUse;

    const Core::GpuTaskExternalStateSource scratchStateSources[] = {
        Core::GpuTaskExternalStateSource{
            .states = m_causticsComputePersistentState.source(),
        },
    };
    const usize scratchStateSourceCount = m_causticsComputePersistentState.valid()
        ? LengthOf(scratchStateSources)
        : 0u
    ;
    const Core::GpuTaskExternalStateSource irradianceReturnStateSources[] = {
        Core::GpuTaskExternalStateSource{
            .states = m_causticIrradianceReturnState.source(),
            .applicableConsumerQueueClass = Core::CommandQueue::Compute,
        },
    };
    const usize irradianceReturnStateSourceCount = m_causticIrradianceReturnState.valid()
        ? LengthOf(irradianceReturnStateSources)
        : 0u
    ;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    const Core::GpuTaskId shadowVisibilityDependency[] = { m_deferredShadowVisibilityTask };


// Black irradiance is the no-producer result. Start the existing Software Caustics Compute packet with this
    // typed CopyDest clear, then explicitly merge the producer callback into it below. A fresh temporal
    // accumulator adds a second typed zero clear before the producer; its CPU initialized mirror commits only on
    // that producer packet's acceptance.
    Core::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    // This starts the independent effect packet, so choose the auxiliary lane rather than inheriting Shadow
    // Visibility's same-class transport. All following direct successors retain this selected lane.
    EnableSameFamilyComputeEffectRouting(irradianceClearScheduling, false);
    EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling);
    Core::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("render.software_caustics.irradiance_clear"))
        .setMarkerLabel("Software Caustics Irradiance Clear")
        .setQueue(ComputeTransferQueueRequest())
        .setScheduling(irradianceClearScheduling)
        .setDependencies(shadowVisibilityDependency, LengthOf(shadowVisibilityDependency))
        .setExternalStateSources(irradianceReturnStateSources, irradianceReturnStateSourceCount)
    ;
    Core::GpuClearTextureTaskDesc irradianceClear;
    irradianceClear.destination = causticIrradiance;
    irradianceClear.subresources = ECSRenderDetail::s_FramebufferSubresources;
    irradianceClear.valueType = Core::GpuClearTextureTaskValueType::Float;
    irradianceClear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
    const Core::GpuTaskId irradianceClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        irradianceClearDesc,
        irradianceClear
    );
    if(!irradianceClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics irradiance clear"));
        return false;
    }
    m_deferredCausticIrradianceClearTask = irradianceClearTask;

    Core::GpuTaskId causticsDependency = irradianceClearTask;
    const bool graphOwnsNonTemporalAccumulatorClear = m_rayTracingState.m_causticTemporalDecay <= 0.f;
    if(graphOwnsNonTemporalAccumulatorClear){
        Core::GpuTaskSchedulingHint accumulatorNonTemporalClearScheduling = irradianceClearScheduling;
        accumulatorNonTemporalClearScheduling.mergeWithPrevious = true;
        // The direct accumulator clear must remain in the accepted Software Caustics producer/timing packet.
        accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorNonTemporalClearScheduling);
        Core::GpuTaskDesc accumulatorNonTemporalClearDesc;
        accumulatorNonTemporalClearDesc
            .setIdentity(Name("render.software_caustics.accumulator_non_temporal_clear"))
            .setMarkerLabel("Software Caustics Accumulator Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(accumulatorNonTemporalClearScheduling)
            .setDependencies(&causticsDependency, 1u)
            .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        ;
        Core::GpuClearTextureTaskDesc accumulatorNonTemporalClear;
        accumulatorNonTemporalClear.destination = causticAccumulator;
        accumulatorNonTemporalClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
        accumulatorNonTemporalClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
        accumulatorNonTemporalClear.uintValue = Core::UIntColor(0u);
        const Core::GpuTaskId accumulatorNonTemporalClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            accumulatorNonTemporalClearDesc,
            accumulatorNonTemporalClear
        );
        if(!accumulatorNonTemporalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics non-temporal accumulator clear"));
            return false;
        }
        m_deferredCausticAccumulatorNonTemporalClearTask = accumulatorNonTemporalClearTask;
        causticsDependency = accumulatorNonTemporalClearTask;
    }
    const bool graphOwnsAccumulatorBootstrapClear =
        !m_rayTracingState.m_causticAccumulatorInitialized
        && m_rayTracingState.m_causticTemporalDecay > 0.f
    ;
    if(graphOwnsAccumulatorBootstrapClear){
        Core::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling;
        accumulatorBootstrapClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accumulatorBootstrapClearScheduling.allowPacketMerge = true;
        accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
        // The direct accumulator clear must remain in the accepted Software Caustics producer/timing packet.
        accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
        EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
        Core::GpuTaskDesc accumulatorBootstrapClearDesc;
        accumulatorBootstrapClearDesc
            .setIdentity(Name("render.software_caustics.accumulator_bootstrap_clear"))
            .setMarkerLabel("Software Caustics Accumulator Bootstrap Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(accumulatorBootstrapClearScheduling)
            .setDependencies(&irradianceClearTask, 1u)
            .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        ;
        Core::GpuClearTextureTaskDesc accumulatorBootstrapClear;
        accumulatorBootstrapClear.destination = causticAccumulator;
        accumulatorBootstrapClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
        accumulatorBootstrapClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
        accumulatorBootstrapClear.uintValue = Core::UIntColor(0u);
        const Core::GpuTaskId accumulatorBootstrapClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            accumulatorBootstrapClearDesc,
            accumulatorBootstrapClear
        );
        if(!accumulatorBootstrapClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics accumulator bootstrap clear"));
            return false;
        }
        m_deferredCausticAccumulatorBootstrapClearTask = accumulatorBootstrapClearTask;
        causticsDependency = accumulatorBootstrapClearTask;
    }

    const bool graphOwnsAccumulatorDecay =
        m_rayTracingState.m_causticAccumulatorInitialized
        && m_rayTracingState.m_causticTemporalDecay > 0.f
    ;
    if(graphOwnsAccumulatorDecay){
        Core::GpuTaskSchedulingHint accumulatorDecayScheduling;
        accumulatorDecayScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accumulatorDecayScheduling.allowPacketMerge = true;
        accumulatorDecayScheduling.mergeWithPrevious = true;
        // The direct accumulator decay must remain in the accepted Software Caustics producer/timing packet.
        accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;
        EnableSameFamilyComputeEffectRouting(accumulatorDecayScheduling);
        EnableCrossFamilyComputeEffectRouting(accumulatorDecayScheduling);
        const Core::GpuTaskResourceUse accumulatorDecayUses[] = {
            ReadWriteTextureUse(
                causticAccumulator,
                ECSRenderDetail::s_CausticAccumulatorSubresources,
                Core::ResourceStates::UnorderedAccess
            ),
        };
        Core::GpuTaskDesc accumulatorDecayDesc;
        accumulatorDecayDesc
            .setIdentity(Name("render.software_caustics.accumulator_decay"))
            .setMarkerLabel("Software Caustics Accumulator Decay")
            .setQueue(ComputePacketQueueRequest())
            .setScheduling(accumulatorDecayScheduling)
            .setDependencies(&causticsDependency, 1u)
            .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
            .setResourceUses(accumulatorDecayUses, LengthOf(accumulatorDecayUses))
        ;
        const Core::GpuTaskId accumulatorDecayTask = m_raytracingSystem.declareCausticAccumulatorDecayTask(
            m_deferredLightingTaskGraph,
            accumulatorDecayDesc,
            deferredTargets,
            &m_shadowPreparationOutcome.ready,
            m_rayTracingState.m_causticTemporalDecay,
            false,
            timingTicket,
            &causticPhotonTiming,
            true
        );
        if(!accumulatorDecayTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred software-caustics accumulator decay"));
            return false;
        }
        m_deferredCausticAccumulatorDecayTask = accumulatorDecayTask;
        causticsDependency = accumulatorDecayTask;
    }

    Core::GpuTaskSchedulingHint causticsScheduling = scheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    // Photon, geometry, and resolve stages are direct serial successors in one Software Caustics timing packet.
    causticsScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc photonDesc;
    photonDesc
        .setIdentity(Name("render.software_caustics.photons"))
        .setMarkerLabel("Software Caustic Photons")
        .setQueue(ComputeQueueRequest())
        .setScheduling(causticsScheduling)
        .setDependencies(&causticsDependency, 1u)
        .setResourceUses(photonResourceUses.data(), photonResourceUses.size())
        .setResourceSetUses(
            photonResourceSetUseCount != 0u ? photonResourceSetUses : nullptr,
            photonResourceSetUseCount
        )
    ;
    m_deferredCausticPhotonTask = m_raytracingSystem.declareSoftwareCausticsTask(
        m_deferredLightingTaskGraph,
        photonDesc,
        deferredTargets,
        &m_shadowPreparationOutcome.ready,
        timingTicket,
        true,
        graphOwnsAccumulatorBootstrapClear,
        graphOwnsNonTemporalAccumulatorClear,
        graphOwnsAccumulatorDecay,
        true,
        &causticPhotonTiming,
        &m_deferredCausticProducerDispatched
    );
    if(!m_deferredCausticPhotonTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics photon graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint geometryScheduling = causticsScheduling;
    geometryScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc geometryDesc;
    geometryDesc
        .setIdentity(Name("render.software_caustics.geometry_downsample"))
        .setMarkerLabel("Software Caustics Geometry Downsample")
        .setQueue(ComputeQueueRequest())
        .setScheduling(geometryScheduling)
        .setDependencies(&m_deferredCausticPhotonTask, 1u)
        .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        .setResourceUses(geometryResourceUses.data(), geometryResourceUses.size())
    ;
    m_deferredCausticGeometryTask = m_raytracingSystem.declareCausticGeometryDownsampleTask(
        m_deferredLightingTaskGraph,
        geometryDesc,
        deferredTargets,
        timingTicket,
        &m_deferredCausticProducerDispatched,
        &causticResolveTiming,
        true
    );
    if(!m_deferredCausticGeometryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics geometry graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolvePrepareScheduling = geometryScheduling;
    resolvePrepareScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolvePrepareDesc;
    resolvePrepareDesc
        .setIdentity(Name("render.software_caustics.resolve_prepare"))
        .setMarkerLabel("Software Caustics Resolve Prepare")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolvePrepareScheduling)
        .setDependencies(&m_deferredCausticGeometryTask, 1u)
        .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        .setResourceUses(resolvePrepareResourceUses.data(), resolvePrepareResourceUses.size())
    ;
    m_deferredCausticResolvePrepareTask = m_raytracingSystem.declareCausticResolvePrepareTask(
        m_deferredLightingTaskGraph,
        resolvePrepareDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolvePrepareTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-prepare graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveWaveletScheduling = resolvePrepareScheduling;
    resolveWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveWaveletDesc;
    resolveWaveletDesc
        .setIdentity(Name("render.software_caustics.resolve_wavelet"))
        .setMarkerLabel("Software Caustics Resolve Wavelet")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveWaveletScheduling)
        .setDependencies(&m_deferredCausticResolvePrepareTask, 1u)
        .setExternalStateSources(scratchStateSources, scratchStateSourceCount)
        .setResourceUses(resolveWaveletResourceUses.data(), resolveWaveletResourceUses.size())
    ;
    m_deferredCausticResolveWaveletTask = m_raytracingSystem.declareCausticResolveWaveletTask(
        m_deferredLightingTaskGraph,
        resolveWaveletDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolveWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics first-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveSecondWaveletScheduling = resolveWaveletScheduling;
    resolveSecondWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveSecondWaveletDesc;
    resolveSecondWaveletDesc
        .setIdentity(Name("render.software_caustics.resolve_second_wavelet"))
        .setMarkerLabel("Software Caustics Resolve Second Wavelet")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveSecondWaveletScheduling)
        .setDependencies(&m_deferredCausticResolveWaveletTask, 1u)
        .setResourceUses(resolveSecondWaveletResourceUses.data(), resolveSecondWaveletResourceUses.size())
    ;
    m_deferredCausticResolveSecondWaveletTask = m_raytracingSystem.declareCausticResolveSecondWaveletTask(
        m_deferredLightingTaskGraph,
        resolveSecondWaveletDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolveSecondWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics second-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveThirdWaveletScheduling = resolveSecondWaveletScheduling;
    resolveThirdWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveThirdWaveletDesc;
    resolveThirdWaveletDesc
        .setIdentity(Name("render.software_caustics.resolve_third_wavelet"))
        .setMarkerLabel("Software Caustics Resolve Third Wavelet")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveThirdWaveletScheduling)
        .setDependencies(&m_deferredCausticResolveSecondWaveletTask, 1u)
        .setResourceUses(resolveThirdWaveletResourceUses.data(), resolveThirdWaveletResourceUses.size())
    ;
    m_deferredCausticResolveThirdWaveletTask = m_raytracingSystem.declareCausticResolveThirdWaveletTask(
        m_deferredLightingTaskGraph,
        resolveThirdWaveletDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolveThirdWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics third-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveFourthWaveletScheduling = resolveThirdWaveletScheduling;
    resolveFourthWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveFourthWaveletDesc;
    resolveFourthWaveletDesc
        .setIdentity(Name("render.software_caustics.resolve_fourth_wavelet"))
        .setMarkerLabel("Software Caustics Resolve Fourth Wavelet")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveFourthWaveletScheduling)
        .setDependencies(&m_deferredCausticResolveThirdWaveletTask, 1u)
        .setResourceUses(resolveFourthWaveletResourceUses.data(), resolveFourthWaveletResourceUses.size())
    ;
    m_deferredCausticResolveFourthWaveletTask = m_raytracingSystem.declareCausticResolveFourthWaveletTask(
        m_deferredLightingTaskGraph,
        resolveFourthWaveletDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolveFourthWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics fourth-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveFifthWaveletScheduling = resolveFourthWaveletScheduling;
    resolveFifthWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveFifthWaveletDesc;
    resolveFifthWaveletDesc
        .setIdentity(Name("render.software_caustics.resolve_fifth_wavelet"))
        .setMarkerLabel("Software Caustics Resolve Fifth Wavelet")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveFifthWaveletScheduling)
        .setDependencies(&m_deferredCausticResolveFourthWaveletTask, 1u)
        .setResourceUses(resolveFifthWaveletResourceUses.data(), resolveFifthWaveletResourceUses.size())
    ;
    m_deferredCausticResolveFifthWaveletTask = m_raytracingSystem.declareCausticResolveFifthWaveletTask(
        m_deferredLightingTaskGraph,
        resolveFifthWaveletDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolveFifthWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics fifth-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveUpsampleScheduling = resolveFifthWaveletScheduling;
    resolveUpsampleScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveUpsampleDesc;
    resolveUpsampleDesc
        .setIdentity(Name("render.software_caustics.resolve_upsample"))
        .setMarkerLabel("Software Caustics Resolve Upsample")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveUpsampleScheduling)
        .setDependencies(&m_deferredCausticResolveFifthWaveletTask, 1u)
        .setResourceUses(resolveUpsampleResourceUses.data(), resolveUpsampleResourceUses.size())
    ;
    m_deferredCausticResolveUpsampleTask = m_raytracingSystem.declareCausticResolveUpsampleTask(
        m_deferredLightingTaskGraph,
        resolveUpsampleDesc,
        deferredTargets,
        &m_deferredCausticProducerDispatched,
        true
    );
    if(!m_deferredCausticResolveUpsampleTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-upsample graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint resolveScheduling = resolveUpsampleScheduling;
    resolveScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveDesc;
    resolveDesc
        .setIdentity(Name("render.software_caustics.resolve_timing_close"))
        .setMarkerLabel("Software Caustics Resolve Timing Close")
        .setQueue(ComputeQueueRequest())
        .setScheduling(resolveScheduling)
        .setDependencies(&m_deferredCausticResolveUpsampleTask, 1u)
    ;
    m_deferredSoftwareCausticsTask = m_raytracingSystem.declareCausticResolveTask(
        m_deferredLightingTaskGraph,
        resolveDesc,
        timingTicket,
        &m_deferredCausticProducerDispatched,
        &causticResolveTiming
    );
    if(!m_deferredSoftwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve graph task"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

