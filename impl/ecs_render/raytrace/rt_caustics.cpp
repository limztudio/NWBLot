// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/graphics/task_graph/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_HwRaygenExportName = "CausticHwRayGen";
static constexpr AStringView s_HwMissExportName = "CausticHwMiss";
static constexpr AStringView s_HwHitGroupExportName = "CausticHwHitGroup";

// A warm temporal accumulator decays before either photon route writes its atomic splats.  This remains a
// separate graph task so the compiler lowers the accumulator's UAV dependency into the producer callback rather
// than depending on a packet-local state reassertion after the decay dispatch.
struct CausticAccumulatorDecayGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        const bool* shadowVisibilityPrepared = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        f32 decayFactor = 0.f;
        bool hardwareCaustics = false;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.timingTicket
            || !payload.causticPhotonTiming
        )
            return false;

        // Match the selected producer's existing no-work and failed-shadow behavior.  A graph declaration can
        // still retain the accumulator dependency, but no dispatch is issued unless the producer would run.
        if(!payload.shadowVisibilityPrepared || !*payload.shadowVisibilityPrepared)
            return true;
        const bool hasWork = payload.hardwareCaustics
            ? payload.raytracingSystem->hasHwCausticWork(payload.meshView)
            : payload.raytracingSystem->hasCausticWork(payload.meshView)
        ;
        if(!hasWork)
            return true;

        // A prior rejected record can retry this task before its graph transaction gets discarded.  Release the
        // incomplete query reservation before starting the retry's one caustic-photons interval.
        if(payload.causticPhotonTiming->has_value()){
            payload.causticPhotonTiming->value().discardTiming();
            payload.causticPhotonTiming->reset();
        }
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.causticPhotonTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            payload.graphics->getDevice(),
            commandList
        );
        payload.causticPhotonTiming->value().finishMarker();
        const bool dispatched = payload.raytracingSystem->dispatchCausticAccumulatorDecay(
            commandList,
            *payload.targets,
            payload.decayFactor,
            payload.graphEntryStatesOwned
        );
        if(!dispatched){
            payload.causticPhotonTiming->value().discardTiming();
            payload.causticPhotonTiming->reset();
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned caustic accumulator decay pass failed"));
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
            payload.causticPhotonTiming->value().discardTiming();
            payload.causticPhotonTiming->reset();
        }
    }
};

// Caustic producers own typed graph-task payloads; RendererFramePipeline composes their packet chain. The renderer still
// supplies declaration-filtered external state until the graph has every producer in the same frame transaction.
struct SoftwareCausticsGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* shadowVisibilityPrepared = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAccumulatorBootstrapClear = false;
        bool graphOwnsNonTemporalAccumulatorClear = false;
        bool graphOwnsAccumulatorDecay = false;
        bool graphOwnsResolve = false;
        bool* causticProducerDispatched = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        if(!payload.raytracingSystem || !payload.targets || !payload.deferredLightingResources.valid() || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        // The typed graph clear retains black irradiance whenever no producer dispatches. Non-temporal reset, fresh
        // bootstrap, and warm temporal decay can all be graph-owned before this callback; direct callers retain the
        // legacy non-temporal reset here.
        if(!payload.graphOwnsNonTemporalAccumulatorClear)
            payload.raytracingSystem->clearNonTemporalCausticAccumulator(commandList, *payload.targets);
        if(payload.shadowVisibilityPrepared && *payload.shadowVisibilityPrepared){
            const bool causticsDispatched = payload.raytracingSystem->renderGpuBvhCaustics(
                commandList,
                payload.meshView,
                *payload.targets,
                payload.deferredLightingResources,
                payload.graphEntryStatesOwned,
                payload.graphOwnsAccumulatorBootstrapClear,
                payload.graphOwnsAccumulatorDecay,
                payload.graphOwnsResolve,
                payload.causticPhotonTiming
            );
            if(!causticsDispatched && payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
                payload.causticPhotonTiming->value().discardTiming();
                payload.causticPhotonTiming->reset();
            }
            if(payload.causticProducerDispatched)
                *payload.causticProducerDispatched = causticsDispatched;
            if(!causticsDispatched && payload.raytracingSystem->hasCausticWork(payload.meshView))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.raytracingSystem && payload.graphOwnsNonTemporalAccumulatorClear)
            payload.raytracingSystem->confirmCausticAccumulatorNonTemporalClear();
        if(
            payload.raytracingSystem
            && payload.graphOwnsAccumulatorBootstrapClear
            && payload.causticProducerDispatched
            && *payload.causticProducerDispatched
        )
            payload.raytracingSystem->confirmCausticAccumulatorBootstrapClear();
    }

    static void discarded(Payload& payload){
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        if(payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
            payload.causticPhotonTiming->value().discardTiming();
            payload.causticPhotonTiming->reset();
        }
    }
};


struct HardwareCausticsGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* shadowVisibilityPrepared = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAccumulatorBootstrapClear = false;
        bool graphOwnsNonTemporalAccumulatorClear = false;
        bool graphOwnsAccumulatorDecay = false;
        bool graphOwnsResolve = false;
        bool* causticProducerDispatched = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        if(!payload.raytracingSystem || !payload.targets || !payload.deferredLightingResources.valid() || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        // The typed graph clear retains black irradiance whenever no producer dispatches. Non-temporal reset, fresh
        // bootstrap, and warm temporal decay can all be graph-owned before this callback; direct callers retain the
        // legacy non-temporal reset here.
        if(!payload.graphOwnsNonTemporalAccumulatorClear)
            payload.raytracingSystem->clearNonTemporalCausticAccumulator(commandList, *payload.targets);
        if(payload.shadowVisibilityPrepared && *payload.shadowVisibilityPrepared){
            const bool causticsDispatched = payload.raytracingSystem->renderHwCaustics(
                commandList,
                payload.meshView,
                *payload.targets,
                payload.deferredLightingResources,
                payload.graphEntryStatesOwned,
                payload.graphOwnsAccumulatorBootstrapClear,
                payload.graphOwnsAccumulatorDecay,
                payload.graphOwnsResolve,
                payload.causticPhotonTiming
            );
            if(!causticsDispatched && payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
                payload.causticPhotonTiming->value().discardTiming();
                payload.causticPhotonTiming->reset();
            }
            if(payload.causticProducerDispatched)
                *payload.causticProducerDispatched = causticsDispatched;
            if(!causticsDispatched && payload.raytracingSystem->hasHwCausticWork(payload.meshView))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.raytracingSystem && payload.graphOwnsNonTemporalAccumulatorClear)
            payload.raytracingSystem->confirmCausticAccumulatorNonTemporalClear();
        if(
            payload.raytracingSystem
            && payload.graphOwnsAccumulatorBootstrapClear
            && payload.causticProducerDispatched
            && *payload.causticProducerDispatched
        )
            payload.raytracingSystem->confirmCausticAccumulatorBootstrapClear();
    }

    static void discarded(Payload& payload){
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        if(payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
            payload.causticPhotonTiming->value().discardTiming();
            payload.causticPhotonTiming->reset();
        }
    }
};

