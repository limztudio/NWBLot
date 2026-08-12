// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

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
            ? payload.raytracingSystem->hasHwCausticWork()
            : payload.raytracingSystem->hasCausticWork()
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

// Caustic producers own typed graph-task payloads; RendererSystem composes their packet chain. The renderer still
// supplies declaration-filtered external state until the graph has every producer in the same frame transaction.
struct SoftwareCausticsGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* shadowVisibilityPrepared = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAccumulatorBootstrapClear = false;
        bool graphOwnsNonTemporalAccumulatorClear = false;
        bool graphOwnsAccumulatorDecay = false;
        bool* accumulatorBootstrapProducerDispatched = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.accumulatorBootstrapProducerDispatched)
            *payload.accumulatorBootstrapProducerDispatched = false;
        if(!payload.raytracingSystem || !payload.targets || !payload.timingTicket)
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
                *payload.targets,
                payload.graphEntryStatesOwned,
                payload.graphOwnsAccumulatorBootstrapClear,
                payload.graphOwnsAccumulatorDecay,
                payload.causticPhotonTiming
            );
            if(!causticsDispatched && payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
                payload.causticPhotonTiming->value().discardTiming();
                payload.causticPhotonTiming->reset();
            }
            if(payload.accumulatorBootstrapProducerDispatched)
                *payload.accumulatorBootstrapProducerDispatched = causticsDispatched;
            if(!causticsDispatched && payload.raytracingSystem->hasCausticWork())
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
            && payload.accumulatorBootstrapProducerDispatched
            && *payload.accumulatorBootstrapProducerDispatched
        )
            payload.raytracingSystem->confirmCausticAccumulatorBootstrapClear();
    }

    static void discarded(Payload& payload){
        if(payload.accumulatorBootstrapProducerDispatched)
            *payload.accumulatorBootstrapProducerDispatched = false;
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
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* shadowVisibilityPrepared = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAccumulatorBootstrapClear = false;
        bool graphOwnsNonTemporalAccumulatorClear = false;
        bool graphOwnsAccumulatorDecay = false;
        bool* accumulatorBootstrapProducerDispatched = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.accumulatorBootstrapProducerDispatched)
            *payload.accumulatorBootstrapProducerDispatched = false;
        if(!payload.raytracingSystem || !payload.targets || !payload.timingTicket)
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
                *payload.targets,
                payload.graphEntryStatesOwned,
                payload.graphOwnsAccumulatorBootstrapClear,
                payload.graphOwnsAccumulatorDecay,
                payload.causticPhotonTiming
            );
            if(!causticsDispatched && payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
                payload.causticPhotonTiming->value().discardTiming();
                payload.causticPhotonTiming->reset();
            }
            if(payload.accumulatorBootstrapProducerDispatched)
                *payload.accumulatorBootstrapProducerDispatched = causticsDispatched;
            if(!causticsDispatched && payload.raytracingSystem->hasHwCausticWork())
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
            && payload.accumulatorBootstrapProducerDispatched
            && *payload.accumulatorBootstrapProducerDispatched
        )
            payload.raytracingSystem->confirmCausticAccumulatorBootstrapClear();
    }

    static void discarded(Payload& payload){
        if(payload.accumulatorBootstrapProducerDispatched)
            *payload.accumulatorBootstrapProducerDispatched = false;
        if(payload.causticPhotonTiming && payload.causticPhotonTiming->has_value()){
            payload.causticPhotonTiming->value().discardTiming();
            payload.causticPhotonTiming->reset();
        }
    }
};

// A ping-pong resolve target with sampled and storage slots.
struct CausticResolvePassResources{
    Core::Texture* texture = nullptr;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena){
    // Photon emission targets are world bounds of refractive instances. Freeze the gathered bytes here, while
    // preflight still owns capacity/descriptor selection; graph declaration retains only this immutable snapshot.
    m_preparedCausticEmissionTargetBytes.clear();
    rayTracingState().m_causticRefractiveInstanceCount = 0u;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return true;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVector combinedMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector combinedMax = VectorReplicate(-s_RayTracingFiniteInfinity);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        MeshResources* mesh = nullptr;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = __hidden_raytracing_system::ResolveRenderableMeshResources(
            *meshSystem,
            m_renderer.meshSystem(),
            entity,
            resolvedMesh,
            mesh
        );
        if(!meshReady || !mesh || !mesh->csgLocalBounds.valid())
            continue;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(!materialInfo || !materialInfo->refractive)
            continue;

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transform
            ? MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            )
            : MatrixIdentity()
        ;

        SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
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
        rayTracingState().m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticRefractiveInstanceCount = 0u;
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

    StoreFloat(combinedMin, &rayTracingState().m_causticTargetBoundsMin);
    StoreFloat(combinedMax, &rayTracingState().m_causticTargetBoundsMax);
    rayTracingState().m_causticRefractiveInstanceCount = targetCount;

    return true;
}

