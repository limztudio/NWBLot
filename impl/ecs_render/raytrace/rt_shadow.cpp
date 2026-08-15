// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <core/graphics/task_graph/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_visibility_task{


// A prepared soft-transparent frame records opaque production, opaque resolve, transparent trace, and terminal
// fold as one native packet. The shared state is stack-owned by the renderer for this graph transaction; it never
// survives acceptance or a retry.
struct ShadowVisibilityOpaqueGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        const bool* prepared = nullptr;
        bool* opaqueProduced = nullptr;
        u32* opaqueFrameIndex = nullptr;
        bool hardwareShadowSupported = false;
        bool graphEntryStatesOwned = false;
        bool graphOwnsOpaqueTemporalMergeEntryStates = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(!queue)
            return false;

        *payload.opaqueProduced = false;
        *payload.opaqueFrameIndex = 0u;
        // A retry must never retain a timestamp whose producer command list is about to be replaced.
        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(queue->queueClass == Core::CommandQueue::Compute){
            payload.asyncTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncShadow,
                payload.graphics->getDevice(),
                commandList
            );
        }
        payload.shadowVisibilityTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_ShadowVisibility,
            payload.graphics->getDevice(),
            commandList
        );

        bool opaqueRecorded = false;
        if(payload.prepared && *payload.prepared){
            opaqueRecorded = payload.hardwareShadowSupported
                ? payload.raytracingSystem->renderShadowVisibilityOpaque(
                    commandList,
                    *payload.targets,
                    *payload.opaqueFrameIndex,
                    payload.graphEntryStatesOwned,
                    payload.graphOwnsOpaqueTemporalMergeEntryStates
                )
                : payload.raytracingSystem->renderGpuBvhShadowVisibilityOpaque(
                    commandList,
                    *payload.targets,
                    *payload.opaqueFrameIndex,
                    payload.graphEntryStatesOwned,
                    payload.graphOwnsOpaqueTemporalMergeEntryStates
                )
            ;
        }
        if(!opaqueRecorded){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility"));
            payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);
            // The following graph callbacks declare the output as UAV. Keep the command-list tracker aligned with
            // their declared no-op handoffs even when this fallback only recorded a typed clear.
            commandList.setTextureState(
                payload.targets->shadowVisibility.get(),
                ECSRenderDetail::s_ShadowVisibilitySubresources,
                Core::ResourceStates::UnorderedAccess
            );
            commandList.commitBarriers();
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
            return true;
        }

        *payload.opaqueProduced = true;
        // Both timestamp ranges end in the terminal fold callback, but their debug markers must close before this
        // graph task's marker closes.
        if(payload.asyncTiming->has_value())
            payload.asyncTiming->value().finishMarker();
        payload.shadowVisibilityTiming->value().finishMarker();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.opaqueProduced)
            *payload.opaqueProduced = false;
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming && payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
    }
};


// The opaque producer leaves the trace and current geometry scratch in their declared states. This adjacent task
// owns the geometry UAV-to-SRV transition before temporal merge and wavelet resolve. Timing still begins in the
// producer and ends in the terminal transparent fold callback.
struct ShadowVisibilityOpaqueResolveGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        bool* opaqueProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool hardwareShadowSupported = false;
        bool graphEntryStatesOwned = false;
        bool graphOwnsOpaqueTemporalMergeEntryStates = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
        )
            return false;
        if(!*payload.opaqueProduced)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.raytracingSystem->renderSoftOpaqueShadowResolve(
            commandList,
            *payload.targets,
            *payload.opaqueFrameIndex,
            payload.hardwareShadowSupported,
            payload.graphEntryStatesOwned,
            payload.graphOwnsOpaqueTemporalMergeEntryStates
        ))
            return true;

        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow resolve failed; retaining all-lit visibility"));
        payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);
        // The skipped transparent callbacks still declare this output as UAV. Keep the native state tracker aligned
        // with their no-op graph handoff after the typed fallback clear.
        commandList.setTextureState(
            payload.targets->shadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
        *payload.opaqueProduced = false;
        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.opaqueProduced)
            *payload.opaqueProduced = false;
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming && payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
    }
};


// The prepared path keeps trace and resolve as separate callbacks so the graph lowers the transparent half output
// from UAV to shader-read between them. The terminal resolve task retains the legacy timing and acceptance owner.
struct ShadowTransparentSoftTraceGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* opaqueProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool* transparentTraceProduced = nullptr;
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
            || !payload.targets
            || !payload.timingTicket
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
            || !payload.transparentTraceProduced
        )
            return false;

        *payload.transparentTraceProduced = false;
        if(!*payload.opaqueProduced)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSoftTransparentShadowTrace(
            commandList,
            *payload.targets,
            *payload.opaqueFrameIndex,
            payload.graphEntryStatesOwned,
            true
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility"));
            return true;
        }
        *payload.transparentTraceProduced = true;
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentTraceProduced)
            *payload.transparentTraceProduced = false;
    }
};


struct ShadowTransparentSoftFoldGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        const bool* opaqueProduced = nullptr;
        bool* transparentTraceProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsTransparentTemporalMergeEntryStates = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.opaqueProduced
            || !payload.transparentTraceProduced
            || !payload.opaqueFrameIndex
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(!queue)
            return false;
        if(!*payload.opaqueProduced)
            return true;
        if(
            !payload.shadowVisibilityTiming->has_value()
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(*payload.transparentTraceProduced){
            if(!payload.raytracingSystem->renderSoftTransparentShadowFold(
                commandList,
                *payload.targets,
                *payload.opaqueFrameIndex,
                payload.graphEntryStatesOwned,
                true,
                true,
                payload.graphOwnsTransparentTemporalMergeEntryStates
            ))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility"));
        }else
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace failed; preserving opaque visibility"));

        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().finishTiming(commandList);
            payload.asyncTiming->reset();
        }
        payload.shadowVisibilityTiming->value().finishTiming(commandList);
        payload.shadowVisibilityTiming->reset();
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.raytracingSystem)
            payload.raytracingSystem->confirmShadowVisibilitySubmission(token);
    }

    static void discarded(Payload& payload){
        if(payload.transparentTraceProduced)
            *payload.transparentTraceProduced = false;
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming && payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
        if(payload.raytracingSystem)
            payload.raytracingSystem->discardSoftShadowTemporalHistory();
    }
};