// Geometry downsample follows the selected photon producer in the same graph packet. Its timing begin is retained
// until wavelet resolve records the endpoint, preserving the established full-resolve interval across callbacks.
struct CausticGeometryDownsampleGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* causticProducerDispatched = nullptr;
        Optional<Core::GpuTimingMeasure>* causticResolveTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.graphics || !payload.targets || !payload.timingTicket || !payload.causticResolveTiming)
            return false;
        if(payload.causticResolveTiming->has_value()){
            payload.causticResolveTiming->value().discardTiming();
            payload.causticResolveTiming->reset();
        }
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.causticResolveTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_CausticResolve,
            payload.graphics->getDevice(),
            commandList
        );
        payload.raytracingSystem->dispatchGraphCausticGeometryDownsample(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        // The next callback writes the timestamp endpoint on this same primary command list. Close this callback's
        // nested marker now, before the packet recorder advances to the wavelet task marker.
        payload.causticResolveTiming->value().finishMarker();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.causticResolveTiming && payload.causticResolveTiming->has_value()){
            payload.causticResolveTiming->value().discardTiming();
            payload.causticResolveTiming->reset();
        }
    }
};


// Resolve prepare owns the first half-resolution ping-pong write. The following wavelet body receives its input and
// output states from graph barriers, while the later alternating passes stay inside the native callback.
struct CausticResolvePrepareGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolvePrepare(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The first wavelet pass consumes the prepare output and writes its counterpart. The next four alternating passes
// stay in separate graph callbacks, while only the upsample body stays native; this first read/write pair receives
// graph-owned entry states.
struct CausticResolveWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The second wavelet pass consumes the first graph-owned output and returns to the parity-selected surface. The next
// three alternating passes stay in separate graph callbacks, but this exact handoff receives graph-owned entry states.
struct CausticResolveSecondWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveSecondWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The third wavelet pass consumes the second graph-owned output and writes its counterpart. The next two alternating
// passes stay in separate graph callbacks, but this exact handoff receives graph-owned entry states.
struct CausticResolveThirdWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveThirdWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The fourth wavelet pass consumes the third graph-owned output and returns to the parity-selected surface. The final
// alternating pass stays in a separate graph callback, but this exact handoff receives graph-owned entry states.
struct CausticResolveFourthWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveFourthWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The fifth wavelet pass consumes the fourth graph-owned output and produces the fixed half-B upsample input. This
// final ping-pong handoff receives graph-owned entry states before the graph-owned upsample callback.
struct CausticResolveFifthWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveFifthWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// Upsample consumes the fifth-wavelet output after the compiler has lowered the final ping-pong UAV-to-SRV handoff.
// The following empty callback only closes the retained full-resolve timing interval.
struct CausticResolveUpsampleGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveUpsample(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The empty timing-close callback follows graph-owned upsample and finishes the retained full-resolve interval.
struct CausticResolveGraphTask{
    struct Payload{
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* causticProducerDispatched = nullptr;
        Optional<Core::GpuTimingMeasure>* causticResolveTiming = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.timingTicket || !payload.causticResolveTiming)
            return false;
        // Preserve the existing no-producer contract: the graph-owned irradiance clear remains authoritative and
        // no resolve dispatch is emitted when the selected photon producer did not record.
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched){
            if(payload.causticResolveTiming->has_value()){
                payload.causticResolveTiming->value().discardTiming();
                payload.causticResolveTiming->reset();
            }
            return true;
        }
        if(!payload.causticResolveTiming->has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.causticResolveTiming->value().finishTiming(commandList);
        payload.causticResolveTiming->reset();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.causticResolveTiming && payload.causticResolveTiming->has_value()){
            payload.causticResolveTiming->value().discardTiming();
            payload.causticResolveTiming->reset();
        }
    }
};

// A ping-pong resolve target with sampled and storage slots.
struct CausticResolvePassResources{
    Core::Texture* texture = nullptr;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
};