bool RendererRayTracingSystem::retainPreparedCausticEmissionTargetUpload(
    Core::GpuTaskGraph& graph,
    Core::GpuUploadBlobId& outBlob
)const{
    outBlob = {};
    const u32 targetCount = rayTracingState().m_causticRefractiveInstanceCount;
    if(targetCount == 0u)
        return m_preparedCausticEmissionTargetBytes.empty();

    const usize targetByteCount = static_cast<usize>(targetCount) * sizeof(NwbCausticEmissionTargetGpu);
    if(
        m_preparedCausticEmissionTargetBytes.size() != targetByteCount
        || !rayTracingState().m_causticEmissionTargetBuffer
        || rayTracingState().m_causticEmissionTargetCapacity < targetCount
        || !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        || rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
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

bool RendererRayTracingSystem::recordPreparedCausticEmissionTargets(Core::CommandList& commandList){
    const u32 targetCount = rayTracingState().m_causticRefractiveInstanceCount;
    if(targetCount == 0u)
        return m_preparedCausticEmissionTargetBytes.empty();

    const usize targetByteCount = static_cast<usize>(targetCount) * sizeof(NwbCausticEmissionTargetGpu);
    if(
        m_preparedCausticEmissionTargetBytes.size() != targetByteCount
        || !rayTracingState().m_causticEmissionTargetBuffer
        || rayTracingState().m_causticEmissionTargetCapacity < targetCount
        || !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
    ){
        // Capacity and descriptor registration are selected during preflight. A recording-time retry would replace
        // an imported graph resource, so retain the safe no-caustic fallback instead.
        rayTracingState().m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticRefractiveInstanceCount = 0u;
        return true;
    }

    Core::Buffer* const targetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    commandList.setBufferState(targetBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        targetBuffer,
        m_preparedCausticEmissionTargetBytes.data(),
        targetByteCount
    );
    commandList.setBufferState(targetBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

void RendererRayTracingSystem::releaseCausticEmissionTargetHeapHandle(){
    if(
        !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        && !rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
    )
        return;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(rayTracingState().m_causticEmissionTargetHeapHandle);
        heap.free(rayTracingState().m_causticMaterialContextSlotsHeapHandle);
    }
    rayTracingState().m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_causticMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::ensureCausticMaterialContextSlotsHeapHandle(){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon selectors require the initialized global descriptor heap"));
        return false;
    }
    if(!rayTracingState().m_rayTraceMaterialContextSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon selectors require the ray-trace material-context payload"));
        return false;
    }

    Core::GpuDescriptorHandle& handle = rayTracingState().m_causticMaterialContextSlotsHeapHandle;
    if(handle.valid()){
        if(__hidden_raytracing_system::IsHeapHandle(handle, Core::GpuDescriptorClass::UniformBuffer))
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic material-context selector has an unexpected descriptor class"));
        return false;
    }

    Core::GpuDescriptorHandle acquired;
    if(!__hidden_raytracing_system::RegisterHeapBuffer(
        heap,
        *rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(),
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
    rayTracingState().m_causticAccumulatorInitialized = false;
    rayTracingState().m_causticTemporalReuseFrameCount = 0u;

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
    targets.causticIrradiance = graphics().createTexture(irradianceDesc);
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
    targets.surfelIrradiance = graphics().createTexture(surfelIrradianceDesc);
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
        .setName("engine/gi/surfel_irradiance_half")
    ;
    targets.surfelIrradianceHalf = graphics().createTexture(surfelIrradianceHalfDesc);
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
        .setName("engine/caustic/accumulator")
    ;
    targets.causticAccumulator = graphics().createTexture(accumulatorDesc);
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
        .setName("engine/caustic/atrous_half_a")
    ;
    targets.causticHistory = graphics().createTexture(historyDesc);
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
        .setName("engine/caustic/atrous_half_b")
    ;
    targets.causticResolveHalf = graphics().createTexture(halfBDesc);
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
        .setName("engine/caustic/resolve_geometry")
    ;
    targets.causticResolveGeometry = graphics().createTexture(geometryDesc);
    if(!targets.causticResolveGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    return true;
}

void RendererRayTracingSystem::clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticIrradiance || !targets.causticAccumulator)
        return;

    // Black is the additive no-op when no producer runs.
    commandList.setTextureState(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));

    clearNonTemporalCausticAccumulator(commandList, targets);
}

void RendererRayTracingSystem::clearNonTemporalCausticAccumulator(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticAccumulator)
        return;

    // Temporal splat accumulation persists; non-temporal accumulation is cleared per frame.
    if(causticTemporalDecay() <= 0.f){
        rayTracingState().m_causticAccumulatorInitialized = false;
        rayTracingState().m_causticTemporalReuseFrameCount = 0u;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
    }
}

void RendererRayTracingSystem::confirmCausticAccumulatorNonTemporalClear(){
    rayTracingState().m_causticAccumulatorInitialized = false;
    rayTracingState().m_causticTemporalReuseFrameCount = 0u;
}

void RendererRayTracingSystem::confirmCausticAccumulatorBootstrapClear(){
    rayTracingState().m_causticAccumulatorInitialized = true;
}

void RendererRayTracingSystem::dispatchCausticResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticResolve, graphics().getDevice(), commandList);
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // Heap-selected resolve resources need explicit transitions and UAV ordering.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::ShaderResource);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticIrradiance.get(), true);

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    // Downsample geometry once for all edge-aware wavelet taps. The normal deferred graph already lowers these
    // descriptor-visible sources and the writable cache through the selected caustics task's declared uses; direct
    // compatibility callers retain the original native setup.
    {
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
        geometryState.setPipeline(rayTracingState().m_causticGeometryDownsamplePipeline.get());
        commandList.setComputeState(geometryState);
        heap.bindCompute(commandList, *rayTracingState().m_causticGeometryDownsamplePipeline.get());
        commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
        commandList.dispatch(halfGroupsX, halfGroupsY, 1u);
    }

    // Normalize temporal accumulation to retain non-temporal brightness.
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;

    const auto runPass = [&](const __hidden_caustics::CausticResolvePassResources& input, const __hidden_caustics::CausticResolvePassResources& output, const u32 stepWidth, const CausticResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        NWB_ASSERT(input.texture);
        NWB_ASSERT(output.texture);
        NWB_ASSERT(input.texture != output.texture);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(input.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(output.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

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
        computeState.setPipeline(rayTracingState().m_causticResolvePipeline.get());
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, *rayTracingState().m_causticResolvePipeline.get());
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // Seed parity so the final ping-pong result always lands in half-B.
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
    const __hidden_caustics::CausticResolvePassResources irradiance{
        targets.causticIrradiance.get(),
        targets.bindless.causticIrradiance.slot(),
        targets.bindless.causticIrradianceStorage.slot()
    };
    runPass(
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        1u, CausticResolveStage::PrepareDownsample, halfGroupsX, halfGroupsY
    );

    // Edge-aware half-resolution wavelet passes.
    bool srcIsHalfB = prepareToHalfB;
    for(u32 pass = 0u; pass < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT); ++pass){
        runPass(srcIsHalfB ? halfB : halfA, srcIsHalfB ? halfA : halfB, 1u << pass, CausticResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // Edge-aware upsample into deferred-lighting irradiance.
    runPass(halfB, irradiance, 1u, CausticResolveStage::Upsample, fullGroupsX, fullGroupsY);
}

void RendererRayTracingSystem::prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor){
    // Bootstrap temporal accumulation once; later frames decay before atomic splats.
    if(!rayTracingState().m_causticAccumulatorInitialized){
        rayTracingState().m_causticAccumulatorInitialized = true;
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
    decayState.setPipeline(rayTracingState().m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *rayTracingState().m_causticAccumulatorDecayPipeline.get());
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
        || !rayTracingState().m_causticAccumulatorDecayPipeline
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
    decayState.setPipeline(rayTracingState().m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *rayTracingState().m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );
    return true;
}

bool RendererRayTracingSystem::hasCausticWork()const noexcept{
    // Software photons require a caustic light, refractor, and software scene BVH.
    const bool hardwareShadowSupported =
        graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
    ;
    return
        !hardwareShadowSupported
        && rayTracingState().m_causticLightCount > 0u
        && rayTracingState().m_causticRefractiveInstanceCount > 0u
        && rayTracingState().m_sceneBvhInstanceCount > 0u
        && rayTracingState().m_swShadowMeshCount > 0u
        && rayTracingState().m_causticEmissionTargetBuffer
        && rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        && rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && drawState().m_meshViewBuffer
        && drawState().m_meshViewBufferHeapHandle.valid()
        && drawState().m_meshViewBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

bool RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Prepare heap-only software pipelines once geometry and emission targets exist.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && graphics().queryFeatureSupport(Core::Feature::RayQuery))
        return true;
    if(
        rayTracingState().m_causticRefractiveInstanceCount == 0u
        || rayTracingState().m_sceneBvhInstanceCount == 0u
        || rayTracingState().m_swShadowMeshCount == 0u
        || !rayTracingState().m_causticEmissionTargetBuffer
    )
        return true;
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !drawState().m_meshViewBuffer
        || !drawState().m_meshViewBufferHeapHandle.valid()
        || !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        drawState().m_meshViewBufferHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
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
            .graphics = &graphics(),
            .targets = &targets,
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
    const bool* const shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear,
    const bool graphOwnsAccumulatorDecay,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    bool* const accumulatorBootstrapProducerDispatched
){
    return graph.addTask<__hidden_caustics::SoftwareCausticsGraphTask>(
        desc,
        __hidden_caustics::SoftwareCausticsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .graphOwnsAccumulatorDecay = graphOwnsAccumulatorDecay,
            .accumulatorBootstrapProducerDispatched = accumulatorBootstrapProducerDispatched,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareHardwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear,
    const bool graphOwnsAccumulatorDecay,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    bool* const accumulatorBootstrapProducerDispatched
){
    return graph.addTask<__hidden_caustics::HardwareCausticsGraphTask>(
        desc,
        __hidden_caustics::HardwareCausticsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .graphOwnsAccumulatorDecay = graphOwnsAccumulatorDecay,
            .accumulatorBootstrapProducerDispatched = accumulatorBootstrapProducerDispatched,
        }
    );
}

bool RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const{
    return
        rayTracingState().m_causticResolvePipeline
        && rayTracingState().m_causticGeometryDownsamplePipeline
        && (temporalDecay <= 0.f || rayTracingState().m_causticAccumulatorDecayPipeline)
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
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Software photon producer runs before deferred lighting.

    if(!hasCausticWork())
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_swCausticPipeline
        || !rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
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
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsAccumulatorDecay){
            commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
        }
        if(!graphEntryStatesOwned){
            commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned || !graphOwnsAccumulatorDecay)
            commandList.commitBarriers();

        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        // Temporal sampling phases retain the full-domain flux.
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticSwPhotonGridSide;
        pushConstants.frameIndex = rayTracingState().m_swCausticFrameIndex;
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = rayTracingState().m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = drawState().m_meshViewBufferHeapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = rayTracingState().m_causticMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_swCausticPipeline.get());
        commandList.setComputeState(computeState);
        Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
        NWB_ASSERT(heap.isInitialized());
        heap.bindCompute(commandList, *rayTracingState().m_swCausticPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(photonCount, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        // Advance temporal phase only after recording a producer dispatch.
        rayTracingState().m_swCausticFrameIndex = rayTracingState().m_swCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            graphics().gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            graphics().getDevice(),
            commandList
        );
        recordPhotons();
    }

    dispatchCausticResolve(commandList, targets, graphEntryStatesOwned);

    if(!rayTracingState().m_swCausticDispatchLogged){
        rayTracingState().m_swCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticSwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwCausticPipeline(){
    if(rayTracingState().m_swCausticPipeline)
        return true;
    if(rayTracingState().m_swCausticPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require the initialized global descriptor heap"));
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_swCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Set 0 is push-only; resources come from the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_swCausticBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding layout"));
            rayTracingState().m_swCausticPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_swCausticShader,
        AssetsGraphicsCaustic::s_SwPhotonShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SwCausticPhotons"
    )){
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_swCausticShader)
        .addBindingLayout(rayTracingState().m_swCausticBindingLayout)
    ;
    // Global heap layouts occupy their fixed sets.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_swCausticPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_swCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticResolvePipeline(){
    if(rayTracingState().m_causticResolvePipeline)
        return true;
    if(rayTracingState().m_causticResolvePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic resolve requires the initialized global descriptor heap"));
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_causticResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Target-generation resources are selected through the push block.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticResolvePushConstants)));

        rayTracingState().m_causticResolveBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve binding layout"));
            rayTracingState().m_causticResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticResolveShader,
        AssetsGraphicsCaustic::s_ResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticResolve"
    )){
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticResolveShader)
        .addBindingLayout(rayTracingState().m_causticResolveBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_causticResolvePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve compute pipeline"));
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(rayTracingState().m_causticGeometryDownsamplePipeline)
        return true;
    if(rayTracingState().m_causticGeometryDownsamplePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic geometry downsample requires the initialized global descriptor heap"));
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticGeometryDownsamplePushConstants)));

        rayTracingState().m_causticGeometryDownsampleBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding layout"));
            rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticGeometryDownsampleShader,
        AssetsGraphicsCaustic::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticGeometryDownsample"
    )){
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticGeometryDownsampleShader)
        .addBindingLayout(rayTracingState().m_causticGeometryDownsampleBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_causticGeometryDownsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

f32 RendererRayTracingSystem::causticTemporalDecay(){
    return rayTracingState().m_causticTemporalDecay;
}

u32 RendererRayTracingSystem::causticTemporalPhaseCount(){
    // Reuse phases only after temporal history warms up.
    if(causticTemporalDecay() <= 0.f)
        return NWB_CAUSTIC_TEMPORAL_DISABLED_PHASE_COUNT;
    return rayTracingState().m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount
        ? s_CausticTemporalBootstrapPhaseCount
        : s_CausticTemporalConvergedPhaseCount
    ;
}

void RendererRayTracingSystem::advanceCausticTemporalReuse(){
    if(causticTemporalDecay() <= 0.f){
        rayTracingState().m_causticTemporalReuseFrameCount = 0u;
        return;
    }
    if(rayTracingState().m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount)
        rayTracingState().m_causticTemporalReuseFrameCount = rayTracingState().m_causticTemporalReuseFrameCount + 1u;
}

bool RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(rayTracingState().m_causticAccumulatorDecayPipeline)
        return true;
    if(rayTracingState().m_causticAccumulatorDecayPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic accumulator decay requires the initialized global descriptor heap"));
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // The accumulator is heap-selected through push constants.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticAccumulatorDecayPushConstants)));

        rayTracingState().m_causticAccumulatorDecayBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding layout"));
            rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticAccumulatorDecayShader,
        AssetsGraphicsCaustic::s_AccumulatorDecayShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticAccumulatorDecay"
    )){
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticAccumulatorDecayShader)
        .addBindingLayout(rayTracingState().m_causticAccumulatorDecayBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_causticAccumulatorDecayPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticAccumulatorDecayPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticRtPipeline(){
    if(rayTracingState().m_hwCausticPipeline)
        return true;
    if(rayTracingState().m_hwCausticPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_hwCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_hwCausticBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_hwCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding layout"));
            rayTracingState().m_hwCausticPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsCaustic::s_HwRaygenShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::RayGeneration, "ECSRender_CausticHwRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsCaustic::s_HwMissShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::Miss, "ECSRender_CausticHwMiss")
        || !m_renderer.shaderSystem().loadShader(closestHitShader, AssetsGraphicsCaustic::s_HwClosestHitShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::ClosestHit, "ECSRender_CausticHwClosestHit")
    ){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(arena());
    // The iterative bounce loop needs no shader recursion.
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32) * 16u));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(rayTracingState().m_hwCausticBindingLayout);
    // Preserve global resource, sampler, and TLAS heap sets.
    pipelineDesc.addBindingLayout(heap.getResourceLayout());
    pipelineDesc.addBindingLayout(heap.getSamplerLayout());
    pipelineDesc.addBindingLayout(heap.getAccelStructLayout());

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName(__hidden_caustics::s_HwRaygenExportName);
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName(__hidden_caustics::s_HwMissExportName);
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName(__hidden_caustics::s_HwHitGroupExportName);
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_hwCausticPipeline = device.createRayTracingPipeline(pipelineDesc);
    if(!rayTracingState().m_hwCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic pipeline"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = rayTracingState().m_hwCausticPipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic shader table"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }
    shaderTable->setRayGenerationShader(__hidden_caustics::s_HwRaygenExportName);
    shaderTable->addMissShader(__hidden_caustics::s_HwMissExportName);
    shaderTable->addHitGroup(__hidden_caustics::s_HwHitGroupExportName);
    rayTracingState().m_hwCausticShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT caustic pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::hasHwCausticWork()const noexcept{
    // Hardware photons require a caustic light, refractor, TLAS, and tracked mesh.
    return graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && rayTracingState().m_causticLightCount > 0u
        && rayTracingState().m_causticRefractiveInstanceCount > 0u
        && rayTracingState().m_tlas
        && rayTracingState().m_shadowMeshCount > 0u
        && rayTracingState().m_causticEmissionTargetBuffer
        && rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        && rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && drawState().m_meshViewBuffer
        && drawState().m_meshViewBufferHeapHandle.valid()
        && drawState().m_meshViewBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

bool RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Prepare hardware resources once geometry and emission targets exist.
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(
        rayTracingState().m_causticRefractiveInstanceCount == 0u
        || !rayTracingState().m_tlas
        || rayTracingState().m_shadowMeshCount == 0u
        || !rayTracingState().m_causticEmissionTargetBuffer
    )
        return true;
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !drawState().m_meshViewBuffer
        || !drawState().m_meshViewBufferHeapHandle.valid()
        || !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        drawState().m_meshViewBufferHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
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
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Hardware photons share the accumulator and resolve with the software reference.
    if(!hasHwCausticWork())
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    {
        Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
        if(!heap.isInitialized() || !rayTracingState().m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch caustics without the descriptor-buffer TLAS heap handle"));
            return false;
        }
    }
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_hwCausticPipeline
        || !rayTracingState().m_hwCausticShaderTable
        || !rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
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
            for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot)
                commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
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
        pushConstants.instanceCount = rayTracingState().m_tlasInstanceCount;
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticHwPhotonGridSide;
        pushConstants.frameIndex = rayTracingState().m_hwCausticFrameIndex;
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = rayTracingState().m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = drawState().m_meshViewBufferHeapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = rayTracingState().m_causticMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::RayTracingState rayTracingPassState;
        rayTracingPassState.setShaderTable(rayTracingState().m_hwCausticShaderTable.get());
        commandList.setRayTracingState(rayTracingPassState);
        // Bind heap blocks after RayTracingState; set 10 selects the TLAS generation.
        Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
        heap.bindRayTracing(commandList, *rayTracingState().m_hwCausticPipeline.get(), rayTracingState().m_tlasHeapHandle);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        Core::RayTracingDispatchRaysArguments dispatchArgs;
        dispatchArgs.setDimensions(s_CausticHwPhotonGridSide, s_CausticHwPhotonGridSide / temporalPhaseCount, 1u);
        commandList.dispatchRays(dispatchArgs);
        // Advance temporal phase only after recording a producer dispatch.
        rayTracingState().m_hwCausticFrameIndex = rayTracingState().m_hwCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            graphics().gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            graphics().getDevice(),
            commandList
        );
        recordPhotons();
    }

    dispatchCausticResolve(commandList, targets, graphEntryStatesOwned);

    if(!rayTracingState().m_hwCausticDispatchLogged){
        rayTracingState().m_hwCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched hardware caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticHwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
    // Replace the heap slot before retiring the old emission-target buffer.
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic emission targets require the initialized global descriptor heap"));
        return false;
    }

    const auto acquireHeapHandle = [&](Core::Buffer& buffer, Core::GpuDescriptorHandle& outHandle) -> bool{
        if(!__hidden_raytracing_system::RegisterHeapBuffer(
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

    if(rayTracingState().m_causticEmissionTargetBuffer && rayTracingState().m_causticEmissionTargetCapacity >= targetCount){
        if(rayTracingState().m_causticEmissionTargetHeapHandle.valid()){
            NWB_ASSERT(rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer);
            return true;
        }
        return acquireHeapHandle(
            *rayTracingState().m_causticEmissionTargetBuffer.get(),
            rayTracingState().m_causticEmissionTargetHeapHandle
        );
    }

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_causticEmissionTargetCapacity,
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
    Core::BufferHandle targetBuffer = graphics().createBuffer(targetBufferDesc);
    if(!targetBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }

    Core::GpuDescriptorHandle targetHeapHandle = Core::GpuDescriptorHandle::invalid();
    if(!acquireHeapHandle(*targetBuffer.get(), targetHeapHandle))
        return false;

    if(rayTracingState().m_causticEmissionTargetHeapHandle.valid())
        heap.free(rayTracingState().m_causticEmissionTargetHeapHandle);
    rayTracingState().m_causticEmissionTargetBuffer = Move(targetBuffer);
    rayTracingState().m_causticEmissionTargetHeapHandle = targetHeapHandle;
    rayTracingState().m_causticEmissionTargetCapacity = capacity;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)")
        , static_cast<u64>(capacity)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