// Shadow visibility owns its graph task; RendererSystem composes the optional software-caustics successor into the
// same packet chain. It still provides declaration-filtered external state for producers outside that graph.
struct ShadowVisibilityGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* prepared = nullptr;
        bool hardwareShadowSupported = false;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.timingTicket
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(!queue)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Optional<Core::GpuTimingMeasure> asyncTiming;
        if(queue->queueClass == Core::CommandQueue::Compute){
            asyncTiming.emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncShadow,
                payload.graphics->getDevice(),
                commandList
            );
        }

        bool shadowVisibilityWritten = false;
        if(payload.prepared && *payload.prepared && payload.hardwareShadowSupported){
            shadowVisibilityWritten = payload.raytracingSystem->renderShadowVisibility(
                commandList,
                *payload.targets,
                payload.graphEntryStatesOwned
            );
            if(!shadowVisibilityWritten)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
            else if(
                !payload.raytracingSystem->softTransparentShadowReady()
                && payload.raytracingSystem->hybridTransparentShadowReady()
            ){
                if(!payload.raytracingSystem->renderGpuBvhShadowVisibility(
                    commandList,
                    *payload.targets,
                    true,
                    payload.graphEntryStatesOwned
                ))
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow pass failed"));
            }
        }
        else if(payload.prepared && *payload.prepared){
            shadowVisibilityWritten = payload.raytracingSystem->renderGpuBvhShadowVisibility(
                commandList,
                *payload.targets,
                false,
                payload.graphEntryStatesOwned
            );
        }
        // Retain all-lit visibility when no shadow producer records.
        if(!shadowVisibilityWritten)
            payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);

        if(asyncTiming){
            asyncTiming->finishTiming(commandList);
            asyncTiming.reset();
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.raytracingSystem)
            payload.raytracingSystem->confirmShadowVisibilitySubmission(token);
    }

    static void discarded(Payload& payload){
        if(payload.raytracingSystem)
            payload.raytracingSystem->discardSoftShadowTemporalHistory();
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    if(handle.valid())
        return true;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!__hidden_raytracing_system::EnsureHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material context buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!__hidden_raytracing_system::ReplaceHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to replace ray-trace material-context heap descriptor"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureRayTraceMaterialContextSlotsBuffer(){
    if(rayTracingState().m_rayTraceMaterialContextSlotsBuffer)
        return true;

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(RayTraceMaterialContextSlots))
        .setIsConstantBuffer(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("raytrace_material_context_slots"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_rayTraceMaterialContextSlotsBuffer = graphics().createBuffer(slotsBufferDesc);
    if(!rayTracingState().m_rayTraceMaterialContextSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create ray-trace material-context slot buffer"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::snapshotRayTraceMaterialContextSlots(RayTraceMaterialContextSlots& outSlots){
    // The shared graph has already imported this constant buffer and its UniformBuffer descriptor by the time its
    // preparation packet records. Do not recreate either one here: a recording-time replacement would invalidate
    // the graph's frozen resource identity and the immutable selector snapshot it retains.
    if(
        !rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        || !rayTracingState().m_shadowMaterialContextSlotsHeapHandle.valid()
        || rayTracingState().m_shadowMaterialContextSlotsHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
    )
        return false;

    RayTraceMaterialContextSlots slots;
    const auto resolveStorageSlot = [](const Core::Buffer* buffer, const Core::GpuDescriptorHandle handle, u32& outSlot) -> bool{
        if(!buffer){
            outSlot = 0u;
            return true;
        }
        if(!handle.valid() || handle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return false;
        outSlot = handle.slot();
        return true;
    };

    const bool complete =
        resolveStorageSlot(rayTracingState().m_sceneBvhNodeBuffer.get(), rayTracingState().m_sceneBvhNodeHeapHandle, slots.sceneBvhNodes)
        && resolveStorageSlot(rayTracingState().m_sceneInstanceBuffer.get(), rayTracingState().m_sceneInstanceHeapHandle, slots.sceneInstances)
        && resolveStorageSlot(rayTracingState().m_shadowInstanceMaterialBuffer.get(), rayTracingState().m_shadowInstanceMaterialHeapHandle, slots.instanceMaterial)
        && resolveStorageSlot(rayTracingState().m_shadowMaterialTypedBuffer.get(), rayTracingState().m_shadowMaterialTypedHeapHandle, slots.materialTyped)
        && resolveStorageSlot(rayTracingState().m_shadowInstanceBuffer.get(), rayTracingState().m_shadowInstanceHeapHandle, slots.meshInstances)
    ;
    if(!complete){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return false;
    }

    outSlots = slots;
    return true;
}

bool RendererRayTracingSystem::uploadRayTraceMaterialContextSlots(Core::CommandList& commandList){
    RayTraceMaterialContextSlots slots;
    if(!snapshotRayTraceMaterialContextSlots(slots))
        return false;

    Core::Buffer* const slotsBuffer = rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get();
    commandList.setBufferState(slotsBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(slotsBuffer, &slots, sizeof(slots));
    commandList.setBufferState(slotsBuffer, Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererRayTracingSystem::ensureRayTraceMaterialContextSlotsHeapHandle(){
    if(!ensureRayTraceMaterialContextSlotsBuffer())
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !__hidden_raytracing_system::EnsureHeapBuffer(
            heap,
            *rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            false,
            rayTracingState().m_shadowMaterialContextSlotsHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material-context selector in the descriptor heap"));
        return false;
    }
    return true;
}

void RendererRayTracingSystem::releaseRayTraceMaterialContextHeapHandles(){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(rayTracingState().m_sceneBvhNodeHeapHandle);
        heap.free(rayTracingState().m_sceneInstanceHeapHandle);
        heap.free(rayTracingState().m_shadowInstanceMaterialHeapHandle);
        heap.free(rayTracingState().m_shadowMaterialTypedHeapHandle);
        heap.free(rayTracingState().m_shadowInstanceHeapHandle);
        heap.free(rayTracingState().m_shadowMaterialContextSlotsHeapHandle);
        heap.free(rayTracingState().m_swShadowEdgeStatsHeapHandle);
        heap.free(rayTracingState().m_swShadowEdgeCounterHeapHandle);
        heap.free(rayTracingState().m_swShadowEdgeListHeapHandle);
        heap.free(rayTracingState().m_swShadowIndirectArgsHeapHandle);
    }
    rayTracingState().m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowEdgeStatsHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    // Deferred lighting always samples this per-slot transmittance target.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setInUAV(true)
        // The graph-owned lagged-history copy may run on a dedicated Transfer family after the graphics/compute
        // producers finish. Keep all three real transports concurrently shareable rather than fabricating aliases.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/shadow/visibility")
    ;
    targets.shadowVisibility = graphics().createTexture(visibilityDesc);
    if(!targets.shadowVisibility){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow visibility target"));
        return false;
    }

    // Round up half-resolution scratch so odd extents retain coverage.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth((targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setHeight((targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setInUAV(true)
        .setName("engine/shadow/coarse_transmittance")
    ;
    targets.shadowCoarseTransmittance = graphics().createTexture(coarseDesc);
    if(!targets.shadowCoarseTransmittance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"));
        return false;
    }

    // Ping-pong half-resolution soft visibility and geometry cache.
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;

    Core::TextureDesc softHalfADesc;
    softHalfADesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowSoftFormat)
        .setInUAV(true)
        .setName("engine/shadow/soft_half_a")
    ;
    targets.shadowSoftHalfA = graphics().createTexture(softHalfADesc);
    if(!targets.shadowSoftHalfA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"));
        return false;
    }

    Core::TextureDesc softHalfBDesc = softHalfADesc;
    softHalfBDesc.setName("engine/shadow/soft_half_b");
    targets.shadowSoftHalfB = graphics().createTexture(softHalfBDesc);
    if(!targets.shadowSoftHalfB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"));
        return false;
    }

    Core::TextureDesc softGeometryDesc;
    softGeometryDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setFormat(targets.shadowSoftGeometryFormat)
        .setInUAV(true)
        .setName("engine/shadow/soft_geometry")
    ;
    targets.shadowSoftGeometry = graphics().createTexture(softGeometryDesc);
    if(!targets.shadowSoftGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"));
        return false;
    }

    // Recreated history must not reproject through the previous target's matrix.
    Core::TextureDesc shadowHistADesc = softHalfADesc;
    shadowHistADesc.setName("engine/shadow/hist_a");
    targets.shadowHistA = graphics().createTexture(shadowHistADesc);
    if(!targets.shadowHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"));
        return false;
    }
    Core::TextureDesc shadowHistBDesc = softHalfADesc;
    shadowHistBDesc.setName("engine/shadow/hist_b");
    targets.shadowHistB = graphics().createTexture(shadowHistBDesc);
    if(!targets.shadowHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"));
        return false;
    }
    Core::TextureDesc shadowMomentsADesc = softHalfADesc;
    shadowMomentsADesc.setName("engine/shadow/moments_a");
    targets.shadowMomentsA = graphics().createTexture(shadowMomentsADesc);
    if(!targets.shadowMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"));
        return false;
    }
    Core::TextureDesc shadowMomentsBDesc = softHalfADesc;
    shadowMomentsBDesc.setName("engine/shadow/moments_b");
    targets.shadowMomentsB = graphics().createTexture(shadowMomentsBDesc);
    if(!targets.shadowMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"));
        return false;
    }
    Core::TextureDesc shadowSoftGeometryPrevDesc = softGeometryDesc;
    shadowSoftGeometryPrevDesc.setName("engine/shadow/soft_geometry_prev");
    targets.shadowSoftGeometryPrev = graphics().createTexture(shadowSoftGeometryPrevDesc);
    if(!targets.shadowSoftGeometryPrev){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"));
        return false;
    }
    rayTracingState().m_softShadowTemporalSeeded = false;
    rayTracingState().m_softShadowTemporalHistoryAdvancePending = false;
    rayTracingState().m_prevWorldToClipValid = false;
    rayTracingState().m_softShadowHistoryFrontIsA = 1u;

    // Transparent temporal history shares geometry and the opaque history selector.
    Core::TextureDesc transparentSoftHalfDesc = softHalfADesc;
    transparentSoftHalfDesc.setName("engine/shadow/transparent_soft_half");
    targets.transparentSoftHalf = graphics().createTexture(transparentSoftHalfDesc);
    if(!targets.transparentSoftHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"));
        return false;
    }
    Core::TextureDesc transparentHistADesc = softHalfADesc;
    transparentHistADesc.setName("engine/shadow/transparent_hist_a");
    targets.transparentHistA = graphics().createTexture(transparentHistADesc);
    if(!targets.transparentHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"));
        return false;
    }
    Core::TextureDesc transparentHistBDesc = softHalfADesc;
    transparentHistBDesc.setName("engine/shadow/transparent_hist_b");
    targets.transparentHistB = graphics().createTexture(transparentHistBDesc);
    if(!targets.transparentHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"));
        return false;
    }
    Core::TextureDesc transparentMomentsADesc = softHalfADesc;
    transparentMomentsADesc.setName("engine/shadow/transparent_moments_a");
    targets.transparentMomentsA = graphics().createTexture(transparentMomentsADesc);
    if(!targets.transparentMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"));
        return false;
    }
    Core::TextureDesc transparentMomentsBDesc = softHalfADesc;
    transparentMomentsBDesc.setName("engine/shadow/transparent_moments_b");
    targets.transparentMomentsB = graphics().createTexture(transparentMomentsBDesc);
    if(!targets.transparentMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"));
        return false;
    }

    // Edge records are capped at one per pixel; overflow falls back to interpolation.
    const u32 edgeListCapacityRecords = targets.width * targets.height;
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("sw_shadow_edge_list"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle edgeListBuffer = graphics().createBuffer(edgeListDesc);
    if(!edgeListBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        rayTracingState().m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !__hidden_raytracing_system::ReplaceHeapBuffer(
            heap,
            *edgeListBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            true,
            rayTracingState().m_swShadowEdgeListHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW shadow edge-list buffer in the descriptor heap"));
        rayTracingState().m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    rayTracingState().m_swShadowEdgeListBuffer = Move(edgeListBuffer);
    rayTracingState().m_swShadowEdgeListCapacity = edgeListCapacityRecords;
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool splitSoftTransparentFold,
    u32* const opaqueFrameIndex,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool splitOpaqueSoftResolve
){
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    if(!targets.shadowVisibility)
        return false;
    if(!rayTracingState().m_tlas || !rayTracingState().m_shadowPipeline)
        return false;

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !rayTracingState().m_tlasHeapHandle.valid()
        || !targets.bindless.valid()
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow trace heap resources are incomplete"));
        return false;
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    if(!graphEntryStatesOwned){
        // Heap-selected resources still need explicit state transitions for direct compatibility callers.
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setAccelStructState(rayTracingState().m_tlas.get(), Core::ResourceStates::AccelStructRead);
    }

    // Hardware tracing shares the half-resolution soft-shadow resolve when available.
    if(rayTracingState().m_softShadowReady && rayTracingState().m_shadowSoftPipeline && rayTracingState().m_softShadowSlotMask != 0u){
        const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));

        if(!graphEntryStatesOwned){
            commandList.setTextureState(
                targets.shadowSoftHalfA.get(),
                ECSRenderDetail::s_ShadowVisibilitySubresources,
                Core::ResourceStates::UnorderedAccess
            );
            commandList.commitBarriers();
        }

        // Resolve reuses the trace outputs as UAV/SRV scratch.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
        // Temporal merge writes history before the wavelet pass reads it.
        if(rayTracingState().m_softShadowTemporalReady){
            commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
        }

        // Advance the jitter sequence once for the primary shadow producer.
        const u32 frameIndex = rayTracingState().m_softShadowFrameIndex++;

        {
            Core::GpuTimingMeasure opaqueTraceTiming(
                graphics().gpuTiming(),
                RendererGpuTimingScope::s_ShadowOpaqueTrace,
                graphics().getDevice(),
                commandList
            );
            Core::ComputeState softState;
            softState.setPipeline(rayTracingState().m_shadowSoftPipeline.get());
            commandList.setComputeState(softState);
            heap.bindCompute(commandList, *rayTracingState().m_shadowSoftPipeline.get(), rayTracingState().m_tlasHeapHandle);

            ShadowRqSoftPushConstants softPush;
            softPush.width = targets.width;
            softPush.height = targets.height;
            softPush.frameIndex = frameIndex;
            softPush.softSampleCount = softShadowTemporalHistoryUsable()
                ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP
                : NWB_SW_SHADOW_SOFT_SPP;
            softPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
            softPush.normalSlot = targets.bindless.gbufferNormal.slot();
            softPush.depthSlot = targets.bindless.gbufferDepth.slot();
            softPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
            softPush.visibilityStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
            commandList.setPushConstants(&softPush, sizeof(softPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence. Direct
        // and unsplit compatibility paths retain the established local fence.
        if(!splitOpaqueSoftResolve){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }

        // A prepared graph may expose the opaque geometry-to-resolve handoff before the transparent-fold tail.
        dispatchSoftShadowDenoiseAndTransparentFold(
            commandList,
            targets,
            frameIndex,
            softGroupsX,
            softGroupsY,
            graphEntryStatesOwned,
            true,
            !splitOpaqueSoftResolve,
            !splitSoftTransparentFold,
            !splitSoftTransparentFold,
            false,
            false,
            false,
            graphOwnsOpaqueTemporalMergeEntryStates
        );
        if(splitSoftTransparentFold){
            NWB_ASSERT(opaqueFrameIndex);
            if(opaqueFrameIndex)
                *opaqueFrameIndex = frameIndex;
        }
        return true;
    }

    // Full-resolution inline-RayQuery fallback.
    if(!graphEntryStatesOwned){
        commandList.setTextureState(
            targets.shadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
    }

    {
        Core::GpuTimingMeasure opaqueTraceTiming(
            graphics().gpuTiming(),
            RendererGpuTimingScope::s_ShadowOpaqueTrace,
            graphics().getDevice(),
            commandList
        );
        Core::ComputeState shadowState;
        shadowState.setPipeline(rayTracingState().m_shadowPipeline.get());
        commandList.setComputeState(shadowState);
        heap.bindCompute(commandList, *rayTracingState().m_shadowPipeline.get(), rayTracingState().m_tlasHeapHandle);
        ShadowRqPushConstants shadowPush;
        shadowPush.frameIndex = rayTracingState().m_softShadowFrameIndex++;
        shadowPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        shadowPush.normalSlot = targets.bindless.gbufferNormal.slot();
        shadowPush.depthSlot = targets.bindless.gbufferDepth.slot();
        shadowPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        shadowPush.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
        commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
        commandList.dispatch(
            DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
            DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
            1u
        );
    }
    return true;
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityOpaqueTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const prepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    bool* const opaqueProduced,
    u32* const opaqueFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates
){
    return graph.addTask<__hidden_shadow_visibility_task::ShadowVisibilityOpaqueGraphTask>(
        desc,
        __hidden_shadow_visibility_task::ShadowVisibilityOpaqueGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &graphics(),
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .prepared = prepared,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityOpaqueResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    bool* const opaqueProduced,
    const u32* const opaqueFrameIndex,
    const bool hardwareShadowSupported,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates
){
    return graph.addTask<__hidden_shadow_visibility_task::ShadowVisibilityOpaqueResolveGraphTask>(
        desc,
        __hidden_shadow_visibility_task::ShadowVisibilityOpaqueResolveGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
        }
    );
}

bool RendererRayTracingSystem::renderShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    u32& outFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates
){
    outFrameIndex = 0u;
    if(
        !rayTracingState().m_softShadowReady
        || !rayTracingState().m_softTransparentReady
        || rayTracingState().m_softShadowSlotMask == 0u
    )
        return false;
    return renderShadowVisibility(
        commandList,
        targets,
        graphEntryStatesOwned,
        true,
        &outFrameIndex,
        graphOwnsOpaqueTemporalMergeEntryStates,
        true
    );
}

bool RendererRayTracingSystem::renderSoftOpaqueShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const u32 frameIndex,
    const bool hardwareShadowSupported,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates
){
    if(
        !rayTracingState().m_softShadowReady
        || !rayTracingState().m_softTransparentReady
        || rayTracingState().m_softShadowSlotMask == 0u
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 groupSize = hardwareShadowSupported
        ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)
        : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)
    ;
    const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
    const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        true,
        false,
        false,
        true,
        false,
        false,
        graphOwnsOpaqueTemporalMergeEntryStates,
        false
    );
    return true;
}

bool RendererRayTracingSystem::renderSoftTransparentShadowTrace(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const u32 frameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueToTransparentBoundary
){
    if(
        !rayTracingState().m_softShadowReady
        || !rayTracingState().m_softTransparentReady
        || rayTracingState().m_softShadowSlotMask == 0u
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        false,
        true,
        false,
        false,
        graphOwnsOpaqueToTransparentBoundary,
        false,
        false,
        false
    );
    return true;
}

bool RendererRayTracingSystem::renderSoftTransparentShadowFold(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const u32 frameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueToTransparentBoundary,
    const bool graphOwnsTransparentTraceToResolveBoundary,
    const bool graphOwnsTransparentTemporalMergeEntryStates
){
    if(
        !rayTracingState().m_softShadowReady
        || !rayTracingState().m_softTransparentReady
        || rayTracingState().m_softShadowSlotMask == 0u
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        false,
        false,
        true,
        false,
        graphOwnsOpaqueToTransparentBoundary,
        graphOwnsTransparentTraceToResolveBoundary,
        false,
        graphOwnsTransparentTemporalMergeEntryStates
    );
    return true;
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowTransparentSoftFoldTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    const bool* const opaqueProduced,
    bool* const transparentTraceProduced,
    const u32* const opaqueFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsTransparentTemporalMergeEntryStates
){
    return graph.addTask<__hidden_shadow_visibility_task::ShadowTransparentSoftFoldGraphTask>(
        desc,
        __hidden_shadow_visibility_task::ShadowTransparentSoftFoldGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsTransparentTemporalMergeEntryStates = graphOwnsTransparentTemporalMergeEntryStates,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowTransparentSoftTraceTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool* const opaqueProduced,
    const u32* const opaqueFrameIndex,
    bool* const transparentTraceProduced,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_shadow_visibility_task::ShadowTransparentSoftTraceGraphTask>(
        desc,
        __hidden_shadow_visibility_task::ShadowTransparentSoftTraceGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .transparentTraceProduced = transparentTraceProduced,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const prepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_shadow_visibility_task::ShadowVisibilityGraphTask>(
        desc,
        __hidden_shadow_visibility_task::ShadowVisibilityGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &graphics(),
            .targets = &targets,
            .timingTicket = &timingTicket,
            .prepared = prepared,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // White transmittance is the all-lit fallback.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool multiplyOntoOpaque,
    const bool graphEntryStatesOwned,
    const bool splitSoftTransparentFold,
    u32* const opaqueFrameIndex,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool splitOpaqueSoftResolve
){
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    // Hybrid mode folds transparent software transmittance onto the hardware opaque mask.
    if(!targets.shadowVisibility)
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhInstanceCount == 0u)
        return false;
    if(!rayTracingState().m_swShadowOpaquePrepassPipeline || rayTracingState().m_swShadowMeshCount == 0u)
        return false;

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !targets.bindless.valid()
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_shadowMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowCoarseTransmittanceStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.transparentSoftHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowEdgeStatsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowEdgeCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowEdgeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-shadow heap resources are incomplete"));
        return false;
    }

    NWB_ASSERT(!splitSoftTransparentFold || !multiplyOntoOpaque);
    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    if(!graphEntryStatesOwned){
        // BVH build leaves traversal inputs in UAV state. Direct compatibility callers restore them locally.
        transitionSwShadowTraversalResources(commandList);
        if(rayTracingState().m_shadowInstanceBuffer)
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    }
    // Subsequent passes read/write these UAVs in place.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true);
    if(!graphEntryStatesOwned)
        commandList.commitBarriers();

    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        return state;
    };

    const auto bindPassHeap = [&](const Core::ComputePipelineHandle& pipeline){
        heap.bindCompute(commandList, *pipeline.get());
    };

    const auto makePush = [&](){
        SwShadowHeapPushConstants push;
        push.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        push.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        push.materialContextSlotsHeapSlot = rayTracingState().m_shadowMaterialContextSlotsHeapHandle.slot();
        push.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
        push.coarseStorageSlot = targets.bindless.shadowCoarseTransmittanceStorage.slot();
        push.softHalfStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
        push.transparentSoftHalfStorageSlot = targets.bindless.transparentSoftHalfStorage.slot();
        push.edgeStatsStorageSlot = rayTracingState().m_swShadowEdgeStatsHeapHandle.slot();
        push.edgeCounterStorageSlot = rayTracingState().m_swShadowEdgeCounterHeapHandle.slot();
        push.edgeListStorageSlot = rayTracingState().m_swShadowEdgeListHeapHandle.slot();
        push.indirectArgsStorageSlot = rayTracingState().m_swShadowIndirectArgsHeapHandle.slot();
        return push;
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);
    const u32 coarseWidth = (targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseHeight = (targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseGroupsX = DivideUp(coarseWidth, groupSize);
    const u32 coarseGroupsY = DivideUp(coarseHeight, groupSize);

    // Skip the fallback after a soft transparent fold.
    bool softTransparentRan = false;

    // Software-only mode first creates the opaque mask.
    if(!multiplyOntoOpaque){
        // Soft upsample overwrites the opaque prepass.
        const bool softWillRun = rayTracingState().m_softShadowReady && rayTracingState().m_softShadowSlotMask != 0u;
        if(!softWillRun){
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants opaquePush = makePush();
            opaquePush.width = targets.width;
            opaquePush.height = targets.height;
            commandList.setComputeState(passState(rayTracingState().m_swShadowOpaquePrepassPipeline));
            bindPassHeap(rayTracingState().m_swShadowOpaquePrepassPipeline);
            commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }

        // The transparent pass reads and multiplies the opaque mask in place.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Soft opaque resolve replaces the full-resolution mask.
        if(softWillRun){
            const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
            const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
            const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
            const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);

            // Advance the primary producer's jitter sequence once.
            const u32 frameIndex = rayTracingState().m_softShadowFrameIndex++;

            // Resolve reads the soft trace and geometry scratch in place.
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
            // Temporal merge writes history before later reads.
            if(rayTracingState().m_softShadowTemporalReady){
                commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
            }

            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure opaqueTraceTiming(
                    graphics().gpuTiming(),
                    RendererGpuTimingScope::s_ShadowOpaqueTrace,
                    graphics().getDevice(),
                    commandList
                );
                SwShadowHeapPushConstants softTracePush = makePush();
                softTracePush.width = targets.width;
                softTracePush.height = targets.height;
                softTracePush.frameIndex = frameIndex;
                softTracePush.softSampleCount = softShadowTemporalHistoryUsable()
                    ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP
                    : NWB_SW_SHADOW_SOFT_SPP;
                commandList.setComputeState(passState(rayTracingState().m_swShadowSoftOpaquePipeline));
                bindPassHeap(rayTracingState().m_swShadowSoftOpaquePipeline);
                commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }

            // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence.
            // Direct and unsplit compatibility paths retain the established local fence.
            if(!splitOpaqueSoftResolve){
                commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.commitBarriers();
            }

            // Shared resolve also guards against a second transparent fold.
            dispatchSoftShadowDenoiseAndTransparentFold(
                commandList,
                targets,
                frameIndex,
                softGroupsX,
                softGroupsY,
                graphEntryStatesOwned,
                true,
                !splitOpaqueSoftResolve,
                !splitSoftTransparentFold,
                !splitSoftTransparentFold,
                false,
                false,
                false,
                graphOwnsOpaqueTemporalMergeEntryStates
            );
            if(splitSoftTransparentFold){
                NWB_ASSERT(opaqueFrameIndex);
                if(opaqueFrameIndex)
                    *opaqueFrameIndex = frameIndex;
                return true;
            }
            softTransparentRan = rayTracingState().m_softTransparentReady;
        }
    }

    // Fallback transparent fold; it is mutually exclusive with the soft path.
    if(!softTransparentRan && rayTracingState().m_swShadowAdaptiveEnabled){
        // Compacted mode traces only classified edge records; stats are sampled asynchronously.
        const bool compact = rayTracingState().m_swShadowCompactEnabled;
        const u32 tick = rayTracingState().m_swShadowEdgeStatsTick++;
        const bool snapshot =
            rayTracingState().m_swShadowEdgeStatsEnabled
            && !rayTracingState().m_swShadowEdgeStatsPending
            && (tick % s_SwShadowEdgeStatsPeriod == 0u)
        ;

        if(snapshot){
            commandList.clearBufferUInt(rayTracingState().m_swShadowEdgeStatsBuffer.get(), 0u);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }

        // Coarse transmittance feeds both adaptive resolve modes.
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants coarsePush = makePush();
        coarsePush.width = targets.width;
        coarsePush.height = targets.height;
        coarsePush.coarseWidth = coarseWidth;
        coarsePush.coarseHeight = coarseHeight;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentCoarsePipeline));
        bindPassHeap(rayTracingState().m_swShadowTransparentCoarsePipeline);
        commandList.setPushConstants(&coarsePush, sizeof(coarsePush));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);

        // Synchronize the coarse write before resolve.
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        if(compact){
            // The append list is bounded by this frame's reset counter.
            commandList.setEnableUavBarriersForBuffer(rayTracingState().m_swShadowEdgeCounterBuffer.get(), true);
            commandList.setEnableUavBarriersForBuffer(rayTracingState().m_swShadowEdgeListBuffer.get(), true);
            commandList.clearBufferUInt(rayTracingState().m_swShadowEdgeCounterBuffer.get(), 0u);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Classify interpolated interiors and append traceable edges.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants classifyPush = makePush();
            classifyPush.width = targets.width;
            classifyPush.height = targets.height;
            classifyPush.coarseWidth = coarseWidth;
            classifyPush.coarseHeight = coarseHeight;
            classifyPush.edgeThreshold = rayTracingState().m_swShadowEdgeThreshold;
            classifyPush.collectStats = snapshot ? 1u : 0u;
            classifyPush.edgeCapacity = rayTracingState().m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentClassifyPipeline));
            bindPassHeap(rayTracingState().m_swShadowTransparentClassifyPipeline);
            commandList.setPushConstants(&classifyPush, sizeof(classifyPush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

            // Classify produces the list and in-place visibility writes.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Build indirect dispatch arguments from the clamped list count.
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants argsPush = makePush();
            argsPush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            argsPush.edgeCapacity = rayTracingState().m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentBuildArgsPipeline));
            bindPassHeap(rayTracingState().m_swShadowTransparentBuildArgsPipeline);
            commandList.setPushConstants(&argsPush, sizeof(argsPush));
            commandList.dispatch(1u, 1u, 1u);

            // Indirect dispatch consumes the generated arguments and list.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            SwShadowHeapPushConstants tracePush = makePush();
            tracePush.width = targets.width;
            tracePush.height = targets.height;
            tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
            commandList.commitBarriers();
            Core::ComputeState computeStateIndirect = passState(rayTracingState().m_swShadowTransparentIndirectPipeline);
            computeStateIndirect.setIndirectParams(rayTracingState().m_swShadowIndirectArgsBuffer.get());
            commandList.setComputeState(computeStateIndirect);
            bindPassHeap(rayTracingState().m_swShadowTransparentIndirectPipeline);
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatchIndirect(0u);
        }
        else{
            // Full-resolution adaptive fallback.
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants resolvePush = makePush();
            resolvePush.width = targets.width;
            resolvePush.height = targets.height;
            resolvePush.coarseWidth = coarseWidth;
            resolvePush.coarseHeight = coarseHeight;
            resolvePush.edgeThreshold = rayTracingState().m_swShadowEdgeThreshold;
            resolvePush.collectStats = snapshot ? 1u : 0u;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentResolvePipeline));
            bindPassHeap(rayTracingState().m_swShadowTransparentResolvePipeline);
            commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }

        if(snapshot){
            // Readback is delayed until its submission completes.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::CopySource);
            commandList.commitBarriers();
            commandList.copyBuffer(
                rayTracingState().m_swShadowEdgeStatsReadback.get(), 0u,
                rayTracingState().m_swShadowEdgeStatsBuffer.get(), 0u,
                static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT)
            );
            rayTracingState().m_swShadowEdgeStatsPending = true;
            rayTracingState().m_swShadowEdgeStatsPendingTick = tick;
            rayTracingState().m_swShadowEdgeStatsPendingSubmissionID = 0u;
            rayTracingState().m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = {};
            rayTracingState().m_swShadowEdgeStatsPendingSubmissionUnconfirmed = true;
        }
        else if(
            rayTracingState().m_swShadowEdgeStatsPending
            && (tick - rayTracingState().m_swShadowEdgeStatsPendingTick) >= s_SwShadowEdgeStatsLogDelay
        ){
            const bool submissionConfirmed = !rayTracingState().m_swShadowEdgeStatsPendingSubmissionUnconfirmed;
            const bool submissionComplete =
                submissionConfirmed
                && (
                    rayTracingState().m_swShadowEdgeStatsPendingSubmissionID == 0u
                    || (
                        rayTracingState().m_swShadowEdgeStatsPendingSubmissionPhysicalQueue.valid()
                        && graphics().getDevice().queueGetCompletedInstance(
                            rayTracingState().m_swShadowEdgeStatsPendingSubmissionPhysicalQueue
                        ) >= rayTracingState().m_swShadowEdgeStatsPendingSubmissionID
                    )
                )
            ;
            if(submissionComplete){
                const u32* stats = static_cast<const u32*>(graphics().getDevice().mapBuffer(rayTracingState().m_swShadowEdgeStatsReadback.get(), Core::CpuAccessMode::Read));
                if(stats){
                    const u32 traced = stats[NWB_SW_SHADOW_EDGE_STATS_TRACED];
                    const u32 total = stats[NWB_SW_SHADOW_EDGE_STATS_TOTAL];
                    graphics().getDevice().unmapBuffer(rayTracingState().m_swShadowEdgeStatsReadback.get());
                    const f64 fraction = (total > 0u) ? (100.0 * static_cast<f64>(traced) / static_cast<f64>(total)) : 0.0;
                    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW shadow adaptive edge fraction = {}% ({} traced / {} total rays, threshold {})")
                        , fraction
                        , static_cast<u64>(traced)
                        , static_cast<u64>(total)
                        , static_cast<f64>(rayTracingState().m_swShadowEdgeThreshold)
                    );
                }
                rayTracingState().m_swShadowEdgeStatsPending = false;
                rayTracingState().m_swShadowEdgeStatsPendingSubmissionID = 0u;
                rayTracingState().m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = {};
                rayTracingState().m_swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
            }
        }
    }
    else if(!softTransparentRan){
        // Non-adaptive half-resolution transparent fallback.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants pushConstants = makePush();
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentUniformPipeline));
        bindPassHeap(rayTracingState().m_swShadowTransparentUniformPipeline);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);
    }

    if(!rayTracingState().m_swShadowDispatchLogged){
        rayTracingState().m_swShadowDispatchLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched software shadow traversal ({}x{}, {} instances)")
            , static_cast<u64>(targets.width)
            , static_cast<u64>(targets.height)
            , static_cast<u64>(rayTracingState().m_sceneBvhInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    u32& outFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates
){
    outFrameIndex = 0u;
    if(
        !rayTracingState().m_softShadowReady
        || !rayTracingState().m_softTransparentReady
        || rayTracingState().m_softShadowSlotMask == 0u
    )
        return false;
    return renderGpuBvhShadowVisibility(
        commandList,
        targets,
        false,
        graphEntryStatesOwned,
        true,
        &outFrameIndex,
        graphOwnsOpaqueTemporalMergeEntryStates,
        true
    );
}

bool RendererRayTracingSystem::hybridTransparentShadowReady()const noexcept{
    return rayTracingState().m_hybridTransparentShadowReady;
}

bool RendererRayTracingSystem::softTransparentShadowReady()const noexcept{
    return rayTracingState().m_softTransparentReady;
}

void RendererRayTracingSystem::appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const{
    // Trace layouts are push-only; resources come from the global heap.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowRqSoftPushConstants)));
}

bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(rayTracingState().m_shadowPipeline)
        return true;
    if(rayTracingState().m_shadowPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayQuery) || !graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        appendShadowTraceBindingLayout(layoutDesc);

        rayTracingState().m_shadowBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            rayTracingState().m_shadowPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowShader,
        AssetsGraphicsShadow::s_RayQueryShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuery"
    )){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowShader)
        .addBindingLayout(rayTracingState().m_shadowBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    rayTracingState().m_shadowPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureShadowSoftPipeline(){
    if(rayTracingState().m_shadowSoftPipeline)
        return true;
    if(rayTracingState().m_shadowSoftPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayQuery) || !graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    // Soft and hard traces share their push-only layout.
    if(!rayTracingState().m_shadowBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow binding layout missing for the soft RayQuery pipeline"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowSoftShader,
        AssetsGraphicsShadow::s_RayQuerySoftShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuerySoft"
    )){
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowSoftShader)
        .addBindingLayout(rayTracingState().m_shadowBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    rayTracingState().m_shadowSoftPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowSoftPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPipeline(){
    if(rayTracingState().m_swShadowPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software shadows require the initialized global descriptor heap"));
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_swShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // All pass resources are selected through the fixed push ABI.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowHeapPushConstants)));

        rayTracingState().m_swShadowBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }


        Core::BufferDesc edgeStatsDesc;
        edgeStatsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("sw_shadow_edge_stats"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowEdgeStatsBuffer = graphics().createBuffer(edgeStatsDesc);
        if(!rayTracingState().m_swShadowEdgeStatsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-stats buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc edgeStatsReadbackDesc;
        edgeStatsReadbackDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT))
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setDebugName(Name("sw_shadow_edge_stats_readback"))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        rayTracingState().m_swShadowEdgeStatsReadback = graphics().createBuffer(edgeStatsReadbackDesc);
        if(!rayTracingState().m_swShadowEdgeStatsReadback){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-stats readback buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        // Compaction uses a persistent counter and UAV-writable indirect-args buffer.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("sw_shadow_edge_counter"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowEdgeCounterBuffer = graphics().createBuffer(edgeCounterDesc);
        if(!rayTracingState().m_swShadowEdgeCounterBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setDebugName(Name("sw_shadow_indirect_args"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowIndirectArgsBuffer = graphics().createBuffer(indirectArgsDesc);
        if(!rayTracingState().m_swShadowIndirectArgsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }
    }

    const bool heapResourcesReady =
        __hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_swShadowEdgeStatsHeapHandle)
        && __hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_swShadowEdgeCounterHeapHandle)
        && __hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_swShadowIndirectArgsHeapHandle)
    ;
    if(!heapResourcesReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-shadow work buffers in the descriptor heap"));
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }

    const bool passesReady =
        ensureSwShadowPassPipeline(rayTracingState().m_swShadowOpaquePrepassShader, rayTracingState().m_swShadowOpaquePrepassPipeline, AssetsGraphicsShadow::s_SwOpaquePrepassShaderName, "ECSRender_SwShadowOpaquePrepass")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowSoftOpaqueShader, rayTracingState().m_swShadowSoftOpaquePipeline, AssetsGraphicsShadow::s_SwSoftOpaqueShaderName, "ECSRender_SwShadowSoftOpaque")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentCoarseShader, rayTracingState().m_swShadowTransparentCoarsePipeline, AssetsGraphicsShadow::s_SwTransparentCoarseShaderName, "ECSRender_SwShadowTransparentCoarse")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentResolveShader, rayTracingState().m_swShadowTransparentResolvePipeline, AssetsGraphicsShadow::s_SwTransparentResolveShaderName, "ECSRender_SwShadowTransparentResolve")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentClassifyShader, rayTracingState().m_swShadowTransparentClassifyPipeline, AssetsGraphicsShadow::s_SwTransparentClassifyShaderName, "ECSRender_SwShadowTransparentClassify")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentBuildArgsShader, rayTracingState().m_swShadowTransparentBuildArgsPipeline, AssetsGraphicsShadow::s_SwTransparentBuildArgsShaderName, "ECSRender_SwShadowTransparentBuildArgs")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentIndirectShader, rayTracingState().m_swShadowTransparentIndirectPipeline, AssetsGraphicsShadow::s_SwTransparentIndirectShaderName, "ECSRender_SwShadowTransparentIndirect")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentUniformShader, rayTracingState().m_swShadowTransparentUniformPipeline, AssetsGraphicsShadow::s_SwTransparentUniformShaderName, "ECSRender_SwShadowTransparentUniform")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentSoftShader, rayTracingState().m_swShadowTransparentSoftPipeline, AssetsGraphicsShadow::s_SwTransparentSoftShaderName, "ECSRender_SwShadowTransparentSoft")
    ;
    if(!passesReady){
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const char* debugLabel){
    if(pipeline)
        return true;

    if(!m_renderer.shaderSystem().loadShader(
        shader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugLabel
    ))
        return false;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(rayTracingState().m_swShadowBindingLayout)
    ;
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    pipeline = graphics().getDevice().createComputePipeline(pipelineDesc);
    if(!pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // CPU-uploaded material context is shared by the exclusive HW/SW backends.
    if(rayTracingState().m_shadowInstanceMaterialBuffer && rayTracingState().m_shadowInstanceMaterialCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowInstanceMaterialBuffer.get(),
            rayTracingState().m_shadowInstanceMaterialHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_shadowInstanceMaterialCapacity,
        instanceCount,
        s_ShadowInstanceMaterialInitialCapacity
    );

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("shadow_instance_material"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialBuffer = graphics().createBuffer(materialBufferDesc);
    if(!materialBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*materialBuffer.get(), rayTracingState().m_shadowInstanceMaterialHeapHandle))
        return false;
    rayTracingState().m_shadowInstanceMaterialBuffer = Move(materialBuffer);
    rayTracingState().m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow tracing needs an instance record for every gathered occluder.
    if(instanceCount == 0u)
        return !rayTracingState().m_shadowInstanceBuffer || ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowInstanceBuffer.get(),
            rayTracingState().m_shadowInstanceHeapHandle
        );
    if(rayTracingState().m_shadowInstanceBuffer && rayTracingState().m_shadowInstanceCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowInstanceBuffer.get(),
            rayTracingState().m_shadowInstanceHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(rayTracingState().m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("shadow_instance_context"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = graphics().createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), rayTracingState().m_shadowInstanceHeapHandle))
        return false;
    rayTracingState().m_shadowInstanceBuffer = Move(instanceBuffer);
    rayTracingState().m_shadowInstanceCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Keep one word so the heap binding remains valid with no transparent occluders.
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
    if(rayTracingState().m_shadowMaterialTypedBuffer && rayTracingState().m_shadowMaterialTypedCapacity >= requiredByteCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowMaterialTypedBuffer.get(),
            rayTracingState().m_shadowMaterialTypedHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(rayTracingState().m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("shadow_material_typed"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialTypedBuffer = graphics().createBuffer(materialTypedBufferDesc);
    if(!materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*materialTypedBuffer.get(), rayTracingState().m_shadowMaterialTypedHeapHandle))
        return false;
    rayTracingState().m_shadowMaterialTypedBuffer = Move(materialTypedBuffer);
    rayTracingState().m_shadowMaterialTypedCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::uploadShadowMaterialContextBuffers(
    Core::CommandList& commandList,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    if(!ensureShadowInstanceContextBuffer(instanceData.size()) || !ensureShadowMaterialTypedBuffer(uploadBytes))
        return false;

    if(!instanceData.empty()){
        Core::Buffer* instanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
        commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(instanceBuffer, instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
        commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(materialTypedBuffer, materialTypedBytes.data(), uploadBytes);
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