static void DispatchCausticResolvePass(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    Core::ComputePipeline& pipeline,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates,
    const CausticResolvePassResources& input,
    const CausticResolvePassResources& output,
    const f32 effectiveIntensity,
    const u32 stepWidth,
    const CausticResolveStage::Enum stage,
    const u32 groupsX,
    const u32 groupsY
){
    NWB_ASSERT(input.texture);
    NWB_ASSERT(output.texture);
    NWB_ASSERT(input.texture != output.texture);
    if(!graphOwnsPassEntryStates){
        // Shared G-buffer reads are graph-declared for normal callers. Compatibility callers retain their original
        // state setup, while later ping-pong passes explicitly establish their own dynamic input/output states.
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        commandList.setTextureState(input.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(output.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    CausticResolvePushConstants resolvePush;
    resolvePush.width = targets.width;
    resolvePush.height = targets.height;
    resolvePush.halfWidth = halfWidth;
    resolvePush.halfHeight = halfHeight;
    resolvePush.causticIntensity = effectiveIntensity;
    resolvePush.stepWidth = stepWidth;
    resolvePush.stage = static_cast<u32>(stage);
    resolvePush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
    resolvePush.depthSlot = targets.bindless.gbufferDepth.slot();
    resolvePush.inputColorSlot = input.sampledSlot;
    resolvePush.geometrySlot = targets.bindless.causticResolveGeometry.slot();
    resolvePush.accumulatorSlot = targets.bindless.causticAccumulator.slot();
    resolvePush.outputStorageSlot = output.storageSlot;

    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, pipeline);
    commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
    commandList.dispatch(groupsX, groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena){
    // Photon emission targets are world bounds of refractive instances. Freeze the gathered bytes here, while
    // preflight still owns capacity/descriptor selection; graph declaration retains only this immutable snapshot.
    m_preparedCausticEmissionTargetBytes.clear();
    m_rayTracingState.m_causticRefractiveInstanceCount = 0u;

    auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return true;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVector combinedMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector combinedMax = VectorReplicate(-s_RayTracingFiniteInfinity);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystem,
            m_meshSystem,
            entity,
            resolvedMesh,
            mesh
        );
        if(!meshReady || !mesh.csgLocalBounds.valid())
            continue;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_materialSystem.findMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(!materialInfo || !materialInfo->refractive)
            continue;

        const NWB::Impl::Scene::TransformComponent* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transform
            ? MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            )
            : MatrixIdentity()
        ;

        SIMDVector localMin = LoadFloatInt(mesh.csgLocalBounds.minBounds);
        SIMDVector localMax = LoadFloatInt(mesh.csgLocalBounds.maxBounds);
        if(resolvedMesh.runtime){
            // Conservative deformation inflation keeps skinned refractors in the emission domain.
            const SIMDVector center = VectorMultiply(VectorAdd(localMin, localMax), VectorReplicate(0.5f));
            const SIMDVector half = VectorMultiply(VectorSubtract(localMax, localMin), VectorReplicate(0.5f * s_CausticRuntimeBoundsInflation));
            localMin = VectorSubtract(center, half);
            localMax = VectorAdd(center, half);
        }
        SIMDVector worldMin{};
        SIMDVector worldMax{};
        if(!AabbTests::Transform(objectToWorld, localMin, localMax, worldMin, worldMax))
            continue;

        combinedMin = VectorMin(combinedMin, worldMin);
        combinedMax = VectorMax(combinedMax, worldMax);

        NwbCausticEmissionTargetGpu target;
        StoreFloat(VectorSetW(worldMin, 0.0f), &target.aabbMin);
        StoreFloat(VectorSetW(worldMax, 0.0f), &target.aabbMax);
        targets.push_back(target);
    }

    const u32 targetCount = static_cast<u32>(targets.size());
    if(targetCount == 0u){
        m_rayTracingState.m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        m_rayTracingState.m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        m_rayTracingState.m_causticRefractiveInstanceCount = 0u;
        return true;
    }

    if(!ensureCausticEmissionTargetBuffer(targetCount))
        return false;

    const usize targetByteCount = targets.size() * sizeof(NwbCausticEmissionTargetGpu);
    m_preparedCausticEmissionTargetBytes.resize(targetByteCount);
    NWB_MEMCPY(
        m_preparedCausticEmissionTargetBytes.data(),
        m_preparedCausticEmissionTargetBytes.size(),
        targets.data(),
        targetByteCount
    );

    StoreFloat(combinedMin, &m_rayTracingState.m_causticTargetBoundsMin);
    StoreFloat(combinedMax, &m_rayTracingState.m_causticTargetBoundsMax);
    m_rayTracingState.m_causticRefractiveInstanceCount = targetCount;

    return true;
}

bool RendererRayTracingSystem::retainPreparedCausticEmissionTargetUpload(
    Core::GpuTaskGraph& graph,
    Core::GpuUploadBlobId& outBlob
)const{
    outBlob = {};
    const u32 targetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
    if(targetCount == 0u)
        return m_preparedCausticEmissionTargetBytes.empty();

    const usize targetByteCount = static_cast<usize>(targetCount) * sizeof(NwbCausticEmissionTargetGpu);
    if(
        m_preparedCausticEmissionTargetBytes.size() != targetByteCount
        || !m_rayTracingState.m_causticEmissionTargetBuffer
        || m_rayTracingState.m_causticEmissionTargetCapacity < targetCount
        || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen caustic emission-target payload no longer matches preflight storage"));
        return false;
    }

    outBlob = graph.copyUploadData(
        m_preparedCausticEmissionTargetBytes.data(),
        targetByteCount,
        alignof(NwbCausticEmissionTargetGpu)
    );
    return outBlob.valid();
}

void RendererRayTracingSystem::releaseCausticEmissionTargetHeapHandle(){
    if(
        !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        && !m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.valid()
    )
        return;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(m_rayTracingState.m_causticEmissionTargetHeapHandle);
        heap.free(m_rayTracingState.m_causticMaterialContextSlotsHeapHandle);
    }
    m_rayTracingState.m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_causticMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::ensureCausticMaterialContextSlotsHeapHandle(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon selectors require the initialized global descriptor heap"));
        return false;
    }
    if(!m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon selectors require the ray-trace material-context payload"));
        return false;
    }

    Core::GpuDescriptorHandle& handle = m_rayTracingState.m_causticMaterialContextSlotsHeapHandle;
    if(handle.valid()){
        if(RayTracingDetail::IsHeapHandle(handle, Core::GpuDescriptorClass::UniformBuffer))
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic material-context selector has an unexpected descriptor class"));
        return false;
    }

    Core::GpuDescriptorHandle acquired;
    if(!RayTracingDetail::RegisterHeapBuffer(
        heap,
        *m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(),
        Core::GpuDescriptorClass::UniformBuffer,
        false,
        acquired
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register caustic material-context selector in the descriptor heap"));
        return false;
    }
    handle = acquired;
    return true;
}

