// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/hardware_caustics_stage_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <impl/assets/graphics/caustic/resolve_binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HardwareCausticsStageBuilder::HardwareCausticsStageBuilder(
    Core::GpuTaskGraph& graph,
    RendererRayTracingSystem& raytracingSystem
)
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool HardwareCausticsStageBuilder::declare(
    const HardwareCausticsStageInputs& inputs,
    HardwareCausticsStageResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = HardwareCausticsStageResult{};
    if(!inputs.declaresHardwareCaustics)
        return true;
    if(
        !inputs.targets
        || !inputs.lightingResources
        || !inputs.meshViewSnapshot
        || !inputs.rayTracingResources
        || !inputs.features
        || !inputs.shadowPreparationReady
        || !inputs.historyWriterDrainCompletion
        || !inputs.accumulatorPersistentState
        || !inputs.producerDispatched
        || !inputs.timingTicket
        || !inputs.photonTiming
        || !inputs.resolveTiming
        || !inputs.worldPosition.valid()
        || !inputs.depth.valid()
        || !inputs.currentCausticIrradiance.valid()
        || !inputs.currentBindlessSlots.valid()
        || !inputs.sceneShading.valid()
        || !inputs.lights.valid()
        || !inputs.graphicsPrefixTask.valid()
        || (inputs.hardwareTraceAttributeResourceCount != 0u && !inputs.hardwareTraceAttributeResources)
    )
        return false;
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_graph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_graph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
        const Core::GpuGraphResourceId causticAccumulator = importTexture(
            inputs.targets->causticAccumulator,
            Name("render.hardware_caustics.accumulator"),
            "Caustic Accumulator"
        );
        const Core::GpuGraphResourceId causticHistory = importTexture(
            inputs.targets->causticHistory,
            Name("render.hardware_caustics.history"),
            "Caustic History"
        );
        const Core::GpuGraphResourceId causticResolveHalf = importTexture(
            inputs.targets->causticResolveHalf,
            Name("render.hardware_caustics.resolve_half"),
            "Caustic Resolve Half"
        );
        const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
            inputs.targets->causticResolveGeometry,
            Name("render.hardware_caustics.resolve_geometry"),
            "Caustic Resolve Geometry"
        );
        const Core::GpuGraphResourceId sceneGeometryDomain = m_graph.importHazardDomain(
            HazardDomainDesc(Name("render.hardware_caustics.scene_geometry"), "Scene Acceleration and Geometry")
        );
        if(
            !causticAccumulator.valid()
            || !causticHistory.valid()
            || !causticResolveHalf.valid()
            || !causticResolveGeometry.valid()
            || !sceneGeometryDomain.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"));
            return false;
        }

        Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwarePhotonResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareGeometryResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolvePrepareResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveSecondWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveThirdWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveFourthWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveFifthWaveletResourceUses{ hardwareCausticsScratchArena };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResolveUpsampleResourceUses{ hardwareCausticsScratchArena };
        const bool hardwareTraceAttributeStatesGraphOwned = inputs.hardwareTraceAttributeSet.valid();
        const Core::GpuTaskResourceSetUse hardwareTraceAttributeSetUse{
            .resourceSet = inputs.hardwareTraceAttributeSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{
            .resourceSet = inputs.traceMaterialSampledTextureSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskResourceSetUse hardwarePhotonResourceSetUses[2u] = {};
        usize hardwarePhotonResourceSetUseCount = 0u;
        if(hardwareTraceAttributeStatesGraphOwned){
            hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = hardwareTraceAttributeSetUse;
        }
        if(inputs.traceMaterialSampledTextureSet.valid()){
            hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = traceMaterialSampledTextureSetUse;
        }
        hardwarePhotonResourceUses.reserve(15u + (
            hardwareTraceAttributeStatesGraphOwned ? 0u : inputs.hardwareTraceAttributeResourceCount
        ));
        hardwareGeometryResourceUses.reserve(3u);
        hardwareResolvePrepareResourceUses.reserve(3u);
        hardwareResolveWaveletResourceUses.reserve(3u);
        hardwareResolveSecondWaveletResourceUses.reserve(3u);
        hardwareResolveThirdWaveletResourceUses.reserve(3u);
        hardwareResolveFourthWaveletResourceUses.reserve(3u);
        hardwareResolveFifthWaveletResourceUses.reserve(3u);
        hardwareResolveUpsampleResourceUses.reserve(5u);
        hardwarePhotonResourceUses.push_back(ReadTextureUse(
            inputs.worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwarePhotonResourceUses.push_back(ReadTextureUse(
            inputs.depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(
            inputs.currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(
            inputs.sceneShading,
            Core::ResourceStates::ConstantBuffer
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
        hardwarePhotonResourceUses.push_back(ReadUse(sceneGeometryDomain));
        hardwarePhotonResourceUses.push_back(ReadWriteTextureUse(
            causticAccumulator,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        // Geometry downsample feeds the wavelet cache; compiler owns the UAV-to-SRV handoff.
        hardwareGeometryResourceUses.push_back(ReadTextureUse(
            inputs.worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareGeometryResourceUses.push_back(ReadTextureUse(
            inputs.depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareGeometryResourceUses.push_back(WriteTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        // Prepare writes the parity-selected ping-pong target; wavelets own the alternating sequence.
        constexpr bool s_HardwareCausticResolvePrepareWritesHalf = (NWB_CAUSTIC_RESOLVE_PASS_COUNT % 2u) == 0u;
        hardwareResolvePrepareResourceUses.push_back(ReadTextureUse(
            causticAccumulator,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolvePrepareResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveSecondWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveThirdWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveFourthWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveFifthWaveletResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        if(s_HardwareCausticResolvePrepareWritesHalf){
            hardwareResolvePrepareResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        else{
            hardwareResolvePrepareResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveSecondWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveThirdWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFourthWaveletResourceUses.push_back(WriteTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
            hardwareResolveFifthWaveletResourceUses.push_back(WriteTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }

        // Upsample takes the final graph handoff; timing-close carries no resource use.
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            inputs.worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            inputs.depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(WriteTextureUse(
            inputs.currentCausticIrradiance,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        const auto appendOptionalReadBuffer = [&](
            const Core::BufferHandle& buffer,
            const Name& identity,
            const AStringView label,
            const Core::ResourceStates::Mask state
        ){
            if(!buffer)
                return true;
            const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
            if(!resource.valid())
                return false;
            hardwarePhotonResourceUses.push_back(ReadUse(resource, state));
            return true;
        };
        bool optionalResourcesImported =
            appendOptionalReadBuffer(
                (*inputs.meshViewSnapshot).buffer,
                Name("render.deferred.mesh_view"),
                "Mesh View",
                Core::ResourceStates::ConstantBuffer
            )
            && appendOptionalReadBuffer(
                inputs.rayTracingResources->shadowInstanceMaterialBuffer,
                Name("render.deferred_effects.instance_material"),
                "Shadow Instance Materials",
                Core::ResourceStates::ShaderResource
            )
            && appendOptionalReadBuffer(
                inputs.rayTracingResources->shadowMaterialTypedBuffer,
                Name("render.deferred_effects.material_typed"),
                "Shadow Typed Materials",
                Core::ResourceStates::ShaderResource
            )
            && appendOptionalReadBuffer(
                inputs.rayTracingResources->shadowInstanceBuffer,
                Name("render.deferred_effects.shadow_instances"),
                "Shadow Instances",
                Core::ResourceStates::ShaderResource
            )
            && appendOptionalReadBuffer(
                inputs.rayTracingResources->causticEmissionTargetBuffer,
                Name("render.hardware_caustics.emission_targets"),
                "Caustic Emission Targets",
                Core::ResourceStates::ShaderResource
            )
        ;
        if(inputs.materialContextSlots.valid()){
            hardwarePhotonResourceUses.push_back(ReadUse(
                inputs.materialContextSlots,
                Core::ResourceStates::ConstantBuffer
            ));
        }
        // Declare retained attribute handles here for the Prefix -> Caustics SRV handoff.
        for(usize resourceIndex = 0u; resourceIndex < inputs.hardwareTraceAttributeResourceCount; ++resourceIndex){
            const Core::GpuGraphResourceId resource = inputs.hardwareTraceAttributeResources[resourceIndex];
            if(!resource.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid prepared hardware-caustics attribute resource"));
                return false;
            }
            if(!hardwareTraceAttributeStatesGraphOwned)
                hardwarePhotonResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
        if(inputs.rayTracingResources->sceneTlas){
            const Core::GpuGraphResourceId tlas = m_graph.importAccelStruct(
                inputs.rayTracingResources->sceneTlas,
                AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
                    .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
            );
            optionalResourcesImported = optionalResourcesImported && tlas.valid();
            if(tlas.valid()){
                hardwarePhotonResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            }
        }
        if(!optionalResourcesImported){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
            return false;
        }

        const Core::GpuTaskId hardwareDependencies[] = { inputs.graphicsPrefixTask };
        const Core::GpuExternalCompletionId* const hardwareExternalDependencies = inputs.features->laggedLightingHistoryWriterWaitPending
            ? &(*inputs.historyWriterDrainCompletion)
            : nullptr
        ;
        const usize hardwareExternalDependencyCount = inputs.features->laggedLightingHistoryWriterWaitPending ? 1u : 0u;
        Core::GpuTaskSchedulingHint hardwareScheduling;
        hardwareScheduling.cost = Core::GpuTaskCostHint::Large;
        hardwareScheduling.forceSubmissionBoundary = true;
        hardwareScheduling.allowPacketMerge = false;
        EnableSameFamilyComputeEffectRouting(hardwareScheduling);
        EnableCrossFamilyComputeEffectRouting(hardwareScheduling);
        const Core::GpuTaskExternalStateSource accumulatorStateSources[] = {
            Core::GpuTaskExternalStateSource{
                .states = (*inputs.accumulatorPersistentState).source(),
            },
        };
        const usize accumulatorStateSourceCount = (*inputs.accumulatorPersistentState).valid()
            ? LengthOf(accumulatorStateSources)
            : 0u
        ;

        // Lagged-history completion must protect the first writer; fresh accumulator clears between them.
        Core::GpuTaskSchedulingHint irradianceClearScheduling;
        irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        irradianceClearScheduling.allowPacketMerge = true;
        // Prefer the same-class GraphicsRuntime lane; later successors retain it.
        EnableSameFamilyComputeEffectRouting(irradianceClearScheduling, false);
        EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling);


        Core::GpuTaskDesc irradianceClearDesc;
        irradianceClearDesc
            .setIdentity(Name("render.hardware_caustics.irradiance_clear"))
            .setMarkerLabel("Hardware Caustics Irradiance Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(irradianceClearScheduling)
            .setDependencies(hardwareDependencies, LengthOf(hardwareDependencies))
            .setExternalDependencies(hardwareExternalDependencies, hardwareExternalDependencyCount)
        ;
        Core::GpuClearTextureTaskDesc irradianceClear;
        irradianceClear.destination = inputs.currentCausticIrradiance;
        irradianceClear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        irradianceClear.valueType = Core::GpuClearTextureTaskValueType::Float;
        irradianceClear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        const Core::GpuTaskId irradianceClearTask = m_graph.addClearTextureTask(
            irradianceClearDesc,
            irradianceClear
        );
        if(!irradianceClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics irradiance clear"));
            return false;
        }
        outResult.causticIrradianceClearTask = irradianceClearTask;

        Core::GpuTaskId causticsDependency = irradianceClearTask;
        const bool graphOwnsNonTemporalAccumulatorClear = inputs.rayTracingResources->causticTemporalDecay <= 0.f;
        if(graphOwnsNonTemporalAccumulatorClear){
            Core::GpuTaskSchedulingHint accumulatorNonTemporalClearScheduling = irradianceClearScheduling;
            accumulatorNonTemporalClearScheduling.mergeWithPrevious = true;
            // Direct accumulator clear stays in the accepted producer/timing packet.
            accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;
            EnableSameFamilyComputeEffectRouting(accumulatorNonTemporalClearScheduling);
            Core::GpuTaskDesc accumulatorNonTemporalClearDesc;
            accumulatorNonTemporalClearDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_non_temporal_clear"))
                .setMarkerLabel("Hardware Caustics Accumulator Clear")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulatorNonTemporalClearScheduling)
                .setDependencies(&causticsDependency, 1u)
                .setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)
            ;
            Core::GpuClearTextureTaskDesc accumulatorNonTemporalClear;
            accumulatorNonTemporalClear.destination = causticAccumulator;
            accumulatorNonTemporalClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
            accumulatorNonTemporalClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
            accumulatorNonTemporalClear.uintValue = Core::UIntColor(0u);
            const Core::GpuTaskId accumulatorNonTemporalClearTask = m_graph.addClearTextureTask(
                accumulatorNonTemporalClearDesc,
                accumulatorNonTemporalClear
            );
            if(!accumulatorNonTemporalClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics non-temporal accumulator clear"));
                return false;
            }
            outResult.causticAccumulatorNonTemporalClearTask = accumulatorNonTemporalClearTask;
            causticsDependency = accumulatorNonTemporalClearTask;
        }
        const bool graphOwnsAccumulatorBootstrapClear =
            !inputs.rayTracingResources->causticAccumulatorInitialized
            && inputs.rayTracingResources->causticTemporalDecay > 0.f
        ;
        if(graphOwnsAccumulatorBootstrapClear){
            Core::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling;
            accumulatorBootstrapClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
            accumulatorBootstrapClearScheduling.allowPacketMerge = true;
            accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
            // Bootstrap clear follows irradiance clear in the accepted producer packet.
            accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;
            EnableSameFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
            EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling);
            Core::GpuTaskDesc accumulatorBootstrapClearDesc;
            accumulatorBootstrapClearDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_bootstrap_clear"))
                .setMarkerLabel("Hardware Caustics Accumulator Bootstrap Clear")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulatorBootstrapClearScheduling)
                .setDependencies(&irradianceClearTask, 1u)
                .setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)
            ;
            Core::GpuClearTextureTaskDesc accumulatorBootstrapClear;
            accumulatorBootstrapClear.destination = causticAccumulator;
            accumulatorBootstrapClear.subresources = ECSRenderDetail::s_CausticAccumulatorSubresources;
            accumulatorBootstrapClear.valueType = Core::GpuClearTextureTaskValueType::UInt;
            accumulatorBootstrapClear.uintValue = Core::UIntColor(0u);
            const Core::GpuTaskId accumulatorBootstrapClearTask = m_graph.addClearTextureTask(
                accumulatorBootstrapClearDesc,
                accumulatorBootstrapClear
            );
            if(!accumulatorBootstrapClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator bootstrap clear"));
                return false;
            }
            outResult.causticAccumulatorBootstrapClearTask = accumulatorBootstrapClearTask;
            causticsDependency = accumulatorBootstrapClearTask;
        }

        const bool graphOwnsAccumulatorDecay =
            inputs.rayTracingResources->causticAccumulatorInitialized
            && inputs.rayTracingResources->causticTemporalDecay > 0.f
        ;
        if(graphOwnsAccumulatorDecay){
            Core::GpuTaskSchedulingHint accumulatorDecayScheduling;
            accumulatorDecayScheduling.cost = Core::GpuTaskCostHint::Tiny;
            accumulatorDecayScheduling.allowPacketMerge = true;
            accumulatorDecayScheduling.mergeWithPrevious = true;
            // Direct accumulator decay stays in the accepted producer/timing packet.
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
                .setIdentity(Name("render.hardware_caustics.accumulator_decay"))
                .setMarkerLabel("Hardware Caustics Accumulator Decay")
                .setQueue(GraphicsPreferredComputeQueueRequest())
                .setScheduling(accumulatorDecayScheduling)
                .setDependencies(&causticsDependency, 1u)
                .setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)
                .setResourceUses(accumulatorDecayUses, LengthOf(accumulatorDecayUses))
            ;
            const Core::GpuTaskId accumulatorDecayTask = m_raytracingSystem.declareCausticAccumulatorDecayTask(
                m_graph,
                accumulatorDecayDesc,
                (*inputs.targets),
                (*inputs.meshViewSnapshot),
                inputs.shadowPreparationReady,
                inputs.rayTracingResources->causticTemporalDecay,
                true,
                (*inputs.timingTicket),
                inputs.photonTiming,
                true
            );
            if(!accumulatorDecayTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator decay"));
                return false;
            }
            outResult.causticAccumulatorDecayTask = accumulatorDecayTask;
            causticsDependency = accumulatorDecayTask;
        }

        Core::GpuTaskSchedulingHint hardwareCausticsScheduling = hardwareScheduling;
        hardwareCausticsScheduling.forceSubmissionBoundary = false;
        hardwareCausticsScheduling.allowPacketMerge = true;
        hardwareCausticsScheduling.mergeWithPrevious = true;
        // Keep photon/geometry/resolve stages in one Hardware Caustics timing chain.
        hardwareCausticsScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc hardwarePhotonDesc;
        hardwarePhotonDesc
            .setIdentity(Name("render.hardware_caustics.photons"))
            .setMarkerLabel("Hardware Caustic Photons")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareCausticsScheduling)
            .setDependencies(&causticsDependency, 1u)
            .setResourceUses(hardwarePhotonResourceUses.data(), hardwarePhotonResourceUses.size())
            .setResourceSetUses(
                hardwarePhotonResourceSetUseCount != 0u ? hardwarePhotonResourceSetUses : nullptr,
                hardwarePhotonResourceSetUseCount
            )
        ;
        outResult.causticPhotonTask = m_raytracingSystem.declareHardwareCausticsTask(
            m_graph,
            hardwarePhotonDesc,
            (*inputs.targets),
            (*inputs.lightingResources),
            (*inputs.meshViewSnapshot),
            inputs.shadowPreparationReady,
            (*inputs.timingTicket),
            true,
            graphOwnsAccumulatorBootstrapClear,
            graphOwnsNonTemporalAccumulatorClear,
            graphOwnsAccumulatorDecay,
            true,
            inputs.photonTiming,
            inputs.producerDispatched
        );
        if(!outResult.causticPhotonTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics photon graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareGeometryScheduling = hardwareCausticsScheduling;
        hardwareGeometryScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareGeometryDesc;
        hardwareGeometryDesc
            .setIdentity(Name("render.hardware_caustics.geometry_downsample"))
            .setMarkerLabel("Hardware Caustics Geometry Downsample")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareGeometryScheduling)
            .setDependencies(&outResult.causticPhotonTask, 1u)
            .setResourceUses(hardwareGeometryResourceUses.data(), hardwareGeometryResourceUses.size())
        ;
        outResult.causticGeometryTask = m_raytracingSystem.declareCausticGeometryDownsampleTask(
            m_graph,
            hardwareGeometryDesc,
            (*inputs.targets),
            (*inputs.timingTicket),
            inputs.producerDispatched,
            inputs.resolveTiming,
            true
        );
        if(!outResult.causticGeometryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics geometry graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolvePrepareScheduling = hardwareGeometryScheduling;
        hardwareResolvePrepareScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolvePrepareDesc;
        hardwareResolvePrepareDesc
            .setIdentity(Name("render.hardware_caustics.resolve_prepare"))
            .setMarkerLabel("Hardware Caustics Resolve Prepare")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolvePrepareScheduling)
            .setDependencies(&outResult.causticGeometryTask, 1u)
            .setResourceUses(hardwareResolvePrepareResourceUses.data(), hardwareResolvePrepareResourceUses.size())
        ;
        outResult.causticResolvePrepareTask = m_raytracingSystem.declareCausticResolvePrepareTask(
            m_graph,
            hardwareResolvePrepareDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolvePrepareTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveWaveletScheduling = hardwareResolvePrepareScheduling;
        hardwareResolveWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveWaveletDesc;
        hardwareResolveWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveWaveletScheduling)
            .setDependencies(&outResult.causticResolvePrepareTask, 1u)
            .setResourceUses(hardwareResolveWaveletResourceUses.data(), hardwareResolveWaveletResourceUses.size())
        ;
        outResult.causticResolveWaveletTask = m_raytracingSystem.declareCausticResolveWaveletTask(
            m_graph,
            hardwareResolveWaveletDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolveWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveSecondWaveletScheduling = hardwareResolveWaveletScheduling;
        hardwareResolveSecondWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveSecondWaveletDesc;
        hardwareResolveSecondWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_second_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Second Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveSecondWaveletScheduling)
            .setDependencies(&outResult.causticResolveWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveSecondWaveletResourceUses.data(),
                hardwareResolveSecondWaveletResourceUses.size()
            )
        ;
        outResult.causticResolveSecondWaveletTask = m_raytracingSystem.declareCausticResolveSecondWaveletTask(
            m_graph,
            hardwareResolveSecondWaveletDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolveSecondWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveThirdWaveletScheduling = hardwareResolveSecondWaveletScheduling;
        hardwareResolveThirdWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveThirdWaveletDesc;
        hardwareResolveThirdWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_third_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Third Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveThirdWaveletScheduling)
            .setDependencies(&outResult.causticResolveSecondWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveThirdWaveletResourceUses.data(),
                hardwareResolveThirdWaveletResourceUses.size()
            )
        ;
        outResult.causticResolveThirdWaveletTask = m_raytracingSystem.declareCausticResolveThirdWaveletTask(
            m_graph,
            hardwareResolveThirdWaveletDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolveThirdWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveFourthWaveletScheduling = hardwareResolveThirdWaveletScheduling;
        hardwareResolveFourthWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveFourthWaveletDesc;
        hardwareResolveFourthWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_fourth_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Fourth Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveFourthWaveletScheduling)
            .setDependencies(&outResult.causticResolveThirdWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveFourthWaveletResourceUses.data(),
                hardwareResolveFourthWaveletResourceUses.size()
            )
        ;
        outResult.causticResolveFourthWaveletTask = m_raytracingSystem.declareCausticResolveFourthWaveletTask(
            m_graph,
            hardwareResolveFourthWaveletDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolveFourthWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveFifthWaveletScheduling = hardwareResolveFourthWaveletScheduling;
        hardwareResolveFifthWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveFifthWaveletDesc;
        hardwareResolveFifthWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_fifth_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Fifth Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveFifthWaveletScheduling)
            .setDependencies(&outResult.causticResolveFourthWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveFifthWaveletResourceUses.data(),
                hardwareResolveFifthWaveletResourceUses.size()
            )
        ;
        outResult.causticResolveFifthWaveletTask = m_raytracingSystem.declareCausticResolveFifthWaveletTask(
            m_graph,
            hardwareResolveFifthWaveletDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolveFifthWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveUpsampleScheduling = hardwareResolveFifthWaveletScheduling;
        hardwareResolveUpsampleScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveUpsampleDesc;
        hardwareResolveUpsampleDesc
            .setIdentity(Name("render.hardware_caustics.resolve_upsample"))
            .setMarkerLabel("Hardware Caustics Resolve Upsample")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveUpsampleScheduling)
            .setDependencies(&outResult.causticResolveFifthWaveletTask, 1u)
            .setResourceUses(hardwareResolveUpsampleResourceUses.data(), hardwareResolveUpsampleResourceUses.size())
        ;
        outResult.causticResolveUpsampleTask = m_raytracingSystem.declareCausticResolveUpsampleTask(
            m_graph,
            hardwareResolveUpsampleDesc,
            (*inputs.targets),
            inputs.producerDispatched,
            true
        );
        if(!outResult.causticResolveUpsampleTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint hardwareResolveScheduling = hardwareResolveUpsampleScheduling;
        hardwareResolveScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveDesc;
        hardwareResolveDesc
            .setIdentity(Name("render.hardware_caustics.resolve_timing_close"))
            .setMarkerLabel("Hardware Caustics Resolve Timing Close")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(hardwareResolveScheduling)
            .setDependencies(&outResult.causticResolveUpsampleTask, 1u)
        ;
        outResult.hardwareCausticsTask = m_raytracingSystem.declareCausticResolveTask(
            m_graph,
            hardwareResolveDesc,
            (*inputs.timingTicket),
            inputs.producerDispatched,
            inputs.resolveTiming
        );
        if(!outResult.hardwareCausticsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task"));
            return false;
        }
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