bool RendererRayTracingSystem::createCausticTargets(DeferredFrameTargets& targets){
    // Full-res additive targets with half-res temporal resolve ping-pong.
    targets.causticIrradianceFormat = Core::Format::RGBA16_FLOAT;
    targets.causticAccumulatorFormat = Core::Format::R32_UINT;
    targets.causticHistoryFormat = Core::Format::RGBA16_FLOAT;

    // Fresh accumulators reseed the temporal EMA.
    m_rayTracingState.m_causticAccumulatorInitialized = false;
    m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setInUAV(true)
        // Graphics/async production and the graph-owned lagged-history transfer copy share this target concurrently.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/caustic/irradiance")
    ;
    targets.causticIrradiance = m_graphics.createTexture(irradianceDesc);
    if(!targets.causticIrradiance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic irradiance target"));
        return false;
    }

    // Deferred lighting samples resolved surfel GI, never the writable pool.
    targets.surfelIrradianceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc surfelIrradianceDesc;
    surfelIrradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.surfelIrradianceFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/gi/surfel_irradiance")
    ;
    targets.surfelIrradiance = m_graphics.createTexture(surfelIrradianceDesc);
    if(!targets.surfelIrradiance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel irradiance target"));
        return false;
    }

    // Transient half-resolution surfel resolve input.
    Core::TextureDesc surfelIrradianceHalfDesc;
    surfelIrradianceHalfDesc
        .setWidth(DivideUp(targets.width, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR)))
        .setHeight(DivideUp(targets.height, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR)))
        .setFormat(targets.surfelIrradianceFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/gi/surfel_irradiance_half")
    ;
    targets.surfelIrradianceHalf = m_graphics.createTexture(surfelIrradianceHalfDesc);
    if(!targets.surfelIrradianceHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel half-res irradiance target"));
        return false;
    }

    Core::TextureDesc accumulatorDesc;
    accumulatorDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(ECSRenderDetail::s_CausticAccumulatorChannelCount)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.causticAccumulatorFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/accumulator")
    ;
    targets.causticAccumulator = m_graphics.createTexture(accumulatorDesc);
    if(!targets.causticAccumulator){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator target"));
        return false;
    }

    Core::TextureDesc historyDesc;
    historyDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/atrous_half_a")
    ;
    targets.causticHistory = m_graphics.createTexture(historyDesc);
    if(!targets.causticHistory){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-A target"));
        return false;
    }

    Core::TextureDesc halfBDesc;
    halfBDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/atrous_half_b")
    ;
    targets.causticResolveHalf = m_graphics.createTexture(halfBDesc);
    if(!targets.causticResolveHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-B target"));
        return false;
    }

    // Half-resolution geometry cache for edge-aware resolve.
    Core::TextureDesc geometryDesc;
    geometryDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/resolve_geometry")
    ;
    targets.causticResolveGeometry = m_graphics.createTexture(geometryDesc);
    if(!targets.causticResolveGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    return true;
}

void RendererRayTracingSystem::clearNonTemporalCausticAccumulator(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticAccumulator)
        return;

    // Temporal splat accumulation persists; non-temporal accumulation is cleared per frame.
    if(causticTemporalDecay() <= 0.f){
        m_rayTracingState.m_causticAccumulatorInitialized = false;
        m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
    }
}

void RendererRayTracingSystem::confirmCausticAccumulatorNonTemporalClear(){
    m_rayTracingState.m_causticAccumulatorInitialized = false;
    m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
}

void RendererRayTracingSystem::confirmCausticAccumulatorBootstrapClear(){
    m_rayTracingState.m_causticAccumulatorInitialized = true;
}

void RendererRayTracingSystem::dispatchCausticResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CausticResolve, m_graphics.getDevice(), commandList);
    dispatchCausticGeometryDownsample(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolvePrepare(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolveFirstWavelet(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolveSecondWavelet(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolveThirdWavelet(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolveFourthWavelet(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolveFifthWavelet(commandList, targets, graphEntryStatesOwned);
    dispatchCausticWaveletResolve(commandList, targets, graphEntryStatesOwned);
}


void RendererRayTracingSystem::dispatchCausticGeometryDownsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The normal graph declares this writable cache separately from wavelet resolve, so the latter receives a
    // compiler-lowered UAV-to-SRV handoff. Direct callers retain the original native entry setup.
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    if(!graphEntryStatesOwned){
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }

    CausticGeometryDownsamplePushConstants geometryPush;
    geometryPush.width = targets.width;
    geometryPush.height = targets.height;
    geometryPush.halfWidth = halfWidth;
    geometryPush.halfHeight = halfHeight;
    geometryPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
    geometryPush.depthSlot = targets.bindless.gbufferDepth.slot();
    geometryPush.outputStorageSlot = targets.bindless.causticResolveGeometryStorage.slot();

    Core::ComputeState geometryState;
    geometryState.setPipeline(m_rayTracingState.m_causticGeometryDownsamplePipeline.get());
    commandList.setComputeState(geometryState);
    heap.bindCompute(commandList, *m_rayTracingState.m_causticGeometryDownsamplePipeline.get());
    commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
    commandList.dispatch(halfGroupsX, halfGroupsY, 1u);
}


void RendererRayTracingSystem::dispatchCausticResolvePrepare(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // Prepare reads accumulated photons and resolve geometry, then writes the parity-selected half-resolution target.
    // The normal graph supplies those exact entry states; compatibility callers retain the original native sequence.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    if(!graphEntryStatesOwned){
        commandList.setTextureState(
            targets.causticAccumulator.get(),
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::ShaderResource
        );
    }

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        effectiveIntensity,
        1u,
        CausticResolveStage::PrepareDownsample,
        halfGroupsX,
        halfGroupsY
    );
}


void RendererRayTracingSystem::dispatchCausticResolveFirstWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfB : halfA,
        prepareToHalfB ? halfA : halfB,
        effectiveIntensity,
        1u,
        CausticResolveStage::Wavelet,
        halfGroupsX,
        halfGroupsY
    );
}


void RendererRayTracingSystem::dispatchCausticResolveSecondWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        effectiveIntensity,
        2u,
        CausticResolveStage::Wavelet,
        halfGroupsX,
        halfGroupsY
    );
}


void RendererRayTracingSystem::dispatchCausticResolveThirdWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfB : halfA,
        prepareToHalfB ? halfA : halfB,
        effectiveIntensity,
        4u,
        CausticResolveStage::Wavelet,
        halfGroupsX,
        halfGroupsY
    );
}


void RendererRayTracingSystem::dispatchCausticResolveFourthWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        effectiveIntensity,
        8u,
        CausticResolveStage::Wavelet,
        halfGroupsX,
        halfGroupsY
    );
}


void RendererRayTracingSystem::dispatchCausticResolveFifthWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfB : halfA,
        prepareToHalfB ? halfA : halfB,
        effectiveIntensity,
        16u,
        CausticResolveStage::Wavelet,
        halfGroupsX,
        halfGroupsY
    );
}


void RendererRayTracingSystem::dispatchCausticWaveletResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The graph-owned five wavelet passes have completed. Only the fixed half-B upsample remains in this callback.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticIrradiance.get(), true);

    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    // Normalize temporal accumulation to retain non-temporal brightness.
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;

    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources irradiance{
        targets.causticIrradiance.get(),
        targets.bindless.causticIrradiance.slot(),
        targets.bindless.causticIrradianceStorage.slot()
    };
    // Edge-aware upsample into deferred-lighting irradiance.
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolvePipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        halfB,
        irradiance,
        effectiveIntensity,
        1u,
        CausticResolveStage::Upsample,
        fullGroupsX,
        fullGroupsY
    );
}

void RendererRayTracingSystem::prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor){
    // Bootstrap temporal accumulation once; later frames decay before atomic splats.
    if(!m_rayTracingState.m_causticAccumulatorInitialized){
        m_rayTracingState.m_causticAccumulatorInitialized = true;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
        return;
    }

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = decayFactor;
    decayPush.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();

    Core::ComputeState decayState;
    decayState.setPipeline(m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );

    // Order decay writes before photon atomic adds.
    commandList.commitBarriers();
}

bool RendererRayTracingSystem::dispatchCausticAccumulatorDecay(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const f32 decayFactor,
    const bool graphEntryStatesOwned
){
    if(
        !targets.causticAccumulator
        || !targets.bindless.causticAccumulatorStorage.valid()
        || !m_rayTracingState.m_causticAccumulatorDecayPipeline
    )
        return false;

    // The normal deferred graph arrives with the accumulator already lowered to UAV by this task's declared use.
    // Compatibility callers retain the native transition; the following graph-owned photon task receives the
    // compiler-planned UAV barrier, whereas direct callers keep their existing packet-local fence.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    if(!graphEntryStatesOwned){
        commandList.setTextureState(
            targets.causticAccumulator.get(),
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
    }

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = decayFactor;
    decayPush.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();

    Core::ComputeState decayState;
    decayState.setPipeline(m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );
    return true;
}

bool RendererRayTracingSystem::hasCausticWork()const noexcept{
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return hasCausticWork(meshView);
}

bool RendererRayTracingSystem::hasCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    // Software photons require a caustic light, refractor, and software scene BVH.
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    return
        !hardwareShadowSupported
        && m_rayTracingState.m_causticLightCount > 0u
        && m_rayTracingState.m_causticRefractiveInstanceCount > 0u
        && m_rayTracingState.m_sceneBvhInstanceCount > 0u
        && m_rayTracingState.m_swShadowMeshCount > 0u
        && m_rayTracingState.m_causticEmissionTargetBuffer
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        && m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.valid()
        && m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && meshView.bindingValid()
    ;
}

bool RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Prepare heap-only software pipelines once geometry and emission targets exist.
    if(m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery))
        return true;
    if(
        m_rayTracingState.m_causticRefractiveInstanceCount == 0u
        || m_rayTracingState.m_sceneBvhInstanceCount == 0u
        || m_rayTracingState.m_swShadowMeshCount == 0u
        || !m_rayTracingState.m_causticEmissionTargetBuffer
    )
        return true;
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !meshView.buffer
        || !meshView.heapHandle.valid()
        || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        meshView.heapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady = ensureCausticMaterialContextSlotsHeapHandle() && ensureSwCausticPipeline();
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticResolvePipeline()
    ;
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || ensureCausticAccumulatorDecayPipeline()
    ;
    return producerReady && resolveReady && temporalReady;
}

Core::GpuTaskId RendererRayTracingSystem::declareCausticAccumulatorDecayTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool* const shadowVisibilityPrepared,
    const f32 decayFactor,
    const bool hardwareCaustics,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticAccumulatorDecayGraphTask>(
        desc,
        __hidden_caustics::CausticAccumulatorDecayGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .meshView = meshView,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .timingTicket = &timingTicket,
            .causticPhotonTiming = causticPhotonTiming,
            .decayFactor = decayFactor,
            .hardwareCaustics = hardwareCaustics,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSoftwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool* const shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    bool* const causticProducerDispatched
){
    return graph.addTask<__hidden_caustics::SoftwareCausticsGraphTask>(
        desc,
        __hidden_caustics::SoftwareCausticsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .meshView = meshView,
            .timingTicket = &timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .graphOwnsAccumulatorDecay = graphOwnsAccumulatorDecay,
            .graphOwnsResolve = graphOwnsResolve,
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareHardwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool* const shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    bool* const causticProducerDispatched
){
    return graph.addTask<__hidden_caustics::HardwareCausticsGraphTask>(
        desc,
        __hidden_caustics::HardwareCausticsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .meshView = meshView,
            .timingTicket = &timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .graphOwnsAccumulatorDecay = graphOwnsAccumulatorDecay,
            .graphOwnsResolve = graphOwnsResolve,
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareCausticGeometryDownsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool* const causticProducerDispatched,
    Optional<Core::GpuTimingMeasure>* const causticResolveTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticGeometryDownsampleGraphTask>(
        desc,
        __hidden_caustics::CausticGeometryDownsampleGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .causticProducerDispatched = causticProducerDispatched,
            .causticResolveTiming = causticResolveTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool* const causticProducerDispatched,
    Optional<Core::GpuTimingMeasure>* const causticResolveTiming
){
    return graph.addTask<__hidden_caustics::CausticResolveGraphTask>(
        desc,
        __hidden_caustics::CausticResolveGraphTask::Payload{
            .timingTicket = &timingTicket,
            .causticProducerDispatched = causticProducerDispatched,
            .causticResolveTiming = causticResolveTiming,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolvePrepareTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolvePrepareGraphTask>(
        desc,
        __hidden_caustics::CausticResolvePrepareGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveSecondWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveSecondWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveSecondWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveThirdWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveThirdWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveThirdWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveFourthWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveFourthWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveFourthWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveFifthWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveFifthWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveFifthWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveUpsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveUpsampleGraphTask>(
        desc,
        __hidden_caustics::CausticResolveUpsampleGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

void RendererRayTracingSystem::dispatchGraphCausticGeometryDownsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticGeometryDownsample(commandList, targets, graphEntryStatesOwned);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveUpsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticWaveletResolve(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolvePrepare(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolvePrepare(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveFirstWavelet(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveSecondWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveSecondWavelet(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveThirdWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveThirdWavelet(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveFourthWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveFourthWavelet(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveFifthWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveFifthWavelet(commandList, targets, graphEntryStatesOwned, true);
}

bool RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const{
    return
        m_rayTracingState.m_causticResolvePipeline
        && m_rayTracingState.m_causticGeometryDownsamplePipeline
        && (temporalDecay <= 0.f || m_rayTracingState.m_causticAccumulatorDecayPipeline)
        && targets.causticAccumulator
        && targets.causticIrradiance
        && targets.causticHistory
        && targets.causticResolveHalf
        && targets.causticResolveGeometry
        && targets.bindless.valid()
        && targets.bindless.causticIrradianceStorage.valid()
        && targets.bindless.causticAccumulatorStorage.valid()
        && targets.bindless.causticHistoryStorage.valid()
        && targets.bindless.causticResolveHalfStorage.valid()
        && targets.bindless.causticResolveGeometryStorage.valid()
    ;
}

bool RendererRayTracingSystem::renderGpuBvhCaustics(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return renderGpuBvhCaustics(
        commandList,
        meshView,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        graphOwnsAccumulatorBootstrapClear,
        graphOwnsAccumulatorDecay,
        graphOwnsResolve,
        causticPhotonTiming
    );
}

bool RendererRayTracingSystem::renderGpuBvhCaustics(
    Core::CommandList& commandList,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Software photon producer runs before deferred lighting.

    if(!hasCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !m_rayTracingState.m_swCausticPipeline
        || !m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const u32 photonCount = s_CausticSwPhotonCount / temporalPhaseCount;

    const auto recordPhotons = [&](){
        if(temporalDecay > 0.f && !graphOwnsAccumulatorBootstrapClear && !graphOwnsAccumulatorDecay)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        if(!graphEntryStatesOwned){
            // Direct compatibility callers restore heap-selected traversal inputs locally. The normal deferred
            // graph declares and commits these descriptor-visible states before this callback begins.
            transitionSwShadowTraversalResources(commandList);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(meshView.buffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsAccumulatorDecay){
            commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
        }
        if(!graphEntryStatesOwned){
            commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned || !graphOwnsAccumulatorDecay)
            commandList.commitBarriers();

        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = m_rayTracingState.m_sceneBvhInstanceCount;
        // Temporal sampling phases retain the full-domain flux.
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticSwPhotonGridSide;
        pushConstants.frameIndex = m_rayTracingState.m_swCausticFrameIndex;
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = m_rayTracingState.m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = meshView.heapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::ComputeState computeState;
        computeState.setPipeline(m_rayTracingState.m_swCausticPipeline.get());
        commandList.setComputeState(computeState);
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        NWB_ASSERT(heap.isInitialized());
        heap.bindCompute(commandList, *m_rayTracingState.m_swCausticPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(photonCount, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        // Advance temporal phase only after recording a producer dispatch.
        m_rayTracingState.m_swCausticFrameIndex = m_rayTracingState.m_swCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            m_graphics.getDevice(),
            commandList
        );
        recordPhotons();
    }

    if(!graphOwnsResolve)
        dispatchCausticResolve(commandList, targets, graphEntryStatesOwned);

    if(!m_rayTracingState.m_swCausticDispatchLogged){
        m_rayTracingState.m_swCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticSwPhotonCount)
            , static_cast<u64>(m_rayTracingState.m_causticLightCount)
            , static_cast<u64>(m_rayTracingState.m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwCausticPipeline(){
    if(m_rayTracingState.m_swCausticPipeline)
        return true;
    if(m_rayTracingState.m_swCausticPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require the initialized global descriptor heap"));
        m_rayTracingState.m_swCausticPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_swCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Set 0 is push-only; resources come from the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        m_rayTracingState.m_swCausticBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_swCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding layout"));
            m_rayTracingState.m_swCausticPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_swCausticShader,
        AssetsGraphicsCaustic::s_SwPhotonShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SwCausticPhotons"
    )){
        m_rayTracingState.m_swCausticPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_swCausticShader)
        .addBindingLayout(m_rayTracingState.m_swCausticBindingLayout)
    ;
    // Global heap layouts occupy their fixed sets.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_swCausticPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_swCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        m_rayTracingState.m_swCausticPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticResolvePipeline(){
    if(m_rayTracingState.m_causticResolvePipeline)
        return true;
    if(m_rayTracingState.m_causticResolvePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic resolve requires the initialized global descriptor heap"));
        m_rayTracingState.m_causticResolvePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_causticResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Target-generation resources are selected through the push block.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticResolvePushConstants)));

        m_rayTracingState.m_causticResolveBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_causticResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve binding layout"));
            m_rayTracingState.m_causticResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_causticResolveShader,
        AssetsGraphicsCaustic::s_ResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticResolve"
    )){
        m_rayTracingState.m_causticResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_causticResolveShader)
        .addBindingLayout(m_rayTracingState.m_causticResolveBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_causticResolvePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_causticResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve compute pipeline"));
        m_rayTracingState.m_causticResolvePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(m_rayTracingState.m_causticGeometryDownsamplePipeline)
        return true;
    if(m_rayTracingState.m_causticGeometryDownsamplePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic geometry downsample requires the initialized global descriptor heap"));
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_causticGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticGeometryDownsamplePushConstants)));

        m_rayTracingState.m_causticGeometryDownsampleBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_causticGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding layout"));
            m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_causticGeometryDownsampleShader,
        AssetsGraphicsCaustic::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticGeometryDownsample"
    )){
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_causticGeometryDownsampleShader)
        .addBindingLayout(m_rayTracingState.m_causticGeometryDownsampleBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_causticGeometryDownsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_causticGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

f32 RendererRayTracingSystem::causticTemporalDecay(){
    return m_rayTracingState.m_causticTemporalDecay;
}

u32 RendererRayTracingSystem::causticTemporalPhaseCount(){
    // Reuse phases only after temporal history warms up.
    if(causticTemporalDecay() <= 0.f)
        return NWB_CAUSTIC_TEMPORAL_DISABLED_PHASE_COUNT;
    return m_rayTracingState.m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount
        ? s_CausticTemporalBootstrapPhaseCount
        : s_CausticTemporalConvergedPhaseCount
    ;
}

void RendererRayTracingSystem::advanceCausticTemporalReuse(){
    if(causticTemporalDecay() <= 0.f){
        m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
        return;
    }
    if(m_rayTracingState.m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount)
        m_rayTracingState.m_causticTemporalReuseFrameCount = m_rayTracingState.m_causticTemporalReuseFrameCount + 1u;
}

bool RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(m_rayTracingState.m_causticAccumulatorDecayPipeline)
        return true;
    if(m_rayTracingState.m_causticAccumulatorDecayPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic accumulator decay requires the initialized global descriptor heap"));
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_causticAccumulatorDecayBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // The accumulator is heap-selected through push constants.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticAccumulatorDecayPushConstants)));

        m_rayTracingState.m_causticAccumulatorDecayBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_causticAccumulatorDecayBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding layout"));
            m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_causticAccumulatorDecayShader,
        AssetsGraphicsCaustic::s_AccumulatorDecayShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticAccumulatorDecay"
    )){
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_causticAccumulatorDecayShader)
        .addBindingLayout(m_rayTracingState.m_causticAccumulatorDecayBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_causticAccumulatorDecayPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_causticAccumulatorDecayPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticRtPipeline(){
    if(m_rayTracingState.m_hwCausticPipeline && m_rayTracingState.m_hwCausticShaderTable)
        return true;
    if(m_rayTracingState.m_hwCausticPipeline || m_rayTracingState.m_hwCausticShaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RT caustic pipeline and shader table cache is inconsistent"));
        m_rayTracingState.m_hwCausticPipeline.reset();
        m_rayTracingState.m_hwCausticShaderTable.reset();
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }
    if(m_rayTracingState.m_hwCausticPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require the descriptor-buffer TLAS heap layout"));
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_hwCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        m_rayTracingState.m_hwCausticBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_hwCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding layout"));
            m_rayTracingState.m_hwCausticPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_shaderSystem.loadShader(raygenShader, AssetsGraphicsCaustic::s_HwRaygenShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::RayGeneration, "ECSRender_CausticHwRaygen")
        || !m_shaderSystem.loadShader(missShader, AssetsGraphicsCaustic::s_HwMissShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::Miss, "ECSRender_CausticHwMiss")
        || !m_shaderSystem.loadShader(closestHitShader, AssetsGraphicsCaustic::s_HwClosestHitShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::ClosestHit, "ECSRender_CausticHwClosestHit")
    ){
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(m_arena);
    // The iterative bounce loop needs no shader recursion.
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32) * 16u));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(m_rayTracingState.m_hwCausticBindingLayout);
    // Preserve global resource, sampler, and TLAS heap sets.
    pipelineDesc.addBindingLayout(heap.getResourceLayout());
    pipelineDesc.addBindingLayout(heap.getSamplerLayout());
    pipelineDesc.addBindingLayout(heap.getAccelStructLayout());

    Core::RayTracingPipelineShaderDesc raygenDesc(m_arena);
    raygenDesc.setShader(raygenShader).setExportName(__hidden_caustics::s_HwRaygenExportName);
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(m_arena);
    missDesc.setShader(missShader).setExportName(__hidden_caustics::s_HwMissExportName);
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(m_arena);
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName(__hidden_caustics::s_HwHitGroupExportName);
    pipelineDesc.addHitGroup(hitGroupDesc);

    Core::RayTracingPipelineHandle pipeline = device.createRayTracingPipeline(pipelineDesc);
    if(!pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic pipeline"));
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = pipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic shader table"));
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }
    if(
        !shaderTable->setRayGenerationShader(__hidden_caustics::s_HwRaygenExportName)
        || shaderTable->addMissShader(__hidden_caustics::s_HwMissExportName) != 0u
        || shaderTable->addHitGroup(__hidden_caustics::s_HwHitGroupExportName) != 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to populate RT caustic shader table"));
        m_rayTracingState.m_hwCausticPipelineFailed = true;
        return false;
    }

    m_rayTracingState.m_hwCausticPipeline = Move(pipeline);
    m_rayTracingState.m_hwCausticShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT caustic pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::hasHwCausticWork()const noexcept{
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return hasHwCausticWork(meshView);
}

bool RendererRayTracingSystem::hasHwCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    // Hardware photons require a caustic light, refractor, TLAS, and tracked mesh.
    return m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_rayTracingState.m_causticLightCount > 0u
        && m_rayTracingState.m_causticRefractiveInstanceCount > 0u
        && m_rayTracingState.m_tlas
        && m_rayTracingState.m_shadowMeshCount > 0u
        && m_rayTracingState.m_causticEmissionTargetBuffer
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        && m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.valid()
        && m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && meshView.bindingValid()
    ;
}

bool RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Prepare hardware resources once geometry and emission targets exist.
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(
        m_rayTracingState.m_causticRefractiveInstanceCount == 0u
        || !m_rayTracingState.m_tlas
        || m_rayTracingState.m_shadowMeshCount == 0u
        || !m_rayTracingState.m_causticEmissionTargetBuffer
    )
        return true;
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !meshView.buffer
        || !meshView.heapHandle.valid()
        || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        meshView.heapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady = ensureCausticMaterialContextSlotsHeapHandle() && ensureCausticRtPipeline();
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticResolvePipeline()
    ;
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || ensureCausticAccumulatorDecayPipeline()
    ;
    return producerReady && resolveReady && temporalReady;
}

bool RendererRayTracingSystem::renderHwCaustics(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return renderHwCaustics(
        commandList,
        meshView,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        graphOwnsAccumulatorBootstrapClear,
        graphOwnsAccumulatorDecay,
        graphOwnsResolve,
        causticPhotonTiming
    );
}

bool RendererRayTracingSystem::renderHwCaustics(
    Core::CommandList& commandList,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Hardware photons share the accumulator and resolve with the software reference.
    if(!hasHwCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    {
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        if(!heap.isInitialized() || !m_rayTracingState.m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch caustics without the descriptor-buffer TLAS heap handle"));
            return false;
        }
    }
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !m_rayTracingState.m_hwCausticPipeline
        || !m_rayTracingState.m_hwCausticShaderTable
        || !m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const u32 photonCount = s_CausticHwPhotonCount / temporalPhaseCount;

    const auto recordPhotons = [&](){
        if(temporalDecay > 0.f && !graphOwnsAccumulatorBootstrapClear && !graphOwnsAccumulatorDecay)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        if(!graphEntryStatesOwned){
            // Direct compatibility callers restore heap-selected static producer inputs locally. The normal deferred
            // graph declares and commits this descriptor-visible batch before the callback begins.
            for(u32 slot = 0u; slot < m_rayTracingState.m_shadowMeshCount; ++slot)
                commandList.setBufferState(m_rayTracingState.m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(meshView.buffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsAccumulatorDecay){
            commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
        }
        if(!graphEntryStatesOwned || !graphOwnsAccumulatorDecay)
            commandList.commitBarriers();

        // Hardware and software producers use matching photon parameters.
        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = m_rayTracingState.m_tlasInstanceCount;
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticHwPhotonGridSide;
        pushConstants.frameIndex = m_rayTracingState.m_hwCausticFrameIndex;
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = m_rayTracingState.m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = meshView.heapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = m_rayTracingState.m_causticMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::RayTracingState rayTracingPassState;
        rayTracingPassState.setShaderTable(m_rayTracingState.m_hwCausticShaderTable.get());
        commandList.setRayTracingState(rayTracingPassState);
        // Bind heap blocks after RayTracingState; set 10 selects the TLAS generation.
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        heap.bindRayTracing(commandList, *m_rayTracingState.m_hwCausticPipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        Core::RayTracingDispatchRaysArguments dispatchArgs;
        dispatchArgs.setDimensions(s_CausticHwPhotonGridSide, s_CausticHwPhotonGridSide / temporalPhaseCount, 1u);
        commandList.dispatchRays(dispatchArgs);
        // Advance temporal phase only after recording a producer dispatch.
        m_rayTracingState.m_hwCausticFrameIndex = m_rayTracingState.m_hwCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            m_graphics.getDevice(),
            commandList
        );
        recordPhotons();
    }

    if(!graphOwnsResolve)
        dispatchCausticResolve(commandList, targets, graphEntryStatesOwned);

    if(!m_rayTracingState.m_hwCausticDispatchLogged){
        m_rayTracingState.m_hwCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched hardware caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticHwPhotonCount)
            , static_cast<u64>(m_rayTracingState.m_causticLightCount)
            , static_cast<u64>(m_rayTracingState.m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
    // Replace the heap slot before retiring the old emission-target buffer.
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic emission targets require the initialized global descriptor heap"));
        return false;
    }

    const auto acquireHeapHandle = [&](Core::Buffer& buffer, Core::GpuDescriptorHandle& outHandle) -> bool{
        if(!RayTracingDetail::RegisterHeapBuffer(
            heap,
            buffer,
            Core::GpuDescriptorClass::StorageBuffer,
            false,
            outHandle
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register caustic emission targets in the descriptor heap"));
            return false;
        }
        return true;
    };

    if(m_rayTracingState.m_causticEmissionTargetBuffer && m_rayTracingState.m_causticEmissionTargetCapacity >= targetCount){
        if(m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()){
            NWB_ASSERT(m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer);
            return true;
        }
        return acquireHeapHandle(
            *m_rayTracingState.m_causticEmissionTargetBuffer.get(),
            m_rayTracingState.m_causticEmissionTargetHeapHandle
        );
    }

    const usize capacity = ::NextGrowingCapacity(
        m_rayTracingState.m_causticEmissionTargetCapacity,
        targetCount,
        s_CausticEmissionTargetInitialCapacity
    );

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name("caustic_emission_targets"))
        // Graphics upload and async photon reads share this immutable per-frame input.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle targetBuffer = m_graphics.createBuffer(targetBufferDesc);
    if(!targetBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }

    Core::GpuDescriptorHandle targetHeapHandle = Core::GpuDescriptorHandle::invalid();
    if(!acquireHeapHandle(*targetBuffer.get(), targetHeapHandle))
        return false;

    if(m_rayTracingState.m_causticEmissionTargetHeapHandle.valid())
        heap.free(m_rayTracingState.m_causticEmissionTargetHeapHandle);
    m_rayTracingState.m_causticEmissionTargetBuffer = Move(targetBuffer);
    m_rayTracingState.m_causticEmissionTargetHeapHandle = targetHeapHandle;
    m_rayTracingState.m_causticEmissionTargetCapacity = capacity;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)")
        , static_cast<u64>(capacity)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

