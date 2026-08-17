// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/gpu_timing.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


// Records the non-publishing endpoint used when a later frame packet rejects after the prefix accepted. The shared
// graph declares this tail eagerly but arms it only for a late recovery record/submission.
struct FrameRecoveryGraphTask{
    struct Payload{
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        bool* armed = nullptr;
        bool* retiresFrameTiming = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return payload.frameTimingTransaction
            && payload.armed
            && payload.retiresFrameTiming
            && *payload.armed
            && (!*payload.retiresFrameTiming || payload.frameTimingTransaction->recordEnd(commandList))
        ;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(
            payload.armed
            && payload.retiresFrameTiming
            && *payload.armed
            && *payload.retiresFrameTiming
            && payload.frameTimingTransaction
            && !payload.frameTimingTransaction->confirmEndSubmission(false)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire frame recovery timing query"));
            payload.frameTimingTransaction->discard();
        }
        if(payload.armed)
            *payload.armed = false;
        if(payload.retiresFrameTiming)
            *payload.retiresFrameTiming = false;
    }

    static void discarded(Payload& payload){
        if(payload.armed && *payload.armed && payload.frameTimingTransaction)
            payload.frameTimingTransaction->discard();
        if(payload.armed)
            *payload.armed = false;
        if(payload.retiresFrameTiming)
            *payload.retiresFrameTiming = false;
    }
};


[[nodiscard]] static const Core::GpuPhysicalQueueInfo* QueueForTask(
    const Core::GpuTaskRecordContext& context,
    const Core::GpuTaskId* const task
){
    if(!task || !task->valid())
        return nullptr;
    const Core::GpuSubmissionPacketId packet = context.graph.packetForTask(*task);
    if(!packet.valid())
        return nullptr;
    return context.graph.queueInfo(context.graph.packet(packet).queue);
}


// Shadow preparation owns the first Graphics packet in the shared deferred graph. Preflight has already selected
// every dynamic trace resource, so recording only emits GPU work against graph-imported handles.
struct ShadowPrepareGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool deferredBindlessSlotsWereUploaded = false;
        bool currentBindlessSlotsGraphOwned = false;
        bool rayTraceMaterialContextSlotsGraphOwned = false;
        bool causticEmissionTargetsGraphOwned = false;
        bool surfelFrameConstantsGraphOwned = false;
        bool shadowMaterialContextBatchGraphOwned = false;
        bool sceneBvhBatchGraphOwned = false;
        bool sceneTlasBuildGraphOwned = false;
        bool meshBlasBuildsGraphOwned = false;
        bool meshBlasGeometryBuildInputStatesGraphOwned = false;
        bool meshSwBvhBuildsGraphOwned = false;
        bool preparedMeshSwBvhBuildsRecordedByGraph = false;
        bool deferHybridSoftwareTail = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.renderer || !payload.targets || !payload.timingTicket)
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        renderer.m_preparedShadowVisibilityReady = false;
        // The compiled ConstantBuffer use established this packet's selector state before this thunk records. The
        // retained descriptor-visible state is also ConstantBuffer, so normal graph frames need no native bridge.
        const bool shadowResourcesPrepared = payload.targets->bindless.valid()
            && renderer.m_raytracingSystem.recordPreflightShadowVisibilityResources(
                commandList,
                *payload.targets,
                renderer.m_preparedShadowVisibilityReady,
                payload.causticEmissionTargetsGraphOwned,
                payload.surfelFrameConstantsGraphOwned,
                payload.shadowMaterialContextBatchGraphOwned,
                payload.sceneBvhBatchGraphOwned,
                payload.sceneTlasBuildGraphOwned,
                payload.meshBlasBuildsGraphOwned,
                payload.meshBlasGeometryBuildInputStatesGraphOwned,
                payload.meshSwBvhBuildsGraphOwned,
                payload.preparedMeshSwBvhBuildsRecordedByGraph,
                payload.deferHybridSoftwareTail
            )
        ;
        // The graph-owned material-context selector was snapshotted after preflight settled every backing handle.
        // Compatibility callers retain the direct write after native preparation recording.
        if(
            !shadowResourcesPrepared
            || (
                !payload.rayTraceMaterialContextSlotsGraphOwned
                && !renderer.m_raytracingSystem.uploadRayTraceMaterialContextSlots(commandList)
            )
        )
            return false;

        // These declarations, and the adjacent hybrid-tail ShaderResource reads when present, export every selected
        // BLAS/SW-BVH input's exact graph-visible boundary state before the following Prefix packet is seeded.
        // Route-local build work remains inside this callback.
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.targets && payload.currentBindlessSlotsGraphOwned)
            payload.targets->bindless.slotsUploaded = true;
        if(payload.renderer)
            payload.renderer->m_raytracingSystem.confirmPreparedShadowTraceGeometryNormalization();
        if(payload.renderer && payload.shadowMaterialContextBatchGraphOwned)
            payload.renderer->m_raytracingSystem.confirmPreparedShadowMaterialContextUploads();
        if(payload.renderer && payload.sceneBvhBatchGraphOwned)
            payload.renderer->m_raytracingSystem.confirmPreparedSceneBvhUploads();
    }

    static void discarded(Payload& payload){
        if(payload.timingTicket)
            payload.timingTicket->discard();
        if(!payload.renderer)
            return;

        RendererSystem& renderer = *payload.renderer;
        // Failed preparation keeps resource storage but invalidates the selected frame plan and every semantic cache.
        renderer.m_preparedShadowVisibilityReady = false;
        renderer.m_preparedShadowVisibilityResourcesValid = false;
        if(payload.targets)
            payload.targets->bindless.slotsUploaded = payload.deferredBindlessSlotsWereUploaded;
        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
    }
};


// Pure-software preparation shares scratch between every frozen mesh build. Keep each typed sentinel setup next to
// its matching compute callback so a later mesh cannot clear a prior mesh's sort/payload rendezvous state.
struct ShadowPrepareSoftwareBvhBuildGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        PreparedMeshSwBvhBuild build;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.timingTicket)
            return false;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        return payload.raytracingSystem->recordPreparedMeshSwBvhBuildAfterGraphClears(
            commandList,
            payload.build
        );
    }
};


// Hybrid HW-to-SW shadow preparation keeps the established opaque-HW fallback transaction, but records its
// software continuation after the hardware build as an explicit packet-local callback. The compiler must retain it
// in Shadow Preparation's accepting Graphics packet: the tail can restore the frozen hardware material context and
// its final resource state joins the same persistent handoff as the preceding BLAS/TLAS work.
struct ShadowPrepareHybridSoftwareTailGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        bool* hardwarePreparationReady = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool surfelFrameConstantsGraphOwned = false;
        bool shadowMaterialContextBatchGraphOwned = false;
        bool sceneBvhBatchGraphOwned = false;
        bool meshSwBvhBuildsGraphOwned = false;
        bool meshSwBvhInputStatesGraphOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets || !payload.hardwarePreparationReady || !payload.timingTicket)
            return false;
        // The tail may record SW-BVH timing scopes, so it shares the accepting packet's timing ticket even though
        // its callback begins after the hardware preparation callback closed its own recording scope.
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);

        return payload.raytracingSystem->recordPreflightHybridSoftwareTail(
            commandList,
            *payload.targets,
            *payload.hardwarePreparationReady,
            payload.surfelFrameConstantsGraphOwned,
            payload.shadowMaterialContextBatchGraphOwned,
            payload.sceneBvhBatchGraphOwned,
            payload.meshSwBvhBuildsGraphOwned,
            payload.meshSwBvhInputStatesGraphOwned
        );
    }

    static void discarded(Payload& payload){
        if(payload.hardwarePreparationReady)
            *payload.hardwarePreparationReady = false;
    }
};


// Mesh-view setup is the first native graphics-prefix task. The immutable data upload immediately following this
// preamble is a built-in graph task; this payload retains only frame-timing ownership.
struct MeshViewSetupGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* asyncPrefixTimingSpansOnePacket = nullptr;
        const Core::GpuTaskId* shadowVisibilityTask = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(
            context,
            payload.shadowVisibilityTask
        );
        if(
            !payload.renderer
            || !payload.frameTimingTransaction
            || !payload.asyncPrefixTiming
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.asyncPrefixTimingSpansOnePacket
            || !shadowVisibilityQueue
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        const bool shadowVisibilityRunsOnCompute =
            shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        const bool recordsGraphicsFrameMarker =
            !shadowVisibilityRunsOnCompute && RendererGpuTimingScope::s_Frame.valid()
        ;
        if(recordsGraphicsFrameMarker)
            commandList.beginMarker(RendererGpuTimingScope::s_Frame.markerLabel);

        const bool frameTimingStarted = payload.frameTimingTransaction->begin(
            RendererGpuTimingScope::s_Frame,
            renderer.m_graphics.getDevice(),
            commandList
        );
        if(shadowVisibilityRunsOnCompute && *payload.asyncPrefixTimingSpansOnePacket){
            payload.asyncPrefixTiming->emplace(
                renderer.m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncPrefix,
                renderer.m_graphics.getDevice(),
                commandList
            );
            payload.asyncPrefixTiming->value().finishMarker();
        }

        if(recordsGraphicsFrameMarker)
            commandList.endMarker();
        return frameTimingStarted;
    }
};


// The CPU mirror is updated only after the built-in upload packet accepts. This keeps a rejected recording from
// suppressing the retry's immutable blob declaration.
struct MeshViewUploadCommitGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        ECSRenderDetail::MeshViewGpuData viewState;
        bool uploadRequired = false;
        bool* ready = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(!payload.renderer || !payload.ready)
            return false;
        *payload.ready = true;
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.renderer && payload.uploadRequired)
            payload.renderer->m_meshSystem.confirmMeshViewBufferUpload(payload.viewState);
    }

    static void discarded(Payload& payload){
        if(payload.ready)
            *payload.ready = false;
    }
};


// Scene-shading setup follows the frame-data uploads. Its native body is now only the semantic/timing endpoint;
// the graph owns each resource write and publishes its declared final state.
struct SceneShadingSetupGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        bool* ready = nullptr;
        ECSRenderDetail::SceneLightGpuData lightData[NWB_SCENE_MAX_LIGHTS] = {};
        ECSRenderDetail::SceneShadingGpuData sceneShadingState;
        u32 lightCount = 0u;
        bool lightUploadRequired = false;
        bool sceneShadingUploadRequired = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(!payload.renderer || !payload.timingTicket || !*payload.timingTicket || !payload.ready)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        *payload.ready = true;
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(!payload.renderer)
            return;
        payload.renderer->m_deferredSystem.confirmSceneShadingBufferUploads(
            payload.lightData,
            payload.lightCount,
            payload.lightUploadRequired,
            payload.sceneShadingState,
            payload.sceneShadingUploadRequired
        );
    }

    static void discarded(Payload& payload){
        if(payload.ready)
            *payload.ready = false;
    }
};


// Built-in clear tasks carry the actual CopyDest declarations and native commands. These hooks keep the semantic
// deferred-clear measure inside the first/last clear operation, so FrontierSafe packetization cannot split a timer
// endpoint away from the opaque-color clear's later asynchronous consumer.
[[nodiscard]] static bool BeginDeferredClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    DeferredClearTimingRecordState* const state = static_cast<DeferredClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->graphics
        || !state->timing
        || !state->timingTicket
        || !*state->timingTicket
        || *state->timing
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**state->timingTicket);
    state->timing->emplace(
        state->graphics->gpuTiming(),
        RendererGpuTimingScope::s_DeferredClear,
        state->graphics->getDevice(),
        commandList
    );
    state->timing->value().finishMarker();
    return true;
}

[[nodiscard]] static bool EndDeferredClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    DeferredClearTimingRecordState* const state = static_cast<DeferredClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->timing
        || !*state->timing
        || !state->timingTicket
        || !*state->timingTicket
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**state->timingTicket);
    state->timing->value().finishTiming(commandList);
    state->timing->reset();
    return true;
}

static void DiscardDeferredClearTiming(void* const rawState){
    DeferredClearTimingRecordState* const state = static_cast<DeferredClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return;
    state->timing->value().discardTiming();
    state->timing->reset();
}


// AVBOIT retains the original clear timing interval while its nine target values record as individual built-in
// clear tasks. The first/last texture hooks deliberately own the endpoints so packetization cannot separate the
// measurement from the values it observes.
[[nodiscard]] static bool BeginAvboitClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    AvboitClearTimingRecordState* const state = static_cast<AvboitClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->graphics
        || !state->timing
        || !state->timingTicket
        || *state->timing
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*state->timingTicket);
    state->timing->emplace(
        state->graphics->gpuTiming(),
        RendererGpuTimingScope::s_AvboitClear,
        state->graphics->getDevice(),
        commandList
    );
    state->timing->value().finishMarker();
    return true;
}

[[nodiscard]] static bool EndAvboitClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    AvboitClearTimingRecordState* const state = static_cast<AvboitClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->timing
        || !*state->timing
        || !state->timingTicket
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*state->timingTicket);
    state->timing->value().finishTiming(commandList);
    state->timing->reset();
    return true;
}

static void DiscardAvboitClearTiming(void* const rawState){
    AvboitClearTimingRecordState* const state = static_cast<AvboitClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return;
    state->timing->value().discardTiming();
    state->timing->reset();
}


[[nodiscard]] static Core::GpuTimingSubmissionTicket* ResolveCsgIntervalClearTimingTicket(
    CsgIntervalClearTimingRecordState& state
){
    return state.rebindableTimingTicket ? *state.rebindableTimingTicket : state.timingTicket;
}

// The CSG work-region clear now records as two typed rectangle primitives. These hooks retain its former one-range
// measurement even though the compiler owns each individual CopyDest operation and their UAV handoffs.
[[nodiscard]] static bool BeginCsgIntervalClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    CsgIntervalClearTimingRecordState* const state = static_cast<CsgIntervalClearTimingRecordState*>(rawState);
    if(!state || !state->graphics || !state->timing || *state->timing)
        return false;
    Core::GpuTimingSubmissionTicket* const timingTicket = ResolveCsgIntervalClearTimingTicket(*state);
    if(!timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    state->timing->emplace(
        state->graphics->gpuTiming(),
        RendererGpuTimingScope::s_CsgIntervalClear,
        state->graphics->getDevice(),
        commandList
    );
    state->timing->value().finishMarker();
    return true;
}

[[nodiscard]] static bool EndCsgIntervalClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    CsgIntervalClearTimingRecordState* const state = static_cast<CsgIntervalClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return false;
    Core::GpuTimingSubmissionTicket* const timingTicket = ResolveCsgIntervalClearTimingTicket(*state);
    if(!timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    state->timing->value().finishTiming(commandList);
    state->timing->reset();
    return true;
}

static void DiscardCsgIntervalClearTiming(void* const rawState){
    CsgIntervalClearTimingRecordState* const state = static_cast<CsgIntervalClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return;
    state->timing->value().discardTiming();
    state->timing->reset();
}


// The opaque material draw ordering and CSG CPU frame data are captured while the graph is declared.  The paired
// instance/material blobs are therefore immutable packet inputs rather than data rebuilt while a native task records.
struct OpaqueMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector regularMeshDrawItems;
    DrawItemVector regularComputeDrawItems;
    DrawItemVector csgMeshDrawItems;
    DrawItemVector csgComputeDrawItems;
    DrawItemVector csgReceiverSurfaceMeshDrawItems;
    DrawItemVector csgReceiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    CsgFrameWorkRegion csgWorkRegion;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool captured = false;

    explicit OpaqueMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : regularMeshDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverSurfaceMeshDrawItems(arena)
        , csgReceiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(
        const MaterialPassDrawItemPartitions& drawItems,
        const CsgFrameGpuData& csgFrameData,
        const usize inInstanceCount,
        const usize inMaterialTypedByteCount
    ){
        regularMeshDrawItems.assign(drawItems.regular.meshDrawItems.begin(), drawItems.regular.meshDrawItems.end());
        regularComputeDrawItems.assign(drawItems.regular.computeDrawItems.begin(), drawItems.regular.computeDrawItems.end());
        csgMeshDrawItems.assign(drawItems.csg.meshDrawItems.begin(), drawItems.csg.meshDrawItems.end());
        csgComputeDrawItems.assign(drawItems.csg.computeDrawItems.begin(), drawItems.csg.computeDrawItems.end());
        csgReceiverSurfaceMeshDrawItems.assign(
            drawItems.csgReceiverSurface.meshDrawItems.begin(),
            drawItems.csgReceiverSurface.meshDrawItems.end()
        );
        csgReceiverSurfaceComputeDrawItems.assign(
            drawItems.csgReceiverSurface.computeDrawItems.begin(),
            drawItems.csgReceiverSurface.computeDrawItems.end()
        );
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        csgCutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(
        MaterialPassDrawItemPartitions& outDrawItems,
        CsgFrameGpuData& outCsgFrameData
    )const{
        outDrawItems.regular.meshDrawItems.assign(regularMeshDrawItems.begin(), regularMeshDrawItems.end());
        outDrawItems.regular.computeDrawItems.assign(regularComputeDrawItems.begin(), regularComputeDrawItems.end());
        outDrawItems.csg.meshDrawItems.assign(csgMeshDrawItems.begin(), csgMeshDrawItems.end());
        outDrawItems.csg.computeDrawItems.assign(csgComputeDrawItems.begin(), csgComputeDrawItems.end());
        outDrawItems.csgReceiverSurface.meshDrawItems.assign(
            csgReceiverSurfaceMeshDrawItems.begin(),
            csgReceiverSurfaceMeshDrawItems.end()
        );
        outDrawItems.csgReceiverSurface.computeDrawItems.assign(
            csgReceiverSurfaceComputeDrawItems.begin(),
            csgReceiverSurfaceComputeDrawItems.end()
        );
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


// Transparent CSG interval work is a distinct AVBOIT producer.  Retain its receiver-surface ordering and CSG CPU
// data at declaration so the interval packet consumes the same immutable blobs that its graph uploads published.
struct TransparentCsgIntervalGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector receiverSurfaceMeshDrawItems;
    DrawItemVector receiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    CsgFrameWorkRegion csgWorkRegion;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool captured = false;

    explicit TransparentCsgIntervalGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : receiverSurfaceMeshDrawItems(arena)
        , receiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const CsgFrameGpuData& csgFrameData,
        const usize inInstanceCount,
        const usize inMaterialTypedByteCount
    ){
        receiverSurfaceMeshDrawItems.assign(
            receiverSurfaceDrawItems.meshDrawItems.begin(),
            receiverSurfaceDrawItems.meshDrawItems.end()
        );
        receiverSurfaceComputeDrawItems.assign(
            receiverSurfaceDrawItems.computeDrawItems.begin(),
            receiverSurfaceDrawItems.computeDrawItems.end()
        );
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        csgCutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(
        MaterialPassDrawItems& outReceiverSurfaceDrawItems,
        CsgFrameGpuData& outCsgFrameData
    )const{
        outReceiverSurfaceDrawItems.meshDrawItems.assign(
            receiverSurfaceMeshDrawItems.begin(),
            receiverSurfaceMeshDrawItems.end()
        );
        outReceiverSurfaceDrawItems.computeDrawItems.assign(
            receiverSurfaceComputeDrawItems.begin(),
            receiverSurfaceComputeDrawItems.end()
        );
        materializeCsgFrameData(outCsgFrameData);
    }

    void materializeCsgFrameData(CsgFrameGpuData& outCsgFrameData)const{
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


// Compute-emulated material draws write one persistent generated-vertex buffer per mesh.  A graph producer may
// only run ahead of the G-buffer raster stage when every frozen regular opaque draw owns a distinct output.  Keep
// the exact draw order and retained handles so recording can reject a mesh-resource replacement instead of
// dispatching into a newly selected buffer that the compiled graph did not import.
struct OpaqueRegularComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    bool captured = false;

    explicit OpaqueRegularComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , outputBuffers(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        outputBuffers.clear();
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems
    ){
        reset();
        if(sourceDrawItems.computeDrawItems.empty())
            return false;

        meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            // This first split is deliberately regular opaque-only. A CSG binding may need clip/image state and
            // maintains a different producer/raster ordering contract, so it remains on the combined callback.
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None){
                reset();
                return false;
            }
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            // The original callback interleaves dispatch and raster specifically because a second instance can
            // overwrite this whole persistent buffer.  This first graph-owned slice deliberately declines that
            // case rather than moving either draw across a potentially aliasing producer.
            for(const Core::BufferHandle& existing : outputBuffers){
                if(existing.get() == mesh->emulationVertexBuffer.get()){
                    reset();
                    return false;
                }
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh->emulationVertexBuffer);
        }
        captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItemVector& currentDrawItems
    )const{
        if(
            !captured
            || currentDrawItems.size() != drawItems.size()
            || outputBuffers.size() != drawItems.size()
        )
            return false;

        for(usize drawIndex = 0u; drawIndex < currentDrawItems.size(); ++drawIndex){
            const MaterialPassDrawItem& expected = drawItems[drawIndex];
            const MaterialPassDrawItem& current = currentDrawItems[drawIndex];
            if(
                current.meshKey != expected.meshKey
                || current.instanceIndex != expected.instanceIndex
                || current.materialConstantByteOffset != expected.materialConstantByteOffset
                || current.shadingModelId != expected.shadingModelId
                || current.meshletConeCullScaleSafe != expected.meshletConeCullScaleSafe
            )
                return false;

            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(current.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            )
                return false;
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems)const{
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
    }
};


// AVBOIT compute-emulation producers retain the descriptor slot as well as the output buffer: this compute path
// selects its writable generated-vertex target through the global heap. They are deliberately regular-only; any CSG
// compute draw keeps the complete phase on its established local bridge.
struct AvboitAliasFreeComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using HeapSlotVector = Vector<u32, Core::Alloc::GlobalArena>;

    DrawItemVector drawItems;
    BufferVector outputBuffers;
    HeapSlotVector outputHeapSlots;
    bool captured = false;

    explicit AvboitAliasFreeComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : drawItems(arena)
        , outputBuffers(arena)
        , outputHeapSlots(arena)
    {}

    void reset(){
        drawItems.clear();
        outputBuffers.clear();
        outputHeapSlots.clear();
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems
    ){
        reset();
        if(sourceDrawItems.computeDrawItems.empty())
            return false;

        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        outputHeapSlots.reserve(sourceDrawItems.computeDrawItems.size());
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None){
                reset();
                return false;
            }
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            for(usize outputIndex = 0u; outputIndex < outputBuffers.size(); ++outputIndex){
                if(
                    outputBuffers[outputIndex].get() == mesh->emulationVertexBuffer.get()
                    || outputHeapSlots[outputIndex] == mesh->emulationVertexHeapHandle.slot()
                ){
                    reset();
                    return false;
                }
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh->emulationVertexBuffer);
            outputHeapSlots.push_back(mesh->emulationVertexHeapHandle.slot());
        }
        captured = drawItems.size() == outputBuffers.size()
            && outputBuffers.size() == outputHeapSlots.size()
            && !drawItems.empty()
        ;
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem)const{
        if(
            !captured
            || outputBuffers.size() != drawItems.size()
            || outputHeapSlots.size() != drawItems.size()
        )
            return false;
        for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItems[drawIndex].meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
                || mesh->emulationVertexHeapHandle.slot() != outputHeapSlots[drawIndex]
            )
                return false;
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems)const{
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
    }
};


// A persistent generated-vertex buffer normally forces local dispatch/raster interleaving. This deliberately
// narrow graph-owned case can retain up to four regular draws that share one buffer and descriptor slot, so each
// consumer can explicitly opt into its supported D(A) -> R(A) -> ... ordering without batching producers.
struct RegularSharedComputeEmulationGraphPlan{
    static constexpr usize s_MinDrawCount = 2u;
    static constexpr usize s_StorageMaxDrawCount = 4u;

    MaterialPassDrawItem drawItems[s_StorageMaxDrawCount] = {};
    Core::BufferHandle outputBuffer;
    u32 outputHeapSlot = 0u;
    usize drawCount = 0u;
    bool captured = false;

    void reset(){
        for(MaterialPassDrawItem& drawItem : drawItems)
            drawItem = {};
        outputBuffer = nullptr;
        outputHeapSlot = 0u;
        drawCount = 0u;
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems,
        const usize allowedMaxDrawCount
    ){
        reset();
        if(
            allowedMaxDrawCount < s_MinDrawCount
            || allowedMaxDrawCount > s_StorageMaxDrawCount
            || sourceDrawItems.computeDrawItems.size() < s_MinDrawCount
            || sourceDrawItems.computeDrawItems.size() > allowedMaxDrawCount
        )
            return false;

        drawCount = sourceDrawItems.computeDrawItems.size();
        for(usize drawIndex = 0u; drawIndex < drawCount; ++drawIndex){
            const MaterialPassDrawItem& drawItem = sourceDrawItems.computeDrawItems[drawIndex];
            if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None)
                return false;

            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            )
                return false;

            if(drawIndex == 0u){
                outputBuffer = mesh->emulationVertexBuffer;
                outputHeapSlot = mesh->emulationVertexHeapHandle.slot();
            }
            else if(
                mesh->emulationVertexBuffer.get() != outputBuffer.get()
                || mesh->emulationVertexHeapHandle.slot() != outputHeapSlot
            )
                return false;

            drawItems[drawIndex] = drawItem;
        }
        captured = static_cast<bool>(outputBuffer);
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem, const usize drawIndex)const{
        if(!captured || drawIndex >= drawCount || !outputBuffer)
            return false;

        MeshResources* mesh = nullptr;
        return meshSystem.findMeshResources(drawItems[drawIndex].meshKey, mesh)
            && mesh
            && mesh->emulationVertexBuffer
            && mesh->emulationVertexHeapHandle.valid()
            && mesh->emulationVertexBuffer.get() == outputBuffer.get()
            && mesh->emulationVertexHeapHandle.slot() == outputHeapSlot
        ;
    }

    void materialize(const usize drawIndex, MaterialPassDrawItems& outDrawItems)const{
        NWB_ASSERT(captured && drawIndex < drawCount);
        outDrawItems.computeDrawItems.push_back(drawItems[drawIndex]);
    }
};


// Receiver-surface compute emulation is a separate opaque slice.  Its output must remain untouched between this
// early producer and its later raster callback, so reject aliases with regular opaque compute work as well as with
// another receiver-surface item.  Later opaque CSG interval-sample work follows receiver rasterization and is
// therefore not part of this pre-raster alias exclusion.
struct OpaqueCsgReceiverComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    DrawItemVector regularDrawItems;
    BufferVector outputBuffers;
    BufferVector regularOutputBuffers;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    bool captured = false;

    explicit OpaqueCsgReceiverComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , regularDrawItems(arena)
        , outputBuffers(arena)
        , regularOutputBuffers(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        regularDrawItems.clear();
        outputBuffers.clear();
        regularOutputBuffers.clear();
        receiverRanges.clear();
        cutters.clear();
        workRegion = {};
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const MaterialPassDrawItems& sourceRegularDrawItems,
        const CsgFrameGpuData& csgFrameData
    ){
        reset();
        if(receiverSurfaceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
            return false;

        meshDrawItems.assign(
            receiverSurfaceDrawItems.meshDrawItems.begin(),
            receiverSurfaceDrawItems.meshDrawItems.end()
        );
        regularDrawItems.assign(
            sourceRegularDrawItems.computeDrawItems.begin(),
            sourceRegularDrawItems.computeDrawItems.end()
        );
        drawItems.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        regularOutputBuffers.reserve(regularDrawItems.size());
        for(const MaterialPassDrawItem& regularDrawItem : regularDrawItems){
            MeshResources* regularMesh = nullptr;
            if(
                !meshSystem.findMeshResources(regularDrawItem.meshKey, regularMesh)
                || !regularMesh
                || !regularMesh->emulationVertexBuffer
                || !regularMesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            regularOutputBuffers.push_back(regularMesh->emulationVertexBuffer);
        }
        for(const MaterialPassDrawItem& drawItem : receiverSurfaceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
                reset();
                return false;
            }
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            for(const Core::BufferHandle& existing : outputBuffers){
                if(existing.get() == mesh->emulationVertexBuffer.get()){
                    reset();
                    return false;
                }
            }
            // G-buffer renders regular opaque work after this producer but before receiver-surface rasterization.
            // A regular compute item that writes this output would replace the generated receiver vertices first.
            for(const Core::BufferHandle& regularOutput : regularOutputBuffers){
                if(regularOutput.get() == mesh->emulationVertexBuffer.get()){
                    reset();
                    return false;
                }
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh->emulationVertexBuffer);
        }
        receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        workRegion = csgFrameData.workRegion;
        captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem)const{
        if(
            !captured
            || outputBuffers.size() != drawItems.size()
            || regularOutputBuffers.size() != regularDrawItems.size()
        )
            return false;
        for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItems[drawIndex].meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            )
                return false;
        }
        for(usize drawIndex = 0u; drawIndex < regularDrawItems.size(); ++drawIndex){
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(regularDrawItems[drawIndex].meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != regularOutputBuffers[drawIndex].get()
            )
                return false;
            for(const Core::BufferHandle& receiverOutput : outputBuffers){
                if(mesh->emulationVertexBuffer.get() == receiverOutput.get())
                    return false;
            }
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
        outCsgFrameData.receiverRanges.assign(receiverRanges.begin(), receiverRanges.end());
        outCsgFrameData.cutters.assign(cutters.begin(), cutters.end());
        outCsgFrameData.workRegion = workRegion;
    }
};


// Pairwise-distinct CSG compute emulation retains the complete frozen CSG stream and generated-vertex targets.
// Opaque interval-sample and CSG-only AVBOIT Occupancy, Extinction, or Accumulation each place their producer
// directly before the matching raster, so aliases with prior phase outputs are harmless; only aliases inside this
// frozen CSG stream could overwrite a generated buffer before its own raster consumes it.
struct OpaqueCsgIntervalSampleComputeEmulationGraphPlan{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;
    using HeapSlotVector = Vector<u32, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector meshDrawItems;
    DrawItemVector drawItems;
    BufferVector outputBuffers;
    HeapSlotVector outputHeapSlots;
    ReceiverRangeVector receiverRanges;
    CutterVector cutters;
    CsgFrameWorkRegion workRegion;
    bool captured = false;

    explicit OpaqueCsgIntervalSampleComputeEmulationGraphPlan(Core::Alloc::GlobalArena& arena)
        : meshDrawItems(arena)
        , drawItems(arena)
        , outputBuffers(arena)
        , outputHeapSlots(arena)
        , receiverRanges(arena)
        , cutters(arena)
    {}

    void reset(){
        meshDrawItems.clear();
        drawItems.clear();
        outputBuffers.clear();
        outputHeapSlots.clear();
        receiverRanges.clear();
        cutters.clear();
        workRegion = {};
        captured = false;
    }

    [[nodiscard]] bool capture(
        RendererMeshSystem& meshSystem,
        const MaterialPassDrawItems& sourceDrawItems,
        const CsgFrameGpuData& csgFrameData
    ){
        reset();
        if(sourceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
            return false;

        meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
        drawItems.reserve(sourceDrawItems.computeDrawItems.size());
        outputBuffers.reserve(sourceDrawItems.computeDrawItems.size());
        outputHeapSlots.reserve(sourceDrawItems.computeDrawItems.size());
        for(const MaterialPassDrawItem& drawItem : sourceDrawItems.computeDrawItems){
            if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
                reset();
                return false;
            }
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItem.meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
            ){
                reset();
                return false;
            }
            for(usize outputIndex = 0u; outputIndex < outputBuffers.size(); ++outputIndex){
                if(
                    outputBuffers[outputIndex].get() == mesh->emulationVertexBuffer.get()
                    || outputHeapSlots[outputIndex] == mesh->emulationVertexHeapHandle.slot()
                ){
                    reset();
                    return false;
                }
            }
            drawItems.push_back(drawItem);
            outputBuffers.push_back(mesh->emulationVertexBuffer);
            outputHeapSlots.push_back(mesh->emulationVertexHeapHandle.slot());
        }
        receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        workRegion = csgFrameData.workRegion;
        captured = drawItems.size() == outputBuffers.size()
            && outputBuffers.size() == outputHeapSlots.size()
            && !drawItems.empty();
        return captured;
    }

    [[nodiscard]] bool matches(RendererMeshSystem& meshSystem)const{
        if(
            !captured
            || outputBuffers.size() != drawItems.size()
            || outputHeapSlots.size() != drawItems.size()
        )
            return false;
        for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
            MeshResources* mesh = nullptr;
            if(
                !meshSystem.findMeshResources(drawItems[drawIndex].meshKey, mesh)
                || !mesh
                || !mesh->emulationVertexBuffer
                || !mesh->emulationVertexHeapHandle.valid()
                || mesh->emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
                || mesh->emulationVertexHeapHandle.slot() != outputHeapSlots[drawIndex]
            )
                return false;
        }
        return true;
    }

    void materialize(MaterialPassDrawItems& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        outDrawItems.meshDrawItems.assign(meshDrawItems.begin(), meshDrawItems.end());
        outDrawItems.computeDrawItems.assign(drawItems.begin(), drawItems.end());
        outCsgFrameData.receiverRanges.assign(receiverRanges.begin(), receiverRanges.end());
        outCsgFrameData.cutters.assign(cutters.begin(), cutters.end());
        outCsgFrameData.workRegion = workRegion;
    }
};


// Prepared transparent CSG exposes receiver-surface -> span before the following interval-combine callback. The
// later phase-local occupancy uploads depend on Combine so they cannot overwrite its frozen CSG buffers first.
struct AvboitCsgReceiverSpanGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentCsgIntervalsTiming = nullptr;
        TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool csgFrameBuffersUploaded = false;
        bool receiverSpanInputImageStatesGraphOwned = false;
        bool receiverSpanOutputImageStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : transparentCsgSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !payload.transparentCsgIntervalsTiming
            || !payload.transparentCsgSnapshot.captured
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.transparentCsgIntervalsTiming->has_value()){
            commandList.endRenderPass();
            return true;
        }
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems receiverSurfaceDrawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        payload.transparentCsgSnapshot.materialize(receiverSurfaceDrawItems, csgFrameData);
        RendererSystem& renderer = *payload.renderer;
        const bool drawBuffersReady = renderer.m_materialSystem.materialPassDrawBuffersReady(
            payload.transparentCsgSnapshot.instanceCount,
            payload.transparentCsgSnapshot.materialTypedByteCount
        );
        const bool csgResourcesReady = renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData);
        const bool receiverSurfaceDrawResourcesReady = renderer.m_materialSystem.materialPassDrawResourcesReady(
            receiverSurfaceDrawItems
        );
        const bool spanReady =
            payload.csgFrameBuffersUploaded
            && payload.targets->framebuffer
            && !receiverSurfaceDrawItems.empty()
            && csgFrameData.hasWork()
            && drawBuffersReady
            && csgResourcesReady
            && receiverSurfaceDrawResourcesReady
        ;
        if(spanReady){
            renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(
                commandList,
                *payload.targets,
                csgFrameData,
                payload.receiverSpanOutputImageStatesGraphOwned,
                payload.receiverSpanInputImageStatesGraphOwned
            );
        }
        else{
            // Pre and Span use the same frozen readiness snapshot. A defensive mismatch must not leave a timestamp
            // reservation open or let Combine consume an unbuilt span image.
            payload.transparentCsgIntervalsTiming->value().discardTiming();
            payload.transparentCsgIntervalsTiming->reset();
        }
        commandList.endRenderPass();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentCsgIntervalsTiming && payload.transparentCsgIntervalsTiming->has_value()){
            payload.transparentCsgIntervalsTiming->value().discardTiming();
            payload.transparentCsgIntervalsTiming->reset();
        }
    }
};


// Interval combine consumes the five graph-visible span/peel inputs and writes the four removed-interval outputs.
struct AvboitCsgIntervalCombineGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentCsgIntervalsTiming = nullptr;
        TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool csgFrameBuffersUploaded = false;
        bool intervalCombineInputImageStatesGraphOwned = false;
        bool removedIntervalOutputImageStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : transparentCsgSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !payload.transparentCsgIntervalsTiming
            || !payload.transparentCsgSnapshot.captured
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.transparentCsgIntervalsTiming->has_value()){
            commandList.endRenderPass();
            return true;
        }
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems receiverSurfaceDrawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        payload.transparentCsgSnapshot.materialize(receiverSurfaceDrawItems, csgFrameData);
        RendererSystem& renderer = *payload.renderer;
        const bool drawBuffersReady = renderer.m_materialSystem.materialPassDrawBuffersReady(
            payload.transparentCsgSnapshot.instanceCount,
            payload.transparentCsgSnapshot.materialTypedByteCount
        );
        const bool csgResourcesReady = renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData);
        const bool receiverSurfaceDrawResourcesReady = renderer.m_materialSystem.materialPassDrawResourcesReady(
            receiverSurfaceDrawItems
        );
        const bool combineReady =
            payload.csgFrameBuffersUploaded
            && payload.targets->framebuffer
            && !receiverSurfaceDrawItems.empty()
            && csgFrameData.hasWork()
            && drawBuffersReady
            && csgResourcesReady
            && receiverSurfaceDrawResourcesReady
        ;
        if(combineReady){
            renderer.m_csgSystem.dispatchCsgIntervalCombine(
                commandList,
                *payload.targets,
                csgFrameData,
                payload.removedIntervalOutputImageStatesGraphOwned,
                payload.intervalCombineInputImageStatesGraphOwned
            );
            payload.transparentCsgIntervalsTiming->value().finishTiming(commandList);
            payload.transparentCsgIntervalsTiming->reset();
        }
        else{
            // Pre and Combine use the same frozen readiness snapshot. A defensive mismatch must not leave a
            // timestamp reservation open or publish a stale removed-interval image.
            payload.transparentCsgIntervalsTiming->value().discardTiming();
            payload.transparentCsgIntervalsTiming->reset();
        }
        commandList.endRenderPass();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentCsgIntervalsTiming && payload.transparentCsgIntervalsTiming->has_value()){
            payload.transparentCsgIntervalsTiming->value().discardTiming();
            payload.transparentCsgIntervalsTiming->reset();
        }
    }
};


// Each transparent AVBOIT raster phase overwrites the shared material and CSG streams. Retain this phase-local
// payload so its native consumer cannot observe a later gather or mutate bytes owned by another phase.
struct TransparentMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector regularMeshDrawItems;
    DrawItemVector regularComputeDrawItems;
    DrawItemVector csgMeshDrawItems;
    DrawItemVector csgComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    CsgFrameWorkRegion csgWorkRegion;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool captured = false;

    explicit TransparentMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : regularMeshDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(
        const MaterialPassDrawItemPartitions& drawItems,
        const CsgFrameGpuData& csgFrameData,
        const usize inInstanceCount,
        const usize inMaterialTypedByteCount
    ){
        regularMeshDrawItems.assign(drawItems.regular.meshDrawItems.begin(), drawItems.regular.meshDrawItems.end());
        regularComputeDrawItems.assign(drawItems.regular.computeDrawItems.begin(), drawItems.regular.computeDrawItems.end());
        csgMeshDrawItems.assign(drawItems.csg.meshDrawItems.begin(), drawItems.csg.meshDrawItems.end());
        csgComputeDrawItems.assign(drawItems.csg.computeDrawItems.begin(), drawItems.csg.computeDrawItems.end());
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        csgCutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(
        MaterialPassDrawItemPartitions& outDrawItems,
        CsgFrameGpuData& outCsgFrameData
    )const{
        outDrawItems.regular.meshDrawItems.assign(regularMeshDrawItems.begin(), regularMeshDrawItems.end());
        outDrawItems.regular.computeDrawItems.assign(regularComputeDrawItems.begin(), regularComputeDrawItems.end());
        outDrawItems.csg.meshDrawItems.assign(csgMeshDrawItems.begin(), csgMeshDrawItems.end());
        outDrawItems.csg.computeDrawItems.assign(csgComputeDrawItems.begin(), csgComputeDrawItems.end());
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


// The opaque regular compute-emulation producer is deliberately a separate callback: it must finish every selected
// alias-free generated-vertex output before G-buffer begins dynamic rendering. The paired G-buffer consumer then
// receives the compiler-owned UAV-to-VertexBuffer transition. Shared-output batches remain on the compatibility
// combined callback, where dispatch and raster still interleave per draw.
struct OpaqueRegularComputeEmulationGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueRegularComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.plan.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(!frameSetupReady)
            return true;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        payload.plan.materialize(drawItems);
        // The graph imported the exact persistent output handles retained by the frozen plan. Reject a live mesh
        // resource replacement instead of dispatching into a newly selected descriptor target outside that set.
        if(!payload.plan.matches(renderer.m_meshSystem, drawItems.computeDrawItems))
            return false;
        if(
            !payload.materialDrawBuffersUploaded
            || !renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
            || !renderer.m_materialSystem.materialPassDrawResourcesReady(drawItems)
        )
            return true;

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(
            payload.targets->framebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            nullptr,
            MaterialPipelinePass::Opaque,
            nullptr,
            deferredViewportState,
            false,
            false,
            false,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        renderer.m_materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        return true;
    }
};


// The small shared-output sequence keeps the compatibility order without hiding its alternating output states
// inside one callback. Each graph instance records either one compute generation or one raster draw; raster phases
// close dynamic rendering before the next generation phase can bind a compute pipeline.
struct OpaqueRegularSharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        Optional<Core::GpuTimingMeasure>* opaqueRegularTiming = nullptr;
        RegularSharedComputeEmulationGraphPlan plan;
        usize drawIndex = 0u;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool finishTiming = false;
        Phase phase = Phase::Generate;
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.opaqueRegularTiming
            || !payload.plan.captured
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        // G-buffer starts the one preserved Opaque Regular range only after its exact frozen material resources
        // are ready. A later defensive miss must be a no-op instead of rasterizing stale generated vertices.
        if(!payload.opaqueRegularTiming->has_value()){
            if(payload.phase == Phase::Raster)
                commandList.endRenderPass();
            return true;
        }

        RendererSystem& renderer = *payload.renderer;
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(!frameSetupReady || !payload.plan.matches(renderer.m_meshSystem, payload.drawIndex))
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        payload.plan.materialize(payload.drawIndex, drawItems);
        if(
            !payload.materialDrawBuffersUploaded
            || !renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
            || !renderer.m_materialSystem.materialPassDrawResourcesReady(drawItems)
        ){
            discardTiming(payload.opaqueRegularTiming);
            if(payload.phase == Phase::Raster)
                commandList.endRenderPass();
            return true;
        }

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(
            payload.targets->framebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.phase == Phase::Raster ? payload.targets->framebuffer.get() : nullptr,
            MaterialPipelinePass::Opaque,
            nullptr,
            deferredViewportState,
            false,
            false,
            false,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        if(payload.phase == Phase::Generate)
            renderer.m_materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        else{
            renderer.m_materialSystem.renderComputeMaterialPassDrawItemsRasterOnly(
                drawContext,
                drawItems.computeDrawItems
            );
            if(payload.finishTiming){
                payload.opaqueRegularTiming->value().finishTiming(commandList);
                payload.opaqueRegularTiming->reset();
            }
            commandList.endRenderPass();
        }
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.opaqueRegularTiming); }
};


// Receiver-surface emulation follows the same graph-owned output boundary as regular opaque work, but has an
// independent CSG readiness contract.  It records before G-buffer's raster callback while its output plan excludes
// every regular opaque compute output that can execute in between.
struct OpaqueCsgReceiverComputeEmulationGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueCsgReceiverComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.plan.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(!frameSetupReady)
            return true;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        payload.plan.materialize(drawItems, csgFrameData);
        // The output set imported by the graph is immutable.  Do not re-resolve a changed mesh resource into an
        // arbitrary descriptor slot after declaration; reject and let the existing graph retry path rebuild it.
        if(!payload.plan.matches(renderer.m_meshSystem))
            return false;

        const bool deferredResourcesReady =
            payload.materialDrawBuffersUploaded
            && renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && payload.csgFrameBuffersUploaded
            && csgFrameData.hasWork()
            && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
        ;
        if(
            !csgResourcesReady
            || !renderer.m_materialSystem.materialPassDrawResourcesReady(drawItems)
        )
            return true;

        Core::ViewportState csgViewportState;
        csgViewportState
            .addViewport(payload.targets->framebuffer->getFramebufferInfo().getViewport())
            .addScissorRect(csgFrameData.workRegion.resolveRect(payload.targets->width, payload.targets->height))
        ;
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.targets->framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            nullptr,
            csgViewportState,
            // Receiver-event images are raster-only outputs.  The subsequent G-buffer task owns their UAV state;
            // this compute producer uses the CSG clip bindings but must not claim or transition those images.
            true,
            false,
            true,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        renderer.m_materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        return true;
    }
};


// Interval-sample CSG compute emulation is split only for pairwise-distinct generated outputs. It follows
// interval combine and precedes the existing CSG material/cap raster callback, keeping the output handoff and the
// original Opaque CSG timing range inside that one semantic Graphics packet.
struct OpaqueCsgIntervalSampleComputeEmulationGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        Optional<Core::GpuTimingMeasure>* opaqueCsgTiming = nullptr;
        OpaqueCsgIntervalSampleComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool intervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
        {}
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.opaqueCsgTiming
            || !payload.plan.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(!frameSetupReady)
            return true;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        payload.plan.materialize(drawItems, csgFrameData);
        // The graph imported these exact output handles and descriptor slots. A live replacement must reject the
        // packet, not write through a newly selected CSG material descriptor after declaration.
        if(!payload.plan.matches(renderer.m_meshSystem))
            return false;

        const bool deferredResourcesReady =
            payload.materialDrawBuffersUploaded
            && renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && payload.csgFrameBuffersUploaded
            && csgFrameData.hasWork()
            && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
        ;
        if(
            !csgResourcesReady
            || !renderer.m_materialSystem.materialPassDrawResourcesReady(drawItems)
        )
            return true;
        if(payload.opaqueCsgTiming->has_value())
            return false;

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(
            payload.targets->framebuffer->getFramebufferInfo().getViewport()
        );
        payload.opaqueCsgTiming->emplace(
            renderer.m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_OpaqueCsg,
            renderer.m_graphics.getDevice(),
            commandList
        );
        // The scope crosses the following raster callback, so close its marker in this producer before command-list
        // finalization. The terminal sample callback owns finishTiming/discard.
        payload.opaqueCsgTiming->value().finishMarker();
        const MaterialPassDrawContext drawContext{
            commandList,
            nullptr,
            MaterialPipelinePass::Opaque,
            nullptr,
            deferredViewportState,
            false,
            payload.intervalSampleImageStatesGraphOwned,
            payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        renderer.m_materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.opaqueCsgTiming); }
};


// The opaque G-buffer runs after graph-owned material and CSG uploads plus the native deferred clear. Its barriers
// and final state are therefore part of the graph packet; transparent CSG retains its separate AVBOIT snapshot.
struct GbufferGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool csgIntervalPeelTargetStatesGraphOwned = false;
        bool csgReceiverSurfaceImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool regularComputeEmulationOutputStatesGraphOwned = false;
        // Two or three shared-output regular compute draws are recorded by serial successor tasks. G-buffer
        // retains only regular mesh rasterization, starts the original timing range, and leaves it open for the
        // terminal shared raster task to finish.
        bool regularSharedComputeEmulationDrawsGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* regularSharedComputeEmulationTiming = nullptr;
        bool csgReceiverComputeEmulationOutputStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : opaqueDrawSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.opaqueDrawSnapshot.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        DeferredFrameTargets& deferredTargets = *payload.targets;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

        MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && payload.sceneShadingSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(frameSetupReady)
            payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && payload.materialDrawBuffersUploaded
            && renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.opaqueDrawSnapshot.instanceCount,
                payload.opaqueDrawSnapshot.materialTypedByteCount
            )
        ;
        const bool regularDrawResourcesReady =
            deferredResourcesReady
            && renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.regular)
        ;
        MaterialPassDrawItems regularMeshDrawItems{ scratchArena };
        const MaterialPassDrawItems* regularDrawItemsForGbuffer = &opaqueDrawItems.regular;
        if(payload.regularSharedComputeEmulationDrawsGraphOwned){
            regularMeshDrawItems.meshDrawItems.assign(
                opaqueDrawItems.regular.meshDrawItems.begin(),
                opaqueDrawItems.regular.meshDrawItems.end()
            );
            regularDrawItemsForGbuffer = &regularMeshDrawItems;
        }
        const bool csgResourcesReady =
            deferredResourcesReady
            && (
                !csgFrameData.hasWork()
                || (
                    payload.csgFrameBuffersUploaded
                    && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
                )
            )
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        if(deferredResourcesReady){
            // Every opaque CSG frame byte is now captured in immutable graph uploads. Native recording consumes those
            // declared resources without rewriting the clip-context or interval-sample uniform payloads.
            const bool csgSampleStateReady = csgResourcesReady;
            if(csgSampleStateReady && csgFrameData.hasWork())
                renderer.m_csgSystem.dispatchCsgIntervalPeels(
                    commandList,
                    deferredTargets,
                    csgFrameData,
                    payload.csgIntervalPeelTargetStatesGraphOwned,
                    payload.csgClipBufferStatesGraphOwned,
                    payload.materialFrameStatesGraphOwned
                );
            const MaterialPassDrawContext opaqueDrawContext{
                commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState,
                false,
                false,
                false,
                payload.materialFrameStatesGraphOwned,
                payload.materialGeometryStatesGraphOwned,
                payload.regularComputeEmulationOutputStatesGraphOwned
            };
            if(
                regularDrawResourcesReady
                && (
                    payload.regularSharedComputeEmulationDrawsGraphOwned
                    || !regularDrawItemsForGbuffer->empty()
                )
            ){
                if(payload.regularSharedComputeEmulationDrawsGraphOwned){
                    if(
                        !payload.regularSharedComputeEmulationTiming
                        || payload.regularSharedComputeEmulationTiming->has_value()
                    )
                        return false;
                    payload.regularSharedComputeEmulationTiming->emplace(
                        renderer.m_graphics.gpuTiming(),
                        RendererGpuTimingScope::s_OpaqueRegular,
                        renderer.m_graphics.getDevice(),
                        commandList
                    );
                    // The timestamps span ordered graph callbacks; the marker must still close in this producer
                    // callback before its command list can be finalized.
                    payload.regularSharedComputeEmulationTiming->value().finishMarker();
                    if(!regularDrawItemsForGbuffer->empty()){
                        renderer.m_materialSystem.renderMaterialPassDrawItems(
                            opaqueDrawContext,
                            *regularDrawItemsForGbuffer
                        );
                    }
                }
                else{
                    Core::GpuTimingMeasure timing(
                        renderer.m_graphics.gpuTiming(),
                        RendererGpuTimingScope::s_OpaqueRegular,
                        renderer.m_graphics.getDevice(),
                        commandList
                    );
                    renderer.m_materialSystem.renderMaterialPassDrawItems(
                        opaqueDrawContext,
                        *regularDrawItemsForGbuffer
                    );
                }
            }

            Core::ViewportState csgIntervalViewportState;
            csgIntervalViewportState
                .addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport())
                .addScissorRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height))
            ;
            const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
                commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::CsgReceiverSurface,
                nullptr,
                csgIntervalViewportState,
                payload.csgReceiverSurfaceImageStatesGraphOwned,
                false,
                payload.csgClipBufferStatesGraphOwned,
                payload.materialFrameStatesGraphOwned,
                payload.materialGeometryStatesGraphOwned,
                payload.csgReceiverComputeEmulationOutputStatesGraphOwned
            };
            if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
                Core::GpuTimingMeasure timing(
                    renderer.m_graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueCsgReceiverSurface,
                    renderer.m_graphics.getDevice(),
                    commandList
                );
                renderer.m_materialSystem.renderMaterialPassDrawItems(
                    csgReceiverSurfaceDrawContext,
                    opaqueDrawItems.csgReceiverSurface
                );
            }
        }
        commandList.endRenderPass();
        return true;
    }

    static void discarded(Payload& payload){
        if(
            !payload.regularSharedComputeEmulationTiming
            || !payload.regularSharedComputeEmulationTiming->has_value()
        )
            return;
        payload.regularSharedComputeEmulationTiming->value().discardTiming();
        payload.regularSharedComputeEmulationTiming->reset();
    }
};


// Prepared acceleration-structure recording stays in Shadow Preparation so frozen-plan fallback and acceptance
// semantics remain unchanged. This state-only successor publishes the actual AccelStructWrite -> AccelStructRead
// boundaries through graph lowering before any later Prefix or Compute consumer can observe the backing storage.
struct ShadowPrepareAccelStructFinalizeGraphTask{
    struct Payload{};

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }
};


// Receiver-span build consumes the StorageImage event aliases emitted by the receiver-surface raster pass and
// publishes span aliases for interval combine. The opaque graph exposes those two exact same-UAV boundaries while
// aggregate native and transparent compatibility callers retain their local fences.
struct CsgReceiverSpanBuildGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool receiverSpanInputImageStatesGraphOwned = false;
        bool receiverSpanOutputImageStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : opaqueDrawSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.opaqueDrawSnapshot.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        DeferredFrameTargets& deferredTargets = *payload.targets;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

        MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && payload.sceneShadingSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(frameSetupReady)
            payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && payload.materialDrawBuffersUploaded
            && renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.opaqueDrawSnapshot.instanceCount,
                payload.opaqueDrawSnapshot.materialTypedByteCount
            )
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (
                !csgFrameData.hasWork()
                || (
                    payload.csgFrameBuffersUploaded
                    && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
                )
            )
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty()
                || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        if(csgResourcesReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
            renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(
                commandList,
                deferredTargets,
                csgFrameData,
                payload.receiverSpanOutputImageStatesGraphOwned,
                payload.receiverSpanInputImageStatesGraphOwned
            );
        }
        commandList.endRenderPass();
        return true;
    }
};


// Interval combine consumes the five StorageImage aliases produced by peel/span build, then writes the four
// removed-interval aliases consumed by the following material/cap draws. Keep both boundaries in the established
// Graphics submission when safe, but let the graph lower their exact same-UAV fences.
struct CsgIntervalCombineGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool intervalCombineInputImageStatesGraphOwned = false;
        bool removedIntervalOutputImageStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : opaqueDrawSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.opaqueDrawSnapshot.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        DeferredFrameTargets& deferredTargets = *payload.targets;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

        MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && payload.sceneShadingSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(frameSetupReady)
            payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && payload.materialDrawBuffersUploaded
            && renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.opaqueDrawSnapshot.instanceCount,
                payload.opaqueDrawSnapshot.materialTypedByteCount
            )
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (
                !csgFrameData.hasWork()
                || (
                    payload.csgFrameBuffersUploaded
                    && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
                )
            )
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty()
                || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        if(csgResourcesReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
            renderer.m_csgSystem.dispatchCsgIntervalCombine(
                commandList,
                deferredTargets,
                csgFrameData,
                payload.removedIntervalOutputImageStatesGraphOwned,
                payload.intervalCombineInputImageStatesGraphOwned
            );
        }
        commandList.endRenderPass();
        return true;
    }
};


// Interval combine writes StorageImage-backed removed-interval outputs, while the following opaque material and cap
// draws load those same aliases. This task receives the graph-lowered output fence rather than replaying it from a
// renderer thunk.
struct CsgIntervalSampleGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool intervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        // When the preceding interval-sample producer owns generated-vertex UAV output, this callback keeps the
        // frozen CSG compute draws raster-only so the compiler supplies the one UAV-to-VertexBuffer boundary.
        bool csgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* opaqueCsgComputeEmulationTiming = nullptr;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : opaqueDrawSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
            || !payload.opaqueDrawSnapshot.captured
            || (payload.csgComputeEmulationOutputStatesGraphOwned
                && !payload.opaqueCsgComputeEmulationTiming)
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        DeferredFrameTargets& deferredTargets = *payload.targets;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

        MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && payload.sceneShadingSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(frameSetupReady)
            payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && payload.materialDrawBuffersUploaded
            && renderer.m_materialSystem.materialPassDrawBuffersReady(
                payload.opaqueDrawSnapshot.instanceCount,
                payload.opaqueDrawSnapshot.materialTypedByteCount
            )
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (
                !csgFrameData.hasWork()
                || (
                    payload.csgFrameBuffersUploaded
                    && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
                )
            )
        ;
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty()
                || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        // The producer validates the same frozen full CSG stream before opening this cross-callback measure. Keep
        // the fallback defensive: if a later readiness check disagrees, retire the reservation instead of leaving
        // a stale generated-vertex raster or an unsubmitted timing scope alive.
        if(
            payload.csgComputeEmulationOutputStatesGraphOwned
            && payload.opaqueCsgComputeEmulationTiming->has_value()
            && (!csgResourcesReady || !csgDrawResourcesReady)
        ){
            payload.opaqueCsgComputeEmulationTiming->value().discardTiming();
            payload.opaqueCsgComputeEmulationTiming->reset();
            commandList.endRenderPass();
            return true;
        }
        const bool csgComputeEmulationReady =
            !payload.csgComputeEmulationOutputStatesGraphOwned
            || payload.opaqueCsgComputeEmulationTiming->has_value()
        ;
        if(csgResourcesReady && csgDrawResourcesReady && csgComputeEmulationReady){
            Core::ViewportState deferredViewportState;
            deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());
            const MaterialPassDrawContext csgDrawContext{
                commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState,
                false,
                payload.intervalSampleImageStatesGraphOwned,
                payload.csgClipBufferStatesGraphOwned,
                payload.materialFrameStatesGraphOwned,
                payload.materialGeometryStatesGraphOwned,
                payload.csgComputeEmulationOutputStatesGraphOwned
            };
            if(!opaqueDrawItems.csg.empty()){
                if(payload.csgComputeEmulationOutputStatesGraphOwned){
                    renderer.m_materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
                    payload.opaqueCsgComputeEmulationTiming->value().finishTiming(commandList);
                    payload.opaqueCsgComputeEmulationTiming->reset();
                }
                else{
                    Core::GpuTimingMeasure timing(
                        renderer.m_graphics.gpuTiming(),
                        RendererGpuTimingScope::s_OpaqueCsg,
                        renderer.m_graphics.getDevice(),
                        commandList
                    );
                    renderer.m_materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
                }
            }
            if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
                renderer.m_csgSystem.renderCsgIntervalCaps(
                    commandList,
                    deferredTargets,
                    csgFrameData,
                    payload.intervalSampleImageStatesGraphOwned,
                    payload.csgClipBufferStatesGraphOwned,
                    payload.materialFrameStatesGraphOwned
                );
            }
        }
        commandList.endRenderPass();
        return true;
    }

    static void discarded(Payload& payload){
        if(
            !payload.opaqueCsgComputeEmulationTiming
            || !payload.opaqueCsgComputeEmulationTiming->has_value()
        )
            return;
        payload.opaqueCsgComputeEmulationTiming->value().discardTiming();
        payload.opaqueCsgComputeEmulationTiming->reset();
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::GpuQueueRequest GraphicsQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Graphics,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// Built-in uploads require Transfer capability, while these small frame updates must stay on the Graphics packet
// that consumes them. Vulkan's Graphics transport advertises Transfer capability, so the compiler retains that
// physical route without introducing an asynchronous ownership handoff.
[[nodiscard]] static Core::GpuQueueRequest GraphicsUploadQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

[[nodiscard]] static Core::GpuQueueRequest ComputeQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Compute,
        true,
        true,
    };
}

// Material raster callbacks may first generate vertices with a compute-emulation dispatch, then issue ordinary
// Graphics work. They must stay on the primary Graphics transport, but their declaration must include both command
// capabilities so debug recording can reject a genuinely incompatible route rather than the valid emulation path.
[[nodiscard]] static Core::GpuQueueRequest GraphicsComputeQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Graphics)
            | static_cast<u8>(Core::GpuQueueCapability::Compute)
        ),
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// These callbacks dispatch Compute work but form one ordered packet with the graphics prefix and subsequent
// deferred passes. Keep the physical primary-Graphics route while declaring the command capability they use.
[[nodiscard]] static Core::GpuQueueRequest GraphicsPreferredComputeQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// Hybrid shadow preparation's software tail dispatches its per-mesh BVH work and may emit small direct buffer
// writes while restoring the established hardware fallback. It still belongs in the accepting primary-Graphics
// packet, but needs both capabilities declared for debug recording to validate the real callback.
[[nodiscard]] static Core::GpuQueueRequest GraphicsComputeUploadQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Transfer)
            | static_cast<u8>(Core::GpuQueueCapability::Compute)
        ),
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

// A tiny setup dispatch can otherwise be rerouted to Graphics while its large Compute consumer selects the dedicated
// Compute transport. Use this only for work that must merge into that consumer's packet, so both requests select the
// same physical queue whenever a Compute transport is available.
[[nodiscard]] static Core::GpuQueueRequest ComputePacketQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Compute,
        true,
        false,
    };
}

// Native image clears require Transfer capability, while Surfel GI keeps its output initialization and compute work
// in one packet on the selected Compute transport. Lock the Compute preference so a tiny clear does not fall back to
// Graphics merely because it is too small to amortize a queue crossing; Graphics remains the explicit fallback when
// no Compute transport exists.
[[nodiscard]] static Core::GpuQueueRequest ComputeTransferQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Compute,
        true,
        false,
    };
}

// Adaptive software-shadow primitives are built-in Transfer operations, but their following/preceding traversal
// callback dispatches Compute work.  Require both capabilities and lock the Compute preference so the whole
// clear -> trace -> readback chain selects one physical packet on every supported topology.
[[nodiscard]] static Core::GpuQueueRequest ComputeTransferPacketQueueRequest(){
    return Core::GpuQueueRequest{
        static_cast<Core::GpuQueueCapability::Mask>(
            static_cast<u8>(Core::GpuQueueCapability::Compute)
            | static_cast<u8>(Core::GpuQueueCapability::Transfer)
        ),
        Core::GpuQueuePreference::Compute,
        true,
        false,
    };
}

// The lagged-history selector must share Deferred Lighting's dedicated Compute packet. Its built-in upload needs
// Transfer capability, which the Vulkan Compute transport also advertises, but may not be rerouted for its tiny cost.
[[nodiscard]] static Core::GpuQueueRequest ComputeUploadQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Compute,
        false,
        false,
    };
}

[[nodiscard]] static Core::GpuQueueRequest TransferQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Transfer,
        true,
        true,
    };
}

// The terminal graphics-prefix task publishes its ordinary and route-selected trace-geometry states before the
// following graph packets. All of those states are declared below, so this callback retains only timing ownership.
struct PostGbufferNormalizeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const Core::GpuTaskId* shadowVisibilityTask = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(
            context,
            payload.shadowVisibilityTask
        );
        if(
            !payload.raytracingSystem
            || !payload.timingTicket
            || !*payload.timingTicket
            || !shadowVisibilityQueue
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        // The graph's explicit uses below lower the ordinary G-buffer, scene, descriptor, and dynamically selected
        // trace-geometry states before this task records.
        if(
            shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute
            && payload.asyncPrefixTiming
            && *payload.asyncPrefixTiming
        ){
            (*payload.asyncPrefixTiming)->finishTiming(commandList);
            payload.asyncPrefixTiming->reset();
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.raytracingSystem)
            payload.raytracingSystem->confirmPreparedShadowTraceGeometryNormalization();
    }
};


struct AvboitPreGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentCsgIntervalsTiming = nullptr;
        bool hasTransparentRenderers = false;
        ECSRenderDetail::TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool transparentCsgStreamsUploaded = false;
        bool transparentCsgIntervalTargetsGraphOwned = false;
        bool transparentCsgIntervalPeelTargetStatesGraphOwned = false;
        bool transparentCsgReceiverSurfaceImageStatesGraphOwned = false;
        bool transparentCsgReceiverSpanOutputImageStatesGraphOwned = false;
        bool transparentCsgRemovedIntervalOutputImageStatesGraphOwned = false;
        bool deferTransparentCsgIntervalCombine = false;
        bool transparentCsgClipBufferStatesGraphOwned = false;
        bool transparentCsgMaterialFrameStatesGraphOwned = false;
        bool transparentCsgMaterialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : transparentCsgSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems transparentCsgReceiverSurfaceDrawItems{ scratchArena };
        CsgFrameGpuData transparentCsgFrameData{ scratchArena };
        const MaterialPassDrawItems* preparedTransparentCsgReceiverSurfaceDrawItems = nullptr;
        const CsgFrameGpuData* preparedTransparentCsgFrameData = nullptr;
        usize preparedTransparentCsgInstanceCount = 0u;
        usize preparedTransparentCsgMaterialTypedByteCount = 0u;
        if(payload.transparentCsgStreamsUploaded && payload.transparentCsgSnapshot.captured){
            payload.transparentCsgSnapshot.materialize(
                transparentCsgReceiverSurfaceDrawItems,
                transparentCsgFrameData
            );
            preparedTransparentCsgReceiverSurfaceDrawItems = &transparentCsgReceiverSurfaceDrawItems;
            preparedTransparentCsgFrameData = &transparentCsgFrameData;
            preparedTransparentCsgInstanceCount = payload.transparentCsgSnapshot.instanceCount;
            preparedTransparentCsgMaterialTypedByteCount = payload.transparentCsgSnapshot.materialTypedByteCount;
        }
        if(payload.hasTransparentRenderers){
            payload.avboitSystem->renderAvboitTransparentCsgIntervals(
                commandList,
                *payload.targets,
                *payload.csgFrameState,
                preparedTransparentCsgReceiverSurfaceDrawItems,
                preparedTransparentCsgFrameData,
                preparedTransparentCsgInstanceCount,
                preparedTransparentCsgMaterialTypedByteCount,
                payload.transparentCsgIntervalTargetsGraphOwned,
                payload.transparentCsgReceiverSurfaceImageStatesGraphOwned,
                payload.transparentCsgIntervalPeelTargetStatesGraphOwned,
                payload.transparentCsgReceiverSpanOutputImageStatesGraphOwned,
                payload.transparentCsgRemovedIntervalOutputImageStatesGraphOwned,
                payload.transparentCsgClipBufferStatesGraphOwned,
                payload.transparentCsgMaterialFrameStatesGraphOwned,
                payload.transparentCsgMaterialGeometryStatesGraphOwned,
                payload.deferTransparentCsgIntervalCombine,
                payload.transparentCsgIntervalsTiming
            );
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentCsgIntervalsTiming && payload.transparentCsgIntervalsTiming->has_value()){
            payload.transparentCsgIntervalsTiming->value().discardTiming();
            payload.transparentCsgIntervalsTiming->reset();
        }
    }
};


// Occupancy's alias-free compute-emulation stream is independently frozen after the phase's final target clear.
// The regular and CSG-only variants are mutually exclusive: their existing Occupancy callback remains the shared
// raster endpoint, while mixed or shared-output streams retain the established local bridge.
struct AvboitOccupancyComputeEmulationGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* occupancyTiming = nullptr;
        ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan plan;
        ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan csgPlan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool csgIntervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
            , csgPlan(arena)
        {}
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !payload.occupancyTiming
            || (!payload.plan.captured && !payload.csgPlan.captured)
            || payload.plan.captured == payload.csgPlan.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        const bool csgComputeEmulation = payload.csgPlan.captured;
        if(
            !(csgComputeEmulation
                ? payload.csgPlan.matches(renderer.meshSystem())
                : payload.plan.matches(renderer.meshSystem()))
            || !payload.materialDrawBuffersUploaded
            || !renderer.materialSystem().materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        )
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        if(csgComputeEmulation)
            payload.csgPlan.materialize(drawItems, csgFrameData);
        else
            payload.plan.materialize(drawItems);
        // The following raster task is graph-owned and cannot safely fall back to its local bridge. Reject a late
        // material/pipeline loss so the packet is discarded and the next frame re-preflights instead of accepting
        // Occupancy with stale generated vertices.
        if(
            !renderer.materialSystem().materialPassDrawResourcesReady(drawItems)
            || (csgComputeEmulation && (
                !payload.csgFrameBuffersUploaded
                || !payload.csgIntervalSampleImageStatesGraphOwned
                || !payload.csgClipBufferStatesGraphOwned
                || !csgFrameData.hasWork()
                || !renderer.csgSystem().csgFrameBuffersReady(csgFrameData)
            ))
        )
            return false;
        if(payload.occupancyTiming->has_value())
            return false;

        commandList.endRenderPass();
        payload.occupancyTiming->emplace(
            renderer.graphics().gpuTiming(),
            RendererGpuTimingScope::s_AvboitOccupancy,
            renderer.graphics().getDevice(),
            commandList
        );
        // Occupancy's raster half records after this producer in the same selected Graphics packet. Close the
        // marker before this command list completes; its consumer owns finishTiming/discard.
        payload.occupancyTiming->value().finishMarker();
        Core::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            nullptr,
            MaterialPipelinePass::AvboitOccupancy,
            &payload.targets->avboit,
            viewportState,
            false,
            csgComputeEmulation && payload.csgIntervalSampleImageStatesGraphOwned,
            csgComputeEmulation && payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.occupancyTiming); }
};


// Two, three, or four regular AVBOIT Occupancy draws sharing one generated-vertex buffer cannot batch their
// generators ahead of rasterization. Keep the original D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) ->
// R(D)] stream as explicit primary-Graphics callbacks so the compiler owns every alternating UAV/VertexBuffer
// boundary before the existing Depth-Warp successor.
struct AvboitOccupancySharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* occupancyTiming = nullptr;
        ECSRenderDetail::RegularSharedComputeEmulationGraphPlan plan;
        usize drawIndex = 0u;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool beginTiming = false;
        bool finishTiming = false;
        Phase phase = Phase::Generate;
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.targets->avboit.lowFramebuffer
            || !payload.timingTicket
            || !payload.occupancyTiming
            || !payload.plan.captured
            || payload.drawIndex >= payload.plan.drawCount
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(
            !payload.plan.matches(renderer.meshSystem(), payload.drawIndex)
            || !payload.materialDrawBuffersUploaded
            || !renderer.materialSystem().materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        )
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        payload.plan.materialize(payload.drawIndex, drawItems);
        if(!renderer.materialSystem().materialPassDrawResourcesReady(drawItems))
            return false;

        if(payload.phase == Phase::Generate){
            // A preceding raster phase leaves dynamic rendering active. End it before the timing marker and
            // compute-state bind, matching the retained local D/R interleaving order.
            commandList.endRenderPass();
            if(payload.beginTiming){
                if(payload.occupancyTiming->has_value())
                    return false;
                payload.occupancyTiming->emplace(
                    renderer.graphics().gpuTiming(),
                    RendererGpuTimingScope::s_AvboitOccupancy,
                    renderer.graphics().getDevice(),
                    commandList
                );
                // The range spans serial callbacks, but this opening command list still needs its marker closed
                // before recording advances to the raster consumer.
                payload.occupancyTiming->value().finishMarker();
            }
            else if(!payload.occupancyTiming->has_value())
                return false;
        }
        else if(!payload.occupancyTiming->has_value())
            return false;

        Core::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.phase == Phase::Raster ? payload.targets->avboit.lowFramebuffer.get() : nullptr,
            MaterialPipelinePass::AvboitOccupancy,
            &payload.targets->avboit,
            viewportState,
            false,
            false,
            false,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        if(payload.phase == Phase::Generate){
            renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        }
        else{
            renderer.materialSystem().renderComputeMaterialPassDrawItemsRasterOnly(
                drawContext,
                drawItems.computeDrawItems
            );
            if(payload.finishTiming){
                payload.occupancyTiming->value().finishTiming(commandList);
                payload.occupancyTiming->reset();
            }
            // The next generator must never bind a compute pipeline while dynamic rendering remains active.
            commandList.endRenderPass();
        }
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.occupancyTiming); }
};


// Occupancy follows the interval producer in the same AVBOIT packet, but has an independent immutable stream:
// each transparent raster phase overwrites the shared material and CSG buffers with phase-local instance indices.
struct AvboitOccupancyGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool hasTransparentRenderers = false;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot occupancySnapshot;
        bool occupancyPhasePrepared = false;
        bool occupancyStreamsUploaded = false;
        bool occupancyCsgIntervalSampleImageStatesGraphOwned = false;
        bool occupancyCsgClipBufferStatesGraphOwned = false;
        bool occupancyMaterialFrameStatesGraphOwned = false;
        bool occupancyMaterialGeometryStatesGraphOwned = false;
        bool occupancyComputeEmulationOutputStatesGraphOwned = false;
        bool occupancyCsgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : occupancySnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.avboitSystem
            || !payload.targets
            || !payload.csgFrameState
            || !payload.timingTicket
            || ((payload.occupancyComputeEmulationOutputStatesGraphOwned
                    || payload.occupancyCsgComputeEmulationOutputStatesGraphOwned)
                && !payload.occupancyComputeEmulationTiming)
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItemPartitions occupancyDrawItems{ scratchArena };
        CsgFrameGpuData occupancyCsgFrameData{ scratchArena };
        const MaterialPassDrawItemPartitions* preparedOccupancyDrawItems = nullptr;
        const CsgFrameGpuData* preparedOccupancyCsgFrameData = nullptr;
        usize preparedOccupancyInstanceCount = 0u;
        usize preparedOccupancyMaterialTypedByteCount = 0u;
        if(payload.hasTransparentRenderers && (!payload.occupancyPhasePrepared || !payload.occupancySnapshot.captured))
            return false;
        if(payload.occupancyPhasePrepared && payload.occupancySnapshot.captured){
            payload.occupancySnapshot.materialize(occupancyDrawItems, occupancyCsgFrameData);
            preparedOccupancyDrawItems = &occupancyDrawItems;
            preparedOccupancyCsgFrameData = &occupancyCsgFrameData;
            preparedOccupancyInstanceCount = payload.occupancySnapshot.instanceCount;
            preparedOccupancyMaterialTypedByteCount = payload.occupancySnapshot.materialTypedByteCount;
        }
        if(payload.hasTransparentRenderers){
            payload.avboitSystem->renderAvboitOccupancyPass(
                commandList,
                *payload.targets,
                *payload.csgFrameState,
                preparedOccupancyDrawItems,
                preparedOccupancyCsgFrameData,
                preparedOccupancyInstanceCount,
                preparedOccupancyMaterialTypedByteCount,
                // The task's declared depth/coverage uses have already lowered and committed their graph barrier.
                true,
                payload.occupancyCsgIntervalSampleImageStatesGraphOwned,
                payload.occupancyCsgClipBufferStatesGraphOwned,
                payload.occupancyMaterialFrameStatesGraphOwned,
                payload.occupancyMaterialGeometryStatesGraphOwned,
                payload.occupancyComputeEmulationOutputStatesGraphOwned,
                payload.occupancyComputeEmulationTiming,
                payload.occupancyCsgComputeEmulationOutputStatesGraphOwned
            );
        }
        // The declared sampled G-buffer uses remain authoritative here. Occupancy's low-resolution framebuffer
        // does not attach any deferred target, so the graph-established states remain valid for either continuation.
        return true;
    }

    static void discarded(Payload& payload){
        if(
            !payload.occupancyComputeEmulationTiming
            || !payload.occupancyComputeEmulationTiming->has_value()
        )
            return;
        payload.occupancyComputeEmulationTiming->value().discardTiming();
        payload.occupancyComputeEmulationTiming->reset();
    }
};


struct AvboitDepthWarpGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->dispatchAvboitDepthWarp(commandList, *payload.targets);
        return true;
    }
};


// Extinction's alias-free compute-emulation stream is independently frozen after the prior AVBOIT phase uploads.
// The regular and CSG-only variants are mutually exclusive: the existing Extinction callback remains the shared
// raster endpoint, while mixed or shared-output streams retain the established local bridge.
struct AvboitExtinctionComputeEmulationGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* extinctionTiming = nullptr;
        ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan plan;
        ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan csgPlan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool csgIntervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
            , csgPlan(arena)
        {}
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !payload.extinctionTiming
            || (!payload.plan.captured && !payload.csgPlan.captured)
            || payload.plan.captured == payload.csgPlan.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        const bool csgComputeEmulation = payload.csgPlan.captured;
        if(
            !(csgComputeEmulation
                ? payload.csgPlan.matches(renderer.meshSystem())
                : payload.plan.matches(renderer.meshSystem()))
            || !payload.materialDrawBuffersUploaded
            || !renderer.materialSystem().materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        )
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        if(csgComputeEmulation)
            payload.csgPlan.materialize(drawItems, csgFrameData);
        else
            payload.plan.materialize(drawItems);
        // The following raster task is deliberately graph-owned and cannot safely fall back to its local bridge.
        // Reject a late material/pipeline loss so the packet is discarded and the next frame re-preflights instead
        // of accepting an all-or-nothing Extinction phase with stale generated vertices.
        if(
            !renderer.materialSystem().materialPassDrawResourcesReady(drawItems)
            || (csgComputeEmulation && (
                !payload.csgFrameBuffersUploaded
                || !payload.csgIntervalSampleImageStatesGraphOwned
                || !payload.csgClipBufferStatesGraphOwned
                || !csgFrameData.hasWork()
                || !renderer.csgSystem().csgFrameBuffersReady(csgFrameData)
            ))
        )
            return false;
        if(payload.extinctionTiming->has_value())
            return false;

        commandList.endRenderPass();
        payload.extinctionTiming->emplace(
            renderer.graphics().gpuTiming(),
            RendererGpuTimingScope::s_AvboitExtinction,
            renderer.graphics().getDevice(),
            commandList
        );
        // Extinction's raster half records after this producer in the same selected Graphics packet. Close the
        // marker before this command list completes; its consumer owns finishTiming/discard.
        payload.extinctionTiming->value().finishMarker();
        Core::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            nullptr,
            MaterialPipelinePass::AvboitExtinction,
            &payload.targets->avboit,
            viewportState,
            false,
            csgComputeEmulation && payload.csgIntervalSampleImageStatesGraphOwned,
            csgComputeEmulation && payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.extinctionTiming); }
};


// Two, three, or four regular Extinction draws targeting one persistent generated-vertex buffer must retain their
// native D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) -> R(D)] order. Each phase is graph-visible so the
// compiler lowers the alternating UAV/VertexBuffer states before the common typed Integration tail consumes the
// packed outputs.
struct AvboitExtinctionSharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* extinctionTiming = nullptr;
        ECSRenderDetail::RegularSharedComputeEmulationGraphPlan plan;
        usize drawIndex = 0u;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool beginTiming = false;
        bool finishTiming = false;
        Phase phase = Phase::Generate;
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.targets->avboit.lowFramebuffer
            || !payload.timingTicket
            || !payload.extinctionTiming
            || !payload.plan.captured
            || payload.drawIndex >= payload.plan.drawCount
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(
            !payload.plan.matches(renderer.meshSystem(), payload.drawIndex)
            || !payload.materialDrawBuffersUploaded
            || !renderer.materialSystem().materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        )
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        payload.plan.materialize(payload.drawIndex, drawItems);
        if(!renderer.materialSystem().materialPassDrawResourcesReady(drawItems))
            return false;

        if(payload.phase == Phase::Generate){
            // Raster A leaves dynamic rendering active.  End it before the next compute generator, retaining the
            // original per-item material order and allowing the terminal typed Integration successor to record.
            commandList.endRenderPass();
            if(payload.beginTiming){
                if(payload.extinctionTiming->has_value())
                    return false;
                payload.extinctionTiming->emplace(
                    renderer.graphics().gpuTiming(),
                    RendererGpuTimingScope::s_AvboitExtinction,
                    renderer.graphics().getDevice(),
                    commandList
                );
                payload.extinctionTiming->value().finishMarker();
            }
            else if(!payload.extinctionTiming->has_value())
                return false;
        }
        else if(!payload.extinctionTiming->has_value())
            return false;

        Core::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            payload.targets->avboit.lowFramebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.phase == Phase::Raster ? payload.targets->avboit.lowFramebuffer.get() : nullptr,
            MaterialPipelinePass::AvboitExtinction,
            &payload.targets->avboit,
            viewportState,
            false,
            false,
            false,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        if(payload.phase == Phase::Generate){
            renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        }
        else{
            renderer.materialSystem().renderComputeMaterialPassDrawItemsRasterOnly(
                drawContext,
                drawItems.computeDrawItems
            );
            if(payload.finishTiming){
                payload.extinctionTiming->value().finishTiming(commandList);
                payload.extinctionTiming->reset();
            }
            // Integration binds a compute pipeline in the next graph callback.
            commandList.endRenderPass();
        }
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.extinctionTiming); }
};


struct AvboitExtinctionGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot extinctionSnapshot;
        bool extinctionPhasePrepared = false;
        bool extinctionCsgIntervalSampleImageStatesGraphOwned = false;
        bool extinctionCsgClipBufferStatesGraphOwned = false;
        bool extinctionMaterialFrameStatesGraphOwned = false;
        bool extinctionMaterialGeometryStatesGraphOwned = false;
        bool extinctionComputeEmulationOutputStatesGraphOwned = false;
        bool extinctionCsgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr;
        bool hasTransparentRenderers = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : extinctionSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.avboitSystem
            || !payload.targets
            || !payload.csgFrameState
            || !payload.timingTicket
            || ((payload.extinctionComputeEmulationOutputStatesGraphOwned
                    || payload.extinctionCsgComputeEmulationOutputStatesGraphOwned)
                && !payload.extinctionComputeEmulationTiming)
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItemPartitions extinctionDrawItems{ scratchArena };
        CsgFrameGpuData extinctionCsgFrameData{ scratchArena };
        const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr;
        const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr;
        usize preparedExtinctionInstanceCount = 0u;
        usize preparedExtinctionMaterialTypedByteCount = 0u;
        if(payload.hasTransparentRenderers && (!payload.extinctionPhasePrepared || !payload.extinctionSnapshot.captured))
            return false;
        if(payload.extinctionPhasePrepared && payload.extinctionSnapshot.captured){
            payload.extinctionSnapshot.materialize(extinctionDrawItems, extinctionCsgFrameData);
            preparedExtinctionDrawItems = &extinctionDrawItems;
            preparedExtinctionCsgFrameData = &extinctionCsgFrameData;
            preparedExtinctionInstanceCount = payload.extinctionSnapshot.instanceCount;
            preparedExtinctionMaterialTypedByteCount = payload.extinctionSnapshot.materialTypedByteCount;
        }
        if(payload.hasTransparentRenderers){
            payload.avboitSystem->renderAvboitExtinctionPass(
                commandList,
                payload.targets->avboit,
                *payload.csgFrameState,
                preparedExtinctionDrawItems,
                preparedExtinctionCsgFrameData,
                preparedExtinctionInstanceCount,
                preparedExtinctionMaterialTypedByteCount,
                payload.extinctionCsgIntervalSampleImageStatesGraphOwned,
                payload.extinctionCsgClipBufferStatesGraphOwned,
                payload.extinctionMaterialFrameStatesGraphOwned,
                payload.extinctionMaterialGeometryStatesGraphOwned,
                payload.extinctionComputeEmulationOutputStatesGraphOwned,
                payload.extinctionComputeEmulationTiming,
                payload.extinctionCsgComputeEmulationOutputStatesGraphOwned
            );
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(
            !payload.extinctionComputeEmulationTiming
            || !payload.extinctionComputeEmulationTiming->has_value()
        )
            return;
        payload.extinctionComputeEmulationTiming->value().discardTiming();
        payload.extinctionComputeEmulationTiming->reset();
    }
};


struct AvboitIntegrationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->dispatchAvboitIntegration(commandList, *payload.targets);
        return true;
    }
};


// Accumulation's alias-free compute-emulation stream is independently frozen after Integration and its immutable
// upload chain. The regular and CSG-only variants are mutually exclusive: the existing Accumulation callback
// remains the shared raster endpoint and its following finalizer retains the terminal attachment handoff.
struct AvboitAccumulationComputeEmulationGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* accumulationTiming = nullptr;
        ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan plan;
        ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan csgPlan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool csgIntervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
            , csgPlan(arena)
        {}
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.timingTicket
            || !payload.accumulationTiming
            || (!payload.plan.captured && !payload.csgPlan.captured)
            || payload.plan.captured == payload.csgPlan.captured
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        const bool csgComputeEmulation = payload.csgPlan.captured;
        if(
            !(csgComputeEmulation
                ? payload.csgPlan.matches(renderer.meshSystem())
                : payload.plan.matches(renderer.meshSystem()))
            || !payload.materialDrawBuffersUploaded
            || !renderer.materialSystem().materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        )
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
        if(csgComputeEmulation)
            payload.csgPlan.materialize(drawItems, csgFrameData);
        else
            payload.plan.materialize(drawItems);
        // The following raster task is deliberately graph-owned and cannot safely fall back to its local bridge.
        // Reject a late material/pipeline loss so the packet is discarded and the next frame re-preflights instead
        // of accepting an all-or-nothing Accumulation phase with stale generated vertices.
        if(
            !renderer.materialSystem().materialPassDrawResourcesReady(drawItems)
            || (csgComputeEmulation && (
                !payload.csgFrameBuffersUploaded
                || !payload.csgIntervalSampleImageStatesGraphOwned
                || !payload.csgClipBufferStatesGraphOwned
                || !csgFrameData.hasWork()
                || !renderer.csgSystem().csgFrameBuffersReady(csgFrameData)
            ))
        )
            return false;
        if(payload.accumulationTiming->has_value())
            return false;

        commandList.endRenderPass();
        payload.accumulationTiming->emplace(
            renderer.graphics().gpuTiming(),
            RendererGpuTimingScope::s_AvboitAccumulate,
            renderer.graphics().getDevice(),
            commandList
        );
        // Accumulation's raster half records after this producer in the selected terminal Graphics packet. Close
        // the opening command-list marker now; its consumer owns finishTiming/discard.
        payload.accumulationTiming->value().finishMarker();
        Core::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            payload.targets->avboit.accumulationFramebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            nullptr,
            MaterialPipelinePass::AvboitAccumulate,
            &payload.targets->avboit,
            viewportState,
            false,
            csgComputeEmulation && payload.csgIntervalSampleImageStatesGraphOwned,
            csgComputeEmulation && payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.accumulationTiming); }
};


// Two or three regular AVBOIT Accumulation draws sharing one generated-vertex buffer cannot batch their generators
// ahead of rasterization. Keep the original D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C)] stream as explicit
// primary-Graphics callbacks so the compiler owns every alternating UAV/VertexBuffer boundary.
struct AvboitAccumulationSharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* accumulationTiming = nullptr;
        ECSRenderDetail::RegularSharedComputeEmulationGraphPlan plan;
        usize drawIndex = 0u;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool beginTiming = false;
        bool finishTiming = false;
        Phase phase = Phase::Generate;
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing){
        if(!timing || !timing->has_value())
            return;
        timing->value().discardTiming();
        timing->reset();
    }

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.targets->avboit.accumulationFramebuffer
            || !payload.timingTicket
            || !payload.accumulationTiming
            || !payload.plan.captured
            || payload.drawIndex >= payload.plan.drawCount
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(
            !payload.plan.matches(renderer.meshSystem(), payload.drawIndex)
            || !payload.materialDrawBuffersUploaded
            || !renderer.materialSystem().materialPassDrawBuffersReady(
                payload.instanceCount,
                payload.materialTypedByteCount
            )
        )
            return false;

        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItems drawItems{ scratchArena };
        payload.plan.materialize(payload.drawIndex, drawItems);
        if(!renderer.materialSystem().materialPassDrawResourcesReady(drawItems))
            return false;

        if(payload.phase == Phase::Generate){
            // A preceding raster phase leaves dynamic rendering active. End it before the timing marker and
            // compute-state bind, matching the retained local D/R interleaving order.
            commandList.endRenderPass();
            if(payload.beginTiming){
                if(payload.accumulationTiming->has_value())
                    return false;
                payload.accumulationTiming->emplace(
                    renderer.graphics().gpuTiming(),
                    RendererGpuTimingScope::s_AvboitAccumulate,
                    renderer.graphics().getDevice(),
                    commandList
                );
                // The range spans serial callbacks, but this opening command list still needs its marker closed
                // before recording advances to the raster consumer.
                payload.accumulationTiming->value().finishMarker();
            }
            else if(!payload.accumulationTiming->has_value())
                return false;
        }
        else if(!payload.accumulationTiming->has_value())
            return false;

        Core::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(
            payload.targets->avboit.accumulationFramebuffer->getFramebufferInfo().getViewport()
        );
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.phase == Phase::Raster ? payload.targets->avboit.accumulationFramebuffer.get() : nullptr,
            MaterialPipelinePass::AvboitAccumulate,
            &payload.targets->avboit,
            viewportState,
            false,
            false,
            false,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            true,
        };
        if(payload.phase == Phase::Generate){
            renderer.materialSystem().generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
        }
        else{
            renderer.materialSystem().renderComputeMaterialPassDrawItemsRasterOnly(
                drawContext,
                drawItems.computeDrawItems
            );
            if(payload.finishTiming){
                payload.accumulationTiming->value().finishTiming(commandList);
                payload.accumulationTiming->reset();
            }
            // The next generator must never bind a compute pipeline while dynamic rendering remains active.
            commandList.endRenderPass();
        }
        return true;
    }

    static void discarded(Payload& payload){ discardTiming(payload.accumulationTiming); }
};


struct AvboitAccumulationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot accumulationSnapshot;
        bool accumulationPhasePrepared = false;
        bool accumulationCsgIntervalSampleImageStatesGraphOwned = false;
        bool accumulationCsgClipBufferStatesGraphOwned = false;
        bool accumulationMaterialFrameStatesGraphOwned = false;
        bool accumulationMaterialGeometryStatesGraphOwned = false;
        bool accumulationComputeEmulationOutputStatesGraphOwned = false;
        bool accumulationCsgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr;
        bool hasTransparentRenderers = false;
        bool splitStages = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : accumulationSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.avboitSystem
            || !payload.targets
            || !payload.csgFrameState
            || !payload.timingTicket
            || ((payload.accumulationComputeEmulationOutputStatesGraphOwned
                    || payload.accumulationCsgComputeEmulationOutputStatesGraphOwned)
                && !payload.accumulationComputeEmulationTiming)
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItemPartitions accumulationDrawItems{ scratchArena };
        CsgFrameGpuData accumulationCsgFrameData{ scratchArena };
        const MaterialPassDrawItemPartitions* preparedAccumulationDrawItems = nullptr;
        const CsgFrameGpuData* preparedAccumulationCsgFrameData = nullptr;
        usize preparedAccumulationInstanceCount = 0u;
        usize preparedAccumulationMaterialTypedByteCount = 0u;
        if(payload.hasTransparentRenderers && (!payload.accumulationPhasePrepared || !payload.accumulationSnapshot.captured))
            return false;
        if(payload.accumulationPhasePrepared && payload.accumulationSnapshot.captured){
            payload.accumulationSnapshot.materialize(accumulationDrawItems, accumulationCsgFrameData);
            preparedAccumulationDrawItems = &accumulationDrawItems;
            preparedAccumulationCsgFrameData = &accumulationCsgFrameData;
            preparedAccumulationInstanceCount = payload.accumulationSnapshot.instanceCount;
            preparedAccumulationMaterialTypedByteCount = payload.accumulationSnapshot.materialTypedByteCount;
        }
        if(payload.hasTransparentRenderers){
            payload.avboitSystem->renderAvboitAccumulatePass(
                commandList,
                *payload.targets,
                *payload.csgFrameState,
                preparedAccumulationDrawItems,
                preparedAccumulationCsgFrameData,
                preparedAccumulationInstanceCount,
                preparedAccumulationMaterialTypedByteCount,
                // The following mergeable Graphics finalizer owns every accumulation-framebuffer handoff.
                true,
                payload.accumulationCsgIntervalSampleImageStatesGraphOwned,
                payload.accumulationCsgClipBufferStatesGraphOwned,
                payload.accumulationMaterialFrameStatesGraphOwned,
                payload.accumulationMaterialGeometryStatesGraphOwned,
                payload.accumulationComputeEmulationOutputStatesGraphOwned,
                payload.accumulationComputeEmulationTiming,
                payload.accumulationCsgComputeEmulationOutputStatesGraphOwned
            );
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(
            !payload.accumulationComputeEmulationTiming
            || !payload.accumulationComputeEmulationTiming->has_value()
        )
            return;
        payload.accumulationComputeEmulationTiming->value().discardTiming();
        payload.accumulationComputeEmulationTiming->reset();
    }
};


// Accumulation produces attachments that Deferred Composite samples on Compute and leaves the read-only deferred
// depth attachment in DepthRead. Keep all ShaderResource handoffs in a Graphics task immediately after
// rasterization, so no following packet has to name a framebuffer attachment source state. The task intentionally
// records no native work; packet-prologue barriers are the entire contract.
struct AvboitAccumulationFinalizeGraphTask{
    struct Payload{};

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }

};


struct DeferredPresentGraphTask{
    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::Framebuffer* presentationFramebuffer = nullptr;
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncFinalTiming = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const Core::GpuTaskId* shadowVisibilityTask = nullptr;
        bool currentBindlessSlotsGraphOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(
            context,
            payload.shadowVisibilityTask
        );
        const bool shadowVisibilityRunsOnCompute = shadowVisibilityQueue
            && shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
        if(
            !payload.deferredSystem
            || !payload.graphics
            || !payload.targets
            || !payload.presentationFramebuffer
            || !payload.frameTimingTransaction
            || !payload.timingTicket
            || !shadowVisibilityQueue
            || (shadowVisibilityRunsOnCompute && !payload.asyncFinalTiming)
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(shadowVisibilityRunsOnCompute){
            payload.asyncFinalTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncFinal,
                payload.graphics->getDevice(),
                commandList
            );
            payload.asyncFinalTiming->value().finishMarker();
        }

        const bool presentRecorded = payload.deferredSystem->renderDeferredPresent(
            commandList,
            *payload.targets,
            payload.presentationFramebuffer,
            payload.currentBindlessSlotsGraphOwned
        );
        const bool frameTimingEnded = presentRecorded
            && payload.frameTimingTransaction->recordEnd(commandList)
        ;
        if(shadowVisibilityRunsOnCompute && presentRecorded && payload.asyncFinalTiming->has_value()){
            payload.asyncFinalTiming->value().finishTiming(commandList);
            payload.asyncFinalTiming->reset();
        }
        return presentRecorded && frameTimingEnded;
    }
};


[[nodiscard]] static Core::GpuGraphResourceDesc TextureResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Texture)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuGraphResourceDesc BufferResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Buffer)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuGraphResourceDesc HazardDomainDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::HazardDomain)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = hasIndependentStateSource,
    };
}

// Material pipelines select mesh source buffers through global heap slots, so the graph cannot infer their physical
// inputs from a pipeline object. Freeze and deduplicate the exact buffers selected by the gathered draw items while
// declaring each packet. A source that Shadow Preparation already imported reuses that graph identity instead of
// attempting an incompatible second import with a material-specific name.
[[nodiscard]] static bool GatherPreparedMaterialGeometryUses(
    RendererMeshSystem& meshSystem,
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& outResourceUses
){
    outResourceUses.clear();
    if(drawItemSetCount != 0u && !drawItemSets)
        return false;

    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> sourceBuffers{ scratchArena };
    const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
        MeshResources* mesh = nullptr;
        if(!meshSystem.findMeshResources(drawItem.meshKey, mesh) || !mesh)
            return false;

        bool buffersReady = true;
        RendererMeshSystem::forEachMeshSourceBuffer(*mesh, [&](const u32, const Core::BufferHandle& buffer, const bool){
            if(!buffersReady)
                return;
            if(!buffer){
                buffersReady = false;
                return;
            }
            for(const Core::BufferHandle& existing : sourceBuffers){
                if(existing.get() == buffer.get())
                    return;
            }
            sourceBuffers.push_back(buffer);
        });
        return buffersReady;
    };
    for(usize drawItemSetIndex = 0u; drawItemSetIndex < drawItemSetCount; ++drawItemSetIndex){
        const MaterialPassDrawItems* const drawItems = drawItemSets[drawItemSetIndex];
        if(!drawItems)
            return false;
        for(const MaterialPassDrawItem& drawItem : drawItems->meshDrawItems){
            if(!appendDrawItem(drawItem))
                return false;
        }
        for(const MaterialPassDrawItem& drawItem : drawItems->computeDrawItems){
            if(!appendDrawItem(drawItem))
                return false;
        }
    }

    outResourceUses.reserve(sourceBuffers.size());
    for(const Core::BufferHandle& buffer : sourceBuffers){
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name identity = buffer->getDescription().debugName;
            if(!identity){
                outResourceUses.clear();
                return false;
            }
            resource = graph.importBuffer(buffer, BufferResourceDesc(identity, "Prepared Material Geometry"));
        }
        if(!resource.valid()){
            outResourceUses.clear();
            return false;
        }
        outResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    return true;
}

// Material geometry is dynamically enumerable from the frozen draw snapshot. Keep collection/import compatibility in
// the established helper above, then give the graph one immutable named collection so a consuming task can declare
// the whole bindless geometry set without retaining its own per-buffer use list.
[[nodiscard]] static bool GatherPreparedMaterialGeometryResourceSet(
    RendererMeshSystem& meshSystem,
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    if(!GatherPreparedMaterialGeometryUses(
        meshSystem,
        graph,
        drawItemSets,
        drawItemSetCount,
        scratchArena,
        resourceUses
    ))
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(resourceUses.size());
    for(const Core::GpuTaskResourceUse& use : resourceUses)
        members.push_back(use.resource);

    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

// The opaque regular compute-emulation producer owns one frozen, pairwise-distinct generated-vertex buffer per
// draw. Reuse an existing typed import when another preparation phase already retained that buffer; a second
// identity for the same native resource would otherwise make the later UAV-to-VertexBuffer handoff ambiguous.
[[nodiscard]] static bool GatherOpaqueRegularComputeEmulationResourceSet(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::OpaqueRegularComputeEmulationGraphPlan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    if(!plan.captured || plan.outputBuffers.empty())
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getDescription().debugName;
            if(!bufferIdentity)
                return false;
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

// Alias-free AVBOIT compute-emulation plans retain descriptor slots because the compute material path selects each
// writable output through the global heap.
[[nodiscard]] static bool GatherAvboitAliasFreeComputeEmulationResourceSet(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    if(!plan.captured || plan.outputBuffers.empty())
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getDescription().debugName;
            if(!bufferIdentity)
                return false;
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

// A small shared-output sequence imports its one retained generated-vertex buffer exactly once. Repeating it in a
// resource set would collapse the alternating uses, so each phase declares this concrete graph resource directly.
[[nodiscard]] static bool GatherRegularSharedComputeEmulationResource(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan,
    const AStringView label,
    Core::GpuGraphResourceId& outResource
){
    outResource = {};
    if(!plan.captured || !plan.outputBuffer)
        return false;

    outResource = graph.findImportedBuffer(plan.outputBuffer);
    if(!outResource.valid()){
        const Name identity = plan.outputBuffer->getDescription().debugName;
        if(!identity)
            return false;
        outResource = graph.importBuffer(plan.outputBuffer, BufferResourceDesc(identity, label));
    }
    return outResource.valid();
}

// Receiver-surface output handles have the same immutable-import contract as regular opaque emulation, but form a
// distinct graph set so the CSG consumer can remain independently optional when its frame resources are unavailable.
[[nodiscard]] static bool GatherOpaqueCsgReceiverComputeEmulationResourceSet(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphPlan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    if(!plan.captured || plan.outputBuffers.empty())
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getDescription().debugName;
            if(!bufferIdentity)
                return false;
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

// CSG compute-emulation outputs are separately optional from receiver-surface outputs, but use the same immutable
// import rule: retain the exact generated-vertex handles selected during declaration so record-time replacement
// rejects the packet instead of silently writing an untracked descriptor target.
[[nodiscard]] static bool GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    if(!plan.captured || plan.outputBuffers.empty())
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getDescription().debugName;
            if(!bufferIdentity)
                return false;
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

// Material constants select persistent sampled textures through global heap slots. The prepared draw stream is the
// only authoritative CPU-side enumeration at declaration time, so retain its exact resolved texture handles in an
// immutable graph set just as we do mesh geometry. Reuse an earlier typed import when a preflight producer already
// chose its identity; otherwise use the asset texture's stable name and one common material-texture label.
[[nodiscard]] static bool GatherPreparedMaterialSampledTextureResourceSet(
    RendererMaterialSystem& materialSystem,
    Core::GpuTaskGraph& graph,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena> sampledTextures{ scratchArena };
    if(!materialSystem.gatherPreparedMaterialPassSampledTextures(
        drawItemSets,
        drawItemSetCount,
        sampledTextures
    ))
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(sampledTextures.size());
    for(const Core::TextureHandle& texture : sampledTextures){
        Core::GpuGraphResourceId resource = graph.findImportedTexture(texture);
        if(!resource.valid()){
            const Name textureIdentity = texture->getDescription().name;
            if(!textureIdentity)
                return false;
            resource = graph.importTexture(
                texture,
                TextureResourceDesc(textureIdentity, "Prepared Material Sampled Texture")
            );
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    if(members.empty())
        return true;

    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
){
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] static Core::GpuTaskResourceUse WriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Write,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse WriteTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state
){
    Core::GpuTaskResourceUse result = WriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] static Core::GpuGraphResourceDesc AccelStructResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::AccelStruct)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadWriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadWriteTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state
){
    Core::GpuTaskResourceUse result = ReadWriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::declareDeferredShadowPrepareTask(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    const Core::GpuGraphResourceId* const softwareBvhBuildStateResources,
    const usize softwareBvhBuildStateResourceCount,
    const bool softwareTraceResourcesPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareHybridSoftwareTailTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
    m_causticEmissionTargetsUploadTask = {};
    m_surfelFrameConstantsUploadTask = {};
    m_shadowInstanceMaterialUploadTask = {};
    m_shadowInstanceUploadTask = {};
    m_shadowMaterialTypedUploadTask = {};
    m_sceneBvhNodesUploadTask = {};
    m_sceneBvhInstancesUploadTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_raytracingSystem.shadowVisibilityResourcesPreflighted()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
        || (shadowTraceGeometryResourceCount != 0u && !shadowTraceGeometryResources)
        || (softwareBvhBuildStateResourceCount != 0u && !softwareBvhBuildStateResources)
    )
        return false;

    const bool currentBindlessSlotsGraphOwned = !deferredTargets.bindless.slotsUploaded;
    if(currentBindlessSlotsGraphOwned){
        const Core::GpuUploadBlobId bindlessSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
            &deferredTargets.bindless.slots,
            sizeof(deferredTargets.bindless.slots),
            alignof(DeferredBindlessResourceSlots)
        );
        if(!bindlessSlotsBlob.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain deferred bindless selector upload data"));
            return false;
        }

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name("render.deferred.bindless_slots_upload"))
            .setMarkerLabel("Deferred Bindless Slots Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(uploadScheduling)
        ;
        m_deferredBindlessSlotsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = bindlessSlotsBlob,
                .destination = currentBindlessSlots,
                // This selector persists its descriptor-visible state across packet closes. The built-in upload
                // performs its intrinsic CopyDest transition, then publishes ConstantBuffer to Shadow Preparation.
                .finalState = Core::ResourceStates::ConstantBuffer,
            }
        );
        if(!m_deferredBindlessSlotsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred bindless selector upload"));
            return false;
        }
    }

    // All trace backing buffers and their heap entries are finalized by preflight. Retain the resolved descriptor
    // slots now, before the graph is compiled, so recording never rereads mutable heap handles.
    RayTraceMaterialContextSlots rayTraceMaterialContextSlots;
    if(!m_raytracingSystem.snapshotRayTraceMaterialContextSlots(rayTraceMaterialContextSlots)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot ray-trace material-context selector"));
        return false;
    }
    const Core::GpuUploadBlobId rayTraceMaterialContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
        &rayTraceMaterialContextSlots,
        sizeof(rayTraceMaterialContextSlots),
        alignof(RayTraceMaterialContextSlots)
    );
    if(!rayTraceMaterialContextSlotsBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain ray-trace material-context selector upload data"));
        return false;
    }

    const Core::GpuTaskId* const materialContextUploadDependencies = currentBindlessSlotsGraphOwned
        ? &m_deferredBindlessSlotsUploadTask
        : nullptr
    ;
    const usize materialContextUploadDependencyCount = currentBindlessSlotsGraphOwned ? 1u : 0u;
    Core::GpuTaskSchedulingHint materialContextUploadScheduling;
    materialContextUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    materialContextUploadScheduling.forceSubmissionBoundary = false;
    materialContextUploadScheduling.allowPacketMerge = true;
    materialContextUploadScheduling.mergeWithPrevious = currentBindlessSlotsGraphOwned;
    Core::GpuTaskDesc materialContextUploadDesc;
    materialContextUploadDesc
        .setIdentity(Name("render.raytrace.material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(materialContextUploadScheduling)
        .setDependencies(materialContextUploadDependencies, materialContextUploadDependencyCount)
    ;
    m_rayTraceMaterialContextSlotsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
        materialContextUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = rayTraceMaterialContextSlotsBlob,
            .destination = materialContextSlots,
            // Automatic-state selector buffers publish Common. Shadow Preparation owns the following
            // ConstantBuffer transition and becomes the cross-queue producer for later trace consumers.
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!m_rayTraceMaterialContextSlotsUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare ray-trace material-context selector upload"));
        return false;
    }

    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Name causticEmissionTargetsIdentity = graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
            ? Name("render.hardware_caustics.emission_targets")
            : Name("render.software_caustics.emission_targets")
    ;
    const Core::GpuGraphResourceId causticEmissionTargets = m_rayTracingState.m_causticEmissionTargetBuffer
        ? importBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            causticEmissionTargetsIdentity,
            "Caustic Emission Targets"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(m_rayTracingState.m_causticEmissionTargetBuffer && !causticEmissionTargets.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted caustic emission targets"));
        return false;
    }

    Core::GpuUploadBlobId causticEmissionTargetsBlob;
    if(!m_raytracingSystem.retainPreparedCausticEmissionTargetUpload(
        m_deferredLightingTaskGraph,
        causticEmissionTargetsBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted caustic emission-target upload data"));
        return false;
    }

    Core::GpuTaskId shadowPrepareDependency = m_rayTraceMaterialContextSlotsUploadTask;
    if(causticEmissionTargetsBlob.valid()){
        if(!causticEmissionTargets.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target upload has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint causticEmissionTargetsUploadScheduling;
        causticEmissionTargetsUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        causticEmissionTargetsUploadScheduling.forceSubmissionBoundary = false;
        causticEmissionTargetsUploadScheduling.allowPacketMerge = true;
        causticEmissionTargetsUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc causticEmissionTargetsUploadDesc;
        causticEmissionTargetsUploadDesc
            .setIdentity(Name("render.raytrace.caustic_emission_targets_upload"))
            .setMarkerLabel("Caustic Emission Targets Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(causticEmissionTargetsUploadScheduling)
            .setDependencies(&m_rayTraceMaterialContextSlotsUploadTask, 1u)
        ;
        m_causticEmissionTargetsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            causticEmissionTargetsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = causticEmissionTargetsBlob,
                .destination = causticEmissionTargets,
                // This automatic-state buffer publishes Common. Shadow Preparation owns the following SRV handoff
                // and thereby becomes the single producer observed by later software or hardware caustic packets.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_causticEmissionTargetsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare caustic emission-target upload"));
            return false;
        }
        shadowPrepareDependency = m_causticEmissionTargetsUploadTask;
    }

    const Core::GpuGraphResourceId surfelFrameConstants = m_rayTracingState.m_surfelConstants
        ? importBuffer(
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(m_rayTracingState.m_surfelConstants && !surfelFrameConstants.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted surfel constants"));
        return false;
    }

    Core::GpuUploadBlobId surfelFrameConstantsBlob;
    if(!m_raytracingSystem.retainPreparedSurfelFrameConstantsUpload(
        m_deferredLightingTaskGraph,
        deferredTargets,
        surfelFrameConstantsBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted surfel-frame constants upload data"));
        return false;
    }
    if(surfelFrameConstantsBlob.valid()){
        if(!surfelFrameConstants.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-frame constants upload has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint surfelFrameConstantsUploadScheduling;
        surfelFrameConstantsUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        surfelFrameConstantsUploadScheduling.forceSubmissionBoundary = false;
        surfelFrameConstantsUploadScheduling.allowPacketMerge = true;
        surfelFrameConstantsUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc surfelFrameConstantsUploadDesc;
        surfelFrameConstantsUploadDesc
            .setIdentity(Name("render.surfel_gi.constants_upload"))
            .setMarkerLabel("Surfel Frame Constants Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(surfelFrameConstantsUploadScheduling)
            .setDependencies(&shadowPrepareDependency, 1u)
        ;
        m_surfelFrameConstantsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            surfelFrameConstantsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = surfelFrameConstantsBlob,
                .destination = surfelFrameConstants,
                // This automatic-state constant buffer publishes Common. Shadow Preparation owns the following CB
                // handoff and therefore becomes the accepted cross-queue producer for Surfel GI.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_surfelFrameConstantsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare surfel-frame constants upload"));
            return false;
        }
        shadowPrepareDependency = m_surfelFrameConstantsUploadTask;
    }

    const Core::GpuGraphResourceId shadowInstanceMaterials = m_rayTracingState.m_shadowInstanceMaterialBuffer
        ? importBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId shadowInstances = m_rayTracingState.m_shadowInstanceBuffer
        ? importBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId shadowMaterialTyped = m_rayTracingState.m_shadowMaterialTypedBuffer
        ? importBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(
        (m_rayTracingState.m_shadowInstanceMaterialBuffer && !shadowInstanceMaterials.valid())
        || (m_rayTracingState.m_shadowInstanceBuffer && !shadowInstances.valid())
        || (m_rayTracingState.m_shadowMaterialTypedBuffer && !shadowMaterialTyped.valid())
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow material-context buffers"));
        return false;
    }

    Core::GpuUploadBlobId shadowInstanceMaterialsBlob;
    Core::GpuUploadBlobId shadowInstancesBlob;
    Core::GpuUploadBlobId shadowMaterialTypedBlob;
    if(!m_raytracingSystem.retainPreparedShadowMaterialContextUploads(
        m_deferredLightingTaskGraph,
        shadowInstanceMaterialsBlob,
        shadowInstancesBlob,
        shadowMaterialTypedBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow material-context upload data"));
        return false;
    }
    const bool shadowMaterialContextBatchGraphOwned = shadowInstanceMaterialsBlob.valid();
    if(
        shadowMaterialContextBatchGraphOwned != shadowInstancesBlob.valid()
        || shadowMaterialContextBatchGraphOwned != shadowMaterialTypedBlob.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen shadow material-context upload batch"));
        return false;
    }
    if(shadowMaterialContextBatchGraphOwned){
        if(!shadowInstanceMaterials.valid() || !shadowInstances.valid() || !shadowMaterialTyped.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen shadow material-context batch has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint shadowMaterialContextUploadScheduling;
        shadowMaterialContextUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        shadowMaterialContextUploadScheduling.forceSubmissionBoundary = false;
        shadowMaterialContextUploadScheduling.allowPacketMerge = true;
        shadowMaterialContextUploadScheduling.mergeWithPrevious = true;

        Core::GpuTaskDesc shadowInstanceMaterialsUploadDesc;
        shadowInstanceMaterialsUploadDesc
            .setIdentity(Name("render.deferred_effects.instance_material_upload"))
            .setMarkerLabel("Shadow Instance Materials Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&shadowPrepareDependency, 1u)
        ;
        m_shadowInstanceMaterialUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            shadowInstanceMaterialsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = shadowInstanceMaterialsBlob,
                .destination = shadowInstanceMaterials,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_shadowInstanceMaterialUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow instance-material upload"));
            return false;
        }

        Core::GpuTaskDesc shadowInstancesUploadDesc;
        shadowInstancesUploadDesc
            .setIdentity(Name("render.deferred_effects.shadow_instances_upload"))
            .setMarkerLabel("Shadow Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&m_shadowInstanceMaterialUploadTask, 1u)
        ;
        m_shadowInstanceUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            shadowInstancesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = shadowInstancesBlob,
                .destination = shadowInstances,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_shadowInstanceUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow instance upload"));
            return false;
        }

        Core::GpuTaskDesc shadowMaterialTypedUploadDesc;
        shadowMaterialTypedUploadDesc
            .setIdentity(Name("render.deferred_effects.material_typed_upload"))
            .setMarkerLabel("Shadow Typed Materials Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&m_shadowInstanceUploadTask, 1u)
        ;
        m_shadowMaterialTypedUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            shadowMaterialTypedUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = shadowMaterialTypedBlob,
                .destination = shadowMaterialTyped,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_shadowMaterialTypedUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow typed-material upload"));
            return false;
        }
        shadowPrepareDependency = m_shadowMaterialTypedUploadTask;
    }

    const Core::GpuGraphResourceId sceneBvhNodes = m_rayTracingState.m_sceneBvhNodeBuffer
        ? importBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId sceneBvhInstances = m_rayTracingState.m_sceneInstanceBuffer
        ? importBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(
        (m_rayTracingState.m_sceneBvhNodeBuffer && !sceneBvhNodes.valid())
        || (m_rayTracingState.m_sceneInstanceBuffer && !sceneBvhInstances.valid())
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted software scene-BVH buffers"));
        return false;
    }

    Core::GpuUploadBlobId sceneBvhNodesBlob;
    Core::GpuUploadBlobId sceneBvhInstancesBlob;
    if(!m_raytracingSystem.retainPreparedSceneBvhUploads(
        m_deferredLightingTaskGraph,
        sceneBvhNodesBlob,
        sceneBvhInstancesBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted software scene-BVH upload data"));
        return false;
    }
    const bool sceneBvhBatchGraphOwned = sceneBvhNodesBlob.valid();
    if(sceneBvhBatchGraphOwned != sceneBvhInstancesBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen software scene-BVH upload pair"));
        return false;
    }
    if(sceneBvhBatchGraphOwned){
        if(!sceneBvhNodes.valid() || !sceneBvhInstances.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene-BVH pair has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint sceneBvhUploadScheduling;
        sceneBvhUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        sceneBvhUploadScheduling.forceSubmissionBoundary = false;
        sceneBvhUploadScheduling.allowPacketMerge = true;
        sceneBvhUploadScheduling.mergeWithPrevious = true;

        Core::GpuTaskDesc sceneBvhNodesUploadDesc;
        sceneBvhNodesUploadDesc
            .setIdentity(Name("render.shadow_visibility.scene_bvh_nodes_upload"))
            .setMarkerLabel("Scene BVH Nodes Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(sceneBvhUploadScheduling)
            .setDependencies(&shadowPrepareDependency, 1u)
        ;
        m_sceneBvhNodesUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            sceneBvhNodesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneBvhNodesBlob,
                .destination = sceneBvhNodes,
                // Automatic-state software scene-BVH storage publishes Common. Shadow Preparation becomes the SRV
                // handoff producer observed by the later Compute shadow, caustic, and surfel consumers.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_sceneBvhNodesUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software scene-BVH node upload"));
            return false;
        }

        Core::GpuTaskDesc sceneBvhInstancesUploadDesc;
        sceneBvhInstancesUploadDesc
            .setIdentity(Name("render.shadow_visibility.scene_instances_upload"))
            .setMarkerLabel("Scene BVH Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(sceneBvhUploadScheduling)
            .setDependencies(&m_sceneBvhNodesUploadTask, 1u)
        ;
        m_sceneBvhInstancesUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            sceneBvhInstancesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneBvhInstancesBlob,
                .destination = sceneBvhInstances,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_sceneBvhInstancesUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software scene-BVH instance upload"));
            return false;
        }
        shadowPrepareDependency = m_sceneBvhInstancesUploadTask;
    }

    // Opaque and healthy hybrid hardware TLAS builds retain their preflight instance stream inside Shadow
    // Preparation itself. They do not add an upload packet: the existing first Graphics packet owns the native
    // build and its acceptance cache.
    const bool sceneTlasBuildGraphOwned = m_raytracingSystem.preparedSceneTlasBuildReady();
    if(sceneTlasBuildGraphOwned && !m_rayTracingState.m_tlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build has no imported acceleration structure"));
        return false;
    }
    const bool meshBlasBuildsGraphOwned = m_raytracingSystem.preparedMeshBlasBuildsReady();
    const PreparedMeshBlasBuildVector& preparedMeshBlasBuilds = m_raytracingSystem.preparedMeshBlasBuilds();
    if(meshBlasBuildsGraphOwned && preparedMeshBlasBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no operations"));
        return false;
    }
    if(meshBlasBuildsGraphOwned && !m_rayTracingState.m_tlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no scene TLAS"));
        return false;
    }
    const bool meshSwBvhBuildsGraphOwned = m_raytracingSystem.preparedMeshSwBvhBuildsReady();
    const PreparedMeshSwBvhBuildVector& preparedMeshSwBvhBuilds = m_raytracingSystem.preparedMeshSwBvhBuilds();
    if(meshSwBvhBuildsGraphOwned && preparedMeshSwBvhBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build plan has no operations"));
        return false;
    }
    // Keep the hybrid HW-to-SW continuation inside the first accepting packet, but make its recording boundary
    // explicit. The tail remains absent for pure-HW and pure-SW routes, whose established aggregate callbacks stay
    // untouched.
    const bool hybridSoftwareTailGraphOwned =
        m_raytracingSystem.hybridShadowVisibilityResourcesPreflighted();
    // A fully frozen hybrid packet has a separate software-tail callback, so the graph can now lower its exact
    // BLAS AccelStructBuildInput -> SW-BVH ShaderResource handoff at that boundary. Keep the direct/retry fallback
    // native unless both frozen plans and every required shared trace-geometry import are available.
    const bool hybridSoftwareTailInputStatesCandidate =
        hybridSoftwareTailGraphOwned
        && meshBlasBuildsGraphOwned
        && meshSwBvhBuildsGraphOwned
    ;
    // A prepared hardware route with no software tail has no later consumer, so its frozen geometry can enter
    // graph-owned directly as before. The hybrid candidate below extends that only to its verified tail boundary.
    bool meshBlasGeometryBuildInputStatesGraphOwned =
        meshBlasBuildsGraphOwned
        && (
            hybridSoftwareTailInputStatesCandidate
            || (!softwareTraceResourcesPrepared && !meshSwBvhBuildsGraphOwned)
        )
    ;
    bool meshSwBvhInputStatesGraphOwned = hybridSoftwareTailInputStatesCandidate;
    // The pure-software route has no accepted hardware fallback. It can therefore move each frozen operation's
    // private sentinel setup into graph built-ins, while the hybrid route deliberately retains its conditional
    // native transaction until its fallback boundary is separately modeled.
    const bool pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate =
        meshSwBvhBuildsGraphOwned
        && !m_raytracingSystem.shadowVisibilityHardwareSupported()
    ;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    struct PreparedMeshSwBvhGraphResources{
        PreparedMeshSwBvhBuild build;
        Core::GpuGraphResourceId position;
        Core::GpuGraphResourceId triangleIndex;
        Core::GpuGraphResourceId node;
        Core::GpuGraphResourceId parent;
        Core::GpuGraphResourceId sortKeys;
        Core::GpuGraphResourceId sortPayload;
        Core::GpuGraphResourceId visitCounter;
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceSetUse, Core::Alloc::ScratchArena> resourceSetUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accelStructFinalizeResourceUses{ scratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> meshBlasGeometryBuildInputResources{ scratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hybridSoftwareTailInputResources{ scratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> shadowPrepareTraceGeometryResources{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hybridSoftwareTailResourceUses{ scratchArena };
    Vector<PreparedMeshSwBvhGraphResources, Core::Alloc::ScratchArena> pureSoftwareMeshSwBvhGraphResources{
        scratchArena
    };
    resourceUses.reserve(
        19u
        + shadowTraceGeometryResourceCount
        + softwareBvhBuildStateResourceCount
        + meshState().m_meshes.size() * 2u
        + preparedMeshBlasBuilds.size() * 4u
    );
    accelStructFinalizeResourceUses.reserve(
        (sceneTlasBuildGraphOwned ? 2u : 0u)
        + preparedMeshBlasBuilds.size() * 2u
    );
    resourceSetUses.reserve(3u);
    meshBlasGeometryBuildInputResources.reserve(preparedMeshBlasBuilds.size() * 2u);
    hybridSoftwareTailInputResources.reserve(preparedMeshSwBvhBuilds.size() * 2u);
    shadowPrepareTraceGeometryResources.reserve(shadowTraceGeometryResourceCount);
    hybridSoftwareTailResourceUses.reserve(preparedMeshSwBvhBuilds.size() * 2u);
    pureSoftwareMeshSwBvhGraphResources.reserve(preparedMeshSwBvhBuilds.size());
    // Shadow Preparation owns each preflight input's post-transition packet boundary. This deliberately supersedes
    // preceding immutable uploads as graph producers, so later Compute readers wait on this first Graphics packet
    // rather than forcing FrontierSafe packetization to split an upload away from its accepting consumer.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as the graph producer for this selector. This WAW handoff retires the immutable
    // upload before later Compute reads without a record-time native state transition.
    resourceUses.push_back(WriteUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(causticEmissionTargets.valid())
        resourceUses.push_back(WriteUse(causticEmissionTargets, Core::ResourceStates::ShaderResource));
    if(surfelFrameConstants.valid())
        resourceUses.push_back(WriteUse(surfelFrameConstants, Core::ResourceStates::ConstantBuffer));
    if(shadowInstanceMaterials.valid())
        resourceUses.push_back(WriteUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
    if(shadowInstances.valid())
        resourceUses.push_back(WriteUse(shadowInstances, Core::ResourceStates::ShaderResource));
    if(shadowMaterialTyped.valid())
        resourceUses.push_back(WriteUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource));
    if(sceneBvhNodes.valid())
        resourceUses.push_back(WriteUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
    if(sceneBvhInstances.valid())
        resourceUses.push_back(WriteUse(sceneBvhInstances, Core::ResourceStates::ShaderResource));

    bool resourcesImported = true;
    const auto isShadowTraceGeometryResource = [&](const Core::GpuGraphResourceId resource){
        for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
            if(shadowTraceGeometryResources[resourceIndex] == resource)
                return true;
        }
        return false;
    };
    const auto appendTraceGeometryResource = [&](
        Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& resources,
        const Core::BufferHandle& buffer
    ){
        const Core::GpuGraphResourceId resource = m_deferredLightingTaskGraph.findImportedBuffer(buffer);
        if(!resource.valid() || !isShadowTraceGeometryResource(resource))
            return false;
        for(const Core::GpuGraphResourceId existing : resources){
            if(existing == resource)
                return true;
        }
        resources.push_back(resource);
        return true;
    };
    if(meshBlasGeometryBuildInputStatesGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            if(
                !appendTraceGeometryResource(meshBlasGeometryBuildInputResources, build.positionBuffer)
                || !appendTraceGeometryResource(meshBlasGeometryBuildInputResources, build.triangleIndexBuffer)
            ){
                // Do not reject an otherwise valid packet merely because a future preflight change omitted one
                // frozen stream from the graph-owned trace set. Retain the complete native BLAS/SW bridge instead.
                meshBlasGeometryBuildInputStatesGraphOwned = false;
                meshSwBvhInputStatesGraphOwned = false;
                meshBlasGeometryBuildInputResources.clear();
                break;
            }
        }
    }
    if(meshSwBvhInputStatesGraphOwned){
        for(const PreparedMeshSwBvhBuild& build : preparedMeshSwBvhBuilds){
            if(
                !appendTraceGeometryResource(hybridSoftwareTailInputResources, build.positionBuffer)
                || !appendTraceGeometryResource(hybridSoftwareTailInputResources, build.triangleIndexBuffer)
            ){
                // The prepared SW recorder accepts one all-or-native input-state policy. If any frozen input cannot
                // be declared at the tail boundary, preserve the established native bridge for both callbacks.
                meshBlasGeometryBuildInputStatesGraphOwned = false;
                meshSwBvhInputStatesGraphOwned = false;
                meshBlasGeometryBuildInputResources.clear();
                hybridSoftwareTailInputResources.clear();
                break;
            }
        }
    }
    const auto isMeshBlasGeometryBuildInput = [&](const Core::GpuGraphResourceId resource){
        for(const Core::GpuGraphResourceId buildInput : meshBlasGeometryBuildInputResources){
            if(buildInput == resource)
                return true;
        }
        return false;
    };
    const auto isPreparedMeshBlasBuild = [&](const Name meshName){
        if(!meshBlasBuildsGraphOwned)
            return false;
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            if(build.meshName == meshName)
                return true;
        }
        return false;
    };
    if(meshBlasBuildsGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            const Name blasIdentity = DeriveName(build.meshName, AStringView(":blas"));
            const Name backingIdentity = DeriveName(build.meshName, AStringView(":blas_backing"));
            const Core::GpuGraphResourceId blas = m_deferredLightingTaskGraph.importAccelStruct(
                build.blas,
                AccelStructResourceDesc(blasIdentity, "Prepared Mesh BLAS")
            );
            const Core::GpuGraphResourceId backing = importBuffer(
                build.blasBackingBuffer,
                backingIdentity,
                "Prepared Mesh BLAS Backing"
            );
            resourcesImported = resourcesImported
                && blas.valid()
                && backing.valid()
            ;
            if(blas.valid() && backing.valid()){
                // The graph owns typed/backing Write states here and the accepting successor publishes both Read
                // aliases. A prepared no-tail route and a fully verified hybrid tail own the matching geometry
                // boundary; direct and incomplete compatibility routes retain their native bridge.
                resourceUses.push_back(ReadWriteUse(blas, Core::ResourceStates::AccelStructWrite));
                resourceUses.push_back(WriteUse(backing, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(blas, Core::ResourceStates::AccelStructRead));
                accelStructFinalizeResourceUses.push_back(ReadUse(backing, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    Core::GpuGraphResourceSetId meshBlasGeometryBuildInputSet;
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputResources.empty()){
        meshBlasGeometryBuildInputSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.blas_geometry_build_inputs"))
                .setMarkerLabel("Shadow Prepare BLAS Geometry Build Inputs")
                .setMembers(
                    meshBlasGeometryBuildInputResources.data(),
                    meshBlasGeometryBuildInputResources.size()
                )
        );
    }
    const bool meshBlasGeometryBuildInputSetGraphOwned = meshBlasGeometryBuildInputSet.valid();
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : meshBlasGeometryBuildInputResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::AccelStructBuildInput));
    }
    const Core::GpuTaskResourceSetUse meshBlasGeometryBuildInputSetUse{
        .resourceSet = meshBlasGeometryBuildInputSet,
        .range = {},
        .requiredState = Core::ResourceStates::AccelStructBuildInput,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    if(meshBlasGeometryBuildInputSetGraphOwned)
        resourceSetUses.push_back(meshBlasGeometryBuildInputSetUse);
    Core::GpuGraphResourceSetId hybridSoftwareTailInputSet;
    if(meshSwBvhInputStatesGraphOwned && !hybridSoftwareTailInputResources.empty()){
        hybridSoftwareTailInputSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.hybrid_software_tail_inputs"))
                .setMarkerLabel("Shadow Prepare Hybrid Software Tail Inputs")
                .setMembers(hybridSoftwareTailInputResources.data(), hybridSoftwareTailInputResources.size())
        );
    }
    const bool hybridSoftwareTailInputSetGraphOwned = hybridSoftwareTailInputSet.valid();
    if(meshSwBvhInputStatesGraphOwned && !hybridSoftwareTailInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : hybridSoftwareTailInputResources)
            hybridSoftwareTailResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse hybridSoftwareTailInputSetUse{
        .resourceSet = hybridSoftwareTailInputSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        if(isMeshBlasGeometryBuildInput(resource))
            continue;
        shadowPrepareTraceGeometryResources.push_back(resource);
    }
    // The frozen trace list can carry BLAS position/index resources that need AccelStructBuildInput in this task.
    // Keep those exact members separate, but declare the remaining SRV subset as one immutable graph collection.
    Core::GpuGraphResourceSetId shadowPrepareTraceGeometrySet;
    if(!shadowPrepareTraceGeometryResources.empty()){
        shadowPrepareTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare_trace_geometry"))
                .setMarkerLabel("Shadow Prepare Trace Geometry")
                .setMembers(
                    shadowPrepareTraceGeometryResources.data(),
                    shadowPrepareTraceGeometryResources.size()
                )
        );
    }
    const bool shadowPrepareTraceGeometryStatesGraphOwned = shadowPrepareTraceGeometrySet.valid();
    if(!shadowPrepareTraceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : shadowPrepareTraceGeometryResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowPrepareTraceGeometrySetUse{
        .resourceSet = shadowPrepareTraceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    if(shadowPrepareTraceGeometryStatesGraphOwned)
        resourceSetUses.push_back(shadowPrepareTraceGeometrySetUse);
    for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
        if(!softwareBvhBuildStateResources[resourceIndex].valid())
            return false;
    }
    Core::GpuGraphResourceSetId softwareBvhBuildStateSet;
    if(softwareBvhBuildStateResourceCount != 0u){
        softwareBvhBuildStateSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.software_bvh_build_state"))
                .setMarkerLabel("Shadow Prepare Software BVH Build State")
                .setMembers(softwareBvhBuildStateResources, softwareBvhBuildStateResourceCount)
        );
    }
    const bool softwareBvhBuildStateStatesGraphOwned = softwareBvhBuildStateSet.valid();
    if(!softwareBvhBuildStateStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
            // Parent links and global build scratch retain their native UAV close state. They are state-only graph
            // resources: later traversal keeps its narrower node/geometry declarations.
            resourceUses.push_back(ReadWriteUse(
                softwareBvhBuildStateResources[resourceIndex],
                Core::ResourceStates::UnorderedAccess
            ));
        }
    }
    const Core::GpuTaskResourceSetUse softwareBvhBuildStateSetUse{
        .resourceSet = softwareBvhBuildStateSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    if(softwareBvhBuildStateStatesGraphOwned)
        resourceSetUses.push_back(softwareBvhBuildStateSetUse);

    Core::GpuGraphResourceId sceneTlas;
    Core::GpuGraphResourceId sceneTlasBacking;
    if(m_rayTracingState.m_tlas){
        sceneTlas = m_deferredLightingTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
        );
        sceneTlasBacking = importBuffer(
            m_rayTracingState.m_tlas->getBackingBufferHandle(),
            Name("render.deferred_effects.tlas_backing"),
            "Scene TLAS Backing"
        );
        resourcesImported = resourcesImported && sceneTlas.valid() && sceneTlasBacking.valid();
        if(sceneTlas.valid() && sceneTlasBacking.valid()){
            if(sceneTlasBuildGraphOwned){
                // The frozen native recorder only builds. The graph lowers its required Write entry state here and
                // the state-only successor below lowers the final Read handoff on the same backing storage.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructWrite));
                resourceUses.push_back(WriteUse(sceneTlasBacking, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlasBacking, Core::ResourceStates::AccelStructRead));
            }
            else{
                // Direct compatibility builders still publish their native Write -> Read sequence inside Shadow
                // Preparation. Keep the graph-visible final handoff unchanged for those routes.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructRead));
                resourceUses.push_back(WriteUse(sceneTlasBacking, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        // A frozen plan owns its retained handles even if a record-time replacement later sends it through the
        // native compatibility fallback. Do not collide a replacement with the frozen graph identity here.
        if(isPreparedMeshBlasBuild(mesh.meshName))
            continue;
        if(!mesh.blas)
            continue;

        const Name blasIdentity = DeriveName(mesh.meshName, AStringView(":blas"));
        const Name backingIdentity = DeriveName(mesh.meshName, AStringView(":blas_backing"));
        const Core::GpuGraphResourceId blas = m_deferredLightingTaskGraph.importAccelStruct(
            mesh.blas,
            AccelStructResourceDesc(blasIdentity, "Mesh BLAS")
        );
        const Core::GpuGraphResourceId backing = importBuffer(
            mesh.blas->getBackingBufferHandle(),
            backingIdentity,
            "Mesh BLAS Backing"
        );
        resourcesImported = resourcesImported && blas.valid() && backing.valid();
        if(blas.valid() && backing.valid()){
            const bool nativeBuildsBlas = mesh.runtimeMesh || mesh.blasBuildPending;
            if(nativeBuildsBlas){
                // Direct hybrid compatibility and frozen opaque plans both record the native write/read sequence
                // here. Keep the backing imported so an accepted prior AccelStructRead state seeds that transition.
                resourceUses.push_back(ReadWriteUse(blas, Core::ResourceStates::AccelStructRead));
                resourceUses.push_back(WriteUse(backing, Core::ResourceStates::AccelStructRead));
            }
            else{
                // State-only import: a later rejected preparation can re-pend this static BLAS, and its next build
                // must seed the true accepted AccelStructRead backing state instead of BufferDesc::initialState.
                resourceUses.push_back(ReadUse(blas, Core::ResourceStates::AccelStructRead));
                resourceUses.push_back(ReadUse(backing, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!resourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-preparation resources"));
        return false;
    }

    bool pureSoftwareMeshSwBvhBuildsGraphOwned = pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate;
    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        for(const PreparedMeshSwBvhBuild& build : preparedMeshSwBvhBuilds){
            const PreparedMeshSwBvhGraphResources resources{
                .build = build,
                .position = m_deferredLightingTaskGraph.findImportedBuffer(build.positionBuffer),
                .triangleIndex = m_deferredLightingTaskGraph.findImportedBuffer(build.triangleIndexBuffer),
                .node = m_deferredLightingTaskGraph.findImportedBuffer(build.nodeBuffer),
                .parent = m_deferredLightingTaskGraph.findImportedBuffer(build.parentBuffer),
                .sortKeys = m_deferredLightingTaskGraph.findImportedBuffer(build.sortKeysBuffer),
                .sortPayload = m_deferredLightingTaskGraph.findImportedBuffer(build.sortPayloadBuffer),
                .visitCounter = m_deferredLightingTaskGraph.findImportedBuffer(build.visitCounterBuffer),
            };
            if(
                !resources.position.valid()
                || !resources.triangleIndex.valid()
                || !resources.node.valid()
                || !resources.parent.valid()
                || !resources.sortKeys.valid()
                || !resources.sortPayload.valid()
                || !resources.visitCounter.valid()
            ){
                // Keep the established aggregate direct path if a future preflight leaves any frozen operation
                // without an exact graph identity. Never mix a partial typed-clear chain with native sentinels.
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH build is missing graph resources; retaining aggregate compatibility recorder"));
                pureSoftwareMeshSwBvhBuildsGraphOwned = false;
                pureSoftwareMeshSwBvhGraphResources.clear();
                break;
            }
            pureSoftwareMeshSwBvhGraphResources.push_back(resources);
        }
    }

    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        // Every operation shares sort keys, payload, and visit-counter scratch. Its built-in clears must therefore
        // remain immediately adjacent to its compute callback, and the entire chain must remain in Shadow
        // Preparation's accepting Graphics packet despite later Compute consumers of the final traversal state.
        Core::GpuTaskSchedulingHint clearScheduling;
        clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        clearScheduling.forceSubmissionBoundary = false;
        clearScheduling.allowPacketMerge = true;
        clearScheduling.mergeWithPrevious = true;
        clearScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskSchedulingHint buildScheduling = clearScheduling;
        buildScheduling.cost = Core::GpuTaskCostHint::Large;

        Core::GpuTaskId buildDependency = shadowPrepareDependency;
        const auto addPureSoftwareBvhClear = [&](
            const Name identity,
            const AStringView label,
            const Core::GpuGraphResourceId destination,
            const u32 clearValue
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(clearScheduling)
                .setDependencies(&buildDependency, 1u)
            ;
            return m_deferredLightingTaskGraph.addClearBufferTask(
                clearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = destination,
                    .clearValue = clearValue,
                }
            );
        };
        const auto appendPureSoftwareBvhClear = [&](const Core::GpuTaskId clearTask){
            if(!clearTask.valid())
                return false;
            if(!m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid())
                m_deferredShadowPrepareSoftwareBvhBuildFirstTask = clearTask;
            buildDependency = clearTask;
            return true;
        };

        for(const PreparedMeshSwBvhGraphResources& resources : pureSoftwareMeshSwBvhGraphResources){
            const PreparedMeshSwBvhBuild& build = resources.build;
            if(!build.performRefit){
                if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(
                    DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_keys_clear")),
                    "Shadow Prepare SW-BVH Sort-Key Clear",
                    resources.sortKeys,
                    BvhNodeIndex::Invalid
                ))
                    || !appendPureSoftwareBvhClear(addPureSoftwareBvhClear(
                        DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_parent_clear")),
                        "Shadow Prepare SW-BVH Parent Clear",
                        resources.parent,
                        BvhNodeIndex::Invalid
                    ))
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH sentinel clears"));
                    return false;
                }
            }
            if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(
                DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_counter_clear")),
                "Shadow Prepare SW-BVH Counter Clear",
                resources.visitCounter,
                0u
            ))){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH counter clear"));
                return false;
            }

            const Core::GpuTaskResourceUse buildResourceUses[]{
                ReadUse(resources.position, Core::ResourceStates::ShaderResource),
                ReadUse(resources.triangleIndex, Core::ResourceStates::ShaderResource),
                ReadWriteUse(resources.node, Core::ResourceStates::UnorderedAccess),
                ReadWriteUse(resources.parent, Core::ResourceStates::UnorderedAccess),
                ReadWriteUse(resources.sortKeys, Core::ResourceStates::UnorderedAccess),
                ReadWriteUse(resources.sortPayload, Core::ResourceStates::UnorderedAccess),
                ReadWriteUse(resources.visitCounter, Core::ResourceStates::UnorderedAccess),
            };
            Core::GpuTaskDesc buildDesc;
            buildDesc
                .setIdentity(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_build")))
                .setMarkerLabel("Shadow Prepare SW-BVH Build")
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(buildScheduling)
                .setDependencies(&buildDependency, 1u)
                .setResourceUses(buildResourceUses, LengthOf(buildResourceUses))
            ;
            const Core::GpuTaskId buildTask = m_deferredLightingTaskGraph.addTask<
                ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask
            >(
                buildDesc,
                ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask::Payload{
                    .raytracingSystem = &m_raytracingSystem,
                    .build = build,
                    .timingTicket = &timingTicket,
                }
            );
            if(!buildTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
                return false;
            }
            buildDependency = buildTask;
            m_deferredShadowPrepareSoftwareBvhBuildLastTask = buildTask;
        }
        if(
            !m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid()
            || !m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH graph chain has no task bounds"));
            return false;
        }
        shadowPrepareDependency = buildDependency;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    // A pure-software per-mesh chain is an explicit immediate predecessor of this semantic endpoint. Retain the
    // complete accepting packet even when later trace consumers form a FrontierSafe consumer frontier.
    scheduling.allowMergeAcrossConsumerFrontier = pureSoftwareMeshSwBvhBuildsGraphOwned;
    const Core::GpuTaskId* const dependencies = &shadowPrepareDependency;
    constexpr usize dependencyCount = 1u;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, dependencyCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses.data(), resourceSetUses.size())
    ;
    m_deferredShadowPrepareTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .renderer = this,
            .targets = &deferredTargets,
            .timingTicket = &timingTicket,
            .deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded,
            .currentBindlessSlotsGraphOwned = currentBindlessSlotsGraphOwned,
            .rayTraceMaterialContextSlotsGraphOwned = true,
            .causticEmissionTargetsGraphOwned = true,
            .surfelFrameConstantsGraphOwned = true,
            .shadowMaterialContextBatchGraphOwned = shadowMaterialContextBatchGraphOwned,
            .sceneBvhBatchGraphOwned = sceneBvhBatchGraphOwned,
            .sceneTlasBuildGraphOwned = sceneTlasBuildGraphOwned,
            .meshBlasBuildsGraphOwned = meshBlasBuildsGraphOwned,
            .meshBlasGeometryBuildInputStatesGraphOwned = meshBlasGeometryBuildInputStatesGraphOwned,
            .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
            .preparedMeshSwBvhBuildsRecordedByGraph = pureSoftwareMeshSwBvhBuildsGraphOwned,
            .deferHybridSoftwareTail = hybridSoftwareTailGraphOwned,
        }
    );
    if(!m_deferredShadowPrepareTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation task"));
        return false;
    }
    Core::GpuTaskId shadowPrepareFinalizeDependency = m_deferredShadowPrepareTask;
    if(hybridSoftwareTailGraphOwned){
        Core::GpuTaskSchedulingHint hybridSoftwareTailScheduling;
        hybridSoftwareTailScheduling.cost = Core::GpuTaskCostHint::Large;
        hybridSoftwareTailScheduling.forceSubmissionBoundary = false;
        hybridSoftwareTailScheduling.allowPacketMerge = true;
        hybridSoftwareTailScheduling.mergeWithPrevious = true;
        // Shadow Preparation already has direct later Compute consumers. The explicit immediate tail restores the
        // monolithic packet's ordering: those consumers wait for the complete HW-to-SW fallback boundary.
        hybridSoftwareTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc hybridSoftwareTailDesc;
        hybridSoftwareTailDesc
            .setIdentity(Name("render.shadow_prepare.hybrid_software_tail"))
            .setMarkerLabel("Shadow Preparation Hybrid Software Tail")
            .setQueue(GraphicsComputeUploadQueueRequest())
            .setScheduling(hybridSoftwareTailScheduling)
            .setDependencies(&m_deferredShadowPrepareTask, 1u)
            .setResourceUses(hybridSoftwareTailResourceUses.data(), hybridSoftwareTailResourceUses.size())
            .setResourceSetUses(
                hybridSoftwareTailInputSetGraphOwned ? &hybridSoftwareTailInputSetUse : nullptr,
                hybridSoftwareTailInputSetGraphOwned ? 1u : 0u
            )
        ;
        m_deferredShadowPrepareHybridSoftwareTailTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::ShadowPrepareHybridSoftwareTailGraphTask
        >(
            hybridSoftwareTailDesc,
            ECSRenderDetail::ShadowPrepareHybridSoftwareTailGraphTask::Payload{
                .raytracingSystem = &m_raytracingSystem,
                .targets = &deferredTargets,
                .hardwarePreparationReady = &m_preparedShadowVisibilityReady,
                .timingTicket = &timingTicket,
                .surfelFrameConstantsGraphOwned = true,
                .shadowMaterialContextBatchGraphOwned = shadowMaterialContextBatchGraphOwned,
                .sceneBvhBatchGraphOwned = sceneBvhBatchGraphOwned,
                .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
                .meshSwBvhInputStatesGraphOwned = meshSwBvhInputStatesGraphOwned,
            }
        );
        if(!m_deferredShadowPrepareHybridSoftwareTailTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hybrid software shadow-preparation tail"));
            return false;
        }
        shadowPrepareFinalizeDependency = m_deferredShadowPrepareHybridSoftwareTailTask;
    }

    const bool accelStructBuildStatesGraphOwned = sceneTlasBuildGraphOwned || meshBlasBuildsGraphOwned;
    if(accelStructBuildStatesGraphOwned){
        const usize expectedFinalizeResourceUseCount =
            (sceneTlasBuildGraphOwned ? 2u : 0u)
            + preparedMeshBlasBuilds.size() * 2u
        ;
        if(
            (sceneTlasBuildGraphOwned && (!sceneTlas.valid() || !sceneTlasBacking.valid()))
            || accelStructFinalizeResourceUses.size() != expectedFinalizeResourceUseCount
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen acceleration-structure build has no final-state graph resources"));
            return false;
        }
        Core::GpuGraphResourceSetId accelStructFinalizeSet;
        if(!accelStructFinalizeResourceUses.empty()){
            Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> accelStructFinalizeResources{ scratchArena };
            accelStructFinalizeResources.reserve(accelStructFinalizeResourceUses.size());
            for(const Core::GpuTaskResourceUse& use : accelStructFinalizeResourceUses)
                accelStructFinalizeResources.push_back(use.resource);
            accelStructFinalizeSet = m_deferredLightingTaskGraph.importResourceSet(
                Core::GpuGraphResourceSetDesc{}
                    .setIdentity(Name("render.shadow_prepare.accel_struct_finalize_resources"))
                    .setMarkerLabel("Shadow Prepare Accel-Struct Finalize Resources")
                    .setMembers(accelStructFinalizeResources.data(), accelStructFinalizeResources.size())
            );
        }
        const bool accelStructFinalizeSetGraphOwned = accelStructFinalizeSet.valid();
        const Core::GpuTaskResourceSetUse accelStructFinalizeSetUse{
            .resourceSet = accelStructFinalizeSet,
            .range = {},
            .requiredState = Core::ResourceStates::AccelStructRead,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskSchedulingHint accelStructFinalizeScheduling;
        accelStructFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accelStructFinalizeScheduling.forceSubmissionBoundary = false;
        accelStructFinalizeScheduling.allowPacketMerge = true;
        accelStructFinalizeScheduling.mergeWithPrevious = true;
        // Shadow Preparation has direct later Compute consumers. Keep these Read finalizers in the same accepting
        // packet so those consumers wait on every completed build and descriptor-visible backing state together.
        accelStructFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc accelStructFinalizeDesc;
        accelStructFinalizeDesc
            .setIdentity(Name("render.shadow_prepare.accel_struct_finalize"))
            .setMarkerLabel("Shadow Preparation Accel-Struct Finalize")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(accelStructFinalizeScheduling)
            .setDependencies(&shadowPrepareFinalizeDependency, 1u)
            // The immutable typed-and-backing final-state collection expands to the same compiler inputs. Retain
            // the individual declarations if a future compatibility route cannot form a complete unique set.
            .setResourceUses(
                accelStructFinalizeSetGraphOwned ? nullptr : accelStructFinalizeResourceUses.data(),
                accelStructFinalizeSetGraphOwned ? 0u : accelStructFinalizeResourceUses.size()
            )
            .setResourceSetUses(
                accelStructFinalizeSetGraphOwned ? &accelStructFinalizeSetUse : nullptr,
                accelStructFinalizeSetGraphOwned ? 1u : 0u
            )
        ;
        m_deferredShadowPrepareAccelStructFinalizeTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask
        >(
            accelStructFinalizeDesc,
            ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask::Payload{}
        );
        if(!m_deferredShadowPrepareAccelStructFinalizeTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare acceleration-structure final-state task"));
            return false;
        }
    }
    return true;
}


bool RendererSystem::declareDeferredGraphicsPrefixTasks(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuTaskId shadowPrepareTask,
    const CsgFrameState& csgFrameState,
    const bool hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const Core::GpuGraphResourceId albedo,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId opaqueColor,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId meshView,
    const Core::GpuGraphResourceId materialInstances,
    const Core::GpuGraphResourceId materialTyped,
    const Core::GpuGraphResourceId csgReceiverRanges,
    const Core::GpuGraphResourceId csgCutters,
    const Core::GpuGraphResourceId csgClipContextSlots,
    const Core::GpuGraphResourceId csgIntervalSampleState,
    const Core::GpuGraphResourceId csgCapBackNormal,
    const Core::GpuGraphResourceId csgIntervalDepth,
    const Core::GpuGraphResourceId csgIntervalId,
    const Core::GpuGraphResourceId csgReceiverEventData,
    const Core::GpuGraphResourceId csgReceiverEventCount,
    const Core::GpuGraphResourceId csgReceiverSpanData,
    const Core::GpuGraphResourceId csgReceiverSpanCount,
    const Core::GpuGraphResourceId csgRemovedIntervalDepth,
    const Core::GpuGraphResourceId csgRemovedIntervalCapNormal,
    const Core::GpuGraphResourceId csgRemovedIntervalData,
    const Core::GpuGraphResourceId csgRemovedIntervalCount,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    const Core::GpuGraphResourceSetId shadowTraceGeometrySet,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
    ECSRenderDetail::CsgIntervalClearTimingRecordState& csgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket** const timingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket
){
    using namespace __hidden_renderer_task_graph;
    using PrefixTimingSlot = ECSRenderDetail::DeferredGraphicsPrefixTimingSlot;

    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    m_graphicsPrefixOpaqueComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_graphicsPrefixOpaqueSharedComputeEmulationTasks)
        task = {};
    m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = {};
    m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgReceiverSpanTask = {};
    m_graphicsPrefixCsgIntervalCombineTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;

    const bool shadowTraceGeometryStatesGraphOwned = shadowTraceGeometrySet.valid();

    if(
        !deferredTargets.valid()
        || !shadowPrepareTask.valid()
        || !m_drawState.m_meshViewBuffer
        || !m_deferredState.m_sceneShadingBuffer
        || !m_deferredState.m_lightBuffer
        || !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !meshView.valid()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
        || (hasOpaqueCsgFrameWork && (
            !csgCapBackNormal.valid()
            || !csgIntervalDepth.valid()
            || !csgIntervalId.valid()
            || !csgReceiverEventData.valid()
            || !csgReceiverEventCount.valid()
            || !csgReceiverSpanData.valid()
            || !csgReceiverSpanCount.valid()
            || !csgRemovedIntervalDepth.valid()
            || !csgRemovedIntervalCapNormal.valid()
            || !csgRemovedIntervalData.valid()
            || !csgRemovedIntervalCount.valid()
        ))
        || !timingTickets
        || !asyncPrefixTimingSpansOnePacket
        || !deferredClearTimingState.graphics
        || deferredClearTimingState.timing != &deferredClearTiming
        || !deferredClearTimingState.timingTicket
        || (shadowTraceGeometryResourceCount != 0u && !shadowTraceGeometryResources)
    )
        return false;
    for(usize timingSlot = 0u; timingSlot < static_cast<usize>(PrefixTimingSlot::kCount); ++timingSlot){
        if(!timingTickets[timingSlot])
            return false;
    }
    const auto timingTicketSlot = [timingTickets](const PrefixTimingSlot slot){
        return &timingTickets[static_cast<usize>(slot)];
    };
    const Core::TextureSubresourceSet csgPeelSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgPeelLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverEventLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverSpanLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgRemovedIntervalLayerCount
    );
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources(0u, 1u, 0u, 1u);

    ECSRenderDetail::MeshViewGpuData meshViewState;
    bool meshViewUploadRequired = false;
    ECSRenderDetail::SceneLightGpuData sceneLightData[NWB_SCENE_MAX_LIGHTS] = {};
    ECSRenderDetail::SceneShadingGpuData sceneShadingState;
    u32 sceneLightCount = 0u;
    bool sceneLightUploadRequired = false;
    bool sceneShadingUploadRequired = false;
    if(
        !m_meshSystem.prepareMeshViewBufferUpload(
            meshViewAspectRatio,
            meshViewState,
            meshViewUploadRequired
        )
        || !m_deferredSystem.prepareSceneShadingBufferUploads(
            meshViewAspectRatio,
            sceneLightData,
            LengthOf(sceneLightData),
            sceneLightCount,
            sceneLightUploadRequired,
            sceneShadingState,
            sceneShadingUploadRequired
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare immutable graphics-prefix upload data"));
        return false;
    }

    Core::GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.cost = Core::GpuTaskCostHint::Medium;
    meshViewSetupScheduling.forceSubmissionBoundary = false;
    meshViewSetupScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_setup"))
        .setMarkerLabel("Mesh View Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewSetupScheduling)
        .setDependencies(&shadowPrepareTask, 1u)
    ;
    m_graphicsPrefixMeshViewSetupTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::MeshViewSetupGraphTask>(
        meshViewSetupDesc,
        ECSRenderDetail::MeshViewSetupGraphTask::Payload{
            .renderer = this,
            .frameTimingTransaction = &frameTimingTransaction,
            .asyncPrefixTiming = &asyncPrefixTiming,
            .timingTicket = timingTicketSlot(PrefixTimingSlot::MeshViewSetup),
            .asyncPrefixTimingSpansOnePacket = asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = &m_deferredShadowVisibilityTask,
        }
    );
    if(!m_graphicsPrefixMeshViewSetupTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare mesh-view setup task"));
        return false;
    }

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskId meshViewUploadTask = m_graphicsPrefixMeshViewSetupTask;
    if(meshViewUploadRequired){
        const Core::GpuUploadBlobId meshViewBlob = m_deferredLightingTaskGraph.copyUploadData(
            &meshViewState,
            sizeof(meshViewState),
            alignof(ECSRenderDetail::MeshViewGpuData)
        );
        Core::GpuTaskDesc meshViewUploadDesc;
        meshViewUploadDesc
            .setIdentity(Name("render.graphics_prefix.mesh_view_upload"))
            .setMarkerLabel("Mesh View Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&m_graphicsPrefixMeshViewSetupTask, 1u)
        ;
        meshViewUploadTask = meshViewBlob.valid()
            ? m_deferredLightingTaskGraph.addUploadBufferTask(
                meshViewUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = meshViewBlob,
                    .destination = meshView,
                    // The backing buffer deliberately restores Common when a native packet closes.  Declare that
                    // exact graph-visible boundary here; the G-buffer consumer below owns the Common ->
                    // ConstantBuffer transition.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!meshViewUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned mesh-view upload"));
            return false;
        }
    }

    Core::GpuTaskSchedulingHint meshViewCommitScheduling = immutableUploadScheduling;
    Core::GpuTaskDesc meshViewCommitDesc;
    meshViewCommitDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_upload_commit"))
        .setMarkerLabel("Mesh View Upload Commit")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewCommitScheduling)
        .setDependencies(&meshViewUploadTask, 1u)
    ;
    const Core::GpuTaskId meshViewCommitTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::MeshViewUploadCommitGraphTask>(
        meshViewCommitDesc,
        ECSRenderDetail::MeshViewUploadCommitGraphTask::Payload{
            .renderer = this,
            .viewState = meshViewState,
            .uploadRequired = meshViewUploadRequired,
            .ready = &m_graphicsPrefixMeshViewSetupReady,
        }
    );
    if(!meshViewCommitTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare mesh-view upload commit"));
        return false;
    }

    Core::GpuTaskId sceneUploadTask = meshViewCommitTask;
    if(sceneLightUploadRequired){
        const usize sceneLightByteCount = static_cast<usize>(sceneLightCount) * sizeof(sceneLightData[0u]);
        const Core::GpuUploadBlobId sceneLightBlob = m_deferredLightingTaskGraph.copyUploadData(
            sceneLightData,
            sceneLightByteCount,
            alignof(ECSRenderDetail::SceneLightGpuData)
        );
        Core::GpuTaskDesc sceneLightUploadDesc;
        sceneLightUploadDesc
            .setIdentity(Name("render.graphics_prefix.scene_lights_upload"))
            .setMarkerLabel("Scene Lights Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&sceneUploadTask, 1u)
        ;
        sceneUploadTask = sceneLightBlob.valid()
            ? m_deferredLightingTaskGraph.addUploadBufferTask(
                sceneLightUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = sceneLightBlob,
                    .destination = lights,
                    // These shared frame buffers retain Common between native packets.  The first declared reader
                    // owns the transition to ShaderResource.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!sceneUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned scene-light upload"));
            return false;
        }
    }

    if(sceneShadingUploadRequired){
        const Core::GpuUploadBlobId sceneShadingBlob = m_deferredLightingTaskGraph.copyUploadData(
            &sceneShadingState,
            sizeof(sceneShadingState),
            alignof(ECSRenderDetail::SceneShadingGpuData)
        );
        Core::GpuTaskDesc sceneShadingUploadDesc;
        sceneShadingUploadDesc
            .setIdentity(Name("render.graphics_prefix.scene_shading_upload"))
            .setMarkerLabel("Scene Shading Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&sceneUploadTask, 1u)
        ;
        sceneUploadTask = sceneShadingBlob.valid()
            ? m_deferredLightingTaskGraph.addUploadBufferTask(
                sceneShadingUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = sceneShadingBlob,
                    .destination = sceneShading,
                    // See the light upload above: preserve the resource's automatic Common boundary and let the
                    // first declared reader lower its ConstantBuffer transition.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!sceneUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned scene-shading upload"));
            return false;
        }
    }

    Core::GpuTaskSchedulingHint sceneShadingSetupScheduling;
    sceneShadingSetupScheduling.cost = Core::GpuTaskCostHint::Tiny;
    sceneShadingSetupScheduling.forceSubmissionBoundary = false;
    sceneShadingSetupScheduling.allowPacketMerge = true;
    sceneShadingSetupScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc sceneShadingSetupDesc;
    sceneShadingSetupDesc
        .setIdentity(Name("render.graphics_prefix.scene_shading_setup"))
        .setMarkerLabel("Scene Shading Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(sceneShadingSetupScheduling)
        .setDependencies(&sceneUploadTask, 1u)
    ;
    ECSRenderDetail::SceneShadingSetupGraphTask::Payload sceneShadingSetupPayload;
    sceneShadingSetupPayload.renderer = this;
    sceneShadingSetupPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::SceneShadingSetup);
    sceneShadingSetupPayload.ready = &m_graphicsPrefixSceneShadingSetupReady;
    NWB_MEMCPY(
        sceneShadingSetupPayload.lightData,
        sizeof(sceneShadingSetupPayload.lightData),
        sceneLightData,
        sizeof(sceneLightData)
    );
    sceneShadingSetupPayload.sceneShadingState = sceneShadingState;
    sceneShadingSetupPayload.lightCount = sceneLightCount;
    sceneShadingSetupPayload.lightUploadRequired = sceneLightUploadRequired;
    sceneShadingSetupPayload.sceneShadingUploadRequired = sceneShadingUploadRequired;
    m_graphicsPrefixSceneShadingSetupTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::SceneShadingSetupGraphTask>(
        sceneShadingSetupDesc,
        Move(sceneShadingSetupPayload)
    );
    if(!m_graphicsPrefixSceneShadingSetupTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare scene-shading setup task"));
        return false;
    }

    // Built-in clears retain one declaration per target and record their exact native operation through the graph.
    // Opaque color deliberately remains last: its post-clear timing endpoint stays inside the operation that owns the
    // later Graphics-to-Compute handoff, so FrontierSafe packetization cannot split timing from the final clear.
    Core::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    const auto makeClearDesc = [&clearScheduling](
        const Name identity,
        const AStringView markerLabel,
        const Core::GpuTaskId* const dependency
    ){
        Core::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(identity)
            .setMarkerLabel(markerLabel)
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(clearScheduling)
            .setDependencies(dependency, 1u)
        ;
        return clearDesc;
    };
    const Core::GpuClearTextureTaskRecordHooks deferredClearBeginHooks{
        .context = &deferredClearTimingState,
        .beforeClear = &ECSRenderDetail::BeginDeferredClearTiming,
        .discarded = &ECSRenderDetail::DiscardDeferredClearTiming,
    };
    const Core::GpuClearTextureTaskRecordHooks deferredClearEndHooks{
        .context = &deferredClearTimingState,
        .afterClear = &ECSRenderDetail::EndDeferredClearTiming,
        .discarded = &ECSRenderDetail::DiscardDeferredClearTiming,
    };
    const Core::GpuClearTextureTaskRecordHooks noDeferredClearHooks;
    const auto makeFloatClearDesc = [](
        const Core::GpuGraphResourceId destination,
        const Core::Color& clearValue,
        const Core::GpuClearTextureTaskRecordHooks& recordHooks
    ){
        Core::GpuClearTextureTaskDesc clearDesc;
        clearDesc.destination = destination;
        clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
        clearDesc.floatValue = clearValue;
        clearDesc.recordHooks = recordHooks;
        return clearDesc;
    };
    const auto makeDepthClearDesc = [](const Core::GpuGraphResourceId destination){
        Core::GpuClearTextureTaskDesc clearDesc;
        clearDesc.destination = destination;
        clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clearDesc.valueType = Core::GpuClearTextureTaskValueType::DepthStencil;
        clearDesc.depthValue = Core::s_DepthClearValue;
        clearDesc.clearDepth = true;
        return clearDesc;
    };
    Core::GpuTaskId deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_albedo"),
            "Deferred Clear Albedo",
            &m_graphicsPrefixSceneShadingSetupTask
        ),
        makeFloatClearDesc(albedo, ECSRenderDetail::s_ClearColor, deferredClearBeginHooks)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred albedo clear"));
        return false;
    }
    m_graphicsPrefixDeferredClearFirstTask = deferredClearTask;
    deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_normal"),
            "Deferred Clear Normal",
            &deferredClearTask
        ),
        makeFloatClearDesc(normal, ECSRenderDetail::s_GBufferNormalClearColor, noDeferredClearHooks)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred normal clear"));
        return false;
    }
    deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_world_position"),
            "Deferred Clear World Position",
            &deferredClearTask
        ),
        makeFloatClearDesc(worldPosition, ECSRenderDetail::s_GBufferWorldPositionClearColor, noDeferredClearHooks)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred world-position clear"));
        return false;
    }
    deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_depth"),
            "Deferred Clear Depth",
            &deferredClearTask
        ),
        makeDepthClearDesc(depth)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred depth clear"));
        return false;
    }
    m_graphicsPrefixDeferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_opaque_color"),
            "Deferred Clear Opaque Color",
            &deferredClearTask
        ),
        makeFloatClearDesc(opaqueColor, ECSRenderDetail::s_ClearColor, deferredClearEndHooks)
    );
    if(!m_graphicsPrefixDeferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-clear task"));
        return false;
    }

    // Freeze the opaque packet's draw ordering before recording.  The two copied blobs below are the exact source
    // of the heap-selected instance/material streams consumed by these saved draw items.
    Core::Alloc::ScratchArena materialUploadScratch(RendererArenaScope::s_TaskGraphArena);
    MaterialPassDrawItemPartitions opaqueDrawItems{ materialUploadScratch };
    InstanceGpuDataVector instanceData{ materialUploadScratch };
    CsgFrameGpuData csgFrameData{ materialUploadScratch };
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{ materialUploadScratch };
#endif
    MaterialTypedByteDataVector materialTypedBytes{ materialUploadScratch };
    m_materialSystem.gatherMaterialPassDrawItems(
        deferredTargets.framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        csgFrameState,
        opaqueDrawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes,
        RendererResourceLookupMode::PreparedOnly,
        &meshViewState
    );

    ECSRenderDetail::GbufferGraphTask::Payload gbufferPayload{ m_arena };
    gbufferPayload.renderer = this;
    gbufferPayload.targets = &deferredTargets;
    gbufferPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
    gbufferPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
    gbufferPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;

    const bool hasOpaqueDrawItems = !opaqueDrawItems.empty();
    // G-buffer and the optional opaque CSG follow-up both declare the shared material entry batch whenever their
    // immutable draw stream exists. The selected source-geometry batch is retained and declared separately below.
    gbufferPayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
    Core::GpuTaskId materialDrawUploadTask = m_graphicsPrefixDeferredClearTask;
    if(hasOpaqueDrawItems){
        if(
            !materialInstances.valid()
            || !materialTyped.valid()
            || !m_materialSystem.materialPassDrawBuffersReady(instanceData, materialTypedBytes)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared opaque material draw buffers were unavailable during graph declaration"));
            return false;
        }
        m_materialSystem.prepareMaterialPassInstanceUploadData(instanceData);
#if defined(NWB_DEBUG)
        if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: opaque material instance upload size overflows graph blob capacity"));
            return false;
        }
        NWB_ASSERT(instanceData.size() == materialTypedRanges.size());
        ECSRenderDetail::AssertMaterialTypedUploadRanges(materialTypedRanges, materialTypedBytes);
#endif

        const Core::GpuUploadBlobId instanceBlob = m_deferredLightingTaskGraph.copyUploadData(
            instanceData.data(),
            instanceData.size() * sizeof(InstanceGpuData),
            alignof(InstanceGpuData)
        );
        const Core::GpuUploadBlobId materialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
            materialTypedBytes.data(),
            materialTypedBytes.size(),
            alignof(u32)
        );
        if(!instanceBlob.valid() || !materialTypedBlob.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable opaque material upload data"));
            return false;
        }

        Core::GpuTaskDesc instanceUploadDesc;
        instanceUploadDesc
            .setIdentity(Name("render.graphics_prefix.material_instances_upload"))
            .setMarkerLabel("Material Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&materialDrawUploadTask, 1u)
        ;
        materialDrawUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            instanceUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = instanceBlob,
                .destination = materialInstances,
                // Both draw buffers use automatic Common restoration when the native packet closes.  Keep that
                // graph-visible boundary exact; the G-buffer read below owns the transient SRV transition.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!materialDrawUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned material instance upload"));
            return false;
        }

        Core::GpuTaskDesc materialTypedUploadDesc;
        materialTypedUploadDesc
            .setIdentity(Name("render.graphics_prefix.material_typed_upload"))
            .setMarkerLabel("Material Typed Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&materialDrawUploadTask, 1u)
        ;
        materialDrawUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            materialTypedUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = materialTypedBlob,
                .destination = materialTyped,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!materialDrawUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned material typed upload"));
            return false;
        }
        gbufferPayload.materialDrawBuffersUploaded = true;
    }

    // Freeze every opaque CSG upload byte after preflight fixed the buffer, descriptor, and target generations.
    // Native G-buffer recording consumes these values without rebuilding either CSG uniform payload from live state.
    const bool hasCsgFrameGpuWork = csgFrameData.hasWork();
    Core::GpuTaskId csgFrameUploadTask = materialDrawUploadTask;
    if(hasCsgFrameGpuWork){
        if(
            !csgReceiverRanges.valid()
            || !csgCutters.valid()
            || !csgClipContextSlots.valid()
            || !csgIntervalSampleState.valid()
            || !m_csgSystem.csgFrameBuffersReady(csgFrameData)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared CSG frame buffers were unavailable during graph declaration"));
            return false;
        }
#if defined(NWB_DEBUG)
        if(
            csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
            || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: CSG frame upload size overflows graph blob capacity"));
            return false;
        }
#endif

        CsgClipContextSlots csgClipContextSlotData;
        CsgIntervalSampleStateGpuData csgIntervalSampleStateData;
        if(
            !m_csgSystem.prepareCsgClipContextSlotData(csgFrameData, csgClipContextSlotData)
            || !m_csgSystem.prepareCsgIntervalSampleStateData(
                deferredTargets,
                csgFrameData,
                csgIntervalSampleStateData
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot opaque CSG auxiliary upload data"));
            return false;
        }

        const Core::GpuUploadBlobId receiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
            csgFrameData.receiverRanges.data(),
            csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
            alignof(CsgReceiverRangeGpuData)
        );
        const Core::GpuUploadBlobId cuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
            csgFrameData.cutters.data(),
            csgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
            alignof(CsgCutterGpuData)
        );
        const Core::GpuUploadBlobId clipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
            &csgClipContextSlotData,
            sizeof(csgClipContextSlotData),
            alignof(CsgClipContextSlots)
        );
        const Core::GpuUploadBlobId intervalSampleStateBlob = m_deferredLightingTaskGraph.copyUploadData(
            &csgIntervalSampleStateData,
            sizeof(csgIntervalSampleStateData),
            alignof(CsgIntervalSampleStateGpuData)
        );
        if(
            !receiverRangesBlob.valid()
            || !cuttersBlob.valid()
            || !clipContextSlotsBlob.valid()
            || !intervalSampleStateBlob.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable CSG frame upload data"));
            return false;
        }

        Core::GpuTaskDesc receiverRangesUploadDesc;
        receiverRangesUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_ranges_upload"))
            .setMarkerLabel("CSG Receiver Ranges Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            receiverRangesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = receiverRangesBlob,
                .destination = csgReceiverRanges,
                // CSG structured buffers restore Common at native packet close; G-buffer owns their transient SRV
                // state exactly like the graph-owned material streams above.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG receiver-range upload"));
            return false;
        }

        Core::GpuTaskDesc cuttersUploadDesc;
        cuttersUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_cutters_upload"))
            .setMarkerLabel("CSG Cutters Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            cuttersUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = cuttersBlob,
                .destination = csgCutters,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG cutter upload"));
            return false;
        }

        Core::GpuTaskDesc clipContextSlotsUploadDesc;
        clipContextSlotsUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_clip_context_slots_upload"))
            .setMarkerLabel("CSG Clip Context Slots Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            clipContextSlotsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = clipContextSlotsBlob,
                .destination = csgClipContextSlots,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG clip-context upload"));
            return false;
        }

        Core::GpuTaskDesc intervalSampleStateUploadDesc;
        intervalSampleStateUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_sample_state_upload"))
            .setMarkerLabel("CSG Interval Sample State Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            intervalSampleStateUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = intervalSampleStateBlob,
                .destination = csgIntervalSampleState,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG interval-state upload"));
            return false;
        }
        gbufferPayload.csgFrameBuffersUploaded = true;
    }
    gbufferPayload.opaqueDrawSnapshot.capture(
        opaqueDrawItems,
        csgFrameData,
        instanceData.size(),
        materialTypedBytes.size()
    );
    ECSRenderDetail::CsgReceiverSpanBuildGraphTask::Payload csgReceiverSpanPayload{ m_arena };
    if(hasOpaqueCsgFrameWork){
        csgReceiverSpanPayload.renderer = this;
        csgReceiverSpanPayload.targets = &deferredTargets;
        // The graph owns the receiver-surface producer fence. Normal prefix compilation merges this callback with
        // G-buffer, while a FrontierSafe boundary retains its own submission-local timing ticket.
        csgReceiverSpanPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::CsgReceiverSpanBuild);
        csgReceiverSpanPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        csgReceiverSpanPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        csgReceiverSpanPayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        csgReceiverSpanPayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        csgReceiverSpanPayload.receiverSpanInputImageStatesGraphOwned = true;
        csgReceiverSpanPayload.receiverSpanOutputImageStatesGraphOwned = true;
        csgReceiverSpanPayload.opaqueDrawSnapshot.capture(
            opaqueDrawItems,
            csgFrameData,
            instanceData.size(),
            materialTypedBytes.size()
        );
    }
    ECSRenderDetail::CsgIntervalCombineGraphTask::Payload csgIntervalCombinePayload{ m_arena };
    if(hasOpaqueCsgFrameWork){
        csgIntervalCombinePayload.renderer = this;
        csgIntervalCombinePayload.targets = &deferredTargets;
        // The graph owns the preceding producer fence. Normal prefix compilation merges this callback with
        // G-buffer, while a FrontierSafe boundary retains its own submission-local timing ticket.
        csgIntervalCombinePayload.timingTicket = timingTicketSlot(PrefixTimingSlot::CsgIntervalCombine);
        csgIntervalCombinePayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        csgIntervalCombinePayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        csgIntervalCombinePayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        csgIntervalCombinePayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        csgIntervalCombinePayload.intervalCombineInputImageStatesGraphOwned = true;
        csgIntervalCombinePayload.removedIntervalOutputImageStatesGraphOwned = true;
        csgIntervalCombinePayload.opaqueDrawSnapshot.capture(
            opaqueDrawItems,
            csgFrameData,
            instanceData.size(),
            materialTypedBytes.size()
        );
    }
    ECSRenderDetail::CsgIntervalSampleGraphTask::Payload csgIntervalSamplePayload{ m_arena };
    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask::Payload
        opaqueCsgIntervalSampleComputeEmulationPayload{ m_arena };
    if(hasOpaqueCsgFrameWork){
        csgIntervalSamplePayload.renderer = this;
        csgIntervalSamplePayload.targets = &deferredTargets;
        csgIntervalSamplePayload.timingTicket = timingTicketSlot(PrefixTimingSlot::CsgIntervalSample);
        csgIntervalSamplePayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        csgIntervalSamplePayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        csgIntervalSamplePayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        csgIntervalSamplePayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        csgIntervalSamplePayload.intervalSampleImageStatesGraphOwned = true;
        csgIntervalSamplePayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
        // The interval-sample task is scheduled for semantic CSG work, but only the gathered GPU work
        // declares these heap-selected clip buffers. Retain the native bridge for an empty gathered frame.
        csgIntervalSamplePayload.csgClipBufferStatesGraphOwned = hasCsgFrameGpuWork;
        csgIntervalSamplePayload.opaqueDrawSnapshot.capture(
            opaqueDrawItems,
            csgFrameData,
            instanceData.size(),
            materialTypedBytes.size()
        );
    }
    // The G-buffer produces peel/event images, span build consumes its event aliases and publishes spans, Combine
    // consumes the five prior-stage aliases and publishes removed-interval images, then Sample consumes those
    // outputs. Their native thunks consume graph-owned StorageImage state without staging target transitions.
    gbufferPayload.csgIntervalPeelTargetStatesGraphOwned = hasOpaqueCsgFrameWork;
    gbufferPayload.csgReceiverSurfaceImageStatesGraphOwned = hasOpaqueCsgFrameWork;
    // Match the actual graph declarations above; semantic CSG work may have no gathered GPU frame data.
    gbufferPayload.csgClipBufferStatesGraphOwned = hasCsgFrameGpuWork;

    // The clear is intentionally keyed to the semantic opaque-CSG frame flag, rather than the later native
    // readiness checks. This preserves the old defensive clear timing while making its two actual CopyDest writes
    // and the following UAV handoff visible to the graph.
    Core::GpuTaskId csgIntervalClearTask = csgFrameUploadTask;
    if(hasOpaqueCsgFrameWork){
        Core::GpuTaskSchedulingHint csgIntervalClearScheduling;
        csgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        csgIntervalClearScheduling.forceSubmissionBoundary = false;
        csgIntervalClearScheduling.allowPacketMerge = true;
        csgIntervalClearScheduling.mergeWithPrevious = true;
        const auto makeCsgIntervalClearTaskDesc = [&csgIntervalClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(csgIntervalClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const Core::Rect csgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        const Core::GpuClearTextureTaskRecordHooks csgIntervalClearBeginHooks{
            .context = &csgIntervalClearTimingState,
            .beforeClear = &ECSRenderDetail::BeginCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        const Core::GpuClearTextureTaskRecordHooks csgIntervalClearEndHooks{
            .context = &csgIntervalClearTimingState,
            .afterClear = &ECSRenderDetail::EndCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        m_graphicsPrefixCsgIntervalClearFirstTask = m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
            makeCsgIntervalClearTaskDesc(
                Name("render.graphics_prefix.csg_interval_clear"),
                "CSG Interval Id Clear",
                csgFrameUploadTask
            ),
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = csgIntervalId,
                .subresources = csgPeelSubresources,
                .rect = csgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = csgIntervalClearBeginHooks,
            }
        );
        if(!m_graphicsPrefixCsgIntervalClearFirstTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-id clear"));
            return false;
        }
        Core::GpuTaskSchedulingHint csgIntervalClearTailScheduling = csgIntervalClearScheduling;
        // The timing endpoint must remain in the first clear's Graphics packet even when another queue observes the
        // interval id. The explicit immediate dependency satisfies FrontierSafe's consumer-frontier override.
        csgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc csgIntervalClearTailDesc;
        csgIntervalClearTailDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_event_count_clear"))
            .setMarkerLabel("CSG Receiver Event Count Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(csgIntervalClearTailScheduling)
            .setDependencies(&m_graphicsPrefixCsgIntervalClearFirstTask, 1u)
        ;
        m_graphicsPrefixCsgIntervalClearTask = m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
            csgIntervalClearTailDesc,
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = csgReceiverEventCount,
                .subresources = csgReceiverEventCountSubresources,
                .rect = csgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = csgIntervalClearEndHooks,
            }
        );
        if(!m_graphicsPrefixCsgIntervalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver-event clear"));
            return false;
        }
        csgIntervalClearTask = m_graphicsPrefixCsgIntervalClearTask;
    }

    Core::Alloc::ScratchArena gbufferResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferResourceUses{ gbufferResourceScratch };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueComputeEmulationResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueSharedComputeEmulationGenerateResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueSharedComputeEmulationRasterResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueCsgReceiverComputeEmulationResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgReceiverSpanResourceUses{ gbufferResourceScratch };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalCombineResourceUses{ gbufferResourceScratch };
    Core::GpuGraphResourceSetId gbufferMaterialGeometrySet;
    Core::GpuGraphResourceSetId gbufferMaterialSampledTextureSet;
    Core::GpuGraphResourceSetId opaqueComputeEmulationOutputSet;
    Core::GpuGraphResourceId opaqueSharedComputeEmulationOutput;
    Core::GpuGraphResourceSetId opaqueCsgReceiverComputeEmulationOutputSet;
    Core::GpuGraphResourceSetId opaqueCsgIntervalSampleComputeEmulationOutputSet;
    const MaterialPassDrawItems* const gbufferMaterialGeometryDrawSets[] = {
        &opaqueDrawItems.regular,
        &opaqueDrawItems.csgReceiverSurface,
    };
    const bool gbufferUsesMaterialGeometry =
        !opaqueDrawItems.regular.empty()
        || !opaqueDrawItems.csgReceiverSurface.empty()
    ;
    gbufferPayload.materialGeometryStatesGraphOwned = gbufferUsesMaterialGeometry
        && GatherPreparedMaterialGeometryResourceSet(
            m_meshSystem,
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            gbufferResourceScratch,
            Name("render.graphics_prefix.gbuffer.material_geometry"),
            "Opaque Material Geometry",
            gbufferMaterialGeometrySet
        )
    ;
    if(gbufferUsesMaterialGeometry && !gbufferPayload.materialGeometryStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material geometry states"));
    const bool gbufferMaterialSampledTexturesCollected = gbufferUsesMaterialGeometry
        && GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            gbufferResourceScratch,
            Name("render.graphics_prefix.gbuffer.material_sampled_textures"),
            "Opaque Material Sampled Textures",
            gbufferMaterialSampledTextureSet
        )
    ;
    if(gbufferUsesMaterialGeometry && !gbufferMaterialSampledTexturesCollected)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material sampled textures"));

    // A generated-vertex buffer is persistent per mesh, so pulling every compute dispatch ahead of raster would be
    // wrong when multiple frozen draw items select the same output. The plan deliberately enables only the fully
    // alias-free regular opaque case; all other streams keep their established local interleaved handoff.
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload opaqueComputeEmulationPayload{ m_arena };
    const bool opaqueComputeEmulationPlanCaptured = gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueComputeEmulationPayload.plan.capture(
            m_meshSystem,
            opaqueDrawItems.regular
        )
    ;
    const bool opaqueComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationPlanCaptured
        && GatherOpaqueRegularComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            opaqueComputeEmulationPayload.plan,
            gbufferResourceScratch,
            Name("render.graphics_prefix.opaque_compute_emulation.outputs"),
            "Opaque Compute Emulation Outputs",
            opaqueComputeEmulationOutputSet
        )
    ;
    if(opaqueComputeEmulationPlanCaptured && !opaqueComputeEmulationOutputStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned opaque compute-emulation output states"
        ));
    }
    gbufferPayload.regularComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationOutputStatesGraphOwned;

    // A persistent generated-vertex buffer is normally a local dispatch/raster bridge. Handle the small alias
    // classes explicitly: two, three, or four regular opaque compute items sharing one frozen output and heap slot.
    // Opaque CSG remains out of scope so no CSG producer/raster phase can observe this alternation.
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan opaqueSharedComputeEmulationPlan;
    const bool opaqueSharedComputeEmulationPlanCaptured =
        !hasOpaqueCsgFrameWork
        && !opaqueComputeEmulationOutputStatesGraphOwned
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueSharedComputeEmulationPlan.capture(m_meshSystem, opaqueDrawItems.regular, 4u)
    ;
    const bool opaqueSharedComputeEmulationOutputStatesGraphOwned =
        opaqueSharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            opaqueSharedComputeEmulationPlan,
            "Opaque Shared Compute Emulation Output",
            opaqueSharedComputeEmulationOutput
        )
    ;
    if(
        opaqueSharedComputeEmulationPlanCaptured
        && !opaqueSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned shared opaque compute-emulation output state"
        ));
    }
    gbufferPayload.regularSharedComputeEmulationDrawsGraphOwned =
        opaqueSharedComputeEmulationOutputStatesGraphOwned;
    gbufferPayload.regularSharedComputeEmulationTiming =
        opaqueSharedComputeEmulationOutputStatesGraphOwned
            ? &opaqueRegularSharedComputeEmulationTiming
            : nullptr
    ;

    ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload opaqueCsgReceiverComputeEmulationPayload{
        m_arena
    };
    const bool opaqueCsgReceiverComputeEmulationPlanCaptured = hasOpaqueCsgFrameWork
        && hasCsgFrameGpuWork
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueCsgReceiverComputeEmulationPayload.plan.capture(
            m_meshSystem,
            opaqueDrawItems.csgReceiverSurface,
            opaqueDrawItems.regular,
            csgFrameData
        )
    ;
    const bool opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned =
        opaqueCsgReceiverComputeEmulationPlanCaptured
        && GatherOpaqueCsgReceiverComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            opaqueCsgReceiverComputeEmulationPayload.plan,
            gbufferResourceScratch,
            Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation.outputs"),
            "Opaque CSG Receiver Compute Emulation Outputs",
            opaqueCsgReceiverComputeEmulationOutputSet
        )
    ;
    if(
        opaqueCsgReceiverComputeEmulationPlanCaptured
        && !opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned opaque CSG receiver compute-emulation output states"
        ));
    }
    gbufferPayload.csgReceiverComputeEmulationOutputStatesGraphOwned =
        opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned;

    gbufferResourceUses.reserve(
        (hasOpaqueDrawItems ? 7u : 5u)
        + (hasCsgFrameGpuWork ? 5u : 0u)
        + (hasOpaqueCsgFrameWork ? 5u : 0u)
    );
    gbufferResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    if(hasOpaqueDrawItems){
        gbufferResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        gbufferResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
    }
    if(hasCsgFrameGpuWork){
        gbufferResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
        gbufferResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
        gbufferResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        gbufferResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // CSG resolves target-specific images through the deferred bindless-slot buffer selected in its context.
        gbufferResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    if(hasOpaqueCsgFrameWork){
        csgReceiverSpanResourceUses.reserve(4u + (hasCsgFrameGpuWork ? 2u : 0u));
        csgIntervalCombineResourceUses.reserve(9u + (hasCsgFrameGpuWork ? 2u : 0u));
        gbufferResourceUses.push_back(
            ReadWriteTextureUse(csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        gbufferResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        gbufferResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        gbufferResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgReceiverSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgReceiverSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        if(hasCsgFrameGpuWork){
            csgReceiverSpanResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgReceiverSpanResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgReceiverSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgReceiverSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgCapBackNormal,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalDepth,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalId,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        if(hasCsgFrameGpuWork){
            csgIntervalCombineResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgIntervalCombineResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    gbufferResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));
    const Core::GpuTaskResourceSetUse gbufferMaterialGeometrySetUse{
        .resourceSet = gbufferMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse gbufferMaterialSampledTextureSetUse{
        .resourceSet = gbufferMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse gbufferMaterialResourceSetUses[4u] = {};
    usize gbufferMaterialResourceSetUseCount = 0u;
    if(gbufferPayload.materialGeometryStatesGraphOwned)
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialGeometrySetUse;
    if(gbufferMaterialSampledTextureSet.valid())
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialSampledTextureSetUse;
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] =
            opaqueComputeEmulationOutputVertexBufferSetUse;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] =
            opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse;
    }

    Core::GpuTaskId gbufferDependency = csgIntervalClearTask;
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        opaqueComputeEmulationPayload.renderer = this;
        opaqueComputeEmulationPayload.targets = &deferredTargets;
        // G-buffer's semantic timing ticket spans its preparatory compute half and its raster half in one packet.
        opaqueComputeEmulationPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
        opaqueComputeEmulationPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        opaqueComputeEmulationPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        opaqueComputeEmulationPayload.instanceCount = instanceData.size();
        opaqueComputeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
        opaqueComputeEmulationPayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        opaqueComputeEmulationPayload.materialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned;
        opaqueComputeEmulationPayload.materialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned;

        opaqueComputeEmulationResourceUses.reserve(3u);
        opaqueComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        opaqueComputeEmulationResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        opaqueComputeEmulationResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        Core::GpuTaskResourceSetUse opaqueComputeEmulationResourceSetUses[3u] = {};
        usize opaqueComputeEmulationResourceSetUseCount = 0u;
        opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = gbufferMaterialGeometrySetUse;
        if(gbufferMaterialSampledTextureSet.valid()){
            opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] =
                gbufferMaterialSampledTextureSetUse;
        }
        opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] =
            opaqueComputeEmulationOutputUavSetUse;
        Core::GpuTaskSchedulingHint opaqueComputeEmulationScheduling;
        opaqueComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueComputeEmulationScheduling.allowPacketMerge = true;
        opaqueComputeEmulationScheduling.mergeWithPrevious = true;
        // This explicit immediate successor must remain with the preceding Graphics prefix work: CSG clear timing
        // and the G-buffer semantic range both require one accepting command list under FrontierSafe packetization.
        opaqueComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc opaqueComputeEmulationDesc;
        opaqueComputeEmulationDesc
            .setIdentity(Name("render.graphics_prefix.opaque_compute_emulation"))
            .setMarkerLabel("Opaque Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(opaqueComputeEmulationScheduling)
            .setDependencies(&gbufferDependency, 1u)
            .setResourceUses(
                opaqueComputeEmulationResourceUses.data(),
                opaqueComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                opaqueComputeEmulationResourceSetUses,
                opaqueComputeEmulationResourceSetUseCount
            )
        ;
        m_graphicsPrefixOpaqueComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask
        >(
            opaqueComputeEmulationDesc,
            Move(opaqueComputeEmulationPayload)
        );
        if(!m_graphicsPrefixOpaqueComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque compute-emulation producer"));
            return false;
        }
        gbufferDependency = m_graphicsPrefixOpaqueComputeEmulationTask;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        opaqueCsgReceiverComputeEmulationPayload.renderer = this;
        opaqueCsgReceiverComputeEmulationPayload.targets = &deferredTargets;
        // The receiver producer is an early part of G-buffer's one semantic Graphics submission; its dispatch
        // timing shares the existing ticket while the receiver raster retains its own scope in G-buffer.
        opaqueCsgReceiverComputeEmulationPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
        opaqueCsgReceiverComputeEmulationPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        opaqueCsgReceiverComputeEmulationPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        opaqueCsgReceiverComputeEmulationPayload.instanceCount = instanceData.size();
        opaqueCsgReceiverComputeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
        opaqueCsgReceiverComputeEmulationPayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        opaqueCsgReceiverComputeEmulationPayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        opaqueCsgReceiverComputeEmulationPayload.materialFrameStatesGraphOwned =
            gbufferPayload.materialFrameStatesGraphOwned;
        opaqueCsgReceiverComputeEmulationPayload.materialGeometryStatesGraphOwned =
            gbufferPayload.materialGeometryStatesGraphOwned;

        opaqueCsgReceiverComputeEmulationResourceUses.reserve(8u);
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        // The compute pipeline shares CSG's descriptor-visible clip context. It does not write receiver-event
        // images; those remain G-buffer raster outputs and are intentionally absent from this producer's uses.
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationResourceSetUses[3u] = {};
        usize opaqueCsgReceiverComputeEmulationResourceSetUseCount = 0u;
        opaqueCsgReceiverComputeEmulationResourceSetUses[
            opaqueCsgReceiverComputeEmulationResourceSetUseCount++
        ] = gbufferMaterialGeometrySetUse;
        if(gbufferMaterialSampledTextureSet.valid()){
            opaqueCsgReceiverComputeEmulationResourceSetUses[
                opaqueCsgReceiverComputeEmulationResourceSetUseCount++
            ] = gbufferMaterialSampledTextureSetUse;
        }
        opaqueCsgReceiverComputeEmulationResourceSetUses[
            opaqueCsgReceiverComputeEmulationResourceSetUseCount++
        ] = opaqueCsgReceiverComputeEmulationOutputUavSetUse;
        Core::GpuTaskSchedulingHint opaqueCsgReceiverComputeEmulationScheduling;
        opaqueCsgReceiverComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueCsgReceiverComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueCsgReceiverComputeEmulationScheduling.allowPacketMerge = true;
        opaqueCsgReceiverComputeEmulationScheduling.mergeWithPrevious = true;
        // This immediate successor stays with G-buffer under FrontierSafe so its compiler-owned output boundary,
        // existing prefix timing ticket, and accepted semantic range all remain one primary Graphics packet.
        opaqueCsgReceiverComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc opaqueCsgReceiverComputeEmulationDesc;
        opaqueCsgReceiverComputeEmulationDesc
            .setIdentity(Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation"))
            .setMarkerLabel("Opaque CSG Receiver Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(opaqueCsgReceiverComputeEmulationScheduling)
            .setDependencies(&gbufferDependency, 1u)
            .setResourceUses(
                opaqueCsgReceiverComputeEmulationResourceUses.data(),
                opaqueCsgReceiverComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                opaqueCsgReceiverComputeEmulationResourceSetUses,
                opaqueCsgReceiverComputeEmulationResourceSetUseCount
            )
        ;
        m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask
        >(
            opaqueCsgReceiverComputeEmulationDesc,
            Move(opaqueCsgReceiverComputeEmulationPayload)
        );
        if(!m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver compute-emulation producer"));
            return false;
        }
        gbufferDependency = m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask;
    }
    Core::GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = Core::GpuTaskCostHint::Medium;
    gbufferScheduling.forceSubmissionBoundary = false;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;
    // The producer is an explicit immediate predecessor. Preserve its compiler-owned UAV-to-VertexBuffer handoff
    // inside the existing primary-Graphics packet even when FrontierSafe sees an earlier cross-queue consumer.
    gbufferScheduling.allowMergeAcrossConsumerFrontier =
        opaqueComputeEmulationOutputStatesGraphOwned
        || opaqueSharedComputeEmulationOutputStatesGraphOwned
        || opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned
    ;
    Core::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("render.graphics_prefix.gbuffer"))
        .setMarkerLabel("Opaque G-Buffer")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(gbufferScheduling)
        .setDependencies(&gbufferDependency, 1u)
        .setResourceUses(gbufferResourceUses.data(), gbufferResourceUses.size())
        .setResourceSetUses(
            gbufferMaterialResourceSetUseCount != 0u ? gbufferMaterialResourceSetUses : nullptr,
            gbufferMaterialResourceSetUseCount
        )
    ;
    // The shared-output successors are declared after G-buffer has moved its payload into the graph. Keep
    // their frozen readiness/state bits by value instead of reading a moved-from payload below.
    const bool opaqueSharedMaterialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
    const bool opaqueSharedMaterialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned;
    const bool opaqueSharedMaterialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned;
    m_graphicsPrefixGbufferTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::GbufferGraphTask>(
        gbufferDesc,
        Move(gbufferPayload)
    );
    if(!m_graphicsPrefixGbufferTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque G-buffer task"));
        return false;
    }

    Core::GpuTaskId gbufferCompletionTask = m_graphicsPrefixGbufferTask;
    if(opaqueSharedComputeEmulationOutputStatesGraphOwned){
        // These two, three, or four draw items target exactly one imported buffer. Keep every state phase explicit
        // so the compiler, not the material callback, owns Common -> UAV -> VertexBuffer -> ... -> VertexBuffer.
        // The immediate chain is also the semantic ordering contract for the retained persistent output alias.
        opaqueSharedComputeEmulationGenerateResourceUses.reserve(4u);
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
        );
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            WriteUse(opaqueSharedComputeEmulationOutput, Core::ResourceStates::UnorderedAccess)
        );

        opaqueSharedComputeEmulationRasterResourceUses.reserve(8u);
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(opaqueSharedComputeEmulationOutput, Core::ResourceStates::VertexBuffer)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(albedo, Core::ResourceStates::RenderTarget)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(normal, Core::ResourceStates::RenderTarget)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(worldPosition, Core::ResourceStates::RenderTarget)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(depth, Core::ResourceStates::DepthWrite)
        );

        Core::GpuTaskResourceSetUse opaqueSharedComputeEmulationResourceSetUses[2u] = {};
        usize opaqueSharedComputeEmulationResourceSetUseCount = 0u;
        opaqueSharedComputeEmulationResourceSetUses[opaqueSharedComputeEmulationResourceSetUseCount++] =
            gbufferMaterialGeometrySetUse;
        if(gbufferMaterialSampledTextureSet.valid()){
            opaqueSharedComputeEmulationResourceSetUses[opaqueSharedComputeEmulationResourceSetUseCount++] =
                gbufferMaterialSampledTextureSetUse;
        }
        Core::GpuTaskSchedulingHint opaqueSharedComputeEmulationScheduling;
        opaqueSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueSharedComputeEmulationScheduling.allowPacketMerge = true;
        opaqueSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Each phase has an explicit immediate predecessor. Allow that serial chain to remain in the existing
        // primary-Graphics packet when FrontierSafe detects the normal downstream Compute consumer frontier.
        opaqueSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addOpaqueSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &opaqueSharedComputeEmulationPlan,
            &opaqueRegularSharedComputeEmulationTiming,
            &instanceData,
            &materialTypedBytes,
            opaqueSharedMaterialDrawBuffersUploaded,
            opaqueSharedMaterialFrameStatesGraphOwned,
            opaqueSharedMaterialGeometryStatesGraphOwned,
            &opaqueSharedComputeEmulationScheduling,
            timingTicketSlot
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(opaqueSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Payload payload;
            payload.renderer = this;
            payload.targets = &deferredTargets;
            payload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
            payload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
            payload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
            payload.opaqueRegularTiming = &opaqueRegularSharedComputeEmulationTiming;
            payload.plan = opaqueSharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = instanceData.size();
            payload.materialTypedByteCount = materialTypedBytes.size();
            payload.materialDrawBuffersUploaded = opaqueSharedMaterialDrawBuffersUploaded;
            payload.materialFrameStatesGraphOwned = opaqueSharedMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = opaqueSharedMaterialGeometryStatesGraphOwned;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<
                ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask
            >(desc, Move(payload));
        };
        using OpaqueSharedPhase = ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase;
        const Name opaqueSharedComputeEmulationPhaseIdentities[] = {
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_a"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_a"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_b"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_b"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_c"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_c"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_d"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_d"),
        };
        const AStringView opaqueSharedComputeEmulationPhaseMarkers[] = {
            "Opaque Shared Compute Emulation Generate A",
            "Opaque Shared Compute Emulation Raster A",
            "Opaque Shared Compute Emulation Generate B",
            "Opaque Shared Compute Emulation Raster B",
            "Opaque Shared Compute Emulation Generate C",
            "Opaque Shared Compute Emulation Raster C",
            "Opaque Shared Compute Emulation Generate D",
            "Opaque Shared Compute Emulation Raster D",
        };
        const usize opaqueSharedComputeEmulationPhaseCount = opaqueSharedComputeEmulationPlan.drawCount * 2u;
        if(
            opaqueSharedComputeEmulationPhaseCount != 4u
            && opaqueSharedComputeEmulationPhaseCount != 6u
            && opaqueSharedComputeEmulationPhaseCount != 8u
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid shared opaque compute-emulation phase count"));
            return false;
        }
        Core::GpuTaskId opaqueSharedComputeEmulationDependency = m_graphicsPrefixGbufferTask;
        for(usize phaseIndex = 0u;
            phaseIndex < opaqueSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase = phaseIndex % 2u != 0u;
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex] =
                addOpaqueSharedComputeEmulationPhase(
                    opaqueSharedComputeEmulationPhaseIdentities[phaseIndex],
                    opaqueSharedComputeEmulationPhaseMarkers[phaseIndex],
                    opaqueSharedComputeEmulationDependency,
                    isRasterPhase ? OpaqueSharedPhase::Raster : OpaqueSharedPhase::Generate,
                    phaseIndex / 2u,
                    phaseIndex + 1u == opaqueSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? opaqueSharedComputeEmulationRasterResourceUses
                        : opaqueSharedComputeEmulationGenerateResourceUses,
                    opaqueSharedComputeEmulationResourceSetUses,
                    opaqueSharedComputeEmulationResourceSetUseCount
                )
            ;
            if(!m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared opaque compute-emulation phase"));
                return false;
            }
            opaqueSharedComputeEmulationDependency = m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex];
        }
        m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = opaqueSharedComputeEmulationPhaseCount;
        gbufferCompletionTask = opaqueSharedComputeEmulationDependency;
    }
    if(hasOpaqueCsgFrameWork){
        Core::GpuTaskSchedulingHint csgReceiverSpanScheduling;
        csgReceiverSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgReceiverSpanScheduling.forceSubmissionBoundary = false;
        csgReceiverSpanScheduling.allowPacketMerge = true;
        csgReceiverSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgReceiverSpanDesc;
        csgReceiverSpanDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_span"))
            .setMarkerLabel("Opaque CSG Receiver Span")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(csgReceiverSpanScheduling)
            .setDependencies(&gbufferCompletionTask, 1u)
            .setResourceUses(csgReceiverSpanResourceUses.data(), csgReceiverSpanResourceUses.size())
        ;
        m_graphicsPrefixCsgReceiverSpanTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::CsgReceiverSpanBuildGraphTask
        >(
            csgReceiverSpanDesc,
            Move(csgReceiverSpanPayload)
        );
        if(!m_graphicsPrefixCsgReceiverSpanTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver-span task"));
            return false;
        }

        Core::GpuTaskSchedulingHint csgIntervalCombineScheduling;
        csgIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgIntervalCombineScheduling.forceSubmissionBoundary = false;
        csgIntervalCombineScheduling.allowPacketMerge = true;
        csgIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgIntervalCombineDesc;
        csgIntervalCombineDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_combine"))
            .setMarkerLabel("Opaque CSG Interval Combine")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(csgIntervalCombineScheduling)
            .setDependencies(&m_graphicsPrefixCsgReceiverSpanTask, 1u)
            .setResourceUses(csgIntervalCombineResourceUses.data(), csgIntervalCombineResourceUses.size())
        ;
        m_graphicsPrefixCsgIntervalCombineTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::CsgIntervalCombineGraphTask
        >(
            csgIntervalCombineDesc,
            Move(csgIntervalCombinePayload)
        );
        if(!m_graphicsPrefixCsgIntervalCombineTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-combine task"));
            return false;
        }

        Core::Alloc::ScratchArena csgIntervalSampleResourceScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalSampleResourceUses{
            csgIntervalSampleResourceScratch
        };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>
            opaqueCsgIntervalSampleComputeEmulationResourceUses{ csgIntervalSampleResourceScratch };
        Core::GpuGraphResourceSetId csgIntervalSampleMaterialGeometrySet;
        Core::GpuGraphResourceSetId csgIntervalSampleMaterialSampledTextureSet;
        const MaterialPassDrawItems* const csgIntervalSampleMaterialGeometryDrawSets[] = { &opaqueDrawItems.csg };
        const bool csgIntervalSampleUsesMaterialGeometry = !opaqueDrawItems.csg.empty();
        csgIntervalSamplePayload.materialGeometryStatesGraphOwned = csgIntervalSampleUsesMaterialGeometry
            && GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                csgIntervalSampleMaterialGeometryDrawSets,
                LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
                csgIntervalSampleResourceScratch,
                Name("render.graphics_prefix.csg_interval_sample.material_geometry"),
                "Opaque CSG Material Geometry",
                csgIntervalSampleMaterialGeometrySet
            )
        ;
        if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSamplePayload.materialGeometryStatesGraphOwned)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material geometry states"));
        const bool csgIntervalSampleMaterialSampledTexturesCollected = csgIntervalSampleUsesMaterialGeometry
            && GatherPreparedMaterialSampledTextureResourceSet(
                m_materialSystem,
                m_deferredLightingTaskGraph,
                csgIntervalSampleMaterialGeometryDrawSets,
                LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
                csgIntervalSampleResourceScratch,
                Name("render.graphics_prefix.csg_interval_sample.material_sampled_textures"),
                "Opaque CSG Material Sampled Textures",
                csgIntervalSampleMaterialSampledTextureSet
            )
        ;
        if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSampleMaterialSampledTexturesCollected)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material sampled textures"));

        // The CSG interval-sample material stream has a different placement from both regular opaque and receiver
        // CSG work: Combine has already completed, and the following Sample callback rasterizes immediately. That
        // makes pairwise-distinct outputs graph-ownable without excluding earlier regular/receiver aliases.
        const bool opaqueCsgIntervalSampleComputeEmulationPlanCaptured = hasCsgFrameGpuWork
            && csgIntervalSamplePayload.materialFrameStatesGraphOwned
            && csgIntervalSamplePayload.materialGeometryStatesGraphOwned
            && csgIntervalSampleMaterialSampledTexturesCollected
            && opaqueCsgIntervalSampleComputeEmulationPayload.plan.capture(
                m_meshSystem,
                opaqueDrawItems.csg,
                csgFrameData
            )
        ;
        const bool opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned =
            opaqueCsgIntervalSampleComputeEmulationPlanCaptured
            && GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
                m_deferredLightingTaskGraph,
                opaqueCsgIntervalSampleComputeEmulationPayload.plan,
                csgIntervalSampleResourceScratch,
                Name("render.graphics_prefix.opaque_csg_interval_sample_compute_emulation.outputs"),
                "Opaque CSG Interval-Sample Compute Emulation Outputs",
                opaqueCsgIntervalSampleComputeEmulationOutputSet
            )
        ;
        if(
            opaqueCsgIntervalSampleComputeEmulationPlanCaptured
            && !opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned
        ){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare graph-owned opaque CSG interval-sample compute-emulation outputs"
            ));
        }
        csgIntervalSamplePayload.csgComputeEmulationOutputStatesGraphOwned =
            opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
        csgIntervalSamplePayload.opaqueCsgComputeEmulationTiming =
            opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned
                ? &opaqueCsgIntervalSampleComputeEmulationTiming
                : nullptr
        ;
        csgIntervalSampleResourceUses.reserve(
            1u
            + (hasOpaqueDrawItems ? 2u : 0u)
            + (hasCsgFrameGpuWork ? 5u : 0u)
            + 4u
            + 4u
        );
        csgIntervalSampleResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        if(hasOpaqueDrawItems){
            csgIntervalSampleResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
            csgIntervalSampleResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        }
        if(hasCsgFrameGpuWork){
            csgIntervalSampleResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            csgIntervalSampleResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            csgIntervalSampleResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgIntervalSampleResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            csgIntervalSampleResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));
        csgIntervalSampleResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));
        csgIntervalSampleResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));
        csgIntervalSampleResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));
        const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialGeometrySetUse{
            .resourceSet = csgIntervalSampleMaterialGeometrySet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialSampledTextureSetUse{
            .resourceSet = csgIntervalSampleMaterialSampledTextureSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse{
            .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
            .range = {},
            .requiredState = Core::ResourceStates::UnorderedAccess,
            .access = Core::GpuTaskResourceAccess::Write,
        };
        const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse{
            .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
            .range = {},
            .requiredState = Core::ResourceStates::VertexBuffer,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskResourceSetUse csgIntervalSampleMaterialResourceSetUses[3u] = {};
        usize csgIntervalSampleMaterialResourceSetUseCount = 0u;
        if(csgIntervalSamplePayload.materialGeometryStatesGraphOwned){
            csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] =
                csgIntervalSampleMaterialGeometrySetUse;
        }
        if(csgIntervalSampleMaterialSampledTextureSet.valid()){
            csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] =
                csgIntervalSampleMaterialSampledTextureSetUse;
        }
        if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
            csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] =
                opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse;
        }

        Core::GpuTaskId csgIntervalSampleDependency = m_graphicsPrefixCsgIntervalCombineTask;
        if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
            opaqueCsgIntervalSampleComputeEmulationPayload.renderer = this;
            opaqueCsgIntervalSampleComputeEmulationPayload.targets = &deferredTargets;
            // Producer and sample share the semantic CSG interval-sample submission/ticket; the timer opens here
            // and closes after the raster half, exactly preserving the former local material timing scope.
            opaqueCsgIntervalSampleComputeEmulationPayload.timingTicket =
                timingTicketSlot(PrefixTimingSlot::CsgIntervalSample);
            opaqueCsgIntervalSampleComputeEmulationPayload.meshViewSetupReady =
                &m_graphicsPrefixMeshViewSetupReady;
            opaqueCsgIntervalSampleComputeEmulationPayload.sceneShadingSetupReady =
                &m_graphicsPrefixSceneShadingSetupReady;
            opaqueCsgIntervalSampleComputeEmulationPayload.opaqueCsgTiming =
                &opaqueCsgIntervalSampleComputeEmulationTiming;
            opaqueCsgIntervalSampleComputeEmulationPayload.instanceCount = instanceData.size();
            opaqueCsgIntervalSampleComputeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
            opaqueCsgIntervalSampleComputeEmulationPayload.materialDrawBuffersUploaded =
                csgIntervalSamplePayload.materialDrawBuffersUploaded;
            opaqueCsgIntervalSampleComputeEmulationPayload.csgFrameBuffersUploaded =
                csgIntervalSamplePayload.csgFrameBuffersUploaded;
            opaqueCsgIntervalSampleComputeEmulationPayload.intervalSampleImageStatesGraphOwned =
                csgIntervalSamplePayload.intervalSampleImageStatesGraphOwned;
            opaqueCsgIntervalSampleComputeEmulationPayload.csgClipBufferStatesGraphOwned =
                csgIntervalSamplePayload.csgClipBufferStatesGraphOwned;
            opaqueCsgIntervalSampleComputeEmulationPayload.materialFrameStatesGraphOwned =
                csgIntervalSamplePayload.materialFrameStatesGraphOwned;
            opaqueCsgIntervalSampleComputeEmulationPayload.materialGeometryStatesGraphOwned =
                csgIntervalSamplePayload.materialGeometryStatesGraphOwned;

            opaqueCsgIntervalSampleComputeEmulationResourceUses.reserve(12u);
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationResourceSetUses[3u] = {};
            usize opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount = 0u;
            opaqueCsgIntervalSampleComputeEmulationResourceSetUses[
                opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++
            ] = csgIntervalSampleMaterialGeometrySetUse;
            if(csgIntervalSampleMaterialSampledTextureSet.valid()){
                opaqueCsgIntervalSampleComputeEmulationResourceSetUses[
                    opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++
                ] = csgIntervalSampleMaterialSampledTextureSetUse;
            }
            opaqueCsgIntervalSampleComputeEmulationResourceSetUses[
                opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++
            ] = opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse;
            Core::GpuTaskSchedulingHint opaqueCsgIntervalSampleComputeEmulationScheduling;
            opaqueCsgIntervalSampleComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
            opaqueCsgIntervalSampleComputeEmulationScheduling.forceSubmissionBoundary = false;
            opaqueCsgIntervalSampleComputeEmulationScheduling.allowPacketMerge = true;
            opaqueCsgIntervalSampleComputeEmulationScheduling.mergeWithPrevious = true;
            // Combine is the explicit immediate predecessor. The graph preserves its interval-image UAV handoff
            // across any FrontierSafe split; Sample merges with this producer so its generated-vertex handoff and
            // timing scope remain in one accepted Graphics packet.
            opaqueCsgIntervalSampleComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
            Core::GpuTaskDesc opaqueCsgIntervalSampleComputeEmulationDesc;
            opaqueCsgIntervalSampleComputeEmulationDesc
                .setIdentity(Name("render.graphics_prefix.opaque_csg_interval_sample_compute_emulation"))
                .setMarkerLabel("Opaque CSG Interval-Sample Compute Emulation")
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(opaqueCsgIntervalSampleComputeEmulationScheduling)
                .setDependencies(&m_graphicsPrefixCsgIntervalCombineTask, 1u)
                .setResourceUses(
                    opaqueCsgIntervalSampleComputeEmulationResourceUses.data(),
                    opaqueCsgIntervalSampleComputeEmulationResourceUses.size()
                )
                .setResourceSetUses(
                    opaqueCsgIntervalSampleComputeEmulationResourceSetUses,
                    opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount
                )
            ;
            m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask =
                m_deferredLightingTaskGraph.addTask<
                    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask
                >(
                    opaqueCsgIntervalSampleComputeEmulationDesc,
                    Move(opaqueCsgIntervalSampleComputeEmulationPayload)
                )
            ;
            if(!m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare opaque CSG interval-sample compute-emulation producer"
                ));
                return false;
            }
            csgIntervalSampleDependency = m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask;
        }

        Core::GpuTaskSchedulingHint csgIntervalSampleScheduling;
        csgIntervalSampleScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgIntervalSampleScheduling.forceSubmissionBoundary = false;
        csgIntervalSampleScheduling.allowPacketMerge = true;
        csgIntervalSampleScheduling.mergeWithPrevious = true;
        csgIntervalSampleScheduling.allowMergeAcrossConsumerFrontier =
            opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
        Core::GpuTaskDesc csgIntervalSampleDesc;
        csgIntervalSampleDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_sample"))
            .setMarkerLabel("Opaque CSG Interval Sample")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(csgIntervalSampleScheduling)
            .setDependencies(&csgIntervalSampleDependency, 1u)
            .setResourceUses(csgIntervalSampleResourceUses.data(), csgIntervalSampleResourceUses.size())
            .setResourceSetUses(
                csgIntervalSampleMaterialResourceSetUseCount != 0u
                    ? csgIntervalSampleMaterialResourceSetUses
                    : nullptr,
                csgIntervalSampleMaterialResourceSetUseCount
            )
        ;
        m_graphicsPrefixCsgIntervalSampleTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::CsgIntervalSampleGraphTask
        >(
            csgIntervalSampleDesc,
            Move(csgIntervalSamplePayload)
        );
        if(!m_graphicsPrefixCsgIntervalSampleTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-sample task"));
            return false;
        }
        gbufferCompletionTask = m_graphicsPrefixCsgIntervalSampleTask;
    }

    Core::Alloc::ScratchArena normalizeScratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> normalizeResourceUses{ normalizeScratchArena };
    normalizeResourceUses.reserve(8u + (shadowTraceGeometryStatesGraphOwned ? 0u : shadowTraceGeometryResourceCount));
    normalizeResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        // This task actually restores the state after G-buffer, so it owns an outgoing Prefix state seed instead of
        // looking like an optional same-state reader.
        if(!shadowTraceGeometryStatesGraphOwned)
            normalizeResourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowTraceGeometrySetUse{
        .resourceSet = shadowTraceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    Core::GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    normalizeScheduling.forceSubmissionBoundary = false;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("render.graphics_prefix.normalize"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(normalizeScheduling)
        .setDependencies(&gbufferCompletionTask, 1u)
        .setResourceUses(normalizeResourceUses.data(), normalizeResourceUses.size())
        .setResourceSetUses(
            shadowTraceGeometryStatesGraphOwned ? &shadowTraceGeometrySetUse : nullptr,
            shadowTraceGeometryStatesGraphOwned ? 1u : 0u
        )
    ;
    m_graphicsPrefixTask = m_deferredLightingTaskGraph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{
            .raytracingSystem = &m_raytracingSystem,
            .asyncPrefixTiming = &asyncPrefixTiming,
            .timingTicket = timingTicketSlot(PrefixTimingSlot::Normalize),
            .shadowVisibilityTask = &m_deferredShadowVisibilityTask,
        }
    );
    if(!m_graphicsPrefixTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare post-G-buffer normalization task"));
        return false;
    }
    return true;
}


bool RendererSystem::declareDeferredShadowVisibilityTask(
    DeferredFrameTargets& deferredTargets,
    const bool hardwareShadowSupported,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId shadowVisibility,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const softwareTraceGeometryResources,
    const usize softwareTraceGeometryResourceCount,
    const Core::GpuGraphResourceSetId softwareTraceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId prefixTask,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    bool& opaqueProduced,
    bool& transparentTraceProduced,
    u32& opaqueFrameIndex
){
    using namespace __hidden_renderer_task_graph;

    m_deferredShadowVisibilityOpaqueTask = {};
    m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_deferredShadowVisibilityTransparentTraceTask = {};
    m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_deferredShadowVisibilityAdaptiveStatsClearTask = {};
    m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_deferredShadowVisibilityAdaptiveStatsReadbackTask = {};
    m_deferredShadowVisibilityAllLitClearTask = {};
    m_deferredShadowVisibilityTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !prefixTask.valid()
        || (softwareTraceGeometryResourceCount != 0u && !softwareTraceGeometryResources)
    )
        return false;

    opaqueProduced = false;
    transparentTraceProduced = false;
    opaqueFrameIndex = 0u;
    // Only the fully prepared soft-transparent route can expose this boundary. Direct, adaptive/hybrid, and
    // resource-degraded paths retain the established monolithic Shadow Visibility callback.
    const bool preparedSoftTransparentFoldCandidate =
        m_rayTracingState.m_softShadowReady
        && m_rayTracingState.m_softShadowSlotMask != 0u
        && m_raytracingSystem.softTransparentShadowReady()
        && deferredTargets.shadowCoarseTransmittance
        && deferredTargets.shadowSoftHalfA
        && deferredTargets.shadowSoftHalfB
        && deferredTargets.shadowSoftGeometry
        && deferredTargets.shadowSoftGeometryPrev
        && deferredTargets.shadowHistA
        && deferredTargets.shadowHistB
        && deferredTargets.shadowMomentsA
        && deferredTargets.shadowMomentsB
        && deferredTargets.transparentSoftHalf
        && deferredTargets.transparentHistA
        && deferredTargets.transparentHistB
        && deferredTargets.transparentMomentsA
        && deferredTargets.transparentMomentsB
        && m_rayTracingState.m_sceneBvhNodeBuffer
        && m_rayTracingState.m_sceneInstanceBuffer
        && m_rayTracingState.m_shadowInstanceMaterialBuffer
        && m_rayTracingState.m_shadowMaterialTypedBuffer
        && m_rayTracingState.m_shadowInstanceBuffer
        && materialContextSlots.valid()
        && softwareTraceGeometryResourceCount != 0u
    ;
    bool graphOwnedSoftTransparentFoldEnabled = true;
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    graphOwnedSoftTransparentFoldEnabled = m_graphOwnedSoftTransparentShadowFoldEnabledForTesting;
    if(
        preparedSoftTransparentFoldCandidate
        && m_graphOwnedSoftTransparentShadowFoldBenchmarkForTesting
        && !m_reportedGraphOwnedSoftTransparentShadowFoldBenchmarkForTesting
    ){
        NWB_LOGGER_ESSENTIAL_INFO(
            graphOwnedSoftTransparentFoldEnabled
                ? NWB_TEXT("RendererSystem: graph-owned soft-transparent shadow-fold benchmark path active")
                : NWB_TEXT("RendererSystem: retained monolithic soft-transparent shadow-fold benchmark path active")
        );
        m_reportedGraphOwnedSoftTransparentShadowFoldBenchmarkForTesting = true;
    }
#endif
    const bool splitSoftTransparentFold = preparedSoftTransparentFoldCandidate && graphOwnedSoftTransparentFoldEnabled;
    // The adaptive fallback remains in the monolithic callback, but its raw buffer primitives are deterministic
    // from this frozen route.  Lift only the work that actually exists this frame; a non-adaptive frame retains
    // its native compatibility behavior and does not gain empty graph nodes.
    GraphOwnedAdaptiveShadowPrimitivePlan graphOwnedAdaptivePrimitives;
    const bool graphOwnedAdaptivePrimitiveCandidate =
        !splitSoftTransparentFold
        && m_rayTracingState.m_swShadowAdaptiveEnabled
        && !m_raytracingSystem.softTransparentShadowReady()
        && (
            hardwareShadowSupported
                ? m_raytracingSystem.hybridTransparentShadowReady()
                : m_raytracingSystem.shadowVisibilitySoftwareResourcesPreflighted()
        )
        && m_rayTracingState.m_swShadowEdgeStatsBuffer
        && m_rayTracingState.m_swShadowEdgeStatsReadback
        && m_rayTracingState.m_swShadowEdgeCounterBuffer
    ;
    if(graphOwnedAdaptivePrimitiveCandidate){
        graphOwnedAdaptivePrimitives.compact = m_rayTracingState.m_swShadowCompactEnabled;
        graphOwnedAdaptivePrimitives.statsTick = m_rayTracingState.m_swShadowEdgeStatsTick;
        graphOwnedAdaptivePrimitives.captureStatsSnapshot =
            m_rayTracingState.m_swShadowEdgeStatsEnabled
            && !m_rayTracingState.m_swShadowEdgeStatsPending
            && (graphOwnedAdaptivePrimitives.statsTick % s_SwShadowEdgeStatsPeriod == 0u)
        ;
        graphOwnedAdaptivePrimitives.enabled =
            graphOwnedAdaptivePrimitives.compact
            || graphOwnedAdaptivePrimitives.captureStatsSnapshot
        ;
    }
    // The opaque temporal merge runs before the transparent tail and shares its history selector. Freeze its exact
    // input/output pair while the compiled packet owns the prepared temporal route.
    const bool graphOwnsOpaqueTemporalMergeEntryStates =
        splitSoftTransparentFold
        && m_rayTracingState.m_softShadowTemporalReady
    ;
    const bool opaqueHistoryFrontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.shadow_visibility.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(!sceneGeometryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred shadow-visibility graph resources"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    const bool softwareTraceGeometryStatesGraphOwned = softwareTraceGeometrySet.valid();
    resourceUses.reserve(40u + (
        softwareTraceGeometryStatesGraphOwned
            ? 0u
            : softwareTraceGeometryResourceCount
    ));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    // Shadow visibility samples the bindless depth image, so its declared layout must match the native shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // The split opaque producer overwrites visibility; the retained monolith may also fold hybrid transparency in
    // place, so it remains ReadWrite on every compatibility route.
    resourceUses.push_back(splitSoftTransparentFold
        ? WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)
        : ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)
    );
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const bool isInput){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        if(graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(isInput
                ? ReadUse(resource, Core::ResourceStates::ShaderResource)
                : WriteUse(resource, Core::ResourceStates::UnorderedAccess)
            );
        else
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalStaticReadTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(graphOwnsOpaqueTemporalMergeEntryStates
            ? ReadUse(resource, Core::ResourceStates::ShaderResource)
            : ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess)
        );
        return true;
    };
    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        appendOptionalReadWriteTexture(
            deferredTargets.shadowCoarseTransmittance,
            Name("render.shadow_visibility.coarse_transmittance"),
            "Shadow Coarse Transmittance"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftHalfA,
            Name("render.shadow_visibility.soft_half_a"),
            "Shadow Soft Half A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftHalfB,
            Name("render.shadow_visibility.soft_half_b"),
            "Shadow Soft Half B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftGeometry,
            Name("render.shadow_visibility.soft_geometry"),
            "Shadow Soft Geometry"
        )
        && appendOptionalOpaqueTemporalStaticReadTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A",
            opaqueHistoryFrontIsA
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B",
            !opaqueHistoryFrontIsA
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A",
            opaqueHistoryFrontIsA
        )
        && appendOptionalOpaqueTemporalHistoryTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B",
            !opaqueHistoryFrontIsA
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentSoftHalf,
            Name("render.shadow_visibility.transparent_soft_half"),
            "Transparent Shadow Soft Half"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentHistA,
            Name("render.shadow_visibility.transparent_history_a"),
            "Transparent Shadow History A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentHistB,
            Name("render.shadow_visibility.transparent_history_b"),
            "Transparent Shadow History B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentMomentsA,
            Name("render.shadow_visibility.transparent_moments_a"),
            "Transparent Shadow Moments A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentMomentsB,
            Name("render.shadow_visibility.transparent_moments_b"),
            "Transparent Shadow Moments B"
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
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeStatsBuffer,
            Name("render.shadow_visibility.edge_stats"),
            "Shadow Edge Statistics",
            Core::ResourceStates::UnorderedAccess
        )
        && (
            graphOwnedAdaptivePrimitives.enabled
            || appendOptionalWriteBuffer(
                m_rayTracingState.m_swShadowEdgeStatsReadback,
                Name("render.shadow_visibility.edge_stats_readback"),
                "Shadow Edge Statistics Readback",
                Core::ResourceStates::CopyDest
            )
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeCounterBuffer,
            Name("render.shadow_visibility.edge_counter"),
            "Shadow Edge Counter",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeListBuffer,
            Name("render.shadow_visibility.edge_list"),
            "Shadow Edge List",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowIndirectArgsBuffer,
            Name("render.shadow_visibility.indirect_args"),
            "Shadow Indirect Arguments",
            Core::ResourceStates::UnorderedAccess
        )
    ;
    if(m_rayTracingState.m_tlas){
        const Core::GpuGraphResourceId tlas = m_deferredLightingTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
        );
        const Core::GpuGraphResourceId tlasBackingBuffer = importBuffer(
            m_rayTracingState.m_tlas->getBackingBufferHandle(),
            Name("render.deferred_effects.tlas_backing"),
            "Scene TLAS Backing"
        );
        optionalResourcesImported = optionalResourcesImported && tlas.valid() && tlasBackingBuffer.valid();
        if(tlas.valid() && tlasBackingBuffer.valid()){
            resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            // setAccelStructState lowers through this buffer; importing it explicitly keeps declaration-driven
            // external state seeding and ownership handoff complete.
            resourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred shadow-visibility dynamic resource"));
        return false;
    }
    Core::GpuGraphResourceId adaptiveEdgeStats;
    Core::GpuGraphResourceId adaptiveEdgeStatsReadback;
    Core::GpuGraphResourceId adaptiveEdgeCounter;
    if(graphOwnedAdaptivePrimitives.enabled){
        adaptiveEdgeStats = importBuffer(
            m_rayTracingState.m_swShadowEdgeStatsBuffer,
            Name("render.shadow_visibility.edge_stats"),
            "Shadow Edge Statistics"
        );
        adaptiveEdgeStatsReadback = importBuffer(
            m_rayTracingState.m_swShadowEdgeStatsReadback,
            Name("render.shadow_visibility.edge_stats_readback"),
            "Shadow Edge Statistics Readback"
        );
        adaptiveEdgeCounter = importBuffer(
            m_rayTracingState.m_swShadowEdgeCounterBuffer,
            Name("render.shadow_visibility.edge_counter"),
            "Shadow Edge Counter"
        );
        if(
            !adaptiveEdgeStats.valid()
            || !adaptiveEdgeStatsReadback.valid()
            || !adaptiveEdgeCounter.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graph-owned adaptive shadow primitive resources"));
            return false;
        }
    }
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!softwareTraceGeometryStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex){
            const Core::GpuGraphResourceId resource = softwareTraceGeometryResources[resourceIndex];
            if(!resource.valid())
                return false;
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
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
    Core::GpuTaskResourceSetUse traceResourceSetUses[2u] = {};
    usize traceResourceSetUseCount = 0u;
    if(softwareTraceGeometryStatesGraphOwned)
        traceResourceSetUses[traceResourceSetUseCount++] = softwareTraceGeometrySetUse;
    if(traceMaterialSampledTextureSet.valid())
        traceResourceSetUses[traceResourceSetUseCount++] = traceMaterialSampledTextureSetUse;

    // The prepared soft path keeps the opaque first wavelet, resolve tail, transparent trace, and temporal/RGB
    // resolve as adjacent callbacks. Re-importing retains shared graph identities while making each graph-owned
    // handoff explicit.
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueFirstWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueResolveResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTraceResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTemporalMergeResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentFirstWaveletResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentFoldResourceUses{ scratchArena };
    bool graphOwnsTransparentTemporalMergeEntryStates = false;
    if(splitSoftTransparentFold){
        const Core::GpuGraphResourceId shadowCoarseTransmittance = importTexture(
            deferredTargets.shadowCoarseTransmittance,
            Name("render.shadow_visibility.coarse_transmittance"),
            "Shadow Coarse Transmittance"
        );
        const Core::GpuGraphResourceId shadowSoftHalfA = importTexture(
            deferredTargets.shadowSoftHalfA,
            Name("render.shadow_visibility.soft_half_a"),
            "Shadow Soft Half A"
        );
        const Core::GpuGraphResourceId shadowSoftHalfB = importTexture(
            deferredTargets.shadowSoftHalfB,
            Name("render.shadow_visibility.soft_half_b"),
            "Shadow Soft Half B"
        );
        const Core::GpuGraphResourceId shadowSoftGeometry = importTexture(
            deferredTargets.shadowSoftGeometry,
            Name("render.shadow_visibility.soft_geometry"),
            "Shadow Soft Geometry"
        );
        const Core::GpuGraphResourceId shadowSoftGeometryPrevious = importTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        );
        const Core::GpuGraphResourceId opaqueHistoryA = importTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A"
        );
        const Core::GpuGraphResourceId opaqueHistoryB = importTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B"
        );
        const Core::GpuGraphResourceId opaqueMomentsA = importTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A"
        );
        const Core::GpuGraphResourceId opaqueMomentsB = importTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B"
        );
        const Core::GpuGraphResourceId transparentSoftHalf = importTexture(
            deferredTargets.transparentSoftHalf,
            Name("render.shadow_visibility.transparent_soft_half"),
            "Transparent Shadow Soft Half"
        );
        const Core::GpuGraphResourceId transparentHistoryA = importTexture(
            deferredTargets.transparentHistA,
            Name("render.shadow_visibility.transparent_history_a"),
            "Transparent Shadow History A"
        );
        const Core::GpuGraphResourceId transparentHistoryB = importTexture(
            deferredTargets.transparentHistB,
            Name("render.shadow_visibility.transparent_history_b"),
            "Transparent Shadow History B"
        );
        const Core::GpuGraphResourceId transparentMomentsA = importTexture(
            deferredTargets.transparentMomentsA,
            Name("render.shadow_visibility.transparent_moments_a"),
            "Transparent Shadow Moments A"
        );
        const Core::GpuGraphResourceId transparentMomentsB = importTexture(
            deferredTargets.transparentMomentsB,
            Name("render.shadow_visibility.transparent_moments_b"),
            "Transparent Shadow Moments B"
        );
        const Core::GpuGraphResourceId sceneBvhNodes = importBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes"
        );
        const Core::GpuGraphResourceId sceneInstances = importBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances"
        );
        const Core::GpuGraphResourceId shadowInstanceMaterials = importBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials"
        );
        const Core::GpuGraphResourceId shadowTypedMaterials = importBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials"
        );
        const Core::GpuGraphResourceId shadowInstances = importBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances"
        );
        if(
            !shadowCoarseTransmittance.valid()
            || !shadowSoftHalfA.valid()
            || !shadowSoftHalfB.valid()
            || !shadowSoftGeometry.valid()
            || !shadowSoftGeometryPrevious.valid()
            || !opaqueHistoryA.valid()
            || !opaqueHistoryB.valid()
            || !opaqueMomentsA.valid()
            || !opaqueMomentsB.valid()
            || !transparentSoftHalf.valid()
            || !transparentHistoryA.valid()
            || !transparentHistoryB.valid()
            || !transparentMomentsA.valid()
            || !transparentMomentsB.valid()
            || !sceneBvhNodes.valid()
            || !sceneInstances.valid()
            || !shadowInstanceMaterials.valid()
            || !shadowTypedMaterials.valid()
            || !shadowInstances.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared soft-transparent shadow-fold resources"));
            return false;
        }

        opaqueFirstWaveletResourceUses.reserve(12u);
        // The compiler lowers the opaque trace from UAV to the exact sampled state required by temporal merge or
        // the first wavelet. The first wavelet then publishes half-B for the resolve tail.
        opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
        opaqueFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        if(graphOwnsOpaqueTemporalMergeEntryStates){
            // The frozen selector permits exact selected input/output declarations for the opaque temporal merge.
            const Core::GpuGraphResourceId opaqueHistoryIn = opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB;
            const Core::GpuGraphResourceId opaqueMomentsIn = opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB;
            const Core::GpuGraphResourceId opaqueHistoryOut = opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA;
            const Core::GpuGraphResourceId opaqueMomentsOut = opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA;
            opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
            opaqueFirstWaveletResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
            opaqueFirstWaveletResourceUses.push_back(ReadUse(opaqueHistoryIn, Core::ResourceStates::ShaderResource));
            opaqueFirstWaveletResourceUses.push_back(ReadUse(opaqueMomentsIn, Core::ResourceStates::ShaderResource));
            opaqueFirstWaveletResourceUses.push_back(WriteUse(opaqueHistoryOut, Core::ResourceStates::UnorderedAccess));
            opaqueFirstWaveletResourceUses.push_back(WriteUse(opaqueMomentsOut, Core::ResourceStates::UnorderedAccess));
        }

        opaqueResolveResourceUses.reserve(8u);
        // With the current one-wavelet opaque resolve, the tail only samples the first-wavelet half-B result for
        // upsample. Keep a conservative native ping-pong declaration if that compile-time pass count grows.
        if(NWB_SHADOW_RESOLVE_PASS_COUNT == 1u)
            opaqueResolveResourceUses.push_back(ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource));
        else{
            opaqueResolveResourceUses.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            opaqueResolveResourceUses.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        }
        opaqueResolveResourceUses.push_back(WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
        opaqueResolveResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        opaqueResolveResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));

        transparentTraceResourceUses.reserve(20u + (
            softwareTraceGeometryStatesGraphOwned
                ? 0u
                : softwareTraceGeometryResourceCount
        ));
        transparentTraceResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        // The opaque soft result -> transparent trace edge remains a same-UAV barrier in this packet.
        transparentTraceResourceUses.push_back(ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
        transparentTraceResourceUses.push_back(ReadUse(sceneGeometryDomain));
        transparentTraceResourceUses.push_back(ReadWriteUse(shadowCoarseTransmittance, Core::ResourceStates::UnorderedAccess));
        transparentTraceResourceUses.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
        transparentTraceResourceUses.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        transparentTraceResourceUses.push_back(WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess));
        transparentTraceResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        transparentTraceResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        transparentTraceResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
        if(!softwareTraceGeometryStatesGraphOwned){
            for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex)
                transparentTraceResourceUses.push_back(ReadUse(softwareTraceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
        }

        graphOwnsTransparentTemporalMergeEntryStates = m_rayTracingState.m_softTransparentTemporalReady;
        const bool transparentHistoryFrontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
        const Core::GpuGraphResourceId transparentHistoryIn = transparentHistoryFrontIsA
            ? transparentHistoryA
            : transparentHistoryB
        ;
        const Core::GpuGraphResourceId transparentMomentsIn = transparentHistoryFrontIsA
            ? transparentMomentsA
            : transparentMomentsB
        ;
        const Core::GpuGraphResourceId transparentHistoryOut = transparentHistoryFrontIsA
            ? transparentHistoryB
            : transparentHistoryA
        ;
        const Core::GpuGraphResourceId transparentMomentsOut = transparentHistoryFrontIsA
            ? transparentMomentsB
            : transparentMomentsA
        ;
        if(graphOwnsTransparentTemporalMergeEntryStates){
            // Selection is frozen with the compiled frame. The merge samples the current front pair and publishes
            // the opposite pair, which the following wavelet receives as graph-owned sampled inputs.
            transparentTemporalMergeResourceUses.reserve(8u);
            transparentTemporalMergeResourceUses.push_back(ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(transparentHistoryIn, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource));
            transparentTemporalMergeResourceUses.push_back(WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess));
            transparentTemporalMergeResourceUses.push_back(WriteUse(transparentMomentsOut, Core::ResourceStates::UnorderedAccess));

            transparentFirstWaveletResourceUses.reserve(4u);
            transparentFirstWaveletResourceUses.push_back(ReadUse(transparentHistoryOut, Core::ResourceStates::ShaderResource));
            transparentFirstWaveletResourceUses.push_back(ReadUse(transparentMomentsOut, Core::ResourceStates::ShaderResource));
            transparentFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            transparentFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        }else{
            // Inactive temporal frames keep their existing trace-to-wavelet route and do not acquire stale-history
            // declarations or a no-op merge callback.
            transparentFirstWaveletResourceUses.reserve(3u);
            transparentFirstWaveletResourceUses.push_back(ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource));
            transparentFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            transparentFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        }
        transparentFoldResourceUses.reserve(8u);
        // With one RGB wavelet the terminal task only samples half-A before multiplying visibility. Preserve a
        // native ping-pong declaration if the compile-time pass count grows.
        if(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u)
            transparentFoldResourceUses.push_back(ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource));
        else{
            transparentFoldResourceUses.push_back(ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
            transparentFoldResourceUses.push_back(ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));
        }
        transparentFoldResourceUses.push_back(ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
        transparentFoldResourceUses.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        transparentFoldResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    }

    if(splitSoftTransparentFold){
        Core::GpuTaskSchedulingHint opaqueScheduling;
        opaqueScheduling.cost = Core::GpuTaskCostHint::Large;
        opaqueScheduling.forceSubmissionBoundary = false;
        opaqueScheduling.allowPacketMerge = true;
        opaqueScheduling.mergeWithPrevious = false;
        Core::GpuTaskDesc opaqueDesc;
        opaqueDesc
            .setIdentity(Name("render.shadow_visibility.opaque"))
            .setMarkerLabel("Shadow Visibility Opaque")
            .setQueue(ComputeTransferPacketQueueRequest())
            .setScheduling(opaqueScheduling)
            .setDependencies(&prefixTask, 1u)
            .setResourceUses(resourceUses.data(), resourceUses.size())
            .setResourceSetUses(
                softwareTraceGeometryStatesGraphOwned ? &softwareTraceGeometrySetUse : nullptr,
                softwareTraceGeometryStatesGraphOwned ? 1u : 0u
            )
        ;
        m_deferredShadowVisibilityOpaqueTask = m_raytracingSystem.declareShadowVisibilityOpaqueTask(
            m_deferredLightingTaskGraph,
            opaqueDesc,
            deferredTargets,
            &m_preparedShadowVisibilityReady,
            hardwareShadowSupported,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &opaqueProduced,
            &opaqueFrameIndex,
            true,
            graphOwnsOpaqueTemporalMergeEntryStates
        );
        if(!m_deferredShadowVisibilityOpaqueTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque shadow-visibility graph task"));
            return false;
        }

        Core::GpuTaskSchedulingHint tailScheduling;
        tailScheduling.cost = Core::GpuTaskCostHint::Medium;
        tailScheduling.forceSubmissionBoundary = false;
        tailScheduling.allowPacketMerge = true;
        tailScheduling.mergeWithPrevious = true;
        const Core::GpuTaskId opaqueFirstWaveletDependencies[] = { m_deferredShadowVisibilityOpaqueTask };
        Core::GpuTaskDesc opaqueFirstWaveletDesc;
        opaqueFirstWaveletDesc
            .setIdentity(Name("render.shadow_visibility.opaque_first_wavelet"))
            .setMarkerLabel("Shadow Opaque First Wavelet")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(opaqueFirstWaveletDependencies, LengthOf(opaqueFirstWaveletDependencies))
            .setResourceUses(opaqueFirstWaveletResourceUses.data(), opaqueFirstWaveletResourceUses.size())
        ;
        m_deferredShadowVisibilityOpaqueFirstWaveletTask = m_raytracingSystem.declareShadowVisibilityOpaqueFirstWaveletTask(
            m_deferredLightingTaskGraph,
            opaqueFirstWaveletDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &opaqueResolveTiming,
            &opaqueProduced,
            &opaqueFrameIndex,
            hardwareShadowSupported,
            true,
            graphOwnsOpaqueTemporalMergeEntryStates
        );
        if(!m_deferredShadowVisibilityOpaqueFirstWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow first-wavelet graph task"));
            return false;
        }

        const Core::GpuTaskId opaqueResolveDependencies[] = { m_deferredShadowVisibilityOpaqueFirstWaveletTask };
        Core::GpuTaskDesc opaqueResolveDesc;
        opaqueResolveDesc
            .setIdentity(Name("render.shadow_visibility.opaque_soft_resolve"))
            .setMarkerLabel("Shadow Opaque Soft Resolve")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(opaqueResolveDependencies, LengthOf(opaqueResolveDependencies))
            .setResourceUses(opaqueResolveResourceUses.data(), opaqueResolveResourceUses.size())
        ;
        m_deferredShadowVisibilityOpaqueResolveTask = m_raytracingSystem.declareShadowVisibilityOpaqueResolveTailTask(
            m_deferredLightingTaskGraph,
            opaqueResolveDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &opaqueResolveTiming,
            &opaqueProduced,
            &opaqueFrameIndex,
            hardwareShadowSupported,
            true
        );
        if(!m_deferredShadowVisibilityOpaqueResolveTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow resolve-tail graph task"));
            return false;
        }

        const Core::GpuTaskId traceDependencies[] = { m_deferredShadowVisibilityOpaqueResolveTask };
        Core::GpuTaskDesc traceDesc;
        traceDesc
            .setIdentity(Name("render.shadow_visibility.soft_transparent_trace"))
            .setMarkerLabel("Shadow Transparent Soft Trace")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(traceDependencies, LengthOf(traceDependencies))
            .setResourceUses(transparentTraceResourceUses.data(), transparentTraceResourceUses.size())
            .setResourceSetUses(
                traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
                traceResourceSetUseCount
            )
        ;
        m_deferredShadowVisibilityTransparentTraceTask = m_raytracingSystem.declareShadowTransparentSoftTraceTask(
            m_deferredLightingTaskGraph,
            traceDesc,
            deferredTargets,
            timingTicket,
            &opaqueProduced,
            &opaqueFrameIndex,
            &transparentTraceProduced,
            true
        );
        if(!m_deferredShadowVisibilityTransparentTraceTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow trace graph task"));
            return false;
        }

        Core::GpuTaskId transparentFirstWaveletDependency = m_deferredShadowVisibilityTransparentTraceTask;
        if(graphOwnsTransparentTemporalMergeEntryStates){
            const Core::GpuTaskId transparentTemporalMergeDependencies[] = {
                m_deferredShadowVisibilityTransparentTraceTask,
            };
            Core::GpuTaskDesc transparentTemporalMergeDesc;
            transparentTemporalMergeDesc
                .setIdentity(Name("render.shadow_visibility.transparent_temporal_merge"))
                .setMarkerLabel("Shadow Transparent Temporal Merge")
                .setQueue(ComputeQueueRequest())
                .setScheduling(tailScheduling)
                .setDependencies(transparentTemporalMergeDependencies, LengthOf(transparentTemporalMergeDependencies))
                .setResourceUses(
                    transparentTemporalMergeResourceUses.data(),
                    transparentTemporalMergeResourceUses.size()
                )
            ;
            m_deferredShadowVisibilityTransparentTemporalMergeTask =
                m_raytracingSystem.declareShadowTransparentSoftTemporalMergeTask(
                    m_deferredLightingTaskGraph,
                    transparentTemporalMergeDesc,
                    deferredTargets,
                    timingTicket,
                    &transparentResolveTiming,
                    &opaqueProduced,
                    &transparentTraceProduced,
                    &opaqueFrameIndex,
                    true,
                    true
                )
            ;
            if(!m_deferredShadowVisibilityTransparentTemporalMergeTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow temporal-merge graph task"));
                return false;
            }
            transparentFirstWaveletDependency = m_deferredShadowVisibilityTransparentTemporalMergeTask;
        }

        const Core::GpuTaskId transparentFirstWaveletDependencies[] = { transparentFirstWaveletDependency };
        Core::GpuTaskDesc transparentFirstWaveletDesc;
        transparentFirstWaveletDesc
            .setIdentity(Name("render.shadow_visibility.transparent_first_wavelet"))
            .setMarkerLabel("Shadow Transparent First Wavelet")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(transparentFirstWaveletDependencies, LengthOf(transparentFirstWaveletDependencies))
            .setResourceUses(transparentFirstWaveletResourceUses.data(), transparentFirstWaveletResourceUses.size())
        ;
        m_deferredShadowVisibilityTransparentFirstWaveletTask = m_raytracingSystem.declareShadowTransparentSoftFirstWaveletTask(
            m_deferredLightingTaskGraph,
            transparentFirstWaveletDesc,
            deferredTargets,
            timingTicket,
            &transparentResolveTiming,
            &opaqueProduced,
            &transparentTraceProduced,
            &opaqueFrameIndex,
            true,
            true,
            !graphOwnsTransparentTemporalMergeEntryStates
        );
        if(!m_deferredShadowVisibilityTransparentFirstWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow first-wavelet graph task"));
            return false;
        }

        const Core::GpuTaskId foldDependencies[] = { m_deferredShadowVisibilityTransparentFirstWaveletTask };
        Core::GpuTaskDesc foldDesc;
        foldDesc
            .setIdentity(Name("render.shadow_visibility.soft_transparent_fold"))
            .setMarkerLabel("Shadow Transparent Soft Fold")
            .setQueue(ComputeQueueRequest())
            .setScheduling(tailScheduling)
            .setDependencies(foldDependencies, LengthOf(foldDependencies))
            .setResourceUses(transparentFoldResourceUses.data(), transparentFoldResourceUses.size())
        ;
        // This terminal ID deliberately replaces the opaque producer and transparent trace as the effect/output
        // owner. Existing caustics, lighting, state-handoff, recovery, and acceptance paths therefore observe the
        // fully folded visibility.
        m_deferredShadowVisibilityTask = m_raytracingSystem.declareShadowTransparentSoftFoldTask(
            m_deferredLightingTaskGraph,
            foldDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            &shadowVisibilityTiming,
            &transparentResolveTiming,
            &opaqueProduced,
            &transparentTraceProduced,
            &opaqueFrameIndex,
            true
        );
        if(!m_deferredShadowVisibilityTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred soft-transparent shadow-fold graph task"));
            return false;
        }
        return true;
    }

    Core::GpuTaskId shadowVisibilityDependency = prefixTask;
    bool adaptivePrimitivePrecedesVisibility = false;
    if(graphOwnedAdaptivePrimitives.enabled){
        // Counter/stat buffers are private adaptive scratch.  Their exact CopyDest -> UAV handoff is now lowered by
        // the compiler before the retained Shadow Visibility callback, while the callback still decides at record
        // time whether the adaptive producer actually ran.
        Core::GpuTaskSchedulingHint primitiveScheduling;
        primitiveScheduling.cost = Core::GpuTaskCostHint::Tiny;
        primitiveScheduling.forceSubmissionBoundary = false;
        primitiveScheduling.allowPacketMerge = true;

        if(graphOwnedAdaptivePrimitives.captureStatsSnapshot){
            Core::GpuTaskDesc statsClearDesc;
            statsClearDesc
                .setIdentity(Name("render.shadow_visibility.adaptive_stats_clear"))
                .setMarkerLabel("Shadow Adaptive Statistics Clear")
                .setQueue(ComputeTransferPacketQueueRequest())
                .setScheduling(primitiveScheduling)
                .setDependencies(&shadowVisibilityDependency, 1u)
            ;
            m_deferredShadowVisibilityAdaptiveStatsClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                statsClearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = adaptiveEdgeStats,
                    .clearValue = 0u,
                }
            );
            if(!m_deferredShadowVisibilityAdaptiveStatsClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow statistics clear"));
                return false;
            }
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveStatsClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }

        if(graphOwnedAdaptivePrimitives.compact){
            Core::GpuTaskSchedulingHint counterClearScheduling = primitiveScheduling;
            counterClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
            Core::GpuTaskDesc counterClearDesc;
            counterClearDesc
                .setIdentity(Name("render.shadow_visibility.adaptive_counter_clear"))
                .setMarkerLabel("Shadow Adaptive Counter Clear")
                .setQueue(ComputeTransferPacketQueueRequest())
                .setScheduling(counterClearScheduling)
                .setDependencies(&shadowVisibilityDependency, 1u)
            ;
            m_deferredShadowVisibilityAdaptiveCounterClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                counterClearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = adaptiveEdgeCounter,
                    .clearValue = 0u,
                }
            );
            if(!m_deferredShadowVisibilityAdaptiveCounterClearTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow counter clear"));
                return false;
            }
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveCounterClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }
    }

    // Preparedness is established while Shadow Prepare records, after this graph is declared.  The normal
    // monolithic path therefore begins with an unconditional all-lit clear: real shadow producers overwrite it,
    // while a missing producer retains the same white fallback without a callback-local native state bridge.
    // Keep this typed Transfer primitive on the same selected Compute/Graphics packet as Shadow Visibility.
    Core::GpuTaskSchedulingHint allLitClearScheduling;
    allLitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    allLitClearScheduling.forceSubmissionBoundary = false;
    allLitClearScheduling.allowPacketMerge = true;
    allLitClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
    Core::GpuTaskDesc allLitClearDesc;
    allLitClearDesc
        .setIdentity(Name("render.shadow_visibility.all_lit_clear"))
        .setMarkerLabel("Shadow Visibility All-Lit Clear")
        .setQueue(ComputeTransferPacketQueueRequest())
        .setScheduling(allLitClearScheduling)
        .setDependencies(&shadowVisibilityDependency, 1u)
    ;
    m_deferredShadowVisibilityAllLitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        allLitClearDesc,
        Core::GpuClearTextureTaskDesc{
            .destination = shadowVisibility,
            .subresources = ECSRenderDetail::s_ShadowVisibilitySubresources,
            .valueType = Core::GpuClearTextureTaskValueType::Float,
            .floatValue = Core::Color(1.f, 1.f, 1.f, 1.f),
        }
    );
    if(!m_deferredShadowVisibilityAllLitClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned all-lit shadow-visibility clear"));
        return false;
    }
    shadowVisibilityDependency = m_deferredShadowVisibilityAllLitClearTask;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(ComputeTransferPacketQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&shadowVisibilityDependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(
            traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
            traceResourceSetUseCount
        )
    ;
    m_deferredShadowVisibilityTask = m_raytracingSystem.declareShadowVisibilityTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        &m_preparedShadowVisibilityReady,
        hardwareShadowSupported,
        timingTicket,
        true,
        true,
        graphOwnedAdaptivePrimitives
    );
    if(!m_deferredShadowVisibilityTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred shadow-visibility graph task"));
        return false;
    }
    if(graphOwnedAdaptivePrimitives.captureStatsSnapshot){
        const Core::GpuCopyBufferTaskRegion statsReadbackRegion{
            .source = adaptiveEdgeStats,
            .destination = adaptiveEdgeStatsReadback,
            .dataSizeBytes = static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT),
        };
        Core::GpuTaskSchedulingHint statsReadbackScheduling;
        statsReadbackScheduling.cost = Core::GpuTaskCostHint::Tiny;
        statsReadbackScheduling.forceSubmissionBoundary = false;
        statsReadbackScheduling.allowPacketMerge = true;
        statsReadbackScheduling.mergeWithPrevious = true;
        // Lighting/caustics consume Shadow Visibility on another physical queue.  This immediate dependent is the
        // semantic terminal of the same packet, so it may intentionally close that consumer frontier.
        statsReadbackScheduling.allowMergeAcrossConsumerFrontier = true;
        const Core::GpuTaskId statsReadbackDependencies[] = { m_deferredShadowVisibilityTask };
        Core::GpuTaskDesc statsReadbackDesc;
        statsReadbackDesc
            .setIdentity(Name("render.shadow_visibility.adaptive_stats_readback"))
            .setMarkerLabel("Shadow Adaptive Statistics Readback")
            .setQueue(ComputeTransferPacketQueueRequest())
            .setScheduling(statsReadbackScheduling)
            .setDependencies(statsReadbackDependencies, LengthOf(statsReadbackDependencies))
        ;
        m_deferredShadowVisibilityAdaptiveStatsReadbackTask = m_deferredLightingTaskGraph.addCopyBufferTask(
            statsReadbackDesc,
            Core::GpuCopyBufferTaskDesc{
                .regions = &statsReadbackRegion,
                .regionCount = 1u,
            }
        );
        if(!m_deferredShadowVisibilityAdaptiveStatsReadbackTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow statistics readback"));
            return false;
        }
    }
    return true;
}


bool RendererSystem::declareDeferredSoftwareCausticsTask(
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
    using namespace __hidden_renderer_task_graph;

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
    resolvePrepareResourceUses.reserve(4u);
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

    // Geometry downsample begins only after the selected photon producer. Its cache write becomes an explicit graph
    // handoff to wavelet resolve, while dynamic ping-pong transitions remain inside the latter callback.
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
    geometryResourceUses.push_back(ReadWriteTextureUse(
        causticResolveGeometry,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::UnorderedAccess
    ));

    // Prepare consumes both immutable graph-produced inputs, then writes the parity-selected first ping-pong target.
    // The five fixed wavelet passes read/write the alternating pair, so the compiler owns their exact UAV-to-SRV
    // handoffs before the native upsample tail begins.
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
        resolvePrepareResourceUses.push_back(ReadTextureUse(
            causticHistory,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
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
        resolvePrepareResourceUses.push_back(ReadTextureUse(
            causticResolveHalf,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
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
    const bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            Name("render.software_caustics.emission_targets"),
            "Caustic Emission Targets",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_drawState.m_meshViewBuffer,
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

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuTaskId shadowVisibilityDependency[] = { m_deferredShadowVisibilityTask };

    // Black irradiance is the no-producer result. Start the existing Software Caustics Compute packet with this
    // typed CopyDest clear, then explicitly merge the producer callback into it below. A fresh temporal
    // accumulator adds a second typed zero clear before the producer; its CPU initialized mirror commits only on
    // that producer packet's acceptance.
    Core::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("render.software_caustics.irradiance_clear"))
        .setMarkerLabel("Software Caustics Irradiance Clear")
        .setQueue(ComputeTransferQueueRequest())
        .setScheduling(irradianceClearScheduling)
        .setDependencies(shadowVisibilityDependency, LengthOf(shadowVisibilityDependency))
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
        Core::GpuTaskDesc accumulatorNonTemporalClearDesc;
        accumulatorNonTemporalClearDesc
            .setIdentity(Name("render.software_caustics.accumulator_non_temporal_clear"))
            .setMarkerLabel("Software Caustics Accumulator Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(accumulatorNonTemporalClearScheduling)
            .setDependencies(&causticsDependency, 1u)
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
        Core::GpuTaskDesc accumulatorBootstrapClearDesc;
        accumulatorBootstrapClearDesc
            .setIdentity(Name("render.software_caustics.accumulator_bootstrap_clear"))
            .setMarkerLabel("Software Caustics Accumulator Bootstrap Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(accumulatorBootstrapClearScheduling)
            .setDependencies(&irradianceClearTask, 1u)
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
            .setResourceUses(accumulatorDecayUses, LengthOf(accumulatorDecayUses))
        ;
        const Core::GpuTaskId accumulatorDecayTask = m_raytracingSystem.declareCausticAccumulatorDecayTask(
            m_deferredLightingTaskGraph,
            accumulatorDecayDesc,
            deferredTargets,
            &m_preparedShadowVisibilityReady,
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
        &m_preparedShadowVisibilityReady,
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


bool RendererSystem::declareDeferredSurfelGiTask(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId surfelIrradiance,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const traceGeometryResources,
    const usize traceGeometryResourceCount,
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId effectsTask,
    const Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    using namespace __hidden_renderer_task_graph;

    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiInitializationLifecycleTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiAgeFreeTask = {};
    m_deferredSurfelGiCellHeadClearTask = {};
    m_deferredSurfelGiHashBuildTask = {};
    m_deferredSurfelGiSpawnTask = {};
    m_deferredSurfelGiTraceBuildArgsTask = {};
    m_deferredSurfelGiTraceTask = {};
    m_deferredSurfelGiResolveTask = {};
    asyncTiming.reset();
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !surfelIrradiance.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !effectsTask.valid()
        || (traceGeometryResourceCount != 0u && !traceGeometryResources)
    )
        return false;

    const bool useHwTrace = m_rayTracingState.m_surfelUseHwTrace;
    const bool hasSurfelWork = m_raytracingSystem.hasSurfelWork();
    const bool traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    const Core::GpuTaskResourceSetUse traceGeometrySetUse{
        .resourceSet = traceGeometrySet,
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
    Core::GpuTaskResourceSetUse traceResourceSetUses[2u] = {};
    usize traceResourceSetUseCount = 0u;
    if(traceGeometryStatesGraphOwned)
        traceResourceSetUses[traceResourceSetUseCount++] = traceGeometrySetUse;
    if(traceMaterialSampledTextureSet.valid())
        traceResourceSetUses[traceResourceSetUseCount++] = traceMaterialSampledTextureSetUse;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId surfelIrradianceHalf = importTexture(
        deferredTargets.surfelIrradianceHalf,
        Name("render.surfel_gi.irradiance_half"),
        "Surfel Irradiance Half"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.surfel_gi.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(!surfelIrradianceHalf.valid() || !sceneGeometryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred surfel-GI graph resources"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> ageFreeResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hashBuildResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> spawnResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> traceBuildArgsResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> traceResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveResourceUses{ scratchArena };
    resourceUses.reserve(28u + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResourceCount));
    ageFreeResourceUses.reserve(4u);
    hashBuildResourceUses.reserve(3u);
    spawnResourceUses.reserve(7u);
    traceBuildArgsResourceUses.reserve(3u);
    traceResourceUses.reserve(16u + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResourceCount));
    resolveResourceUses.reserve(6u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(usize resourceIndex = 0u; resourceIndex < traceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = traceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        if(!traceGeometryStatesGraphOwned)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state, Core::GpuGraphResourceId* const outResource = nullptr){
        if(!buffer){
            if(outResource)
                *outResource = {};
            return true;
        }
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        if(outResource)
            *outResource = resource;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state, Core::GpuGraphResourceId* const outResource = nullptr){
        if(!buffer){
            if(outResource)
                *outResource = {};
            return true;
        }
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        if(outResource)
            *outResource = resource;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    Core::GpuGraphResourceId shadowInstanceMaterials;
    Core::GpuGraphResourceId shadowMaterialTyped;
    Core::GpuGraphResourceId shadowInstances;
    Core::GpuGraphResourceId surfelConstants;
    Core::GpuGraphResourceId surfelPool;
    Core::GpuGraphResourceId surfelCellHead;
    Core::GpuGraphResourceId surfelCounter;
    Core::GpuGraphResourceId surfelTraceIndirectArgs;
    Core::GpuGraphResourceId surfelFreeList;
    Core::GpuGraphResourceId surfelPoolSnapshot;
    Core::GpuGraphResourceId surfelCellHeadSnapshot;
    Core::GpuGraphResourceId sceneBvhNodes;
    Core::GpuGraphResourceId sceneInstances;
    Core::GpuGraphResourceId tlas;
    Core::GpuGraphResourceId tlasBackingBuffer;
    bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource,
            &shadowInstanceMaterials
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource,
            &shadowMaterialTyped
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource,
            &shadowInstances
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants",
            Core::ResourceStates::ConstantBuffer,
            &surfelConstants
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelPoolBuffer,
            Name("render.surfel_gi.pool"),
            "Surfel Pool",
            Core::ResourceStates::UnorderedAccess,
            &surfelPool
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCellHeadBuffer,
            Name("render.surfel_gi.cell_heads"),
            "Surfel Cell Heads",
            Core::ResourceStates::UnorderedAccess,
            &surfelCellHead
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCounterBuffer,
            Name("render.surfel_gi.counter"),
            "Surfel Counter",
            Core::ResourceStates::UnorderedAccess,
            &surfelCounter
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelFreeListBuffer,
            Name("render.surfel_gi.free_list"),
            "Surfel Free List",
            Core::ResourceStates::UnorderedAccess,
            &surfelFreeList
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelPoolSnapshotBuffer,
            Name("render.surfel_gi.pool_snapshot"),
            "Surfel Pool Snapshot",
            Core::ResourceStates::ShaderResource,
            &surfelPoolSnapshot
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
            Name("render.surfel_gi.cell_head_snapshot"),
            "Surfel Cell Head Snapshot",
            Core::ResourceStates::ShaderResource,
            &surfelCellHeadSnapshot
        )
    ;
    if(optionalResourcesImported && m_rayTracingState.m_surfelTraceIndirectArgsBuffer){
        surfelTraceIndirectArgs = importBuffer(
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
            Name("render.surfel_gi.trace_args"),
            "Surfel Trace Arguments"
        );
        optionalResourcesImported = surfelTraceIndirectArgs.valid();
    }
    if(optionalResourcesImported && !useHwTrace){
        optionalResourcesImported =
            appendOptionalReadBuffer(
                m_rayTracingState.m_sceneBvhNodeBuffer,
                Name("render.shadow_visibility.scene_bvh_nodes"),
                "Scene BVH Nodes",
                Core::ResourceStates::ShaderResource,
                &sceneBvhNodes
            )
            && appendOptionalReadBuffer(
                m_rayTracingState.m_sceneInstanceBuffer,
                Name("render.shadow_visibility.scene_instances"),
                "Scene Instances",
                Core::ResourceStates::ShaderResource,
                &sceneInstances
            )
        ;
    }
    if(optionalResourcesImported && useHwTrace){
        if(!m_rayTracingState.m_tlas)
            optionalResourcesImported = false;
        else{
            tlas = m_deferredLightingTaskGraph.importAccelStruct(
                m_rayTracingState.m_tlas,
                AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
            );
            tlasBackingBuffer = importBuffer(
                m_rayTracingState.m_tlas->getBackingBufferHandle(),
                Name("render.deferred_effects.tlas_backing"),
                "Scene TLAS Backing"
            );
            optionalResourcesImported = tlas.valid() && tlasBackingBuffer.valid();
            if(optionalResourcesImported){
                resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
                resourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred surfel-GI dynamic resource domain"));
        return false;
    }

    // A prepared normal Surfel GI frame can split its age/free, per-frame cell-head reset, hash build, Spawn,
    // trace-build-args, trace, and resolve. Keep unavailable resources or pipelines on the established monolithic
    // compatibility callback; otherwise the graph owns each handoff in one selected Compute packet.
    const bool graphOwnsSurfelGiResolve =
        hasSurfelWork
        && shadowInstanceMaterials.valid()
        && shadowMaterialTyped.valid()
        && shadowInstances.valid()
        && materialContextSlots.valid()
        && surfelConstants.valid()
        && surfelPool.valid()
        && surfelCellHead.valid()
        && surfelCounter.valid()
        && surfelTraceIndirectArgs.valid()
        && surfelFreeList.valid()
        && surfelPoolSnapshot.valid()
        && surfelCellHeadSnapshot.valid()
        && (useHwTrace ? (tlas.valid() && tlasBackingBuffer.valid()) : (sceneBvhNodes.valid() && sceneInstances.valid()))
        && m_rayTracingState.m_surfelAgeFreePipeline
        && m_rayTracingState.m_surfelHashBuildPipeline
        && m_rayTracingState.m_surfelSpawnPipeline
        && (useHwTrace ? m_rayTracingState.m_surfelTraceHwPipeline : m_rayTracingState.m_surfelTracePipeline)
        && m_rayTracingState.m_surfelResolvePipeline
        && m_rayTracingState.m_surfelUpsamplePipeline
        && m_rayTracingState.m_surfelTraceBuildArgsPipeline
    ;
    if(graphOwnsSurfelGiResolve){
        resourceUses.clear();
        resourceUses.push_back(ReadUse(worldPosition));
        resourceUses.push_back(ReadUse(normal));
        resourceUses.push_back(ReadUse(surfelIrradianceHalf, Core::ResourceStates::ShaderResource));
        resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));

        resolveResourceUses.push_back(ReadUse(worldPosition));
        resolveResourceUses.push_back(ReadUse(normal));
        resolveResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        resolveResourceUses.push_back(ReadUse(surfelPool, Core::ResourceStates::ShaderResource));
        resolveResourceUses.push_back(ReadUse(surfelCellHead, Core::ResourceStates::ShaderResource));
        resolveResourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));

        traceResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(sceneGeometryDomain));
        traceResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        traceResourceUses.push_back(ReadUse(surfelPoolSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelCellHeadSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelTraceIndirectArgs, Core::ResourceStates::IndirectArgument));
        if(!traceGeometryStatesGraphOwned){
            for(usize resourceIndex = 0u; resourceIndex < traceGeometryResourceCount; ++resourceIndex)
                traceResourceUses.push_back(ReadUse(traceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
        }
        if(useHwTrace){
            traceResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            traceResourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
        }
        else{
            traceResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
            traceResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        }
    }
    else if(surfelTraceIndirectArgs.valid()){
        resourceUses.push_back(WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess));
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuExternalCompletionId* const surfelGiExternalDependencies =
        surfelCounterReadbackCompletion.valid() ? &surfelCounterReadbackCompletion : nullptr
    ;
    const usize surfelGiExternalDependencyCount = surfelCounterReadbackCompletion.valid() ? 1u : 0u;

    // A fresh persistent field must clear before the snapshot reads it. The four typed clear primitives and their
    // resource-free lifecycle tail form one Compute-preferred, Transfer-capable graph packet; the lifecycle task
    // publishes its CPU
    // mirror only after every clear recorded and that packet accepts. Once initialized, the two fixed regions become
    // one graph-owned copy task; compiler declarations own every CopySource/CopyDest transition and handoff.
    Core::GpuTaskId surfelGiDependency = effectsTask;
    if(hasSurfelWork){
        if(
            !surfelPool.valid()
            || !surfelCellHead.valid()
            || !surfelCounter.valid()
            || !surfelFreeList.valid()
            || !surfelPoolSnapshot.valid()
            || !surfelCellHeadSnapshot.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-GI snapshot resources were unavailable during graph declaration"));
            return false;
        }

        if(m_raytracingSystem.needsSurfelResourceInitialization()){
            Core::GpuTaskSchedulingHint initializationScheduling;
            initializationScheduling.cost = Core::GpuTaskCostHint::Medium;
            // Explicit merging keeps the first clear as a new packet under the renderer's FrontierSafe policy, then
            // lets its serial successors share that packet. Do not force a boundary here: that would also forbid the
            // following typed clears from joining their own initialization packet.
            initializationScheduling.allowPacketMerge = true;
            Core::GpuTaskDesc poolClearDesc;
            poolClearDesc
                .setIdentity(Name("render.surfel_gi.initialize_pool_clear"))
                .setMarkerLabel("Surfel GI Initialize Pool Clear")
                .setQueue(ComputeTransferQueueRequest())
                .setScheduling(initializationScheduling)
                .setDependencies(&surfelGiDependency, 1u)
                .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
            ;
            m_deferredSurfelGiPreparationTask = m_deferredLightingTaskGraph.addClearBufferTask(
                poolClearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = surfelPool,
                    .clearValue = 0u,
                }
            );
            if(!m_deferredSurfelGiPreparationTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI pool initialization clear"));
                return false;
            }

            Core::GpuTaskSchedulingHint chainedInitializationScheduling = initializationScheduling;
            chainedInitializationScheduling.cost = Core::GpuTaskCostHint::Tiny;
            chainedInitializationScheduling.mergeWithPrevious = true;
            // The subsequent snapshot can route to a dedicated Transfer queue and therefore creates a consumer
            // frontier for the first two clears. Every tail is an explicit immediate successor, so retain the
            // complete initialization chain in its single accepted packet rather than splitting before that wait.
            chainedInitializationScheduling.allowMergeAcrossConsumerFrontier = true;
            Core::GpuTaskId initializationDependency = m_deferredSurfelGiPreparationTask;
            const auto addInitializationClear = [&](
                const Name& identity,
                const AStringView label,
                const Core::GpuGraphResourceId destination,
                const u32 clearValue
            ){
                Core::GpuTaskDesc clearDesc;
                clearDesc
                    .setIdentity(identity)
                    .setMarkerLabel(label)
                    .setQueue(ComputeTransferQueueRequest())
                    .setScheduling(chainedInitializationScheduling)
                    .setDependencies(&initializationDependency, 1u)
                ;
                const Core::GpuTaskId clearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                    clearDesc,
                    Core::GpuClearBufferTaskDesc{
                        .destination = destination,
                        .clearValue = clearValue,
                    }
                );
                if(clearTask.valid())
                    initializationDependency = clearTask;
                return clearTask;
            };
            if(
                !addInitializationClear(
                    Name("render.surfel_gi.initialize_cell_head_clear"),
                    "Surfel GI Initialize Cell-Head Clear",
                    surfelCellHead,
                    NWB_SURFEL_CELL_INVALID
                ).valid()
                || !addInitializationClear(
                    Name("render.surfel_gi.initialize_counter_clear"),
                    "Surfel GI Initialize Counter Clear",
                    surfelCounter,
                    0u
                ).valid()
                || !addInitializationClear(
                    Name("render.surfel_gi.initialize_free_list_clear"),
                    "Surfel GI Initialize Free-List Clear",
                    surfelFreeList,
                    0u
                ).valid()
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI initialization clears"));
                return false;
            }

            Core::GpuTaskDesc initializationLifecycleDesc;
            initializationLifecycleDesc
                .setIdentity(Name("render.surfel_gi.initialize_lifecycle"))
                .setMarkerLabel("Surfel GI Initialize Lifecycle")
                .setQueue(ComputeTransferQueueRequest())
                .setScheduling(chainedInitializationScheduling)
                .setDependencies(&initializationDependency, 1u)
            ;
            m_deferredSurfelGiInitializationLifecycleTask =
                m_raytracingSystem.declareSurfelResourceInitializationLifecycleTask(
                    m_deferredLightingTaskGraph,
                    initializationLifecycleDesc
                );
            if(!m_deferredSurfelGiInitializationLifecycleTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI initialization lifecycle"));
                return false;
            }
            surfelGiDependency = m_deferredSurfelGiInitializationLifecycleTask;
        }

        const Core::GpuCopyBufferTaskRegion snapshotRegions[] = {
            Core::GpuCopyBufferTaskRegion{
                .source = surfelPool,
                .destination = surfelPoolSnapshot,
                .dataSizeBytes = m_rayTracingState.m_surfelPoolBuffer->getDescription().byteSize,
            },
            Core::GpuCopyBufferTaskRegion{
                .source = surfelCellHead,
                .destination = surfelCellHeadSnapshot,
                .dataSizeBytes = m_rayTracingState.m_surfelCellHeadBuffer->getDescription().byteSize,
            },
        };
        Core::GpuTaskDesc snapshotDesc;
        snapshotDesc
            .setIdentity(Name("render.surfel_gi.snapshot_copy"))
            .setMarkerLabel("Surfel GI Snapshot Copy")
            .setQueue(TransferQueueRequest())
            .setScheduling(scheduling)
            .setDependencies(&surfelGiDependency, 1u)
        ;
        m_deferredSurfelGiSnapshotCopyTask = m_deferredLightingTaskGraph.addCopyBufferTask(
            snapshotDesc,
            Core::GpuCopyBufferTaskDesc{
                .regions = snapshotRegions,
                .regionCount = LengthOf(snapshotRegions),
            }
        );
        if(!m_deferredSurfelGiSnapshotCopyTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI snapshot-copy task"));
            return false;
        }
        if(!m_deferredSurfelGiPreparationTask.valid())
            m_deferredSurfelGiPreparationTask = m_deferredSurfelGiSnapshotCopyTask;
        surfelGiDependency = m_deferredSurfelGiSnapshotCopyTask;
    }

    // Zero coverage is the deferred-lighting no-op. Keep the typed CopyDest clear at the front of the same Compute
    // packet as GI: it does not merge with the preceding effects packet, while GI explicitly merges with it below.
    // This preserves the established semantic GI boundary on every Compute/Graphics fallback route.
    Core::GpuTaskSchedulingHint surfelIrradianceClearScheduling;
    surfelIrradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    surfelIrradianceClearScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc surfelIrradianceClearDesc;
    surfelIrradianceClearDesc
        .setIdentity(Name("render.surfel_gi.irradiance_clear"))
        .setMarkerLabel("Surfel Irradiance Clear")
        .setQueue(ComputeTransferQueueRequest())
        .setScheduling(surfelIrradianceClearScheduling)
        .setDependencies(&surfelGiDependency, 1u)
    ;
    const Core::GpuTaskId surfelIrradianceClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        surfelIrradianceClearDesc,
        Core::GpuClearTextureTaskDesc{
            .destination = surfelIrradiance,
            .subresources = ECSRenderDetail::s_FramebufferSubresources,
            .valueType = Core::GpuClearTextureTaskValueType::Float,
            .floatValue = Core::Color(0.f, 0.f, 0.f, 0.f),
        }
    );
    if(!surfelIrradianceClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred surfel-irradiance clear"));
        return false;
    }
    m_deferredSurfelGiIrradianceClearTask = surfelIrradianceClearTask;
    surfelGiDependency = surfelIrradianceClearTask;

    Core::GpuTaskSchedulingHint surfelGiScheduling = scheduling;
    surfelGiScheduling.forceSubmissionBoundary = false;
    surfelGiScheduling.allowPacketMerge = true;
    surfelGiScheduling.mergeWithPrevious = true;
    if(graphOwnsSurfelGiResolve){
        ageFreeResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        ageFreeResourceUses.push_back(WriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        ageFreeResourceUses.push_back(WriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        ageFreeResourceUses.push_back(WriteUse(surfelFreeList, Core::ResourceStates::UnorderedAccess));

        Core::GpuTaskDesc ageFreeDesc;
        ageFreeDesc
            .setIdentity(Name("render.surfel_gi.age_free"))
            .setMarkerLabel("Surfel GI Age Free")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&surfelGiDependency, 1u)
            .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
            .setResourceUses(ageFreeResourceUses.data(), ageFreeResourceUses.size())
        ;
        m_deferredSurfelGiAgeFreeTask = m_raytracingSystem.declareSurfelGiAgeFreeTask(
            m_deferredLightingTaskGraph,
            ageFreeDesc,
            deferredTargets,
            timingTicket,
            asyncTiming,
            true
        );
        if(!m_deferredSurfelGiAgeFreeTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI age/free graph task"));
            return false;
        }

        Core::GpuTaskDesc cellHeadClearDesc;
        cellHeadClearDesc
            .setIdentity(Name("render.surfel_gi.cell_head_clear"))
            .setMarkerLabel("Surfel GI Cell Head Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiAgeFreeTask, 1u)
        ;
        m_deferredSurfelGiCellHeadClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
            cellHeadClearDesc,
            Core::GpuClearBufferTaskDesc{
                .destination = surfelCellHead,
                .clearValue = NWB_SURFEL_CELL_INVALID,
            }
        );
        if(!m_deferredSurfelGiCellHeadClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred surfel cell-head clear"));
            return false;
        }

        hashBuildResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        hashBuildResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        hashBuildResourceUses.push_back(ReadWriteUse(surfelCellHead, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskDesc hashBuildDesc;
        hashBuildDesc
            .setIdentity(Name("render.surfel_gi.hash_build"))
            .setMarkerLabel("Surfel GI Hash Build")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiCellHeadClearTask, 1u)
            .setResourceUses(hashBuildResourceUses.data(), hashBuildResourceUses.size())
        ;
        m_deferredSurfelGiHashBuildTask = m_raytracingSystem.declareSurfelGiHashBuildTask(
            m_deferredLightingTaskGraph,
            hashBuildDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiHashBuildTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI hash-build graph task"));
            return false;
        }

        spawnResourceUses.push_back(ReadUse(worldPosition));
        spawnResourceUses.push_back(ReadUse(normal));
        spawnResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        spawnResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        spawnResourceUses.push_back(ReadWriteUse(surfelCellHead, Core::ResourceStates::UnorderedAccess));
        spawnResourceUses.push_back(ReadWriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        spawnResourceUses.push_back(ReadWriteUse(surfelFreeList, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskDesc spawnDesc;
        spawnDesc
            .setIdentity(Name("render.surfel_gi.spawn"))
            .setMarkerLabel("Surfel GI Spawn")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiHashBuildTask, 1u)
            .setResourceUses(spawnResourceUses.data(), spawnResourceUses.size())
        ;
        m_deferredSurfelGiSpawnTask = m_raytracingSystem.declareSurfelGiSpawnTask(
            m_deferredLightingTaskGraph,
            spawnDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiSpawnTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI spawn graph task"));
            return false;
        }

        traceBuildArgsResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        traceBuildArgsResourceUses.push_back(ReadUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        traceBuildArgsResourceUses.push_back(WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskDesc traceBuildArgsDesc;
        traceBuildArgsDesc
            .setIdentity(Name("render.surfel_gi.trace_build_args"))
            .setMarkerLabel("Surfel GI Trace Build Args")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiSpawnTask, 1u)
            .setResourceUses(traceBuildArgsResourceUses.data(), traceBuildArgsResourceUses.size())
        ;
        m_deferredSurfelGiTraceBuildArgsTask = m_raytracingSystem.declareSurfelGiTraceBuildArgsTask(
            m_deferredLightingTaskGraph,
            traceBuildArgsDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiTraceBuildArgsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI trace-build-args graph task"));
            return false;
        }

        Core::GpuTaskDesc traceDesc;
        traceDesc
            .setIdentity(Name("render.surfel_gi.trace"))
            .setMarkerLabel("Surfel GI Trace")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiTraceBuildArgsTask, 1u)
            .setResourceUses(traceResourceUses.data(), traceResourceUses.size())
            .setResourceSetUses(
                traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
                traceResourceSetUseCount
            )
        ;
        m_deferredSurfelGiTraceTask = m_raytracingSystem.declareSurfelGiTraceTask(
            m_deferredLightingTaskGraph,
            traceDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiTraceTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI trace graph task"));
            return false;
        }

        Core::GpuTaskDesc resolveDesc;
        resolveDesc
            .setIdentity(Name("render.surfel_gi.resolve"))
            .setMarkerLabel("Surfel GI Resolve")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiTraceTask, 1u)
            .setResourceUses(resolveResourceUses.data(), resolveResourceUses.size())
        ;
        m_deferredSurfelGiResolveTask = m_raytracingSystem.declareSurfelGiResolveTask(
            m_deferredLightingTaskGraph,
            resolveDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiResolveTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI resolve graph task"));
            return false;
        }
        surfelGiDependency = m_deferredSurfelGiResolveTask;
    }

    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(ComputeQueueRequest())
        .setScheduling(surfelGiScheduling)
        .setDependencies(&surfelGiDependency, 1u)
        .setExternalDependencies(
            graphOwnsSurfelGiResolve ? nullptr : surfelGiExternalDependencies,
            graphOwnsSurfelGiResolve ? 0u : surfelGiExternalDependencyCount
        )
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(
            !graphOwnsSurfelGiResolve && traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
            !graphOwnsSurfelGiResolve ? traceResourceSetUseCount : 0u
        )
    ;
    m_deferredSurfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        timingTicket,
        true,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve ? &asyncTiming : nullptr
    );
    if(!m_deferredSurfelGiTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI graph task"));
        return false;
    }
    return true;
}


void RendererSystem::declareDeferredSurfelCountReadbackTask(){
    using namespace __hidden_renderer_task_graph;

    m_deferredSurfelGiCounterReadbackTask = {};
    if(
        !m_deferredSurfelGiTask.valid()
        || !m_raytracingSystem.shouldCaptureSurfelCountReadback()
    )
        return;

    const Core::GpuGraphResourceId counter = m_deferredLightingTaskGraph.importBuffer(
        m_rayTracingState.m_surfelCounterBuffer,
        BufferResourceDesc(Name("render.surfel_gi.counter"), "Surfel Counter")
    );
    const Core::GpuGraphResourceId readback = m_deferredLightingTaskGraph.importBuffer(
        m_rayTracingState.m_surfelCounterReadback,
        BufferResourceDesc(Name("render.surfel_gi.counter_readback"), "Surfel Counter Readback")
    );
    if(!counter.valid() || !readback.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback graph resources"));
        return;
    }

    const Core::GpuCopyBufferTaskRegion regions[] = {
        Core::GpuCopyBufferTaskRegion{
            .source = counter,
            .destination = readback,
            .dataSizeBytes = static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE,
        },
    };
    Core::GpuTaskSchedulingHint scheduling;
    // This infrequent diagnostic is independent of the deferred suffix. Treat it as a small copy so a dedicated
    // Transfer transport can absorb it after Present, while Compute/Graphics remain valid fallbacks.
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuTaskId dependencies[] = { m_deferredSurfelGiTask };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi.counter_readback"))
        .setMarkerLabel("Surfel Counter Readback")
        .setQueue(TransferQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, LengthOf(dependencies))
    ;
    m_deferredSurfelGiCounterReadbackTask = m_deferredLightingTaskGraph.addCopyBufferTask(
        desc,
        Core::GpuCopyBufferTaskDesc{
            .regions = regions,
            .regionCount = LengthOf(regions),
            .acceptedToken = &m_rayTracingState.m_surfelCountReadbackSubmissionToken,
        }
    );
    if(!m_deferredSurfelGiCounterReadbackTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel counter-readback task"));
        return;
    }
    // The token stays invalid until native submission accepts; this timestamp is only read when it publishes.
    m_raytracingSystem.markSurfelCountReadbackScheduled();
}


void RendererSystem::buildDeferredLightingTaskGraph(
    const ECSRenderDetail::RendererFrameGraphFeatures& features,
    DeferredFrameTargets& deferredTargets,
    const CsgFrameState& csgFrameState,
    const bool clearAvboitTargets,
    const bool hasTransparentRenderers,
    const bool hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    Core::Framebuffer* const presentationFramebuffer,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
    ECSRenderDetail::CsgIntervalClearTimingRecordState& opaqueCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
    Core::GpuTimingSubmissionTicket** const graphicsPrefixTimingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
    ECSRenderDetail::AvboitClearTimingRecordState& avboitClearTimingState,
    ECSRenderDetail::CsgIntervalClearTimingRecordState& transparentCsgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTiming,
    Optional<Core::GpuTimingMeasure>& avboitOccupancyComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& avboitExtinctionComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& avboitAccumulationComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
    Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityAsyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueSoftResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentSoftResolveTiming,
    bool& shadowVisibilityOpaqueProduced,
    bool& shadowVisibilityTransparentTraceProduced,
    u32& shadowVisibilityOpaqueFrameIndex,
    Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket,
    Core::GpuTimingSubmissionTicket& surfelGiTimingTicket,
    Optional<Core::GpuTimingMeasure>& surfelGiAsyncTiming,
    Core::GpuTimingSubmissionTicket& hardwareCausticsTimingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming,
    Core::GpuTimingSubmissionTicket& lightingTimingTicket,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket,
    const bool includeLaggedLightingHistoryCapture
){
    using namespace __hidden_renderer_task_graph;

    m_deferredLightingTaskGraphValid = false;
    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
    m_causticEmissionTargetsUploadTask = {};
    m_surfelFrameConstantsUploadTask = {};
    m_shadowInstanceMaterialUploadTask = {};
    m_shadowInstanceUploadTask = {};
    m_shadowMaterialTypedUploadTask = {};
    m_sceneBvhNodesUploadTask = {};
    m_sceneBvhInstancesUploadTask = {};
    m_deferredLaggedLightingHistorySlotsUploadTask = {};
    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareHybridSoftwareTailTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    m_graphicsPrefixOpaqueComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_graphicsPrefixOpaqueSharedComputeEmulationTasks)
        task = {};
    m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = {};
    m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgReceiverSpanTask = {};
    m_graphicsPrefixCsgIntervalCombineTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_deferredShadowVisibilityOpaqueTask = {};
    m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_deferredShadowVisibilityTransparentTraceTask = {};
    m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_deferredShadowVisibilityAdaptiveStatsClearTask = {};
    m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_deferredShadowVisibilityAdaptiveStatsReadbackTask = {};
    m_deferredShadowVisibilityAllLitClearTask = {};
    m_deferredShadowVisibilityTask = {};
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
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiInitializationLifecycleTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiAgeFreeTask = {};
    m_deferredSurfelGiCellHeadClearTask = {};
    m_deferredSurfelGiHashBuildTask = {};
    m_deferredSurfelGiSpawnTask = {};
    m_deferredSurfelGiTraceBuildArgsTask = {};
    m_deferredSurfelGiTraceTask = {};
    m_deferredSurfelGiResolveTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitClearFirstTask = {};
    m_deferredAvboitClearTask = {};
    m_deferredAvboitTransparentCsgIntervalClearFirstTask = {};
    m_deferredAvboitTransparentCsgIntervalClearTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitCsgReceiverSpanTask = {};
    m_deferredAvboitCsgIntervalCombineTask = {};
    m_deferredAvboitOccupancyStreamTask = {};
    m_deferredAvboitOccupancyComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitOccupancySharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitOccupancySharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitExtinctionSharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitExtinctionSharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
    m_deferredAvboitAccumulationComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_deferredAvboitAccumulationSharedComputeEmulationTasks)
        task = {};
    m_deferredAvboitAccumulationSharedComputeEmulationTaskCount = 0u;
    m_deferredAvboitAccumulationTask = {};
    m_deferredAvboitAccumulationFinalizeTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentationOverlayTask = {};
    m_deferredPresentTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredFrameRecoveryTask = {};
    m_deferredSurfelGiCounterReadbackCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_deferredFrameRecoveryArmed = false;
    m_deferredFrameRecoveryRetiresTiming = false;
    m_deferredPresentationOverlayRequired = false;
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const u32 transferFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Transfer);
    const bool dedicatedAsyncCompute = computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const bool dedicatedTransfer = transferFamilyIndex != Limit<u32>::s_Max
        && transferFamilyIndex != graphicsFamilyIndex
        && transferFamilyIndex != computeFamilyIndex
    ;
    const bool useLaggedLightingHistory = dedicatedAsyncCompute
        && features.frameLaggedAsyncLightingEnabled
        && features.laggedLightingHistoryReady
        && features.laggedLightingHistoryAccepted
    ;
    const bool splitAvboitStages = !useLaggedLightingHistory
        && dedicatedAsyncCompute
        && features.hasTransparentRenderers
    ;
    if(!splitAvboitStages){
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
    }
    const bool declaresHardwareCaustics = features.hardwareCaustics;
    const bool capturesLaggedLightingHistory = includeLaggedLightingHistoryCapture
        && dedicatedAsyncCompute
        && features.frameLaggedAsyncLightingEnabled
    ;
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    const DeferredLaggedLightingHistoryResources* const captureHistory = capturesLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_drawState.m_meshViewBuffer
        || !m_deferredState.m_sceneShadingBuffer
        || !m_deferredState.m_lightBuffer
        || !presentationFramebuffer
        || hasTransparentRenderers != features.hasTransparentRenderers
        || (useLaggedLightingHistory && (!history || !history->valid()))
        || (capturesLaggedLightingHistory && (!captureHistory || !captureHistory->valid()))
    )
        return;

    // Preflight has already frozen the exact visible HW/SW mesh tables. Keep retained handles here rather than the
    // raw descriptor-table pointers, import every physical buffer once, and fan the same IDs out to all packets.
    // A fresh/replaced buffer starts from its creation state; a buffer normalized by an accepted earlier Prefix is
    // explicitly imported as SRV so the first packet never claims a stale state.
    if(!m_raytracingSystem.freezePreparedShadowTraceGeometryBuffers()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow-trace geometry buffers"));
        return;
    }
    const PreparedShadowTraceGeometryBufferVector& preparedTraceGeometry =
        m_raytracingSystem.preparedShadowTraceGeometryBuffers()
    ;
    const PreparedShadowTraceMaterialSampledTextureVector& preparedTraceMaterialSampledTextures =
        m_raytracingSystem.preparedShadowTraceMaterialSampledTextures()
    ;
    Core::Alloc::ScratchArena traceGeometryScratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> traceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceAttributeResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareTraceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> traceMaterialSampledTextureResources{
        traceGeometryScratchArena
    };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareBvhBuildStateResources{ traceGeometryScratchArena };
    Vector<Core::Buffer*, Core::Alloc::ScratchArena> softwareBvhBuildStateBuffers{ traceGeometryScratchArena };
    traceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceAttributeResources.reserve(preparedTraceGeometry.size());
    softwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    traceMaterialSampledTextureResources.reserve(preparedTraceMaterialSampledTextures.size());
    softwareBvhBuildStateResources.reserve(meshState().m_meshes.size() + 3u);
    softwareBvhBuildStateBuffers.reserve(meshState().m_meshes.size() + 3u);

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const auto importCurrentBindlessSlots = [&](const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(
            deferredTargets.bindless.slotsBuffer,
            BufferResourceDesc(identity, label)
        );
    };
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        Core::GpuGraphResourceDesc desc = BufferResourceDesc(preparedBuffer.identity, "Prepared Shadow Trace Geometry");
        desc.setInitialState(preparedBuffer.initialState);
        const Core::GpuGraphResourceId resource = m_deferredLightingTaskGraph.importBuffer(preparedBuffer.buffer, desc);
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-trace geometry buffer"));
            return;
        }
        traceGeometryResources.push_back(resource);
        if(preparedBuffer.roles & (
            PreparedShadowTraceGeometryRole::HardwarePosition
            | PreparedShadowTraceGeometryRole::HardwareIndex
            | PreparedShadowTraceGeometryRole::HardwareAttribute
        ))
            hardwareTraceGeometryResources.push_back(resource);
        if(preparedBuffer.roles & PreparedShadowTraceGeometryRole::HardwareAttribute)
            hardwareTraceAttributeResources.push_back(resource);
        if(preparedBuffer.roles & (
            PreparedShadowTraceGeometryRole::SoftwareNode
            | PreparedShadowTraceGeometryRole::SoftwarePosition
            | PreparedShadowTraceGeometryRole::SoftwareIndex
            | PreparedShadowTraceGeometryRole::SoftwareAttribute
        ))
            softwareTraceGeometryResources.push_back(resource);
    }
    // Shadow, caustic, and surfel closest-hit dispatchers use the frozen material context to select these Texture2D
    // assets through the bindless heap. Reuse a typed preflight import when G-buffer/AVBOIT already owns it, rather
    // than introducing an opaque descriptor domain around the trace paths.
    for(const Core::TextureHandle& texture : preparedTraceMaterialSampledTextures){
        Core::GpuGraphResourceId resource = m_deferredLightingTaskGraph.findImportedTexture(texture);
        if(!resource.valid()){
            const Name textureIdentity = texture ? texture->getDescription().name : NAME_NONE;
            if(!textureIdentity){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared trace material texture has no stable identity"));
                return;
            }
            resource = importTexture(texture, textureIdentity, "Prepared Trace Material Sampled Texture");
        }
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared trace material sampled texture"));
            return;
        }
        traceMaterialSampledTextureResources.push_back(resource);
    }
    Core::GpuGraphResourceSetId shadowTraceGeometrySet;
    if(!traceGeometryResources.empty()){
        shadowTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.post_gbuffer_trace_geometry"))
                .setMarkerLabel("Post-G-Buffer Trace Geometry")
                .setMembers(traceGeometryResources.data(), traceGeometryResources.size())
        );
    }
    Core::GpuGraphResourceSetId softwareTraceGeometrySet;
    if(!softwareTraceGeometryResources.empty()){
        softwareTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.software_trace_geometry"))
                .setMarkerLabel("Software Trace Geometry")
                .setMembers(softwareTraceGeometryResources.data(), softwareTraceGeometryResources.size())
        );
    }
    Core::GpuGraphResourceSetId traceMaterialSampledTextureSet;
    if(!traceMaterialSampledTextureResources.empty()){
        traceMaterialSampledTextureSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.trace_material_sampled_textures"))
                .setMarkerLabel("Trace Material Sampled Textures")
                .setMembers(
                    traceMaterialSampledTextureResources.data(),
                    traceMaterialSampledTextureResources.size()
                )
        );
        if(!traceMaterialSampledTextureSet.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared trace material sampled textures"));
            return;
        }
    }
    Core::GpuGraphResourceSetId hardwareTraceGeometrySet;
    if(!hardwareTraceGeometryResources.empty()){
        hardwareTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.hardware_trace_geometry"))
                .setMarkerLabel("Hardware Trace Geometry")
                .setMembers(hardwareTraceGeometryResources.data(), hardwareTraceGeometryResources.size())
        );
    }
    Core::GpuGraphResourceSetId hardwareTraceAttributeSet;
    if(!hardwareTraceAttributeResources.empty()){
        hardwareTraceAttributeSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.hardware_trace_attributes"))
                .setMarkerLabel("Hardware Trace Attributes")
                .setMembers(hardwareTraceAttributeResources.data(), hardwareTraceAttributeResources.size())
        );
    }
    bool softwareTraceResourcesPrepared = false;
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        if(preparedBuffer.roles & (
            PreparedShadowTraceGeometryRole::SoftwareNode
            | PreparedShadowTraceGeometryRole::SoftwarePosition
            | PreparedShadowTraceGeometryRole::SoftwareIndex
            | PreparedShadowTraceGeometryRole::SoftwareAttribute
        )){
            softwareTraceResourcesPrepared = true;
            break;
        }
    }
    if(softwareTraceResourcesPrepared){
        const auto appendSoftwareBvhBuildState = [&](
            const Core::BufferHandle& buffer,
            const Name identity,
            const AStringView label
        ){
            if(!buffer || !identity)
                return false;
            for(Core::Buffer* const existing : softwareBvhBuildStateBuffers){
                if(existing == buffer.get())
                    return true;
            }
            const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
            if(!resource.valid())
                return false;
            softwareBvhBuildStateBuffers.push_back(buffer.get());
            softwareBvhBuildStateResources.push_back(resource);
            return true;
        };
        for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
            const MeshResources& mesh = meshIt.value();
            if(!mesh.swBvhNodeBuffer && !mesh.swBvhParentBuffer)
                continue;
            if(
                !mesh.swBvhNodeBuffer
                || !mesh.swBvhParentBuffer
                || !appendSoftwareBvhBuildState(
                    mesh.swBvhParentBuffer,
                    DeriveName(mesh.meshName, AStringView(":shadow_trace_sw_parent")),
                    "Software BVH Parent"
                )
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software BVH parent build state"));
                return;
            }
        }
        const RendererRayTracingState& traceState = rayTracingState();
        if(
            !traceState.m_bvhSortKeysBuffer
            || !traceState.m_bvhSortPayloadBuffer
            || !traceState.m_bvhVisitCounterBuffer
            || !appendSoftwareBvhBuildState(
                traceState.m_bvhSortKeysBuffer,
                Name("render.shadow_trace.sw_bvh_sort_keys"),
                "Software BVH Sort Keys"
            )
            || !appendSoftwareBvhBuildState(
                traceState.m_bvhSortPayloadBuffer,
                Name("render.shadow_trace.sw_bvh_sort_payload"),
                "Software BVH Sort Payload"
            )
            || !appendSoftwareBvhBuildState(
                traceState.m_bvhVisitCounterBuffer,
                Name("render.shadow_trace.sw_bvh_visit_counter"),
                "Software BVH Visit Counter"
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared software BVH build state"));
            return;
        }
    }
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.deferred_lighting.albedo"),
        "G-Buffer Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.deferred_lighting.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.deferred_lighting.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.deferred_lighting.depth"),
        "G-Buffer Depth"
    );
    // The CSG working set—peel targets, receiver-event/span images, and removed-interval outputs—is declared by
    // the graph. Its exact clear/StorageImage handoffs are visible here; the wider CSG target lifecycle remains in
    // native compatibility producers for its own bounded migration.
    const Core::GpuGraphResourceId csgCapBackNormal = importTexture(
        deferredTargets.csgCapBackNormal,
        Name("render.deferred.csg_cap_back_normal"),
        "CSG Cap Back Normal"
    );
    const Core::GpuGraphResourceId csgIntervalDepth = importTexture(
        deferredTargets.csgIntervalDepth,
        Name("render.deferred.csg_interval_depth"),
        "CSG Interval Depth"
    );
    const Core::GpuGraphResourceId csgIntervalId = importTexture(
        deferredTargets.csgIntervalId,
        Name("render.deferred.csg_interval_id"),
        "CSG Interval ID"
    );
    const Core::GpuGraphResourceId csgReceiverEventData = importTexture(
        deferredTargets.csgReceiverEventData,
        Name("render.deferred.csg_receiver_event_data"),
        "CSG Receiver Event Data"
    );
    const Core::GpuGraphResourceId csgReceiverEventCount = importTexture(
        deferredTargets.csgReceiverEventCount,
        Name("render.deferred.csg_receiver_event_count"),
        "CSG Receiver Event Count"
    );
    const Core::GpuGraphResourceId csgReceiverSpanData = importTexture(
        deferredTargets.csgReceiverSpanData,
        Name("render.deferred.csg_receiver_span_data"),
        "CSG Receiver Span Data"
    );
    const Core::GpuGraphResourceId csgReceiverSpanCount = importTexture(
        deferredTargets.csgReceiverSpanCount,
        Name("render.deferred.csg_receiver_span_count"),
        "CSG Receiver Span Count"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalDepth = importTexture(
        deferredTargets.csgRemovedIntervalDepth,
        Name("render.deferred.csg_removed_interval_depth"),
        "CSG Removed Interval Depth"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalCapNormal = importTexture(
        deferredTargets.csgRemovedIntervalCapNormal,
        Name("render.deferred.csg_removed_interval_cap_normal"),
        "CSG Removed Interval Cap Normal"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalData = importTexture(
        deferredTargets.csgRemovedIntervalData,
        Name("render.deferred.csg_removed_interval_data"),
        "CSG Removed Interval Data"
    );
    const Core::GpuGraphResourceId csgRemovedIntervalCount = importTexture(
        deferredTargets.csgRemovedIntervalCount,
        Name("render.deferred.csg_removed_interval_count"),
        "CSG Removed Interval Count"
    );
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        history ? history->shadowVisibility : deferredTargets.shadowVisibility,
        Name("render.deferred_lighting.shadow_visibility"),
        history ? "Lagged Shadow Visibility" : "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        history ? history->causticIrradiance : deferredTargets.causticIrradiance,
        Name("render.deferred_lighting.caustic_irradiance"),
        history ? "Lagged Caustic Irradiance" : "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,
        Name("render.deferred_lighting.surfel_irradiance"),
        history ? "Lagged Surfel Irradiance" : "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId currentShadowVisibility = !history
        ? shadowVisibility
        : importTexture(
            deferredTargets.shadowVisibility,
            Name("render.deferred_shadow_visibility.current_output"),
            "Shadow Visibility"
        )
    ;
    const Core::GpuGraphResourceId currentCausticIrradiance = !history
        ? causticIrradiance
        : importTexture(
            deferredTargets.causticIrradiance,
            Name("render.deferred_effects.current_caustic_irradiance"),
            "Caustic Irradiance"
        )
    ;
    const Core::GpuGraphResourceId currentSurfelIrradiance = !history
        ? surfelIrradiance
        : importTexture(
            deferredTargets.surfelIrradiance,
            Name("render.deferred_surfel_gi.current_irradiance"),
            "Surfel Irradiance"
        )
    ;
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_lighting.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId sceneShading = importBuffer(
        m_deferredState.m_sceneShadingBuffer,
        Name("render.deferred_lighting.scene_shading"),
        "Scene Shading"
    );
    const Core::GpuGraphResourceId lights = importBuffer(
        m_deferredState.m_lightBuffer,
        Name("render.deferred_lighting.lights"),
        "Lights"
    );
    const Core::GpuGraphResourceId meshView = importBuffer(
        m_drawState.m_meshViewBuffer,
        Name("render.deferred.mesh_view"),
        "Mesh View"
    );
    const Core::GpuGraphResourceId materialInstances = m_drawState.m_instanceBuffer
        ? importBuffer(
            m_drawState.m_instanceBuffer,
            Name("render.deferred.material_instances"),
            "Material Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId materialTyped = m_drawState.m_materialTypedBuffer
        ? importBuffer(
            m_drawState.m_materialTypedBuffer,
            Name("render.deferred.material_typed"),
            "Material Typed Data"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgReceiverRanges = m_csgState.m_receiverRangeBuffer
        ? importBuffer(
            m_csgState.m_receiverRangeBuffer,
            Name("render.deferred.csg_receiver_ranges"),
            "CSG Receiver Ranges"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgCutters = m_csgState.m_cutterBuffer
        ? importBuffer(
            m_csgState.m_cutterBuffer,
            Name("render.deferred.csg_cutters"),
            "CSG Cutters"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgClipContextSlots = m_csgState.m_clipContextSlotsBuffer
        ? importBuffer(
            m_csgState.m_clipContextSlotsBuffer,
            Name("render.deferred.csg_clip_context_slots"),
            "CSG Clip Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId csgIntervalSampleState = m_csgState.m_intervalSampleStateBuffer
        ? importBuffer(
            m_csgState.m_intervalSampleStateBuffer,
            Name("render.deferred.csg_interval_sample_state"),
            "CSG Interval Sample State"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId bindlessSlots = history
        ? importBuffer(
            history->slotsBuffer,
            Name("render.deferred_lighting.bindless_slots"),
            "Lagged Deferred Bindless Slots"
        )
        : importCurrentBindlessSlots(
            Name("render.deferred_lighting.bindless_slots"),
            "Deferred Bindless Slots"
        )
    ;
    const Core::GpuGraphResourceId currentBindlessSlots =
        !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get()
            ? bindlessSlots
            : importCurrentBindlessSlots(
                Name("render.deferred_composite.bindless_slots"),
                "Deferred Bindless Slots"
            )
    ;
    const Core::GpuGraphResourceId materialContextSlots = m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        ? importBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.deferred.material_context_slots"),
            "Ray Trace Material Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;
    // The optional history copy is declared in this graph after Present, but records only after its accepted
    // producer snapshots exist. Active lighting samples the history resources above, so reuse those exact graph
    // identities for copy destinations and import only the current producer images that are otherwise absent.
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    if(capturesLaggedLightingHistory){
        historyCopyShadowVisibility = currentShadowVisibility;
        historyCopyCausticIrradiance = currentCausticIrradiance;
        historyCopySurfelIrradiance = history
            ? currentSurfelIrradiance
            : surfelIrradiance
        ;
        historyCopyDestinationShadowVisibility = history
            ? shadowVisibility
            : importTexture(
                captureHistory->shadowVisibility,
                Name("render.lagged_history_copy.history_shadow_visibility"),
                "History Shadow Visibility"
            )
        ;
        historyCopyDestinationCausticIrradiance = history
            ? causticIrradiance
            : importTexture(
                captureHistory->causticIrradiance,
                Name("render.lagged_history_copy.history_caustic_irradiance"),
                "History Caustic Irradiance"
            )
        ;
        historyCopyDestinationSurfelIrradiance = history
            ? surfelIrradiance
            : importTexture(
                captureHistory->surfelIrradiance,
                Name("render.lagged_history_copy.history_surfel_irradiance"),
                "History Surfel Irradiance"
            )
        ;
    }
    // AVBOIT shares the deferred graph's G-buffer and current bindless imports. Its private targets remain
    // distinct resources, while the compiler owns every producer/consumer state seed through Lighting and
    // Composite on both the live and active-lagged routes.
    const Core::GpuGraphResourceId avboitLowRaster = importTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    const Core::GpuGraphResourceId avboitAccumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId avboitAccumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId avboitTransmittance = importTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    const Core::GpuGraphResourceId avboitCoverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    const Core::GpuGraphResourceId avboitDepthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    const Core::GpuGraphResourceId avboitControl = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    const Core::GpuGraphResourceId avboitExtinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    const Core::GpuGraphResourceId avboitExtinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    const Core::GpuGraphResourceId avboitMaterialDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    const Core::GpuGraphResourceId avboitCsgDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !csgCapBackNormal.valid()
        || !csgIntervalDepth.valid()
        || !csgIntervalId.valid()
        || !csgReceiverEventData.valid()
        || !csgReceiverEventCount.valid()
        || !csgReceiverSpanData.valid()
        || !csgReceiverSpanCount.valid()
        || !csgRemovedIntervalDepth.valid()
        || !csgRemovedIntervalCapNormal.valid()
        || !csgRemovedIntervalData.valid()
        || !csgRemovedIntervalCount.valid()
        || !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !currentShadowVisibility.valid()
        || !currentCausticIrradiance.valid()
        || !currentSurfelIrradiance.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !meshView.valid()
        || !bindlessSlots.valid()
        || !currentBindlessSlots.valid()
        || (m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer && !materialContextSlots.valid())
        || (capturesLaggedLightingHistory && (
            !historyCopyShadowVisibility.valid()
            || !historyCopyCausticIrradiance.valid()
            || !historyCopySurfelIrradiance.valid()
            || !historyCopyDestinationShadowVisibility.valid()
            || !historyCopyDestinationCausticIrradiance.valid()
            || !historyCopyDestinationSurfelIrradiance.valid()
        ))
        || !avboitLowRaster.valid()
        || !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !avboitTransmittance.valid()
        || !avboitCoverage.valid()
        || !avboitDepthWarp.valid()
        || !avboitControl.valid()
        || !avboitExtinction.valid()
        || !avboitExtinctionOverflow.valid()
        || !avboitMaterialDomain.valid()
        || !avboitCsgDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }
    const Core::TextureSubresourceSet csgPeelSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgPeelLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverEventLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverSpanLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgRemovedIntervalLayerCount
    );
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources(0u, 1u, 0u, 1u);

    if(!declareDeferredShadowPrepareTask(
        deferredTargets,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometryResources.data(),
        traceGeometryResources.size(),
        softwareBvhBuildStateResources.data(),
        softwareBvhBuildStateResources.size(),
        softwareTraceResourcesPrepared,
        shadowPrepareTimingTicket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation packet"));
        return;
    }
    const Core::GpuTaskId shadowPrepareHandoffTask = m_deferredShadowPrepareAccelStructFinalizeTask.valid()
        ? m_deferredShadowPrepareAccelStructFinalizeTask
        : (m_deferredShadowPrepareHybridSoftwareTailTask.valid()
            ? m_deferredShadowPrepareHybridSoftwareTailTask
            : m_deferredShadowPrepareTask)
    ;
    const bool currentBindlessSlotsGraphOwned = m_deferredBindlessSlotsUploadTask.valid();

    if(!declareDeferredGraphicsPrefixTasks(
        deferredTargets,
        shadowPrepareHandoffTask,
        csgFrameState,
        hasOpaqueCsgFrameWork,
        meshViewAspectRatio,
        albedo,
        normal,
        worldPosition,
        depth,
        opaqueColor,
        sceneShading,
        lights,
        meshView,
        materialInstances,
        materialTyped,
        csgReceiverRanges,
        csgCutters,
        csgClipContextSlots,
        csgIntervalSampleState,
        csgCapBackNormal,
        csgIntervalDepth,
        csgIntervalId,
        csgReceiverEventData,
        csgReceiverEventCount,
        csgReceiverSpanData,
        csgReceiverSpanCount,
        csgRemovedIntervalDepth,
        csgRemovedIntervalCapNormal,
        csgRemovedIntervalData,
        csgRemovedIntervalCount,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometryResources.data(),
        traceGeometryResources.size(),
        shadowTraceGeometrySet,
        frameTimingTransaction,
        asyncPrefixTiming,
        deferredClearTiming,
        deferredClearTimingState,
        opaqueCsgIntervalClearTimingState,
        opaqueRegularSharedComputeEmulationTiming,
        opaqueCsgIntervalSampleComputeEmulationTiming,
        graphicsPrefixTimingTickets,
        asyncPrefixTimingSpansOnePacket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graphics-prefix packet"));
        return;
    }

    if(useLaggedLightingHistory){
        Core::GpuExternalCompletionDesc lightingHistoryCompletionDesc;
        lightingHistoryCompletionDesc
            .setIdentity(Name("render.deferred_lighting.lagged_history_complete"))
            .setMarkerLabel("Lagged Lighting History Complete")
        ;
        m_deferredLightingHistoryCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            lightingHistoryCompletionDesc
        );
        if(!m_deferredLightingHistoryCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history completion"));
            return;
        }
    }

    if(
        m_raytracingSystem.hasSurfelWork()
        && m_rayTracingState.m_surfelCountReadbackSubmissionToken.valid()
    ){
        Core::GpuExternalCompletionDesc surfelCounterReadbackCompletionDesc;
        surfelCounterReadbackCompletionDesc
            .setIdentity(Name("render.surfel_gi.counter_readback_complete"))
            .setMarkerLabel("Surfel Counter Readback Complete")
        ;
        m_deferredSurfelGiCounterReadbackCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            surfelCounterReadbackCompletionDesc
        );
        if(!m_deferredSurfelGiCounterReadbackCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback completion"));
            return;
        }
    }

    // Effects start from the accepted graphics-prefix packet and remain compiler-owned through the deferred suffix.
    // Shadow/Software are declared first so their queue assignments are stable before Surf, AVBOIT, and Lighting.
    if(!declareDeferredShadowVisibilityTask(
        deferredTargets,
        declaresHardwareCaustics,
        worldPosition,
        normal,
        depth,
        currentShadowVisibility,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        softwareTraceGeometryResources.data(),
        softwareTraceGeometryResources.size(),
        softwareTraceGeometrySet,
        traceMaterialSampledTextureSet,
        m_graphicsPrefixTask,
        shadowVisibilityTimingTicket,
        shadowVisibilityAsyncTiming,
        shadowVisibilityTiming,
        opaqueSoftResolveTiming,
        transparentSoftResolveTiming,
        shadowVisibilityOpaqueProduced,
        shadowVisibilityTransparentTraceProduced,
        shadowVisibilityOpaqueFrameIndex
    ))
        return;
    if(!declaresHardwareCaustics && !declareDeferredSoftwareCausticsTask(
        declaresHardwareCaustics,
        deferredTargets,
        worldPosition,
        depth,
        currentCausticIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        softwareTraceGeometryResources.data(),
        softwareTraceGeometryResources.size(),
        softwareTraceGeometrySet,
        traceMaterialSampledTextureSet,
        softwareCausticsTimingTicket,
        causticPhotonTiming,
        causticResolveTiming
    ))
        return;
    const Core::GpuTaskId effectsTask = declaresHardwareCaustics
        ? m_deferredShadowVisibilityTask
        : m_deferredSoftwareCausticsTask
    ;
    // Surfel GI remains the terminal effects task. Declaring it before hardware/AVBOIT preserves the established
    // effects -> surfel -> suffix order without renderer-side completion stitching.
    if(!declareDeferredSurfelGiTask(
        deferredTargets,
        worldPosition,
        normal,
        currentSurfelIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        (
            m_rayTracingState.m_surfelUseHwTrace
                ? hardwareTraceGeometryResources.data()
                : softwareTraceGeometryResources.data()
        ),
        (
            m_rayTracingState.m_surfelUseHwTrace
                ? hardwareTraceGeometryResources.size()
                : softwareTraceGeometryResources.size()
        ),
        (
            m_rayTracingState.m_surfelUseHwTrace
                ? hardwareTraceGeometrySet
                : softwareTraceGeometrySet
        ),
        traceMaterialSampledTextureSet,
        effectsTask,
        m_deferredSurfelGiCounterReadbackCompletion,
        surfelGiTimingTicket,
        surfelGiAsyncTiming
    ))
        return;

    // Hardware Caustics belongs to this graph so the live irradiance producer/consumer transition is compiler-owned.
    // It is declared before Lighting: declaration order establishes the live current-irradiance RAW edge, while the
    // lagged route uses distinct current/history targets and intentionally has no Hardware-to-Lighting dependency.
    if(declaresHardwareCaustics){
        const Core::GpuGraphResourceId causticAccumulator = importTexture(
            deferredTargets.causticAccumulator,
            Name("render.hardware_caustics.accumulator"),
            "Caustic Accumulator"
        );
        const Core::GpuGraphResourceId causticHistory = importTexture(
            deferredTargets.causticHistory,
            Name("render.hardware_caustics.history"),
            "Caustic History"
        );
        const Core::GpuGraphResourceId causticResolveHalf = importTexture(
            deferredTargets.causticResolveHalf,
            Name("render.hardware_caustics.resolve_half"),
            "Caustic Resolve Half"
        );
        const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
            deferredTargets.causticResolveGeometry,
            Name("render.hardware_caustics.resolve_geometry"),
            "Caustic Resolve Geometry"
        );
        const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
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
            return;
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
        const bool hardwareTraceAttributeStatesGraphOwned = hardwareTraceAttributeSet.valid();
        const Core::GpuTaskResourceSetUse hardwareTraceAttributeSetUse{
            .resourceSet = hardwareTraceAttributeSet,
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
        Core::GpuTaskResourceSetUse hardwarePhotonResourceSetUses[2u] = {};
        usize hardwarePhotonResourceSetUseCount = 0u;
        if(hardwareTraceAttributeStatesGraphOwned){
            hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = hardwareTraceAttributeSetUse;
        }
        if(traceMaterialSampledTextureSet.valid()){
            hardwarePhotonResourceSetUses[hardwarePhotonResourceSetUseCount++] = traceMaterialSampledTextureSetUse;
        }
        hardwarePhotonResourceUses.reserve(15u + (
            hardwareTraceAttributeStatesGraphOwned ? 0u : hardwareTraceAttributeResources.size()
        ));
        hardwareGeometryResourceUses.reserve(3u);
        hardwareResolvePrepareResourceUses.reserve(4u);
        hardwareResolveWaveletResourceUses.reserve(3u);
        hardwareResolveSecondWaveletResourceUses.reserve(3u);
        hardwareResolveThirdWaveletResourceUses.reserve(3u);
        hardwareResolveFourthWaveletResourceUses.reserve(3u);
        hardwareResolveFifthWaveletResourceUses.reserve(3u);
        hardwareResolveUpsampleResourceUses.reserve(5u);
        hardwarePhotonResourceUses.push_back(ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            true
        ));
        hardwarePhotonResourceUses.push_back(ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        hardwarePhotonResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource, true));
        hardwarePhotonResourceUses.push_back(ReadUse(sceneGeometryDomain));
        hardwarePhotonResourceUses.push_back(ReadWriteTextureUse(
            causticAccumulator,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        // Geometry downsample writes its cache after photons. The following wavelet callback reads that cache, so
        // the compiler owns their exact UAV-to-SRV handoff while ping-pong transitions remain local.
        hardwareGeometryResourceUses.push_back(ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareGeometryResourceUses.push_back(ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareGeometryResourceUses.push_back(ReadWriteTextureUse(
            causticResolveGeometry,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::UnorderedAccess
        ));

        // Prepare consumes both immutable graph-produced inputs, then writes the parity-selected first ping-pong
        // target. The five fixed wavelet passes read/write the alternating pair through graph barriers.
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
            hardwareResolvePrepareResourceUses.push_back(ReadTextureUse(
                causticHistory,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
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
            hardwareResolvePrepareResourceUses.push_back(ReadTextureUse(
                causticResolveHalf,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::ShaderResource
            ));
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

        // Upsample receives its exact final graph handoff. The following timing-close callback carries no resource use.
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ));
        hardwareResolveUpsampleResourceUses.push_back(ReadTextureUse(
            depth,
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
            currentCausticIrradiance,
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
                m_drawState.m_meshViewBuffer,
                Name("render.deferred.mesh_view"),
                "Mesh View",
                Core::ResourceStates::ConstantBuffer
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
            && appendOptionalReadBuffer(
                m_rayTracingState.m_causticEmissionTargetBuffer,
                Name("render.hardware_caustics.emission_targets"),
                "Caustic Emission Targets",
                Core::ResourceStates::ShaderResource
            )
        ;
        if(materialContextSlots.valid()){
            hardwarePhotonResourceUses.push_back(ReadUse(
                materialContextSlots,
                Core::ResourceStates::ConstantBuffer,
                true
            ));
        }
        // Hardware caustic closest-hit shaders directly heap-load the selected mesh attribute streams.  These are
        // centrally imported retained handles, so declaring them here gives the compiler the Prefix -> Caustics
        // SRV handoff instead of relying on the recorder's manual staging loop.
        for(const Core::GpuGraphResourceId resource : hardwareTraceAttributeResources){
            if(!resource.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid prepared hardware-caustics attribute resource"));
                return;
            }
            if(!hardwareTraceAttributeStatesGraphOwned)
                hardwarePhotonResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
        if(m_rayTracingState.m_tlas){
            const Core::GpuGraphResourceId tlas = m_deferredLightingTaskGraph.importAccelStruct(
                m_rayTracingState.m_tlas,
                AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
            );
            const Core::GpuGraphResourceId tlasBackingBuffer = importBuffer(
                m_rayTracingState.m_tlas->getBackingBufferHandle(),
                Name("render.deferred_effects.tlas_backing"),
                "Scene TLAS Backing"
            );
            optionalResourcesImported = optionalResourcesImported && tlas.valid() && tlasBackingBuffer.valid();
            if(tlas.valid() && tlasBackingBuffer.valid()){
                hardwarePhotonResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
                hardwarePhotonResourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
            }
        }
        if(!optionalResourcesImported){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
            return;
        }

        const Core::GpuTaskId hardwareDependencies[] = { m_graphicsPrefixTask };
        const Core::GpuExternalCompletionId* const hardwareExternalDependencies = useLaggedLightingHistory
            ? &m_deferredLightingHistoryCompletion
            : nullptr
        ;
        const usize hardwareExternalDependencyCount = useLaggedLightingHistory ? 1u : 0u;
        Core::GpuTaskSchedulingHint hardwareScheduling;
        hardwareScheduling.cost = Core::GpuTaskCostHint::Large;
        hardwareScheduling.forceSubmissionBoundary = true;
        hardwareScheduling.allowPacketMerge = false;

        // The lagged-history completion must protect the first writer too: clear starts the existing Hardware
        // Caustics Graphics packet and the ray-tracing producer merges into it below. A fresh temporal accumulator
        // inserts its typed zero clear between that no-producer result and the producer callback.
        Core::GpuTaskSchedulingHint irradianceClearScheduling;
        irradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        irradianceClearScheduling.allowPacketMerge = true;
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
        irradianceClear.destination = currentCausticIrradiance;
        irradianceClear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        irradianceClear.valueType = Core::GpuClearTextureTaskValueType::Float;
        irradianceClear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        const Core::GpuTaskId irradianceClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            irradianceClearDesc,
            irradianceClear
        );
        if(!irradianceClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics irradiance clear"));
            return;
        }
        m_deferredCausticIrradianceClearTask = irradianceClearTask;

        Core::GpuTaskId causticsDependency = irradianceClearTask;
        const bool graphOwnsNonTemporalAccumulatorClear = m_rayTracingState.m_causticTemporalDecay <= 0.f;
        if(graphOwnsNonTemporalAccumulatorClear){
            Core::GpuTaskSchedulingHint accumulatorNonTemporalClearScheduling = irradianceClearScheduling;
            accumulatorNonTemporalClearScheduling.mergeWithPrevious = true;
            Core::GpuTaskDesc accumulatorNonTemporalClearDesc;
            accumulatorNonTemporalClearDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_non_temporal_clear"))
                .setMarkerLabel("Hardware Caustics Accumulator Clear")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulatorNonTemporalClearScheduling)
                .setDependencies(&causticsDependency, 1u)
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
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics non-temporal accumulator clear"));
                return;
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
            Core::GpuTaskDesc accumulatorBootstrapClearDesc;
            accumulatorBootstrapClearDesc
                .setIdentity(Name("render.hardware_caustics.accumulator_bootstrap_clear"))
                .setMarkerLabel("Hardware Caustics Accumulator Bootstrap Clear")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulatorBootstrapClearScheduling)
                .setDependencies(&irradianceClearTask, 1u)
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
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator bootstrap clear"));
                return;
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
                .setResourceUses(accumulatorDecayUses, LengthOf(accumulatorDecayUses))
            ;
            const Core::GpuTaskId accumulatorDecayTask = m_raytracingSystem.declareCausticAccumulatorDecayTask(
                m_deferredLightingTaskGraph,
                accumulatorDecayDesc,
                deferredTargets,
                &m_preparedShadowVisibilityReady,
                m_rayTracingState.m_causticTemporalDecay,
                true,
                hardwareCausticsTimingTicket,
                &causticPhotonTiming,
                true
            );
            if(!accumulatorDecayTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred hardware-caustics accumulator decay"));
                return;
            }
            m_deferredCausticAccumulatorDecayTask = accumulatorDecayTask;
            causticsDependency = accumulatorDecayTask;
        }

        Core::GpuTaskSchedulingHint hardwareCausticsScheduling = hardwareScheduling;
        hardwareCausticsScheduling.forceSubmissionBoundary = false;
        hardwareCausticsScheduling.allowPacketMerge = true;
        hardwareCausticsScheduling.mergeWithPrevious = true;
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
        m_deferredCausticPhotonTask = m_raytracingSystem.declareHardwareCausticsTask(
            m_deferredLightingTaskGraph,
            hardwarePhotonDesc,
            deferredTargets,
            &m_preparedShadowVisibilityReady,
            hardwareCausticsTimingTicket,
            true,
            graphOwnsAccumulatorBootstrapClear,
            graphOwnsNonTemporalAccumulatorClear,
            graphOwnsAccumulatorDecay,
            true,
            &causticPhotonTiming,
            &m_deferredCausticProducerDispatched
        );
        if(!m_deferredCausticPhotonTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics photon graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareGeometryScheduling = hardwareCausticsScheduling;
        hardwareGeometryScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareGeometryDesc;
        hardwareGeometryDesc
            .setIdentity(Name("render.hardware_caustics.geometry_downsample"))
            .setMarkerLabel("Hardware Caustics Geometry Downsample")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareGeometryScheduling)
            .setDependencies(&m_deferredCausticPhotonTask, 1u)
            .setResourceUses(hardwareGeometryResourceUses.data(), hardwareGeometryResourceUses.size())
        ;
        m_deferredCausticGeometryTask = m_raytracingSystem.declareCausticGeometryDownsampleTask(
            m_deferredLightingTaskGraph,
            hardwareGeometryDesc,
            deferredTargets,
            hardwareCausticsTimingTicket,
            &m_deferredCausticProducerDispatched,
            &causticResolveTiming,
            true
        );
        if(!m_deferredCausticGeometryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics geometry graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolvePrepareScheduling = hardwareGeometryScheduling;
        hardwareResolvePrepareScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolvePrepareDesc;
        hardwareResolvePrepareDesc
            .setIdentity(Name("render.hardware_caustics.resolve_prepare"))
            .setMarkerLabel("Hardware Caustics Resolve Prepare")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolvePrepareScheduling)
            .setDependencies(&m_deferredCausticGeometryTask, 1u)
            .setResourceUses(hardwareResolvePrepareResourceUses.data(), hardwareResolvePrepareResourceUses.size())
        ;
        m_deferredCausticResolvePrepareTask = m_raytracingSystem.declareCausticResolvePrepareTask(
            m_deferredLightingTaskGraph,
            hardwareResolvePrepareDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolvePrepareTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveWaveletScheduling = hardwareResolvePrepareScheduling;
        hardwareResolveWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveWaveletDesc;
        hardwareResolveWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveWaveletScheduling)
            .setDependencies(&m_deferredCausticResolvePrepareTask, 1u)
            .setResourceUses(hardwareResolveWaveletResourceUses.data(), hardwareResolveWaveletResourceUses.size())
        ;
        m_deferredCausticResolveWaveletTask = m_raytracingSystem.declareCausticResolveWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveSecondWaveletScheduling = hardwareResolveWaveletScheduling;
        hardwareResolveSecondWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveSecondWaveletDesc;
        hardwareResolveSecondWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_second_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Second Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveSecondWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveSecondWaveletResourceUses.data(),
                hardwareResolveSecondWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveSecondWaveletTask = m_raytracingSystem.declareCausticResolveSecondWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveSecondWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveSecondWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveThirdWaveletScheduling = hardwareResolveSecondWaveletScheduling;
        hardwareResolveThirdWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveThirdWaveletDesc;
        hardwareResolveThirdWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_third_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Third Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveThirdWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveSecondWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveThirdWaveletResourceUses.data(),
                hardwareResolveThirdWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveThirdWaveletTask = m_raytracingSystem.declareCausticResolveThirdWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveThirdWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveThirdWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveFourthWaveletScheduling = hardwareResolveThirdWaveletScheduling;
        hardwareResolveFourthWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveFourthWaveletDesc;
        hardwareResolveFourthWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_fourth_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Fourth Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveFourthWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveThirdWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveFourthWaveletResourceUses.data(),
                hardwareResolveFourthWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveFourthWaveletTask = m_raytracingSystem.declareCausticResolveFourthWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveFourthWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveFourthWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveFifthWaveletScheduling = hardwareResolveFourthWaveletScheduling;
        hardwareResolveFifthWaveletScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveFifthWaveletDesc;
        hardwareResolveFifthWaveletDesc
            .setIdentity(Name("render.hardware_caustics.resolve_fifth_wavelet"))
            .setMarkerLabel("Hardware Caustics Resolve Fifth Wavelet")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveFifthWaveletScheduling)
            .setDependencies(&m_deferredCausticResolveFourthWaveletTask, 1u)
            .setResourceUses(
                hardwareResolveFifthWaveletResourceUses.data(),
                hardwareResolveFifthWaveletResourceUses.size()
            )
        ;
        m_deferredCausticResolveFifthWaveletTask = m_raytracingSystem.declareCausticResolveFifthWaveletTask(
            m_deferredLightingTaskGraph,
            hardwareResolveFifthWaveletDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveFifthWaveletTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveUpsampleScheduling = hardwareResolveFifthWaveletScheduling;
        hardwareResolveUpsampleScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveUpsampleDesc;
        hardwareResolveUpsampleDesc
            .setIdentity(Name("render.hardware_caustics.resolve_upsample"))
            .setMarkerLabel("Hardware Caustics Resolve Upsample")
            .setQueue(GraphicsPreferredComputeQueueRequest())
            .setScheduling(hardwareResolveUpsampleScheduling)
            .setDependencies(&m_deferredCausticResolveFifthWaveletTask, 1u)
            .setResourceUses(hardwareResolveUpsampleResourceUses.data(), hardwareResolveUpsampleResourceUses.size())
        ;
        m_deferredCausticResolveUpsampleTask = m_raytracingSystem.declareCausticResolveUpsampleTask(
            m_deferredLightingTaskGraph,
            hardwareResolveUpsampleDesc,
            deferredTargets,
            &m_deferredCausticProducerDispatched,
            true
        );
        if(!m_deferredCausticResolveUpsampleTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task"));
            return;
        }

        Core::GpuTaskSchedulingHint hardwareResolveScheduling = hardwareResolveUpsampleScheduling;
        hardwareResolveScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc hardwareResolveDesc;
        hardwareResolveDesc
            .setIdentity(Name("render.hardware_caustics.resolve_timing_close"))
            .setMarkerLabel("Hardware Caustics Resolve Timing Close")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(hardwareResolveScheduling)
            .setDependencies(&m_deferredCausticResolveUpsampleTask, 1u)
        ;
        m_deferredHardwareCausticsTask = m_raytracingSystem.declareCausticResolveTask(
            m_deferredLightingTaskGraph,
            hardwareResolveDesc,
            hardwareCausticsTimingTicket,
            &m_deferredCausticProducerDispatched,
            &causticResolveTiming
        );
        if(!m_deferredHardwareCausticsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task"));
            return;
        }
    }

    AvboitPreGraphTask::Payload avboitPrePayload{ m_arena };
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload avboitCsgReceiverSpanPayload{ m_arena };
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload avboitCsgIntervalCombinePayload{ m_arena };
    avboitPrePayload.avboitSystem = &m_avboitSystem;
    avboitPrePayload.targets = &deferredTargets;
    avboitPrePayload.csgFrameState = &csgFrameState;
    avboitPrePayload.timingTicket = &avboitPreTimingTicket;
    avboitPrePayload.transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming;
    avboitPrePayload.hasTransparentRenderers = hasTransparentRenderers;

    // Freeze the transparent CSG interval producer before AVBOIT native recording.  Its shared instance/material
    // and CSG buffers are intentionally overwritten by the later occupancy/extinction/accumulation compatibility
    // paths, so this snapshot applies only to the receiver-surface interval work immediately before occupancy.
    Core::GpuTaskId transparentCsgUploadTask = m_graphicsPrefixTask;
    Core::Alloc::ScratchArena transparentCsgMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet;
    Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet;
    const bool hasTransparentCsgFrameWork = hasTransparentRenderers
        && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork)
    ;
    if(hasTransparentCsgFrameWork){
        Core::Alloc::ScratchArena transparentCsgUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions transparentCsgDrawItems{ transparentCsgUploadScratch };
        InstanceGpuDataVector transparentCsgInstanceData{ transparentCsgUploadScratch };
        CsgFrameGpuData transparentCsgFrameData{ transparentCsgUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector transparentCsgMaterialTypedRanges{ transparentCsgUploadScratch };
#endif
        MaterialTypedByteDataVector transparentCsgMaterialTypedBytes{ transparentCsgUploadScratch };
        ECSRenderDetail::MeshViewGpuData transparentCsgMeshViewState;
        bool transparentCsgMeshViewUploadRequired = false;
        if(!m_meshSystem.prepareMeshViewBufferUpload(
            meshViewAspectRatio,
            transparentCsgMeshViewState,
            transparentCsgMeshViewUploadRequired
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare transparent CSG interval mesh-view data"));
            return;
        }
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            csgFrameState,
            transparentCsgDrawItems,
            transparentCsgInstanceData,
            transparentCsgFrameData,
#if defined(NWB_DEBUG)
            transparentCsgMaterialTypedRanges,
#endif
            transparentCsgMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly,
            &transparentCsgMeshViewState
        );

        if(!transparentCsgDrawItems.csgReceiverSurface.empty() && transparentCsgFrameData.hasWork()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !csgReceiverRanges.valid()
                || !csgCutters.valid()
                || !csgClipContextSlots.valid()
                || !csgIntervalSampleState.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    transparentCsgInstanceData,
                    transparentCsgMaterialTypedBytes
                )
                || !m_csgSystem.csgFrameBuffersReady(transparentCsgFrameData)
                || !m_materialSystem.materialPassDrawResourcesReady(transparentCsgDrawItems.csgReceiverSurface)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared transparent CSG interval resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const transparentCsgMaterialGeometryDrawSets[] = {
                &transparentCsgDrawItems.csgReceiverSurface,
            };
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                transparentCsgMaterialGeometryDrawSets,
                LengthOf(transparentCsgMaterialGeometryDrawSets),
                transparentCsgMaterialGeometryScratch,
                Name("render.avboit.intervals.transparent_csg_material_geometry"),
                "Transparent CSG Material Geometry",
                transparentCsgMaterialGeometrySet
            );
            if(!avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));
            const bool transparentCsgMaterialSampledTexturesCollected =
                avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    transparentCsgMaterialGeometryDrawSets,
                    LengthOf(transparentCsgMaterialGeometryDrawSets),
                    transparentCsgMaterialGeometryScratch,
                    Name("render.avboit.intervals.transparent_csg_material_sampled_textures"),
                    "Transparent CSG Material Sampled Textures",
                    transparentCsgMaterialSampledTextureSet
                )
            ;
            if(
                avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned
                && !transparentCsgMaterialSampledTexturesCollected
            )
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material sampled textures"));

            m_materialSystem.prepareMaterialPassInstanceUploadData(transparentCsgInstanceData);
#if defined(NWB_DEBUG)
            if(
                transparentCsgInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || transparentCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || transparentCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: transparent CSG interval upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(transparentCsgInstanceData.size() == transparentCsgMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                transparentCsgMaterialTypedRanges,
                transparentCsgMaterialTypedBytes
            );
#endif

            CsgClipContextSlots transparentCsgClipContextSlotData;
            CsgIntervalSampleStateGpuData transparentCsgIntervalSampleStateData;
            if(
                !m_csgSystem.prepareCsgClipContextSlotData(
                    transparentCsgFrameData,
                    transparentCsgClipContextSlotData
                )
                || !m_csgSystem.prepareCsgIntervalSampleStateData(
                    deferredTargets,
                    transparentCsgFrameData,
                    transparentCsgIntervalSampleStateData
                )
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return;
            }

            const Core::GpuUploadBlobId transparentCsgInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgInstanceData.data(),
                transparentCsgInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgMaterialTypedBytes.data(),
                transparentCsgMaterialTypedBytes.size(),
                alignof(u32)
            );
            const Core::GpuUploadBlobId transparentCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgFrameData.receiverRanges.data(),
                transparentCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                alignof(CsgReceiverRangeGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                transparentCsgFrameData.cutters.data(),
                transparentCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                alignof(CsgCutterGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                &transparentCsgClipContextSlotData,
                sizeof(transparentCsgClipContextSlotData),
                alignof(CsgClipContextSlots)
            );
            const Core::GpuUploadBlobId transparentCsgIntervalSampleStateBlob =
                m_deferredLightingTaskGraph.copyUploadData(
                    &transparentCsgIntervalSampleStateData,
                    sizeof(transparentCsgIntervalSampleStateData),
                    alignof(CsgIntervalSampleStateGpuData)
                )
            ;
            if(
                !transparentCsgInstanceBlob.valid()
                || !transparentCsgMaterialTypedBlob.valid()
                || !transparentCsgReceiverRangesBlob.valid()
                || !transparentCsgCuttersBlob.valid()
                || !transparentCsgClipContextSlotsBlob.valid()
                || !transparentCsgIntervalSampleStateBlob.valid()
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable transparent CSG interval upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint transparentCsgUploadScheduling;
            transparentCsgUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            transparentCsgUploadScheduling.forceSubmissionBoundary = false;
            transparentCsgUploadScheduling.allowPacketMerge = true;
            transparentCsgUploadScheduling.mergeWithPrevious = true;
            // Hardware Caustics and AVBOIT have independent timing and acceptance submissions even when both route
            // to Graphics. Start this frozen AVBOIT upload chain in its own packet, then merge every following
            // upload/clear/interval callback into that new semantic packet.
            Core::GpuTaskSchedulingHint transparentCsgFirstUploadScheduling = transparentCsgUploadScheduling;
            transparentCsgFirstUploadScheduling.mergeWithPrevious = false;

            Core::GpuTaskDesc transparentCsgInstanceUploadDesc;
            transparentCsgInstanceUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_instances_upload"))
                .setMarkerLabel("Transparent CSG Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgFirstUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material instance upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgMaterialTypedUploadDesc;
            transparentCsgMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_typed_upload"))
                .setMarkerLabel("Transparent CSG Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material typed upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgReceiverRangesUploadDesc;
            transparentCsgReceiverRangesUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.receiver_ranges_upload"))
                .setMarkerLabel("Transparent CSG Receiver Ranges Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgReceiverRangesUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgReceiverRangesBlob,
                    .destination = csgReceiverRanges,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-range upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgCuttersUploadDesc;
            transparentCsgCuttersUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.cutters_upload"))
                .setMarkerLabel("Transparent CSG Cutters Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgCuttersUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgCuttersBlob,
                    .destination = csgCutters,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG cutter upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgClipContextSlotsUploadDesc;
            transparentCsgClipContextSlotsUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.clip_context_slots_upload"))
                .setMarkerLabel("Transparent CSG Clip Context Slots Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgClipContextSlotsUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgClipContextSlotsBlob,
                    .destination = csgClipContextSlots,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG clip-context upload"));
                return;
            }

            Core::GpuTaskDesc transparentCsgIntervalSampleStateUploadDesc;
            transparentCsgIntervalSampleStateUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.interval_sample_state_upload"))
                .setMarkerLabel("Transparent CSG Interval State Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&transparentCsgUploadTask, 1u)
            ;
            transparentCsgUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                transparentCsgIntervalSampleStateUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgIntervalSampleStateBlob,
                    .destination = csgIntervalSampleState,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!transparentCsgUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-state upload"));
                return;
            }

            avboitPrePayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            avboitCsgReceiverSpanPayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            avboitCsgReceiverSpanPayload.csgFrameBuffersUploaded = true;
            avboitCsgIntervalCombinePayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            avboitCsgIntervalCombinePayload.csgFrameBuffersUploaded = true;
            avboitPrePayload.transparentCsgStreamsUploaded = true;
        }
    }

    // Prepared transparent CSG uses the same persistent interval values and peel targets as opaque CSG. Place its
    // frozen rect clear immediately after immutable stream uploads so the graph owns CopyDest -> UAV ordering,
    // then declare the CSG StorageImage working set on the producer task. An unprepared compatibility path
    // continues to call the legacy all-target helper.
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        transparentCsgIntervalClearScheduling.forceSubmissionBoundary = false;
        transparentCsgIntervalClearScheduling.allowPacketMerge = true;
        transparentCsgIntervalClearScheduling.mergeWithPrevious = true;
        const auto makeTransparentCsgIntervalClearTaskDesc = [&transparentCsgIntervalClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgIntervalClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const Core::Rect transparentCsgClearRect = avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(
            deferredTargets.width,
            deferredTargets.height
        );
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearBeginHooks{
            .context = &transparentCsgIntervalClearTimingState,
            .beforeClear = &ECSRenderDetail::BeginCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearEndHooks{
            .context = &transparentCsgIntervalClearTimingState,
            .afterClear = &ECSRenderDetail::EndCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        m_deferredAvboitTransparentCsgIntervalClearFirstTask =
            m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
                makeTransparentCsgIntervalClearTaskDesc(
                    Name("render.avboit.transparent_csg.interval_clear"),
                    "Transparent CSG Interval Id Clear",
                    transparentCsgUploadTask
                ),
                Core::GpuClearTextureRectUIntTaskDesc{
                    .destination = csgIntervalId,
                    .subresources = csgPeelSubresources,
                    .rect = transparentCsgClearRect,
                    .uintValue = Core::UIntColor(0u),
                    .recordHooks = transparentCsgIntervalClearBeginHooks,
                }
            );
        if(!m_deferredAvboitTransparentCsgIntervalClearFirstTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval-id clear"));
            return;
        }
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearTailScheduling = transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc transparentCsgIntervalClearTailDesc;
        transparentCsgIntervalClearTailDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_event_count_clear"))
            .setMarkerLabel("Transparent CSG Receiver Event Count Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(transparentCsgIntervalClearTailScheduling)
            .setDependencies(&m_deferredAvboitTransparentCsgIntervalClearFirstTask, 1u)
        ;
        m_deferredAvboitTransparentCsgIntervalClearTask = m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
            transparentCsgIntervalClearTailDesc,
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = csgReceiverEventCount,
                .subresources = csgReceiverEventCountSubresources,
                .rect = transparentCsgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = transparentCsgIntervalClearEndHooks,
            }
        );
        if(!m_deferredAvboitTransparentCsgIntervalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG receiver-event clear"));
            return;
        }
        transparentCsgUploadTask = m_deferredAvboitTransparentCsgIntervalClearTask;
        avboitPrePayload.transparentCsgIntervalTargetsGraphOwned = true;
        avboitPrePayload.transparentCsgIntervalPeelTargetStatesGraphOwned = true;
        avboitPrePayload.transparentCsgReceiverSurfaceImageStatesGraphOwned = true;
        // The following Span/Combine callbacks own their exact UAV handoffs, while direct and aggregate
        // compatibility calls retain native fences.
        avboitPrePayload.deferTransparentCsgIntervalCombine = true;
        avboitPrePayload.transparentCsgClipBufferStatesGraphOwned = true;
        avboitPrePayload.transparentCsgMaterialFrameStatesGraphOwned = true;
        NWB_ASSERT(
            avboitPrePayload.transparentCsgStreamsUploaded
            && avboitPrePayload.transparentCsgSnapshot.captured
        );
    }

    // The interval producer consumes the first frozen transparent CSG stream. Its graph-visible states must be
    // declared here, before its native work records, rather than on the later occupancy task.
    Core::Alloc::ScratchArena avboitIntervalResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ avboitIntervalResourceScratch };
    avboitIntervalResourceUses.reserve(16u);
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        avboitIntervalResourceUses.push_back(ReadUse(depth));
        avboitIntervalResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    const Core::GpuTaskResourceSetUse transparentCsgMaterialGeometrySetUse{
        .resourceSet = transparentCsgMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse transparentCsgMaterialSampledTextureSetUse{
        .resourceSet = transparentCsgMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse transparentCsgMaterialResourceSetUses[2u] = {};
    usize transparentCsgMaterialResourceSetUseCount = 0u;
    if(avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned){
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] =
            transparentCsgMaterialGeometrySetUse;
    }
    if(transparentCsgMaterialSampledTextureSet.valid()){
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] =
            transparentCsgMaterialSampledTextureSetUse;
    }
    avboitIntervalResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer, true));
    avboitIntervalResourceUses.push_back(ReadUse(avboitMaterialDomain));
    avboitIntervalResourceUses.push_back(ReadWriteUse(avboitCsgDomain, Core::ResourceStates::ShaderResource));

    Core::GpuTaskSchedulingHint avboitIntervalScheduling;
    avboitIntervalScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitIntervalScheduling.forceSubmissionBoundary = false;
    avboitIntervalScheduling.allowPacketMerge = true;
    avboitIntervalScheduling.mergeWithPrevious = avboitPrePayload.transparentCsgStreamsUploaded;
    Core::GpuTaskDesc avboitIntervalDesc;
    avboitIntervalDesc
        .setIdentity(Name("render.avboit.intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(avboitIntervalScheduling)
        .setDependencies(&transparentCsgUploadTask, 1u)
        .setResourceUses(avboitIntervalResourceUses.data(), avboitIntervalResourceUses.size())
        .setResourceSetUses(
            transparentCsgMaterialResourceSetUseCount != 0u ? transparentCsgMaterialResourceSetUses : nullptr,
            transparentCsgMaterialResourceSetUseCount
        )
    ;
    const bool avboitCsgReceiverSpanGraphOwned =
        avboitPrePayload.transparentCsgStreamsUploaded
        && avboitPrePayload.transparentCsgSnapshot.captured
        && avboitPrePayload.deferTransparentCsgIntervalCombine
        && avboitCsgReceiverSpanPayload.transparentCsgSnapshot.captured
        && avboitCsgReceiverSpanPayload.csgFrameBuffersUploaded
    ;
    const bool avboitCsgIntervalCombineGraphOwned =
        avboitCsgReceiverSpanGraphOwned
        && avboitCsgIntervalCombinePayload.transparentCsgSnapshot.captured
        && avboitCsgIntervalCombinePayload.csgFrameBuffersUploaded
    ;
    Core::Alloc::ScratchArena avboitIntervalSpanResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalSpanResourceUses{
        avboitIntervalSpanResourceScratch
    };
    Core::Alloc::ScratchArena avboitIntervalCombineResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalCombineResourceUses{
        avboitIntervalCombineResourceScratch
    };
    if(avboitCsgReceiverSpanGraphOwned){
        avboitIntervalSpanResourceUses.reserve(6u);
        avboitIntervalSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(ReadUse(
            csgClipContextSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalSpanResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        avboitIntervalSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitCsgReceiverSpanPayload.renderer = this;
        avboitCsgReceiverSpanPayload.targets = &deferredTargets;
        avboitCsgReceiverSpanPayload.timingTicket = &avboitPreTimingTicket;
        avboitCsgReceiverSpanPayload.transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming;
        avboitCsgReceiverSpanPayload.receiverSpanInputImageStatesGraphOwned = true;
        avboitCsgReceiverSpanPayload.receiverSpanOutputImageStatesGraphOwned = true;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        avboitIntervalCombineResourceUses.reserve(11u);
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgCapBackNormal,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalDepth,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalId,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadUse(
            csgClipContextSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalCombineResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitCsgIntervalCombinePayload.renderer = this;
        avboitCsgIntervalCombinePayload.targets = &deferredTargets;
        avboitCsgIntervalCombinePayload.timingTicket = &avboitPreTimingTicket;
        avboitCsgIntervalCombinePayload.transparentCsgIntervalsTiming = &transparentCsgIntervalsTiming;
        avboitCsgIntervalCombinePayload.intervalCombineInputImageStatesGraphOwned = true;
        avboitCsgIntervalCombinePayload.removedIntervalOutputImageStatesGraphOwned = true;
    }
    m_deferredAvboitPreTask = m_deferredLightingTaskGraph.addTask<AvboitPreGraphTask>(
        avboitIntervalDesc,
        Move(avboitPrePayload)
    );
    if(!m_deferredAvboitPreTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph task"));
        return;
    }

    Core::GpuTaskId avboitIntervalCompletionTask = m_deferredAvboitPreTask;
    bool avboitIntervalOutputsGraphOwned = false;
    if(avboitCsgReceiverSpanGraphOwned){
        Core::GpuTaskSchedulingHint avboitIntervalSpanScheduling;
        avboitIntervalSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalSpanScheduling.forceSubmissionBoundary = false;
        avboitIntervalSpanScheduling.allowPacketMerge = true;
        avboitIntervalSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalSpanDesc;
        avboitIntervalSpanDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_span"))
            .setMarkerLabel("Transparent CSG Receiver Span")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitIntervalSpanScheduling)
            .setDependencies(&m_deferredAvboitPreTask, 1u)
            .setResourceUses(
                avboitIntervalSpanResourceUses.data(),
                avboitIntervalSpanResourceUses.size()
            )
        ;
        m_deferredAvboitCsgReceiverSpanTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::AvboitCsgReceiverSpanGraphTask
        >(
            avboitIntervalSpanDesc,
            Move(avboitCsgReceiverSpanPayload)
        );
        if(!m_deferredAvboitCsgReceiverSpanTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-span graph task"));
            return;
        }
        avboitIntervalCompletionTask = m_deferredAvboitCsgReceiverSpanTask;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        Core::GpuTaskSchedulingHint avboitIntervalCombineScheduling;
        avboitIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalCombineScheduling.forceSubmissionBoundary = false;
        avboitIntervalCombineScheduling.allowPacketMerge = true;
        avboitIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalCombineDesc;
        avboitIntervalCombineDesc
            .setIdentity(Name("render.avboit.transparent_csg.interval_combine"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitIntervalCombineScheduling)
            .setDependencies(&avboitIntervalCompletionTask, 1u)
            .setResourceUses(
                avboitIntervalCombineResourceUses.data(),
                avboitIntervalCombineResourceUses.size()
            )
        ;
        m_deferredAvboitCsgIntervalCombineTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::AvboitCsgIntervalCombineGraphTask
        >(
            avboitIntervalCombineDesc,
            Move(avboitCsgIntervalCombinePayload)
        );
        if(!m_deferredAvboitCsgIntervalCombineTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-combine graph task"));
            return;
        }
        avboitIntervalCompletionTask = m_deferredAvboitCsgIntervalCombineTask;
        avboitIntervalOutputsGraphOwned = true;
    }

    AvboitOccupancyGraphTask::Payload avboitOccupancyPayload{ m_arena };
    AvboitOccupancyComputeEmulationGraphTask::Payload avboitOccupancyComputeEmulationPayload{ m_arena };
    avboitOccupancyPayload.avboitSystem = &m_avboitSystem;
    avboitOccupancyPayload.targets = &deferredTargets;
    avboitOccupancyPayload.csgFrameState = &csgFrameState;
    avboitOccupancyPayload.timingTicket = &avboitPreTimingTicket;
    avboitOccupancyPayload.hasTransparentRenderers = hasTransparentRenderers;

    Core::GpuTaskId occupancyUploadTask = avboitIntervalCompletionTask;
    bool occupancyCsgStreamsUploaded = false;
    bool occupancyRegularComputeEmulationPlanCaptured = false;
    bool occupancyCsgComputeEmulationPlanCaptured = false;
    bool occupancySharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan occupancySharedComputeEmulationPlan;
    usize occupancySharedComputeEmulationInstanceCount = 0u;
    usize occupancySharedComputeEmulationMaterialTypedByteCount = 0u;
    bool occupancyMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena occupancyMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId occupancyMaterialGeometrySet;
    Core::GpuGraphResourceSetId occupancyMaterialSampledTextureSet;
    if(hasTransparentRenderers){
        Core::Alloc::ScratchArena occupancyUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions occupancyDrawItems{ occupancyUploadScratch };
        InstanceGpuDataVector occupancyInstanceData{ occupancyUploadScratch };
        CsgFrameGpuData occupancyCsgFrameData{ occupancyUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector occupancyMaterialTypedRanges{ occupancyUploadScratch };
#endif
        MaterialTypedByteDataVector occupancyMaterialTypedBytes{ occupancyUploadScratch };
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.avboit.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitOccupancy,
            true,
            csgFrameState,
            occupancyDrawItems,
            occupancyInstanceData,
            occupancyCsgFrameData,
#if defined(NWB_DEBUG)
            occupancyMaterialTypedRanges,
#endif
            occupancyMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly
        );

        const bool occupancyHasCsgDrawItems = !occupancyDrawItems.csg.empty();
        if(!occupancyDrawItems.empty()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    occupancyInstanceData,
                    occupancyMaterialTypedBytes
                )
                || !m_materialSystem.materialPassDrawResourcesReady(occupancyDrawItems.regular)
                || (occupancyHasCsgDrawItems && (
                    !occupancyCsgFrameData.hasWork()
                    ||
                    !csgReceiverRanges.valid()
                    || !csgCutters.valid()
                    || !csgClipContextSlots.valid()
                    || !m_csgSystem.csgFrameBuffersReady(occupancyCsgFrameData)
                    || !m_materialSystem.materialPassDrawResourcesReady(occupancyDrawItems.csg)
                ))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT occupancy resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const occupancyMaterialGeometryDrawSets[] = {
                &occupancyDrawItems.regular,
                &occupancyDrawItems.csg,
            };
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                occupancyMaterialGeometryDrawSets,
                LengthOf(occupancyMaterialGeometryDrawSets),
                occupancyMaterialGeometryScratch,
                Name("render.avboit.occupancy.material_geometry"),
                "AVBOIT Occupancy Material Geometry",
                occupancyMaterialGeometrySet
            );
            if(!avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT occupancy material geometry states"));
            occupancyMaterialSampledTexturesCollected =
                avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    occupancyMaterialGeometryDrawSets,
                    LengthOf(occupancyMaterialGeometryDrawSets),
                    occupancyMaterialGeometryScratch,
                    Name("render.avboit.occupancy.material_sampled_textures"),
                    "AVBOIT Occupancy Material Sampled Textures",
                    occupancyMaterialSampledTextureSet
                )
            ;
            if(
                avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && !occupancyMaterialSampledTexturesCollected
            )
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT occupancy material sampled textures"));

            m_materialSystem.prepareMaterialPassInstanceUploadData(occupancyInstanceData);
#if defined(NWB_DEBUG)
            if(
                occupancyInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || occupancyCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || occupancyCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT occupancy upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(occupancyInstanceData.size() == occupancyMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                occupancyMaterialTypedRanges,
                occupancyMaterialTypedBytes
            );
#endif

            const Core::GpuUploadBlobId occupancyInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                occupancyInstanceData.data(),
                occupancyInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId occupancyMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                occupancyMaterialTypedBytes.data(),
                occupancyMaterialTypedBytes.size(),
                alignof(u32)
            );
            if(!occupancyInstanceBlob.valid() || !occupancyMaterialTypedBlob.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT occupancy material upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint occupancyUploadScheduling;
            occupancyUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            occupancyUploadScheduling.forceSubmissionBoundary = false;
            occupancyUploadScheduling.allowPacketMerge = true;
            occupancyUploadScheduling.mergeWithPrevious = true;

            Core::GpuTaskDesc occupancyInstanceUploadDesc;
            occupancyInstanceUploadDesc
                .setIdentity(Name("render.avboit.occupancy.material_instances_upload"))
                .setMarkerLabel("AVBOIT Occupancy Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(occupancyUploadScheduling)
                .setDependencies(&occupancyUploadTask, 1u)
            ;
            occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                occupancyInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = occupancyInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!occupancyUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy material instance upload"));
                return;
            }

            Core::GpuTaskDesc occupancyMaterialTypedUploadDesc;
            occupancyMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.occupancy.material_typed_upload"))
                .setMarkerLabel("AVBOIT Occupancy Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(occupancyUploadScheduling)
                .setDependencies(&occupancyUploadTask, 1u)
            ;
            occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                occupancyMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = occupancyMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!occupancyUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy material typed upload"));
                return;
            }

            if(occupancyHasCsgDrawItems){
                CsgClipContextSlots occupancyCsgClipContextSlotData;
                if(!m_csgSystem.prepareCsgClipContextSlotData(
                    occupancyCsgFrameData,
                    occupancyCsgClipContextSlotData
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT occupancy CSG context data"));
                    return;
                }
                const Core::GpuUploadBlobId occupancyCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                    occupancyCsgFrameData.receiverRanges.data(),
                    occupancyCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                    alignof(CsgReceiverRangeGpuData)
                );
                const Core::GpuUploadBlobId occupancyCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                    occupancyCsgFrameData.cutters.data(),
                    occupancyCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                    alignof(CsgCutterGpuData)
                );
                const Core::GpuUploadBlobId occupancyCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                    &occupancyCsgClipContextSlotData,
                    sizeof(occupancyCsgClipContextSlotData),
                    alignof(CsgClipContextSlots)
                );
                if(
                    !occupancyCsgReceiverRangesBlob.valid()
                    || !occupancyCsgCuttersBlob.valid()
                    || !occupancyCsgClipContextSlotsBlob.valid()
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT occupancy CSG upload data"));
                    return;
                }

                Core::GpuTaskDesc occupancyCsgReceiverRangesUploadDesc;
                occupancyCsgReceiverRangesUploadDesc
                    .setIdentity(Name("render.avboit.occupancy.csg_receiver_ranges_upload"))
                    .setMarkerLabel("AVBOIT Occupancy CSG Receiver Ranges Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(occupancyUploadScheduling)
                    .setDependencies(&occupancyUploadTask, 1u)
                ;
                occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    occupancyCsgReceiverRangesUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = occupancyCsgReceiverRangesBlob,
                        .destination = csgReceiverRanges,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!occupancyUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy CSG receiver-range upload"));
                    return;
                }

                Core::GpuTaskDesc occupancyCsgCuttersUploadDesc;
                occupancyCsgCuttersUploadDesc
                    .setIdentity(Name("render.avboit.occupancy.csg_cutters_upload"))
                    .setMarkerLabel("AVBOIT Occupancy CSG Cutters Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(occupancyUploadScheduling)
                    .setDependencies(&occupancyUploadTask, 1u)
                ;
                occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    occupancyCsgCuttersUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = occupancyCsgCuttersBlob,
                        .destination = csgCutters,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!occupancyUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy CSG cutter upload"));
                    return;
                }

                Core::GpuTaskDesc occupancyCsgClipContextSlotsUploadDesc;
                occupancyCsgClipContextSlotsUploadDesc
                    .setIdentity(Name("render.avboit.occupancy.csg_clip_context_slots_upload"))
                    .setMarkerLabel("AVBOIT Occupancy CSG Clip Context Slots Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(occupancyUploadScheduling)
                    .setDependencies(&occupancyUploadTask, 1u)
                ;
                occupancyUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    occupancyCsgClipContextSlotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = occupancyCsgClipContextSlotsBlob,
                        .destination = csgClipContextSlots,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!occupancyUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy CSG clip-context upload"));
                    return;
                }
                occupancyCsgStreamsUploaded = true;
            }

            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
            avboitOccupancyPayload.occupancyStreamsUploaded = true;
            // A phase may graph-own exactly one alias-free compute stream. Mixed regular/CSG work retains the
            // established local interleaving because a single producer/raster handoff cannot preserve its draw
            // order.
            occupancyRegularComputeEmulationPlanCaptured = occupancyDrawItems.csg.computeDrawItems.empty()
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && avboitOccupancyComputeEmulationPayload.plan.capture(
                    m_meshSystem,
                    occupancyDrawItems.regular
                )
            ;
            occupancyCsgComputeEmulationPlanCaptured = occupancyDrawItems.regular.computeDrawItems.empty()
                && occupancyCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && avboitOccupancyComputeEmulationPayload.csgPlan.capture(
                    m_meshSystem,
                    occupancyDrawItems.csg,
                    occupancyCsgFrameData
                )
            ;
            // The unsplit all-compute two-, three-, or four-draw case can preserve one shared generated output only
            // as an explicit D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) -> R(D)] sequence. Keep mesh and
            // CSG work out of this narrow slice so the aggregate Occupancy callback is never partially replayed
            // around its phases.
            occupancySharedComputeEmulationPlanCaptured = !splitAvboitStages
                && !occupancyRegularComputeEmulationPlanCaptured
                && occupancyDrawItems.regular.meshDrawItems.empty()
                && occupancyDrawItems.csg.empty()
                && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
                && occupancyMaterialSampledTexturesCollected
                && occupancySharedComputeEmulationPlan.capture(
                    m_meshSystem,
                    occupancyDrawItems.regular,
                    4u
                )
                && occupancySharedComputeEmulationPlan.drawCount >= 2u
                && occupancySharedComputeEmulationPlan.drawCount <= 4u
            ;
            NWB_ASSERT(
                !(occupancyRegularComputeEmulationPlanCaptured && occupancyCsgComputeEmulationPlanCaptured)
            );
            NWB_ASSERT(
                !occupancySharedComputeEmulationPlanCaptured
                || (!occupancyRegularComputeEmulationPlanCaptured
                    && !occupancyCsgComputeEmulationPlanCaptured)
            );
            if(occupancySharedComputeEmulationPlanCaptured){
                occupancySharedComputeEmulationInstanceCount = occupancyInstanceData.size();
                occupancySharedComputeEmulationMaterialTypedByteCount = occupancyMaterialTypedBytes.size();
            }
        }
        else{
            // The graph phase is still authoritative for an empty visible set. Retaining the empty snapshot prevents
            // native recording from re-gathering mutable renderer state as a compatibility fallback.
            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
        }
    }

    // Preserve the native order: phase-local material/CSG uploads first, then the serial AVBOIT target values, then
    // occupancy. Each value now records as a typed built-in clear, so the graph owns all nine CopyDest operations
    // instead of a custom native thunk hiding them behind one broad resource-use declaration.
    Core::GpuTaskId avboitClearTask = occupancyUploadTask;
    if(clearAvboitTargets){
        Core::GpuTaskSchedulingHint avboitClearScheduling;
        avboitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        avboitClearScheduling.forceSubmissionBoundary = false;
        avboitClearScheduling.allowPacketMerge = true;
        avboitClearScheduling.mergeWithPrevious = true;
        const auto makeAvboitClearTaskDesc = [&avboitClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(avboitClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const auto makeAvboitFloatClearDesc = [](
            const Core::GpuGraphResourceId destination,
            const Core::Color& value,
            const Core::GpuClearTextureTaskRecordHooks& recordHooks = {}
        ){
            Core::GpuClearTextureTaskDesc clearDesc;
            clearDesc.destination = destination;
            clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
            clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
            clearDesc.floatValue = value;
            clearDesc.recordHooks = recordHooks;
            return clearDesc;
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearBeginHooks{
            .context = &avboitClearTimingState,
            .beforeClear = &ECSRenderDetail::BeginAvboitClearTiming,
            .discarded = &ECSRenderDetail::DiscardAvboitClearTiming,
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearEndHooks{
            .context = &avboitClearTimingState,
            .afterClear = &ECSRenderDetail::EndAvboitClearTiming,
            .discarded = &ECSRenderDetail::DiscardAvboitClearTiming,
        };
        const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.low_raster"),
                "AVBOIT Clear Low Raster",
                occupancyUploadTask
            ),
            makeAvboitFloatClearDesc(avboitLowRaster, transparentBlack, avboitClearBeginHooks)
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT low-raster clear"));
            return;
        }
        m_deferredAvboitClearFirstTask = avboitClearTask;
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.accum_color"),
                "AVBOIT Clear Accumulation Color",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(avboitAccumColor, transparentBlack)
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT accumulation-color clear"));
            return;
        }
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.accum_extinction"),
                "AVBOIT Clear Accumulation Extinction",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(avboitAccumExtinction, transparentBlack)
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT accumulation-extinction clear"));
            return;
        }
        const auto appendAvboitBufferClear = [&](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuGraphResourceId destination,
            const u32 value
        ){
            avboitClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                makeAvboitClearTaskDesc(identity, markerLabel, avboitClearTask),
                Core::GpuClearBufferTaskDesc{
                    .destination = destination,
                    .clearValue = value,
                }
            );
            return avboitClearTask.valid();
        };
        if(
            !appendAvboitBufferClear(
                Name("render.avboit.clear.coverage"),
                "AVBOIT Clear Coverage",
                avboitCoverage,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.depth_warp"),
                "AVBOIT Clear Depth Warp",
                avboitDepthWarp,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.control"),
                "AVBOIT Clear Control",
                avboitControl,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.extinction"),
                "AVBOIT Clear Extinction",
                avboitExtinction,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.extinction_overflow"),
                "AVBOIT Clear Extinction Overflow",
                avboitExtinctionOverflow,
                NWB_AVBOIT_OVERFLOW_INVALID
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT buffer clear"));
            return;
        }
        avboitClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.transmittance"),
                "AVBOIT Clear Transmittance",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(
                avboitTransmittance,
                Core::Color(1.f, 1.f, 1.f, 1.f),
                avboitClearEndHooks
            )
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT transmittance clear"));
            return;
        }
        m_deferredAvboitClearTask = avboitClearTask;
    }

    const bool occupancyCsgIntervalSampleImageStatesGraphOwned =
        avboitIntervalOutputsGraphOwned && occupancyCsgStreamsUploaded
    ;
    const bool occupancyCsgClipBufferStatesGraphOwned = occupancyCsgStreamsUploaded;
    NWB_ASSERT(
        !occupancyCsgIntervalSampleImageStatesGraphOwned
        || (
            avboitOccupancyPayload.occupancyStreamsUploaded
            && avboitOccupancyPayload.occupancySnapshot.captured
        )
    );
    NWB_ASSERT(
        !occupancyCsgClipBufferStatesGraphOwned
        || (
            avboitOccupancyPayload.occupancyStreamsUploaded
            && avboitOccupancyPayload.occupancySnapshot.captured
        )
    );
    avboitOccupancyPayload.occupancyCsgIntervalSampleImageStatesGraphOwned =
        occupancyCsgIntervalSampleImageStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyCsgClipBufferStatesGraphOwned =
        occupancyCsgClipBufferStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyMaterialFrameStatesGraphOwned = avboitOccupancyPayload.occupancyStreamsUploaded;
    avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned =
        avboitOccupancyPayload.occupancyStreamsUploaded
        && avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId occupancyComputeEmulationOutputSet;
    Core::Alloc::ScratchArena occupancyComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool occupancyComputeEmulationPlanCaptured =
        occupancyRegularComputeEmulationPlanCaptured
        || occupancyCsgComputeEmulationPlanCaptured
    ;
    bool occupancyComputeEmulationOutputStatesGraphOwned = false;
    if(occupancyRegularComputeEmulationPlanCaptured){
        occupancyComputeEmulationOutputStatesGraphOwned = GatherAvboitAliasFreeComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            avboitOccupancyComputeEmulationPayload.plan,
            occupancyComputeEmulationResourceScratch,
            Name("render.avboit.occupancy.compute_emulation.outputs"),
            "AVBOIT Occupancy Compute Emulation Outputs",
            occupancyComputeEmulationOutputSet
        );
    }
    else if(occupancyCsgComputeEmulationPlanCaptured){
        occupancyComputeEmulationOutputStatesGraphOwned =
            GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
                m_deferredLightingTaskGraph,
                avboitOccupancyComputeEmulationPayload.csgPlan,
                occupancyComputeEmulationResourceScratch,
                Name("render.avboit.occupancy.csg_compute_emulation.outputs"),
                "AVBOIT Occupancy CSG Compute Emulation Outputs",
                occupancyComputeEmulationOutputSet
            )
        ;
    }
    if(
        occupancyComputeEmulationPlanCaptured
        && !occupancyComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Occupancy compute-emulation output states"
        ));
    }
    avboitOccupancyPayload.occupancyComputeEmulationOutputStatesGraphOwned =
        occupancyRegularComputeEmulationPlanCaptured
        && occupancyComputeEmulationOutputStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyCsgComputeEmulationOutputStatesGraphOwned =
        occupancyCsgComputeEmulationPlanCaptured
        && occupancyComputeEmulationOutputStatesGraphOwned
    ;
    avboitOccupancyPayload.occupancyComputeEmulationTiming =
        occupancyComputeEmulationOutputStatesGraphOwned
            ? &avboitOccupancyComputeEmulationTiming
            : nullptr
    ;
    Core::GpuGraphResourceId occupancySharedComputeEmulationOutput;
    const bool occupancySharedComputeEmulationOutputStatesGraphOwned =
        occupancySharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            occupancySharedComputeEmulationPlan,
            "AVBOIT Occupancy Shared Compute Emulation Output",
            occupancySharedComputeEmulationOutput
        )
    ;
    if(
        occupancySharedComputeEmulationPlanCaptured
        && !occupancySharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Occupancy shared compute-emulation output state"
        ));
    }

    Core::Alloc::ScratchArena avboitPreResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitPreResourceUses{ avboitPreResourceScratch };
    avboitPreResourceUses.reserve(
        13u
        + (avboitOccupancyPayload.occupancyStreamsUploaded ? 7u : 0u)
        + (occupancyCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    avboitPreResourceUses.push_back(ReadUse(albedo));
    avboitPreResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource, true));
    avboitPreResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true));
    avboitPreResourceUses.push_back(ReadUse(depth));
    avboitPreResourceUses.push_back(ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
    avboitPreResourceUses.push_back(ReadWriteUse(avboitCoverage, Core::ResourceStates::UnorderedAccess));
    if(avboitOccupancyPayload.occupancyStreamsUploaded){
        avboitPreResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitPreResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        avboitPreResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        if(occupancyCsgStreamsUploaded){
            avboitPreResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            avboitPreResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            avboitPreResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // The preceding full-resolution interval producer owns this state. Occupancy only samples it.
            avboitPreResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        }
    }
    if(occupancyCsgIntervalSampleImageStatesGraphOwned){
        // The prepared interval producer wrote these aliases in the preceding AVBOIT task. The occupancy material
        // shaders load them through StorageImage descriptors, so the graph lowers the required UAV handoff here.
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    const Core::GpuTaskResourceSetUse occupancyMaterialGeometrySetUse{
        .resourceSet = occupancyMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse occupancyMaterialSampledTextureSetUse{
        .resourceSet = occupancyMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse occupancyComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = occupancyComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse occupancyMaterialResourceSetUses[3u] = {};
    usize occupancyMaterialResourceSetUseCount = 0u;
    if(avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned)
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] = occupancyMaterialGeometrySetUse;
    if(occupancyMaterialSampledTextureSet.valid())
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] = occupancyMaterialSampledTextureSetUse;
    if(occupancyComputeEmulationOutputStatesGraphOwned){
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] =
            occupancyComputeEmulationOutputVertexBufferSetUse;
    }
    avboitPreResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer, true));
    avboitPreResourceUses.push_back(ReadUse(avboitMaterialDomain));
    avboitPreResourceUses.push_back(ReadUse(avboitCsgDomain, Core::ResourceStates::ShaderResource));

    const Core::GpuTaskResourceSetUse occupancyComputeEmulationOutputUavSetUse{
        .resourceSet = occupancyComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    // Keep the final immutable upload as the stream anchor. The optional generator only becomes Occupancy's
    // immediate dependency; replacing this anchor would hide a broken upload/clear-to-producer handoff.
    const Core::GpuTaskId occupancyStreamTask = occupancyUploadTask;
    if(avboitOccupancyPayload.occupancyStreamsUploaded)
        m_deferredAvboitOccupancyStreamTask = occupancyStreamTask;
    Core::GpuTaskId occupancyDependency = avboitClearTask;

    Core::GpuTaskSchedulingHint avboitOccupancyScheduling;
    avboitOccupancyScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitOccupancyScheduling.forceSubmissionBoundary = false;
    avboitOccupancyScheduling.allowPacketMerge = true;
    avboitOccupancyScheduling.mergeWithPrevious = true;
    if(occupancyComputeEmulationOutputStatesGraphOwned){
        avboitOccupancyComputeEmulationPayload.renderer = this;
        avboitOccupancyComputeEmulationPayload.targets = &deferredTargets;
        avboitOccupancyComputeEmulationPayload.timingTicket = &avboitPreTimingTicket;
        avboitOccupancyComputeEmulationPayload.occupancyTiming = &avboitOccupancyComputeEmulationTiming;
        avboitOccupancyComputeEmulationPayload.instanceCount = avboitOccupancyPayload.occupancySnapshot.instanceCount;
        avboitOccupancyComputeEmulationPayload.materialTypedByteCount =
            avboitOccupancyPayload.occupancySnapshot.materialTypedByteCount;
        avboitOccupancyComputeEmulationPayload.materialDrawBuffersUploaded =
            avboitOccupancyPayload.occupancyStreamsUploaded;
        avboitOccupancyComputeEmulationPayload.csgFrameBuffersUploaded = occupancyCsgStreamsUploaded;
        avboitOccupancyComputeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            occupancyCsgIntervalSampleImageStatesGraphOwned;
        avboitOccupancyComputeEmulationPayload.csgClipBufferStatesGraphOwned =
            occupancyCsgClipBufferStatesGraphOwned;
        avboitOccupancyComputeEmulationPayload.materialFrameStatesGraphOwned =
            avboitOccupancyPayload.occupancyMaterialFrameStatesGraphOwned;
        avboitOccupancyComputeEmulationPayload.materialGeometryStatesGraphOwned =
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancyComputeEmulationResourceUses{
            occupancyComputeEmulationResourceScratch
        };
        occupancyComputeEmulationResourceUses.reserve(
            4u + (occupancyCsgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        occupancyComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(occupancyCsgComputeEmulationPlanCaptured){
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse occupancyComputeEmulationResourceSetUses[3u] = {};
        usize occupancyComputeEmulationResourceSetUseCount = 0u;
        occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
            occupancyMaterialGeometrySetUse;
        if(occupancyMaterialSampledTextureSet.valid()){
            occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
                occupancyMaterialSampledTextureSetUse;
        }
        occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
            occupancyComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint occupancyComputeEmulationScheduling;
        occupancyComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        occupancyComputeEmulationScheduling.forceSubmissionBoundary = false;
        occupancyComputeEmulationScheduling.allowPacketMerge = true;
        occupancyComputeEmulationScheduling.mergeWithPrevious = true;
        // Depth Warp is a later Compute consumer. Retain the immediate producer/raster pair in AVBOIT Pre's
        // Graphics packet so one timing ticket and graph-owned UAV-to-VertexBuffer handoff remain authoritative.
        occupancyComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc occupancyComputeEmulationDesc;
        occupancyComputeEmulationDesc
            .setIdentity(occupancyCsgComputeEmulationPlanCaptured
                ? Name("render.avboit.occupancy.csg_compute_emulation")
                : Name("render.avboit.occupancy.compute_emulation"))
            .setMarkerLabel(occupancyCsgComputeEmulationPlanCaptured
                ? "AVBOIT Occupancy CSG Compute Emulation"
                : "AVBOIT Occupancy Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(occupancyComputeEmulationScheduling)
            .setDependencies(&occupancyDependency, 1u)
            .setResourceUses(
                occupancyComputeEmulationResourceUses.data(),
                occupancyComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                occupancyComputeEmulationResourceSetUses,
                occupancyComputeEmulationResourceSetUseCount
            )
        ;
        m_deferredAvboitOccupancyComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            AvboitOccupancyComputeEmulationGraphTask
        >(
            occupancyComputeEmulationDesc,
            Move(avboitOccupancyComputeEmulationPayload)
        );
        if(!m_deferredAvboitOccupancyComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Occupancy compute-emulation producer"
            ));
            return;
        }
        occupancyDependency = m_deferredAvboitOccupancyComputeEmulationTask;
        avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(occupancySharedComputeEmulationOutputStatesGraphOwned){
        // The one retained output appears in every phase, so keep it as an exact resource rather than placing it
        // in a resource set whose duplicate expansion would erase the alternating UAV/VertexBuffer uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancySharedGenerateResourceUses{
            avboitPreResourceScratch
        };
        occupancySharedGenerateResourceUses.reserve(5u);
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            materialInstances,
            Core::ResourceStates::ShaderResource
        ));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            materialTyped,
            Core::ResourceStates::ShaderResource
        ));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        occupancySharedGenerateResourceUses.push_back(WriteUse(
            occupancySharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancySharedRasterResourceUses{
            avboitPreResourceScratch
        };
        occupancySharedRasterResourceUses.assign(
            avboitPreResourceUses.begin(),
            avboitPreResourceUses.end()
        );
        occupancySharedRasterResourceUses.push_back(ReadUse(
            occupancySharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint occupancySharedComputeEmulationScheduling;
        occupancySharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        occupancySharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        occupancySharedComputeEmulationScheduling.allowPacketMerge = true;
        occupancySharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Every phase is an explicit immediate successor. Keep the full alternating chain in AVBOIT Pre despite
        // Depth Warp's later Compute consumer so one command list owns the timing scope and its output handoff.
        occupancySharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addOccupancySharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &occupancySharedComputeEmulationPlan,
            &avboitOccupancyComputeEmulationTiming,
            occupancySharedComputeEmulationInstanceCount,
            occupancySharedComputeEmulationMaterialTypedByteCount,
            occupancyStreamsUploaded = avboitOccupancyPayload.occupancyStreamsUploaded,
            occupancyMaterialFrameStatesGraphOwned = avboitOccupancyPayload.occupancyMaterialFrameStatesGraphOwned,
            occupancyMaterialGeometryStatesGraphOwned = avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned,
            &avboitPreTimingTicket,
            &occupancySharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitOccupancySharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool beginTiming,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(occupancySharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitOccupancySharedComputeEmulationGraphTask::Payload payload;
            payload.renderer = this;
            payload.targets = &deferredTargets;
            payload.timingTicket = &avboitPreTimingTicket;
            payload.occupancyTiming = &avboitOccupancyComputeEmulationTiming;
            payload.plan = occupancySharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = occupancySharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = occupancySharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = occupancyStreamsUploaded;
            payload.materialFrameStatesGraphOwned = occupancyMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = occupancyMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<AvboitOccupancySharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using OccupancySharedPhase = AvboitOccupancySharedComputeEmulationGraphTask::Phase;
        const Name occupancySharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.occupancy.shared_compute_emulation_generate_a"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_a"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_b"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_b"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_c"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_c"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_d"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_d"),
        };
        const AStringView occupancySharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Occupancy Shared Compute Emulation Generate A",
            "AVBOIT Occupancy Shared Compute Emulation Raster A",
            "AVBOIT Occupancy Shared Compute Emulation Generate B",
            "AVBOIT Occupancy Shared Compute Emulation Raster B",
            "AVBOIT Occupancy Shared Compute Emulation Generate C",
            "AVBOIT Occupancy Shared Compute Emulation Raster C",
            "AVBOIT Occupancy Shared Compute Emulation Generate D",
            "AVBOIT Occupancy Shared Compute Emulation Raster D",
        };
        const usize occupancySharedComputeEmulationPhaseCount =
            occupancySharedComputeEmulationPlan.drawCount * 2u
        ;
        NWB_ASSERT(
            occupancySharedComputeEmulationPlan.drawCount == 2u
            || occupancySharedComputeEmulationPlan.drawCount == 3u
            || occupancySharedComputeEmulationPlan.drawCount == 4u
        );
        NWB_ASSERT(
            occupancySharedComputeEmulationPhaseCount
            <= LengthOf(occupancySharedComputeEmulationPhaseIdentities)
        );
        Core::GpuTaskId occupancySharedComputeEmulationDependency = occupancyDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < occupancySharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase = phaseIndex % 2u != 0u;
            m_deferredAvboitOccupancySharedComputeEmulationTasks[phaseIndex] =
                addOccupancySharedComputeEmulationPhase(
                    occupancySharedComputeEmulationPhaseIdentities[phaseIndex],
                    occupancySharedComputeEmulationPhaseMarkers[phaseIndex],
                    occupancySharedComputeEmulationDependency,
                    isRasterPhase ? OccupancySharedPhase::Raster : OccupancySharedPhase::Generate,
                    phaseIndex / 2u,
                    phaseIndex == 0u,
                    phaseIndex + 1u == occupancySharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? occupancySharedRasterResourceUses
                        : occupancySharedGenerateResourceUses,
                    occupancyMaterialResourceSetUses,
                    occupancyMaterialResourceSetUseCount
                )
            ;
            if(!m_deferredAvboitOccupancySharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Occupancy shared compute-emulation phase"
                ));
                return;
            }
            occupancySharedComputeEmulationDependency =
                m_deferredAvboitOccupancySharedComputeEmulationTasks[phaseIndex];
        }
        m_deferredAvboitOccupancySharedComputeEmulationTaskCount =
            occupancySharedComputeEmulationPhaseCount;
        // The terminal raster is the existing Occupancy semantic endpoint: Depth Warp, timing, state cache, and
        // accepted-token publication remain tied to this packet-local task.
        m_deferredAvboitOccupancyTask = occupancySharedComputeEmulationDependency;
    }
    else{
        Core::GpuTaskDesc avboitOccupancyDesc;
        avboitOccupancyDesc
            .setIdentity(Name("render.avboit.pre"))
            .setMarkerLabel(splitAvboitStages ? "AVBOIT Pre" : "AVBOIT")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitOccupancyScheduling)
            .setDependencies(&occupancyDependency, 1u)
            .setResourceUses(avboitPreResourceUses.data(), avboitPreResourceUses.size())
            .setResourceSetUses(
                occupancyMaterialResourceSetUseCount != 0u ? occupancyMaterialResourceSetUses : nullptr,
                occupancyMaterialResourceSetUseCount
            )
        ;
        m_deferredAvboitOccupancyTask = m_deferredLightingTaskGraph.addTask<AvboitOccupancyGraphTask>(
            avboitOccupancyDesc,
            Move(avboitOccupancyPayload)
        );
        if(!m_deferredAvboitOccupancyTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
            return;
        }
    }

    Core::GpuTaskSchedulingHint avboitComputeScheduling;
    avboitComputeScheduling.cost = Core::GpuTaskCostHint::Medium;
    avboitComputeScheduling.forceSubmissionBoundary = true;
    avboitComputeScheduling.allowPacketMerge = false;
    Core::GpuTaskId avboitDepthWarpCompletionTask = m_deferredAvboitOccupancyTask;
    if(splitAvboitStages){
        const Core::GpuTaskResourceUse depthWarpResourceUses[] = {
            ReadUse(avboitCoverage),
            ReadWriteUse(avboitDepthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitControl, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId preDependency[] = { m_deferredAvboitOccupancyTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_deferredAvboitDepthWarpTask = m_deferredLightingTaskGraph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitDepthWarpTimingTicket,
            }
        );
        if(!m_deferredAvboitDepthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
            return;
        }
        avboitDepthWarpCompletionTask = m_deferredAvboitDepthWarpTask;
    }
    else if(hasTransparentRenderers){
        // Keep Depth Warp as a distinct Graphics task even on the one-packet route. Occupancy writes coverage via
        // UAV descriptors, and this explicit successor lets the compiler lower the required same-UAV ordering
        // before Depth Warp reads it. The following immutable Extinction upload can then feed a producer directly
        // into its raster consumer without an unrelated callback in between.
        const Core::GpuTaskResourceUse unsplitDepthWarpResourceUses[] = {
            ReadUse(avboitCoverage, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitDepthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitControl, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        Core::GpuTaskSchedulingHint unsplitDepthWarpScheduling;
        unsplitDepthWarpScheduling.cost = Core::GpuTaskCostHint::Medium;
        unsplitDepthWarpScheduling.forceSubmissionBoundary = false;
        unsplitDepthWarpScheduling.allowPacketMerge = true;
        unsplitDepthWarpScheduling.mergeWithPrevious = true;
        unsplitDepthWarpScheduling.allowMergeAcrossConsumerFrontier = true;
        const Core::GpuTaskId occupancyDependency[] = { m_deferredAvboitOccupancyTask };
        Core::GpuTaskDesc unsplitDepthWarpDesc;
        unsplitDepthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(unsplitDepthWarpScheduling)
            .setDependencies(occupancyDependency, LengthOf(occupancyDependency))
            .setResourceUses(unsplitDepthWarpResourceUses, LengthOf(unsplitDepthWarpResourceUses))
        ;
        avboitDepthWarpCompletionTask = m_deferredLightingTaskGraph.addTask<AvboitDepthWarpGraphTask>(
            unsplitDepthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitPreTimingTicket,
            }
        );
        if(!avboitDepthWarpCompletionTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare one-packet AVBOIT depth-warp graph task"));
            return;
        }
    }

    if(hasTransparentRenderers){
    // Extinction is a distinct shared-buffer write point. Snapshot and publish it only after occupancy, or after
    // the split Compute depth warp, so neither phase can overwrite the other phase's instance/typed/CSG stream.
    AvboitExtinctionGraphTask::Payload avboitExtinctionPayload{ m_arena };
    AvboitExtinctionComputeEmulationGraphTask::Payload avboitExtinctionComputeEmulationPayload{ m_arena };
    avboitExtinctionPayload.avboitSystem = &m_avboitSystem;
    avboitExtinctionPayload.targets = &deferredTargets;
    avboitExtinctionPayload.csgFrameState = &csgFrameState;
    avboitExtinctionPayload.timingTicket = splitAvboitStages
        ? &avboitExtinctionTimingTicket
        : &avboitPreTimingTicket
    ;
    avboitExtinctionPayload.hasTransparentRenderers = hasTransparentRenderers;

    Core::GpuTaskId extinctionUploadTask = avboitDepthWarpCompletionTask;
    bool extinctionStreamsUploaded = false;
    bool extinctionCsgStreamsUploaded = false;
    bool extinctionRegularComputeEmulationPlanCaptured = false;
    bool extinctionCsgComputeEmulationPlanCaptured = false;
    bool extinctionSharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan extinctionSharedComputeEmulationPlan;
    usize extinctionSharedComputeEmulationInstanceCount = 0u;
    usize extinctionSharedComputeEmulationMaterialTypedByteCount = 0u;
    bool extinctionMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena extinctionMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId extinctionMaterialGeometrySet;
    Core::GpuGraphResourceSetId extinctionMaterialSampledTextureSet;
        Core::Alloc::ScratchArena extinctionUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions extinctionDrawItems{ extinctionUploadScratch };
        InstanceGpuDataVector extinctionInstanceData{ extinctionUploadScratch };
        CsgFrameGpuData extinctionCsgFrameData{ extinctionUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector extinctionMaterialTypedRanges{ extinctionUploadScratch };
#endif
        MaterialTypedByteDataVector extinctionMaterialTypedBytes{ extinctionUploadScratch };
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.avboit.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitExtinction,
            true,
            csgFrameState,
            extinctionDrawItems,
            extinctionInstanceData,
            extinctionCsgFrameData,
#if defined(NWB_DEBUG)
            extinctionMaterialTypedRanges,
#endif
            extinctionMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly
        );

        const bool extinctionHasCsgDrawItems = !extinctionDrawItems.csg.empty();
        if(!extinctionDrawItems.empty()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    extinctionInstanceData,
                    extinctionMaterialTypedBytes
                )
                || !m_materialSystem.materialPassDrawResourcesReady(extinctionDrawItems.regular)
                || (extinctionHasCsgDrawItems && (
                    !extinctionCsgFrameData.hasWork()
                    || !csgReceiverRanges.valid()
                    || !csgCutters.valid()
                    || !csgClipContextSlots.valid()
                    || !csgIntervalSampleState.valid()
                    || !m_csgSystem.csgFrameBuffersReady(extinctionCsgFrameData)
                    || !m_materialSystem.materialPassDrawResourcesReady(extinctionDrawItems.csg)
                ))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT extinction resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const extinctionMaterialGeometryDrawSets[] = {
                &extinctionDrawItems.regular,
                &extinctionDrawItems.csg,
            };
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                extinctionMaterialGeometryDrawSets,
                LengthOf(extinctionMaterialGeometryDrawSets),
                extinctionMaterialGeometryScratch,
                Name("render.avboit.extinction.material_geometry"),
                "AVBOIT Extinction Material Geometry",
                extinctionMaterialGeometrySet
            );
            if(!avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT extinction material geometry states"));
            extinctionMaterialSampledTexturesCollected =
                avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    extinctionMaterialGeometryDrawSets,
                    LengthOf(extinctionMaterialGeometryDrawSets),
                    extinctionMaterialGeometryScratch,
                    Name("render.avboit.extinction.material_sampled_textures"),
                    "AVBOIT Extinction Material Sampled Textures",
                    extinctionMaterialSampledTextureSet
                )
            ;
            if(
                avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && !extinctionMaterialSampledTexturesCollected
            )
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT extinction material sampled textures"));

            m_materialSystem.prepareMaterialPassInstanceUploadData(extinctionInstanceData);
#if defined(NWB_DEBUG)
            if(
                extinctionInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || extinctionCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || extinctionCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT extinction upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(extinctionInstanceData.size() == extinctionMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                extinctionMaterialTypedRanges,
                extinctionMaterialTypedBytes
            );
#endif

            const Core::GpuUploadBlobId extinctionInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                extinctionInstanceData.data(),
                extinctionInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId extinctionMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                extinctionMaterialTypedBytes.data(),
                extinctionMaterialTypedBytes.size(),
                alignof(u32)
            );
            if(!extinctionInstanceBlob.valid() || !extinctionMaterialTypedBlob.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT extinction material upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint extinctionUploadScheduling;
            extinctionUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            extinctionUploadScheduling.forceSubmissionBoundary = false;
            extinctionUploadScheduling.allowPacketMerge = true;
            extinctionUploadScheduling.mergeWithPrevious = true;

            Core::GpuTaskDesc extinctionInstanceUploadDesc;
            extinctionInstanceUploadDesc
                .setIdentity(Name("render.avboit.extinction.material_instances_upload"))
                .setMarkerLabel("AVBOIT Extinction Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(extinctionUploadScheduling)
                .setDependencies(&extinctionUploadTask, 1u)
            ;
            extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                extinctionInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = extinctionInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!extinctionUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction material instance upload"));
                return;
            }

            Core::GpuTaskDesc extinctionMaterialTypedUploadDesc;
            extinctionMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.extinction.material_typed_upload"))
                .setMarkerLabel("AVBOIT Extinction Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(extinctionUploadScheduling)
                .setDependencies(&extinctionUploadTask, 1u)
            ;
            extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                extinctionMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = extinctionMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!extinctionUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction material typed upload"));
                return;
            }

            if(extinctionHasCsgDrawItems){
                CsgClipContextSlots extinctionCsgClipContextSlotData;
                if(!m_csgSystem.prepareCsgClipContextSlotData(
                    extinctionCsgFrameData,
                    extinctionCsgClipContextSlotData
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT extinction CSG context data"));
                    return;
                }
                const Core::GpuUploadBlobId extinctionCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                    extinctionCsgFrameData.receiverRanges.data(),
                    extinctionCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                    alignof(CsgReceiverRangeGpuData)
                );
                const Core::GpuUploadBlobId extinctionCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                    extinctionCsgFrameData.cutters.data(),
                    extinctionCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                    alignof(CsgCutterGpuData)
                );
                const Core::GpuUploadBlobId extinctionCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                    &extinctionCsgClipContextSlotData,
                    sizeof(extinctionCsgClipContextSlotData),
                    alignof(CsgClipContextSlots)
                );
                if(
                    !extinctionCsgReceiverRangesBlob.valid()
                    || !extinctionCsgCuttersBlob.valid()
                    || !extinctionCsgClipContextSlotsBlob.valid()
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT extinction CSG upload data"));
                    return;
                }

                Core::GpuTaskDesc extinctionCsgReceiverRangesUploadDesc;
                extinctionCsgReceiverRangesUploadDesc
                    .setIdentity(Name("render.avboit.extinction.csg_receiver_ranges_upload"))
                    .setMarkerLabel("AVBOIT Extinction CSG Receiver Ranges Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(extinctionUploadScheduling)
                    .setDependencies(&extinctionUploadTask, 1u)
                ;
                extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    extinctionCsgReceiverRangesUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = extinctionCsgReceiverRangesBlob,
                        .destination = csgReceiverRanges,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!extinctionUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction CSG receiver-range upload"));
                    return;
                }

                Core::GpuTaskDesc extinctionCsgCuttersUploadDesc;
                extinctionCsgCuttersUploadDesc
                    .setIdentity(Name("render.avboit.extinction.csg_cutters_upload"))
                    .setMarkerLabel("AVBOIT Extinction CSG Cutters Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(extinctionUploadScheduling)
                    .setDependencies(&extinctionUploadTask, 1u)
                ;
                extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    extinctionCsgCuttersUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = extinctionCsgCuttersBlob,
                        .destination = csgCutters,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!extinctionUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction CSG cutter upload"));
                    return;
                }

                Core::GpuTaskDesc extinctionCsgClipContextSlotsUploadDesc;
                extinctionCsgClipContextSlotsUploadDesc
                    .setIdentity(Name("render.avboit.extinction.csg_clip_context_slots_upload"))
                    .setMarkerLabel("AVBOIT Extinction CSG Clip Context Slots Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(extinctionUploadScheduling)
                    .setDependencies(&extinctionUploadTask, 1u)
                ;
                extinctionUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    extinctionCsgClipContextSlotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = extinctionCsgClipContextSlotsBlob,
                        .destination = csgClipContextSlots,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!extinctionUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction CSG clip-context upload"));
                    return;
                }
                extinctionCsgStreamsUploaded = true;
            }

            avboitExtinctionPayload.extinctionSnapshot.capture(
                extinctionDrawItems,
                extinctionCsgFrameData,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            avboitExtinctionPayload.extinctionPhasePrepared = true;
            extinctionStreamsUploaded = true;
            // A phase may graph-own one alias-free stream or a narrowly retained regular shared stream.
            // Mixed regular/CSG work and every other shared-output shape retain local interleaving because a single
            // producer/raster handoff cannot preserve their per-draw overwrite order.
            extinctionRegularComputeEmulationPlanCaptured = extinctionDrawItems.csg.computeDrawItems.empty()
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && avboitExtinctionComputeEmulationPayload.plan.capture(
                    m_meshSystem,
                    extinctionDrawItems.regular
                )
            ;
            extinctionCsgComputeEmulationPlanCaptured = extinctionDrawItems.regular.computeDrawItems.empty()
                && extinctionCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && avboitExtinctionComputeEmulationPayload.csgPlan.capture(
                    m_meshSystem,
                    extinctionDrawItems.csg,
                    extinctionCsgFrameData
                )
            ;
            extinctionSharedComputeEmulationPlanCaptured = !splitAvboitStages
                && !extinctionRegularComputeEmulationPlanCaptured
                && extinctionDrawItems.regular.meshDrawItems.empty()
                && extinctionDrawItems.csg.empty()
                && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
                && extinctionMaterialSampledTexturesCollected
                && extinctionSharedComputeEmulationPlan.capture(m_meshSystem, extinctionDrawItems.regular, 4u)
                && extinctionSharedComputeEmulationPlan.drawCount >= 2u
                && extinctionSharedComputeEmulationPlan.drawCount <= 4u
            ;
            if(extinctionSharedComputeEmulationPlanCaptured){
                extinctionSharedComputeEmulationInstanceCount = extinctionInstanceData.size();
                extinctionSharedComputeEmulationMaterialTypedByteCount = extinctionMaterialTypedBytes.size();
            }
            NWB_ASSERT(!(extinctionRegularComputeEmulationPlanCaptured && extinctionCsgComputeEmulationPlanCaptured));
            NWB_ASSERT(!(extinctionRegularComputeEmulationPlanCaptured && extinctionSharedComputeEmulationPlanCaptured));
            NWB_ASSERT(!(extinctionCsgComputeEmulationPlanCaptured && extinctionSharedComputeEmulationPlanCaptured));
        }
        else{
            // Preserve graph ownership even when a transparent frame has no ready extinction draws: native recording
            // consumes this explicit empty phase rather than regathering mutable renderer state.
            avboitExtinctionPayload.extinctionSnapshot.capture(
                extinctionDrawItems,
                extinctionCsgFrameData,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            avboitExtinctionPayload.extinctionPhasePrepared = true;
        }
    const bool extinctionCsgIntervalSampleImageStatesGraphOwned =
        avboitIntervalOutputsGraphOwned && extinctionCsgStreamsUploaded
    ;
    const bool extinctionCsgClipBufferStatesGraphOwned = extinctionCsgStreamsUploaded;
    NWB_ASSERT(
        !extinctionCsgIntervalSampleImageStatesGraphOwned
        || (
            avboitExtinctionPayload.extinctionPhasePrepared
            && avboitExtinctionPayload.extinctionSnapshot.captured
        )
    );
    NWB_ASSERT(
        !extinctionCsgClipBufferStatesGraphOwned
        || (
            avboitExtinctionPayload.extinctionPhasePrepared
            && avboitExtinctionPayload.extinctionSnapshot.captured
        )
    );
    avboitExtinctionPayload.extinctionCsgIntervalSampleImageStatesGraphOwned =
        extinctionCsgIntervalSampleImageStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionCsgClipBufferStatesGraphOwned =
        extinctionCsgClipBufferStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionMaterialFrameStatesGraphOwned = extinctionStreamsUploaded;
    avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned =
        extinctionStreamsUploaded
        && avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId extinctionComputeEmulationOutputSet;
    Core::Alloc::ScratchArena extinctionComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool extinctionComputeEmulationPlanCaptured =
        extinctionRegularComputeEmulationPlanCaptured
        || extinctionCsgComputeEmulationPlanCaptured
    ;
    bool extinctionComputeEmulationOutputStatesGraphOwned = false;
    if(extinctionRegularComputeEmulationPlanCaptured){
        extinctionComputeEmulationOutputStatesGraphOwned = GatherAvboitAliasFreeComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            avboitExtinctionComputeEmulationPayload.plan,
            extinctionComputeEmulationResourceScratch,
            Name("render.avboit.extinction.compute_emulation.outputs"),
            "AVBOIT Extinction Compute Emulation Outputs",
            extinctionComputeEmulationOutputSet
        );
    }
    else if(extinctionCsgComputeEmulationPlanCaptured){
        extinctionComputeEmulationOutputStatesGraphOwned =
            GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
                m_deferredLightingTaskGraph,
                avboitExtinctionComputeEmulationPayload.csgPlan,
                extinctionComputeEmulationResourceScratch,
                Name("render.avboit.extinction.csg_compute_emulation.outputs"),
                "AVBOIT Extinction CSG Compute Emulation Outputs",
                extinctionComputeEmulationOutputSet
            )
        ;
    }
    if(
        extinctionComputeEmulationPlanCaptured
        && !extinctionComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Extinction compute-emulation output states"
        ));
    }
    Core::GpuGraphResourceId extinctionSharedComputeEmulationOutput;
    const bool extinctionSharedComputeEmulationOutputStatesGraphOwned =
        extinctionSharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            extinctionSharedComputeEmulationPlan,
            "AVBOIT Extinction Shared Compute Emulation Output",
            extinctionSharedComputeEmulationOutput
        )
    ;
    if(
        extinctionSharedComputeEmulationPlanCaptured
        && !extinctionSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Extinction shared compute-emulation output"
        ));
    }
    avboitExtinctionPayload.extinctionComputeEmulationOutputStatesGraphOwned =
        extinctionRegularComputeEmulationPlanCaptured
        && extinctionComputeEmulationOutputStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionCsgComputeEmulationOutputStatesGraphOwned =
        extinctionCsgComputeEmulationPlanCaptured
        && extinctionComputeEmulationOutputStatesGraphOwned
    ;
    avboitExtinctionPayload.extinctionComputeEmulationTiming =
        extinctionComputeEmulationOutputStatesGraphOwned
            ? &avboitExtinctionComputeEmulationTiming
            : nullptr
    ;
    Core::Alloc::ScratchArena extinctionResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionResourceUses{ extinctionResourceScratch };
    extinctionResourceUses.reserve(
        (splitAvboitStages ? 9u : 16u)
        + (extinctionStreamsUploaded ? 7u : 0u)
        + (extinctionCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    if(splitAvboitStages){
        extinctionResourceUses.push_back(ReadUse(depth));
        extinctionResourceUses.push_back(ReadUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
        extinctionResourceUses.push_back(ReadUse(avboitDepthWarp));
        extinctionResourceUses.push_back(ReadUse(avboitControl));
        extinctionResourceUses.push_back(ReadWriteUse(avboitExtinction, Core::ResourceStates::UnorderedAccess));
        extinctionResourceUses.push_back(ReadWriteUse(avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess));
    }
    else{
        // A preceding packet-local Graphics task owns Depth Warp. This tail keeps Extinction immediately after its
        // optional producer, then records Integration before the separately frozen Accumulation stream.
        extinctionResourceUses.push_back(ReadUse(albedo));
        extinctionResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource, true));
        extinctionResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true));
        extinctionResourceUses.push_back(ReadUse(depth));
        extinctionResourceUses.push_back(ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
        extinctionResourceUses.push_back(ReadUse(avboitDepthWarp));
        extinctionResourceUses.push_back(ReadUse(avboitControl));
        extinctionResourceUses.push_back(ReadWriteUse(avboitExtinction, Core::ResourceStates::UnorderedAccess));
        extinctionResourceUses.push_back(ReadWriteUse(avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess));
    }
    if(extinctionStreamsUploaded){
        extinctionResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        extinctionResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        extinctionResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        if(extinctionCsgStreamsUploaded){
            extinctionResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            extinctionResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            extinctionResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // The full-resolution interval producer owns this sample state throughout all low-raster AVBOIT phases.
            extinctionResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(extinctionCsgIntervalSampleImageStatesGraphOwned){
                // The prepared transparent interval producer wrote these aliases. Extinction loads them through
                // StorageImage descriptors, so the graph lowers its same-UAV handoff before this thunk records.
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalDepth,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCapNormal,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalData,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCount,
                    csgRemovedIntervalCountSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
            }
        }
    }
    const Core::GpuTaskResourceSetUse extinctionMaterialGeometrySetUse{
        .resourceSet = extinctionMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse extinctionMaterialSampledTextureSetUse{
        .resourceSet = extinctionMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse extinctionComputeEmulationOutputUavSetUse{
        .resourceSet = extinctionComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse extinctionComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = extinctionComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse extinctionMaterialResourceSetUses[3u] = {};
    usize extinctionMaterialResourceSetUseCount = 0u;
    if(avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned)
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] = extinctionMaterialGeometrySetUse;
    if(extinctionMaterialSampledTextureSet.valid())
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] = extinctionMaterialSampledTextureSetUse;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] =
            extinctionComputeEmulationOutputVertexBufferSetUse;
    }
    extinctionResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    extinctionResourceUses.push_back(ReadUse(avboitMaterialDomain));
    extinctionResourceUses.push_back(ReadUse(avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitExtinctionScheduling;
    avboitExtinctionScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitExtinctionScheduling.forceSubmissionBoundary = false;
    avboitExtinctionScheduling.allowPacketMerge = true;
    avboitExtinctionScheduling.mergeWithPrevious = true;

    // Keep the final immutable upload as the semantic stream anchor. The optional producer becomes only the
    // immediate Extinction dependency; replacing this anchor would hide a broken upload-to-producer handoff.
    const Core::GpuTaskId extinctionStreamTask = extinctionUploadTask;
    if(extinctionStreamsUploaded)
        m_deferredAvboitExtinctionStreamTask = extinctionStreamTask;
    Core::GpuTaskId extinctionDependency = extinctionUploadTask;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        avboitExtinctionComputeEmulationPayload.renderer = this;
        avboitExtinctionComputeEmulationPayload.targets = &deferredTargets;
        avboitExtinctionComputeEmulationPayload.timingTicket = avboitExtinctionPayload.timingTicket;
        avboitExtinctionComputeEmulationPayload.extinctionTiming = &avboitExtinctionComputeEmulationTiming;
        avboitExtinctionComputeEmulationPayload.instanceCount = extinctionInstanceData.size();
        avboitExtinctionComputeEmulationPayload.materialTypedByteCount = extinctionMaterialTypedBytes.size();
        avboitExtinctionComputeEmulationPayload.materialDrawBuffersUploaded = extinctionStreamsUploaded;
        avboitExtinctionComputeEmulationPayload.csgFrameBuffersUploaded = extinctionCsgStreamsUploaded;
        avboitExtinctionComputeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            extinctionCsgIntervalSampleImageStatesGraphOwned;
        avboitExtinctionComputeEmulationPayload.csgClipBufferStatesGraphOwned =
            extinctionCsgClipBufferStatesGraphOwned;
        avboitExtinctionComputeEmulationPayload.materialFrameStatesGraphOwned =
            avboitExtinctionPayload.extinctionMaterialFrameStatesGraphOwned;
        avboitExtinctionComputeEmulationPayload.materialGeometryStatesGraphOwned =
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionComputeEmulationResourceUses{
            extinctionResourceScratch
        };
        extinctionComputeEmulationResourceUses.reserve(
            4u + (extinctionCsgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        extinctionComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(extinctionCsgComputeEmulationPlanCaptured){
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse extinctionComputeEmulationResourceSetUses[3u] = {};
        usize extinctionComputeEmulationResourceSetUseCount = 0u;
        extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
            extinctionMaterialGeometrySetUse;
        if(extinctionMaterialSampledTextureSet.valid()){
            extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
                extinctionMaterialSampledTextureSetUse;
        }
        extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
            extinctionComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint extinctionComputeEmulationScheduling;
        extinctionComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        extinctionComputeEmulationScheduling.forceSubmissionBoundary = false;
        extinctionComputeEmulationScheduling.allowPacketMerge = true;
        extinctionComputeEmulationScheduling.mergeWithPrevious = true;
        // The next raster consumes this producer's graph-owned UAV output and shares its Extinction timing ticket.
        // Keep the immediate pair intact even if FrontierSafe sees Integration on a later Compute packet.
        extinctionComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc extinctionComputeEmulationDesc;
        extinctionComputeEmulationDesc
            .setIdentity(extinctionCsgComputeEmulationPlanCaptured
                ? Name("render.avboit.extinction.csg_compute_emulation")
                : Name("render.avboit.extinction.compute_emulation"))
            .setMarkerLabel(extinctionCsgComputeEmulationPlanCaptured
                ? "AVBOIT Extinction CSG Compute Emulation"
                : "AVBOIT Extinction Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(extinctionComputeEmulationScheduling)
            .setDependencies(&extinctionDependency, 1u)
            .setResourceUses(
                extinctionComputeEmulationResourceUses.data(),
                extinctionComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                extinctionComputeEmulationResourceSetUses,
                extinctionComputeEmulationResourceSetUseCount
            )
        ;
        m_deferredAvboitExtinctionComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            AvboitExtinctionComputeEmulationGraphTask
        >(
            extinctionComputeEmulationDesc,
            Move(avboitExtinctionComputeEmulationPayload)
        );
        if(!m_deferredAvboitExtinctionComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Extinction compute-emulation producer"
            ));
            return;
        }
        extinctionDependency = m_deferredAvboitExtinctionComputeEmulationTask;
        avboitExtinctionScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(extinctionSharedComputeEmulationOutputStatesGraphOwned){
        // Keep the retained output concrete rather than placing it in a duplicate-expanding resource set.  The
        // alternating phases need their distinct UAV/VertexBuffer uses preserved by the compiler.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedGenerateResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedGenerateResourceUses.reserve(5u);
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            materialInstances,
            Core::ResourceStates::ShaderResource
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            materialTyped,
            Core::ResourceStates::ShaderResource
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(WriteUse(
            extinctionSharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedRasterResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedRasterResourceUses.assign(
            extinctionResourceUses.begin(),
            extinctionResourceUses.end()
        );
        extinctionSharedRasterResourceUses.push_back(ReadUse(
            extinctionSharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint extinctionSharedComputeEmulationScheduling;
        extinctionSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        extinctionSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        extinctionSharedComputeEmulationScheduling.allowPacketMerge = true;
        extinctionSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Integration and later Accumulation consume the terminal raster.  Every immediate D/R successor carries
        // its explicit dependency, so retaining this one Graphics packet remains FrontierSafe.
        extinctionSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addExtinctionSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &extinctionSharedComputeEmulationPlan,
            &avboitExtinctionComputeEmulationTiming,
            extinctionSharedComputeEmulationInstanceCount,
            extinctionSharedComputeEmulationMaterialTypedByteCount,
            extinctionStreamsUploaded,
            extinctionMaterialFrameStatesGraphOwned = avboitExtinctionPayload.extinctionMaterialFrameStatesGraphOwned,
            extinctionMaterialGeometryStatesGraphOwned = avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned,
            avboitExtinctionTimingTicket = avboitExtinctionPayload.timingTicket,
            &extinctionSharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitExtinctionSharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool beginTiming,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(extinctionSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitExtinctionSharedComputeEmulationGraphTask::Payload payload;
            payload.renderer = this;
            payload.targets = &deferredTargets;
            payload.timingTicket = avboitExtinctionTimingTicket;
            payload.extinctionTiming = &avboitExtinctionComputeEmulationTiming;
            payload.plan = extinctionSharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = extinctionSharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = extinctionSharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = extinctionStreamsUploaded;
            payload.materialFrameStatesGraphOwned = extinctionMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = extinctionMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<AvboitExtinctionSharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using ExtinctionSharedPhase = AvboitExtinctionSharedComputeEmulationGraphTask::Phase;
        const Name extinctionSharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.extinction.shared_compute_emulation_generate_a"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_a"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_b"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_b"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_c"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_c"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_d"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_d"),
        };
        const AStringView extinctionSharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Extinction Shared Compute Emulation Generate A",
            "AVBOIT Extinction Shared Compute Emulation Raster A",
            "AVBOIT Extinction Shared Compute Emulation Generate B",
            "AVBOIT Extinction Shared Compute Emulation Raster B",
            "AVBOIT Extinction Shared Compute Emulation Generate C",
            "AVBOIT Extinction Shared Compute Emulation Raster C",
            "AVBOIT Extinction Shared Compute Emulation Generate D",
            "AVBOIT Extinction Shared Compute Emulation Raster D",
        };
        const usize extinctionSharedComputeEmulationPhaseCount =
            extinctionSharedComputeEmulationPlan.drawCount * 2u
        ;
        NWB_ASSERT(
            extinctionSharedComputeEmulationPlan.drawCount == 2u
            || extinctionSharedComputeEmulationPlan.drawCount == 3u
            || extinctionSharedComputeEmulationPlan.drawCount == 4u
        );
        NWB_ASSERT(extinctionSharedComputeEmulationPhaseCount <= LengthOf(extinctionSharedComputeEmulationPhaseIdentities));
        Core::GpuTaskId extinctionSharedComputeEmulationDependency = extinctionDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < extinctionSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase = phaseIndex % 2u != 0u;
            m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseIndex] =
                addExtinctionSharedComputeEmulationPhase(
                    extinctionSharedComputeEmulationPhaseIdentities[phaseIndex],
                    extinctionSharedComputeEmulationPhaseMarkers[phaseIndex],
                    extinctionSharedComputeEmulationDependency,
                    isRasterPhase ? ExtinctionSharedPhase::Raster : ExtinctionSharedPhase::Generate,
                    phaseIndex / 2u,
                    phaseIndex == 0u,
                    phaseIndex + 1u == extinctionSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? extinctionSharedRasterResourceUses
                        : extinctionSharedGenerateResourceUses,
                    extinctionMaterialResourceSetUses,
                    extinctionMaterialResourceSetUseCount
                )
            ;
            if(!m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Extinction shared compute-emulation phase"
                ));
                return;
            }
            extinctionSharedComputeEmulationDependency =
                m_deferredAvboitExtinctionSharedComputeEmulationTasks[phaseIndex];
        }
        m_deferredAvboitExtinctionSharedComputeEmulationTaskCount =
            extinctionSharedComputeEmulationPhaseCount;
        // The terminal raster is the Extinction semantic endpoint.  The common typed Integration task immediately
        // follows it, so packet ranges, timing, and accepted-token ownership remain graph-derived.
        m_deferredAvboitExtinctionTask = extinctionSharedComputeEmulationDependency;
    }
    else{
    Core::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("render.avboit.extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(avboitExtinctionScheduling)
        .setDependencies(&extinctionDependency, 1u)
        .setResourceUses(extinctionResourceUses.data(), extinctionResourceUses.size())
        .setResourceSetUses(
            extinctionMaterialResourceSetUseCount != 0u ? extinctionMaterialResourceSetUses : nullptr,
            extinctionMaterialResourceSetUseCount
        )
    ;
    m_deferredAvboitExtinctionTask = m_deferredLightingTaskGraph.addTask<AvboitExtinctionGraphTask>(
        extinctionDesc,
        Move(avboitExtinctionPayload)
    );
    if(!m_deferredAvboitExtinctionTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return;
    }
    }
    // Integration was the last normal AVBOIT dispatch still hidden in Extinction's unsplit callback.  Keep the
    // established split Compute route, while the unsplit route records the same typed work as an adjacent Graphics
    // tail in AVBOIT Pre so the compiler owns Extinction/UAV-to-Integration/SRV state lowering in both modes.
    const Core::GpuTaskResourceUse integrationResourceUses[] = {
        ReadUse(avboitExtinction),
        ReadUse(avboitControl),
        ReadUse(avboitExtinctionOverflow),
        ReadWriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    const Core::GpuTaskId integrationDependency[] = { m_deferredAvboitExtinctionTask };
    Core::GpuTaskDesc integrationDesc;
    if(splitAvboitStages){
        integrationDesc
            .setIdentity(Name("render.avboit.integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
        ;
    }
    else{
        Core::GpuTaskSchedulingHint unsplitIntegrationScheduling;
        unsplitIntegrationScheduling.cost = Core::GpuTaskCostHint::Medium;
        unsplitIntegrationScheduling.forceSubmissionBoundary = false;
        unsplitIntegrationScheduling.allowPacketMerge = true;
        unsplitIntegrationScheduling.mergeWithPrevious = true;
        // The explicit Extinction successor is permitted to stay in AVBOIT Pre even when later consumers form a
        // FrontierSafe frontier; it has an immediate dependency and must share the one Pre timing submission.
        unsplitIntegrationScheduling.allowMergeAcrossConsumerFrontier = true;
        integrationDesc
            .setIdentity(Name("render.avboit.integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(unsplitIntegrationScheduling)
        ;
    }
    integrationDesc
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
    ;
    m_deferredAvboitIntegrationTask = m_deferredLightingTaskGraph.addTask<AvboitIntegrationGraphTask>(
        integrationDesc,
        AvboitIntegrationGraphTask::Payload{
            .avboitSystem = &m_avboitSystem,
            .targets = &deferredTargets.avboit,
            .timingTicket = splitAvboitStages
                ? &avboitIntegrationTimingTicket
                : &avboitPreTimingTicket,
        }
    );
    if(!m_deferredAvboitIntegrationTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return;
    }

    // Accumulation is another independent write point for the shared material/CSG buffers. Freeze and publish its
    // bytes after integration, rather than letting native recording re-gather mutable scene state after extinction.
    AvboitAccumulationGraphTask::Payload avboitAccumulationPayload{ m_arena };
    AvboitAccumulationComputeEmulationGraphTask::Payload avboitAccumulationComputeEmulationPayload{ m_arena };
    avboitAccumulationPayload.avboitSystem = &m_avboitSystem;
    avboitAccumulationPayload.targets = &deferredTargets;
    avboitAccumulationPayload.csgFrameState = &csgFrameState;
    avboitAccumulationPayload.timingTicket = splitAvboitStages
        ? &avboitAccumulationTimingTicket
        : &avboitPreTimingTicket
    ;
    avboitAccumulationPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitAccumulationPayload.splitStages = splitAvboitStages;

    Core::GpuTaskId accumulationUploadTask = m_deferredAvboitIntegrationTask;
    bool accumulationStreamsUploaded = false;
    bool accumulationCsgStreamsUploaded = false;
    bool accumulationRegularComputeEmulationPlanCaptured = false;
    bool accumulationCsgComputeEmulationPlanCaptured = false;
    bool accumulationSharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan accumulationSharedComputeEmulationPlan;
    usize accumulationSharedComputeEmulationInstanceCount = 0u;
    usize accumulationSharedComputeEmulationMaterialTypedByteCount = 0u;
    bool accumulationMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena accumulationMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId accumulationMaterialGeometrySet;
    Core::GpuGraphResourceSetId accumulationMaterialSampledTextureSet;
    {
        Core::Alloc::ScratchArena accumulationUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions accumulationDrawItems{ accumulationUploadScratch };
        InstanceGpuDataVector accumulationInstanceData{ accumulationUploadScratch };
        CsgFrameGpuData accumulationCsgFrameData{ accumulationUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector accumulationMaterialTypedRanges{ accumulationUploadScratch };
#endif
        MaterialTypedByteDataVector accumulationMaterialTypedBytes{ accumulationUploadScratch };
        m_materialSystem.gatherMaterialPassDrawItems(
            deferredTargets.avboit.accumulationFramebuffer.get(),
            MaterialPipelinePass::AvboitAccumulate,
            true,
            csgFrameState,
            accumulationDrawItems,
            accumulationInstanceData,
            accumulationCsgFrameData,
#if defined(NWB_DEBUG)
            accumulationMaterialTypedRanges,
#endif
            accumulationMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly
        );

        const bool accumulationHasCsgDrawItems = !accumulationDrawItems.csg.empty();
        if(!accumulationDrawItems.empty()){
            if(
                !materialInstances.valid()
                || !materialTyped.valid()
                || !m_materialSystem.materialPassDrawBuffersReady(
                    accumulationInstanceData,
                    accumulationMaterialTypedBytes
                )
                || !m_materialSystem.materialPassDrawResourcesReady(accumulationDrawItems.regular)
                || (accumulationHasCsgDrawItems && (
                    !accumulationCsgFrameData.hasWork()
                    || !csgReceiverRanges.valid()
                    || !csgCutters.valid()
                    || !csgClipContextSlots.valid()
                    || !csgIntervalSampleState.valid()
                    || !m_csgSystem.csgFrameBuffersReady(accumulationCsgFrameData)
                    || !m_materialSystem.materialPassDrawResourcesReady(accumulationDrawItems.csg)
                ))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT accumulation resources were unavailable during graph declaration"));
                return;
            }

            const MaterialPassDrawItems* const accumulationMaterialGeometryDrawSets[] = {
                &accumulationDrawItems.regular,
                &accumulationDrawItems.csg,
            };
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                accumulationMaterialGeometryDrawSets,
                LengthOf(accumulationMaterialGeometryDrawSets),
                accumulationMaterialGeometryScratch,
                Name("render.avboit.accumulation.material_geometry"),
                "AVBOIT Accumulation Material Geometry",
                accumulationMaterialGeometrySet
            );
            if(!avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT accumulation material geometry states"));
            accumulationMaterialSampledTexturesCollected =
                avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_deferredLightingTaskGraph,
                    accumulationMaterialGeometryDrawSets,
                    LengthOf(accumulationMaterialGeometryDrawSets),
                    accumulationMaterialGeometryScratch,
                    Name("render.avboit.accumulation.material_sampled_textures"),
                    "AVBOIT Accumulation Material Sampled Textures",
                    accumulationMaterialSampledTextureSet
                )
            ;
            if(
                avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && !accumulationMaterialSampledTexturesCollected
            )
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT accumulation material sampled textures"));

            m_materialSystem.prepareMaterialPassInstanceUploadData(accumulationInstanceData);
#if defined(NWB_DEBUG)
            if(
                accumulationInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || accumulationCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || accumulationCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT accumulation upload size overflows graph blob capacity"));
                return;
            }
            NWB_ASSERT(accumulationInstanceData.size() == accumulationMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                accumulationMaterialTypedRanges,
                accumulationMaterialTypedBytes
            );
#endif

            const Core::GpuUploadBlobId accumulationInstanceBlob = m_deferredLightingTaskGraph.copyUploadData(
                accumulationInstanceData.data(),
                accumulationInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId accumulationMaterialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
                accumulationMaterialTypedBytes.data(),
                accumulationMaterialTypedBytes.size(),
                alignof(u32)
            );
            if(!accumulationInstanceBlob.valid() || !accumulationMaterialTypedBlob.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT accumulation material upload data"));
                return;
            }

            Core::GpuTaskSchedulingHint accumulationUploadScheduling;
            accumulationUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            accumulationUploadScheduling.forceSubmissionBoundary = false;
            accumulationUploadScheduling.allowPacketMerge = true;
            accumulationUploadScheduling.mergeWithPrevious = true;

            Core::GpuTaskDesc accumulationInstanceUploadDesc;
            accumulationInstanceUploadDesc
                .setIdentity(Name("render.avboit.accumulation.material_instances_upload"))
                .setMarkerLabel("AVBOIT Accumulation Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulationUploadScheduling)
                .setDependencies(&accumulationUploadTask, 1u)
            ;
            accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                accumulationInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = accumulationInstanceBlob,
                    .destination = materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!accumulationUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation material instance upload"));
                return;
            }

            Core::GpuTaskDesc accumulationMaterialTypedUploadDesc;
            accumulationMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.accumulation.material_typed_upload"))
                .setMarkerLabel("AVBOIT Accumulation Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(accumulationUploadScheduling)
                .setDependencies(&accumulationUploadTask, 1u)
            ;
            accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                accumulationMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = accumulationMaterialTypedBlob,
                    .destination = materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!accumulationUploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation material typed upload"));
                return;
            }

            if(accumulationHasCsgDrawItems){
                CsgClipContextSlots accumulationCsgClipContextSlotData;
                if(!m_csgSystem.prepareCsgClipContextSlotData(
                    accumulationCsgFrameData,
                    accumulationCsgClipContextSlotData
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT accumulation CSG context data"));
                    return;
                }
                const Core::GpuUploadBlobId accumulationCsgReceiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
                    accumulationCsgFrameData.receiverRanges.data(),
                    accumulationCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                    alignof(CsgReceiverRangeGpuData)
                );
                const Core::GpuUploadBlobId accumulationCsgCuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
                    accumulationCsgFrameData.cutters.data(),
                    accumulationCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                    alignof(CsgCutterGpuData)
                );
                const Core::GpuUploadBlobId accumulationCsgClipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
                    &accumulationCsgClipContextSlotData,
                    sizeof(accumulationCsgClipContextSlotData),
                    alignof(CsgClipContextSlots)
                );
                if(
                    !accumulationCsgReceiverRangesBlob.valid()
                    || !accumulationCsgCuttersBlob.valid()
                    || !accumulationCsgClipContextSlotsBlob.valid()
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT accumulation CSG upload data"));
                    return;
                }

                Core::GpuTaskDesc accumulationCsgReceiverRangesUploadDesc;
                accumulationCsgReceiverRangesUploadDesc
                    .setIdentity(Name("render.avboit.accumulation.csg_receiver_ranges_upload"))
                    .setMarkerLabel("AVBOIT Accumulation CSG Receiver Ranges Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(accumulationUploadScheduling)
                    .setDependencies(&accumulationUploadTask, 1u)
                ;
                accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    accumulationCsgReceiverRangesUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = accumulationCsgReceiverRangesBlob,
                        .destination = csgReceiverRanges,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!accumulationUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation CSG receiver-range upload"));
                    return;
                }

                Core::GpuTaskDesc accumulationCsgCuttersUploadDesc;
                accumulationCsgCuttersUploadDesc
                    .setIdentity(Name("render.avboit.accumulation.csg_cutters_upload"))
                    .setMarkerLabel("AVBOIT Accumulation CSG Cutters Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(accumulationUploadScheduling)
                    .setDependencies(&accumulationUploadTask, 1u)
                ;
                accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    accumulationCsgCuttersUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = accumulationCsgCuttersBlob,
                        .destination = csgCutters,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!accumulationUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation CSG cutter upload"));
                    return;
                }

                Core::GpuTaskDesc accumulationCsgClipContextSlotsUploadDesc;
                accumulationCsgClipContextSlotsUploadDesc
                    .setIdentity(Name("render.avboit.accumulation.csg_clip_context_slots_upload"))
                    .setMarkerLabel("AVBOIT Accumulation CSG Clip Context Slots Upload")
                    .setQueue(GraphicsUploadQueueRequest())
                    .setScheduling(accumulationUploadScheduling)
                    .setDependencies(&accumulationUploadTask, 1u)
                ;
                accumulationUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
                    accumulationCsgClipContextSlotsUploadDesc,
                    Core::GpuUploadBufferTaskDesc{
                        .source = accumulationCsgClipContextSlotsBlob,
                        .destination = csgClipContextSlots,
                        .finalState = Core::ResourceStates::Common,
                    }
                );
                if(!accumulationUploadTask.valid()){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation CSG clip-context upload"));
                    return;
                }
                accumulationCsgStreamsUploaded = true;
            }

            avboitAccumulationPayload.accumulationSnapshot.capture(
                accumulationDrawItems,
                accumulationCsgFrameData,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size()
            );
            avboitAccumulationPayload.accumulationPhasePrepared = true;
            accumulationStreamsUploaded = true;
            // A phase may graph-own exactly one alias-free compute stream. Mixed regular/CSG work retains the
            // established local interleaving because one producer/raster handoff cannot preserve its draw order.
            accumulationRegularComputeEmulationPlanCaptured = accumulationDrawItems.csg.computeDrawItems.empty()
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && avboitAccumulationComputeEmulationPayload.plan.capture(
                    m_meshSystem,
                    accumulationDrawItems.regular
                )
            ;
            accumulationCsgComputeEmulationPlanCaptured = accumulationDrawItems.regular.computeDrawItems.empty()
                && accumulationCsgStreamsUploaded
                && avboitIntervalOutputsGraphOwned
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && avboitAccumulationComputeEmulationPayload.csgPlan.capture(
                    m_meshSystem,
                    accumulationDrawItems.csg,
                    accumulationCsgFrameData
                )
            ;
            // The unsplit all-compute two-, three-, or four-draw case can preserve one shared generated output only
            // as an explicit D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) -> R(D)] sequence. Keep mesh and
            // CSG work out of this narrow slice so the aggregate accumulation callback is never partially replayed
            // around its phases.
            accumulationSharedComputeEmulationPlanCaptured = !splitAvboitStages
                && !accumulationRegularComputeEmulationPlanCaptured
                && accumulationDrawItems.regular.meshDrawItems.empty()
                && accumulationDrawItems.csg.empty()
                && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
                && accumulationMaterialSampledTexturesCollected
                && accumulationSharedComputeEmulationPlan.capture(
                    m_meshSystem,
                    accumulationDrawItems.regular,
                    4u
                )
                && accumulationSharedComputeEmulationPlan.drawCount >= 2u
                && accumulationSharedComputeEmulationPlan.drawCount <= 4u
            ;
            NWB_ASSERT(
                !(accumulationRegularComputeEmulationPlanCaptured && accumulationCsgComputeEmulationPlanCaptured)
            );
            NWB_ASSERT(
                !accumulationSharedComputeEmulationPlanCaptured
                || (!accumulationRegularComputeEmulationPlanCaptured
                    && !accumulationCsgComputeEmulationPlanCaptured)
            );
            if(
                accumulationRegularComputeEmulationPlanCaptured
                || accumulationCsgComputeEmulationPlanCaptured
            ){
                avboitAccumulationComputeEmulationPayload.instanceCount = accumulationInstanceData.size();
                avboitAccumulationComputeEmulationPayload.materialTypedByteCount = accumulationMaterialTypedBytes.size();
            }
            if(accumulationSharedComputeEmulationPlanCaptured){
                accumulationSharedComputeEmulationInstanceCount = accumulationInstanceData.size();
                accumulationSharedComputeEmulationMaterialTypedByteCount = accumulationMaterialTypedBytes.size();
            }
        }
        else{
            // An empty captured phase is still authoritative: recording must not re-gather mutable renderer state.
            avboitAccumulationPayload.accumulationSnapshot.capture(
                accumulationDrawItems,
                accumulationCsgFrameData,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size()
            );
            avboitAccumulationPayload.accumulationPhasePrepared = true;
        }
    }

    const bool accumulationCsgIntervalSampleImageStatesGraphOwned =
        avboitIntervalOutputsGraphOwned && accumulationCsgStreamsUploaded
    ;
    const bool accumulationCsgClipBufferStatesGraphOwned = accumulationCsgStreamsUploaded;
    NWB_ASSERT(
        !accumulationCsgIntervalSampleImageStatesGraphOwned
        || (
            avboitAccumulationPayload.accumulationPhasePrepared
            && avboitAccumulationPayload.accumulationSnapshot.captured
        )
    );
    NWB_ASSERT(
        !accumulationCsgClipBufferStatesGraphOwned
        || (
            avboitAccumulationPayload.accumulationPhasePrepared
            && avboitAccumulationPayload.accumulationSnapshot.captured
        )
    );
    avboitAccumulationPayload.accumulationCsgIntervalSampleImageStatesGraphOwned =
        accumulationCsgIntervalSampleImageStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationCsgClipBufferStatesGraphOwned =
        accumulationCsgClipBufferStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationMaterialFrameStatesGraphOwned = accumulationStreamsUploaded;
    avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned =
        accumulationStreamsUploaded
        && avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId accumulationComputeEmulationOutputSet;
    Core::Alloc::ScratchArena accumulationComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool accumulationComputeEmulationPlanCaptured =
        accumulationRegularComputeEmulationPlanCaptured
        || accumulationCsgComputeEmulationPlanCaptured
    ;
    bool accumulationComputeEmulationOutputStatesGraphOwned = false;
    if(accumulationRegularComputeEmulationPlanCaptured){
        accumulationComputeEmulationOutputStatesGraphOwned = GatherAvboitAliasFreeComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            avboitAccumulationComputeEmulationPayload.plan,
            accumulationComputeEmulationResourceScratch,
            Name("render.avboit.accumulation.compute_emulation.outputs"),
            "AVBOIT Accumulation Compute Emulation Outputs",
            accumulationComputeEmulationOutputSet
        );
    }
    else if(accumulationCsgComputeEmulationPlanCaptured){
        accumulationComputeEmulationOutputStatesGraphOwned =
            GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
                m_deferredLightingTaskGraph,
                avboitAccumulationComputeEmulationPayload.csgPlan,
                accumulationComputeEmulationResourceScratch,
                Name("render.avboit.accumulation.csg_compute_emulation.outputs"),
                "AVBOIT Accumulation CSG Compute Emulation Outputs",
                accumulationComputeEmulationOutputSet
            )
        ;
    }
    if(
        accumulationComputeEmulationPlanCaptured
        && !accumulationComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Accumulation compute-emulation output states"
        ));
    }
    avboitAccumulationPayload.accumulationComputeEmulationOutputStatesGraphOwned =
        accumulationRegularComputeEmulationPlanCaptured
        && accumulationComputeEmulationOutputStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationCsgComputeEmulationOutputStatesGraphOwned =
        accumulationCsgComputeEmulationPlanCaptured
        && accumulationComputeEmulationOutputStatesGraphOwned
    ;
    avboitAccumulationPayload.accumulationComputeEmulationTiming =
        accumulationComputeEmulationOutputStatesGraphOwned
            ? &avboitAccumulationComputeEmulationTiming
            : nullptr
    ;
    Core::GpuGraphResourceId accumulationSharedComputeEmulationOutput;
    const bool accumulationSharedComputeEmulationOutputStatesGraphOwned =
        accumulationSharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            accumulationSharedComputeEmulationPlan,
            "AVBOIT Accumulation Shared Compute Emulation Output",
            accumulationSharedComputeEmulationOutput
        )
    ;
    if(
        accumulationSharedComputeEmulationPlanCaptured
        && !accumulationSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Accumulation shared compute-emulation output state"
        ));
    }

    Core::Alloc::ScratchArena accumulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationResourceUses{ accumulationResourceScratch };
    accumulationResourceUses.reserve(
        (splitAvboitStages ? 9u : 12u)
        + (accumulationStreamsUploaded ? 7u : 0u)
        + (accumulationCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    if(!splitAvboitStages){
        // Graphics-only accumulation samples these full-resolution G-buffer inputs through material descriptors.
        accumulationResourceUses.push_back(ReadUse(albedo));
        accumulationResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource, true));
        accumulationResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true));
    }
    // accumulationFramebuffer binds deferred depth read-only, which Vulkan tracks as DepthRead rather than SRV.
    accumulationResourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    accumulationResourceUses.push_back(ReadUse(avboitTransmittance));
    accumulationResourceUses.push_back(ReadUse(avboitDepthWarp));
    accumulationResourceUses.push_back(ReadUse(avboitControl));
    accumulationResourceUses.push_back(ReadWriteUse(avboitAccumColor, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(avboitAccumExtinction, Core::ResourceStates::RenderTarget));
    if(accumulationStreamsUploaded){
        accumulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        accumulationResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        accumulationResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        if(accumulationCsgStreamsUploaded){
            accumulationResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            accumulationResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            accumulationResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // This remains the full-resolution interval producer's state; accumulation only samples it.
            accumulationResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(accumulationCsgIntervalSampleImageStatesGraphOwned){
                // The prepared transparent interval producer wrote these aliases. Accumulation loads them through
                // StorageImage descriptors, so the graph lowers its same-UAV handoff before this thunk records.
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalDepth,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCapNormal,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalData,
                    csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    csgRemovedIntervalCount,
                    csgRemovedIntervalCountSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
            }
        }
    }
    const Core::GpuTaskResourceSetUse accumulationMaterialGeometrySetUse{
        .resourceSet = accumulationMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse accumulationMaterialSampledTextureSetUse{
        .resourceSet = accumulationMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse accumulationComputeEmulationOutputUavSetUse{
        .resourceSet = accumulationComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse accumulationComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = accumulationComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse accumulationMaterialResourceSetUses[3u] = {};
    usize accumulationMaterialResourceSetUseCount = 0u;
    if(avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationMaterialGeometrySetUse;
    }
    if(accumulationMaterialSampledTextureSet.valid()){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationMaterialSampledTextureSetUse;
    }
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationComputeEmulationOutputVertexBufferSetUse;
    }
    accumulationResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    accumulationResourceUses.push_back(ReadUse(avboitMaterialDomain));
    accumulationResourceUses.push_back(ReadUse(avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitAccumulationScheduling;
    avboitAccumulationScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitAccumulationScheduling.forceSubmissionBoundary = false;
    avboitAccumulationScheduling.allowPacketMerge = true;
    avboitAccumulationScheduling.mergeWithPrevious = true;

    // Keep the final immutable upload as the semantic stream anchor. The optional producer becomes only the
    // immediate Accumulation dependency; replacing this anchor would hide a broken upload-to-producer handoff.
    const Core::GpuTaskId accumulationStreamTask = accumulationUploadTask;
    if(accumulationStreamsUploaded)
        m_deferredAvboitAccumulationStreamTask = accumulationStreamTask;
    Core::GpuTaskId accumulationDependency = accumulationUploadTask;
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        avboitAccumulationComputeEmulationPayload.renderer = this;
        avboitAccumulationComputeEmulationPayload.targets = &deferredTargets;
        avboitAccumulationComputeEmulationPayload.timingTicket = avboitAccumulationPayload.timingTicket;
        avboitAccumulationComputeEmulationPayload.accumulationTiming = &avboitAccumulationComputeEmulationTiming;
        avboitAccumulationComputeEmulationPayload.materialDrawBuffersUploaded = accumulationStreamsUploaded;
        avboitAccumulationComputeEmulationPayload.csgFrameBuffersUploaded = accumulationCsgStreamsUploaded;
        avboitAccumulationComputeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            accumulationCsgIntervalSampleImageStatesGraphOwned;
        avboitAccumulationComputeEmulationPayload.csgClipBufferStatesGraphOwned =
            accumulationCsgClipBufferStatesGraphOwned;
        avboitAccumulationComputeEmulationPayload.materialFrameStatesGraphOwned =
            avboitAccumulationPayload.accumulationMaterialFrameStatesGraphOwned;
        avboitAccumulationComputeEmulationPayload.materialGeometryStatesGraphOwned =
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationComputeEmulationResourceUses{
            accumulationResourceScratch
        };
        accumulationComputeEmulationResourceUses.reserve(
            4u + (accumulationCsgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        accumulationComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(accumulationCsgComputeEmulationPlanCaptured){
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse accumulationComputeEmulationResourceSetUses[3u] = {};
        usize accumulationComputeEmulationResourceSetUseCount = 0u;
        accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
            accumulationMaterialGeometrySetUse;
        if(accumulationMaterialSampledTextureSet.valid()){
            accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
                accumulationMaterialSampledTextureSetUse;
        }
        accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
            accumulationComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint accumulationComputeEmulationScheduling;
        accumulationComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        accumulationComputeEmulationScheduling.forceSubmissionBoundary = false;
        accumulationComputeEmulationScheduling.allowPacketMerge = true;
        accumulationComputeEmulationScheduling.mergeWithPrevious = true;
        // The next raster consumes this producer's graph-owned UAV output and shares its Accumulation timing
        // ticket. Keep the immediate pair intact even if Composite observes the finalizer from a later Compute
        // packet.
        accumulationComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc accumulationComputeEmulationDesc;
        accumulationComputeEmulationDesc
            .setIdentity(accumulationCsgComputeEmulationPlanCaptured
                ? Name("render.avboit.accumulation.csg_compute_emulation")
                : Name("render.avboit.accumulation.compute_emulation"))
            .setMarkerLabel(accumulationCsgComputeEmulationPlanCaptured
                ? "AVBOIT Accumulation CSG Compute Emulation"
                : "AVBOIT Accumulation Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(accumulationComputeEmulationScheduling)
            .setDependencies(&accumulationDependency, 1u)
            .setResourceUses(
                accumulationComputeEmulationResourceUses.data(),
                accumulationComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                accumulationComputeEmulationResourceSetUses,
                accumulationComputeEmulationResourceSetUseCount
            )
        ;
        m_deferredAvboitAccumulationComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            AvboitAccumulationComputeEmulationGraphTask
        >(
            accumulationComputeEmulationDesc,
            Move(avboitAccumulationComputeEmulationPayload)
        );
        if(!m_deferredAvboitAccumulationComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Accumulation compute-emulation producer"
            ));
            return;
        }
        accumulationDependency = m_deferredAvboitAccumulationComputeEmulationTask;
        avboitAccumulationScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(accumulationSharedComputeEmulationOutputStatesGraphOwned){
        // The one retained output appears in every phase, so keep it as an exact resource rather than placing it
        // in a resource set whose duplicate expansion would erase the alternating UAV/VertexBuffer uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedGenerateResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedGenerateResourceUses.reserve(5u);
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            materialInstances,
            Core::ResourceStates::ShaderResource
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            materialTyped,
            Core::ResourceStates::ShaderResource
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(WriteUse(
            accumulationSharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedRasterResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedRasterResourceUses.assign(
            accumulationResourceUses.begin(),
            accumulationResourceUses.end()
        );
        accumulationSharedRasterResourceUses.push_back(ReadUse(
            accumulationSharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint accumulationSharedComputeEmulationScheduling;
        accumulationSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        accumulationSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        accumulationSharedComputeEmulationScheduling.allowPacketMerge = true;
        accumulationSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Every phase is an explicit immediate successor. Keep the full alternating chain in AVBOIT Pre despite
        // Composite's later Compute consumer so one command list owns the timing scope and finalizer handoff.
        accumulationSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addAccumulationSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &accumulationSharedComputeEmulationPlan,
            &avboitAccumulationComputeEmulationTiming,
            accumulationSharedComputeEmulationInstanceCount,
            accumulationSharedComputeEmulationMaterialTypedByteCount,
            accumulationStreamsUploaded,
            accumulationMaterialFrameStatesGraphOwned = avboitAccumulationPayload.accumulationMaterialFrameStatesGraphOwned,
            accumulationMaterialGeometryStatesGraphOwned = avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned,
            avboitAccumulationTimingTicket = avboitAccumulationPayload.timingTicket,
            &accumulationSharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitAccumulationSharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool beginTiming,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(accumulationSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitAccumulationSharedComputeEmulationGraphTask::Payload payload;
            payload.renderer = this;
            payload.targets = &deferredTargets;
            payload.timingTicket = avboitAccumulationTimingTicket;
            payload.accumulationTiming = &avboitAccumulationComputeEmulationTiming;
            payload.plan = accumulationSharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = accumulationSharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = accumulationSharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = accumulationStreamsUploaded;
            payload.materialFrameStatesGraphOwned = accumulationMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = accumulationMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<AvboitAccumulationSharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using AccumulationSharedPhase = AvboitAccumulationSharedComputeEmulationGraphTask::Phase;
        const Name accumulationSharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.accumulation.shared_compute_emulation_generate_a"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_a"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_b"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_b"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_c"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_c"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_d"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_d"),
        };
        const AStringView accumulationSharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Accumulation Shared Compute Emulation Generate A",
            "AVBOIT Accumulation Shared Compute Emulation Raster A",
            "AVBOIT Accumulation Shared Compute Emulation Generate B",
            "AVBOIT Accumulation Shared Compute Emulation Raster B",
            "AVBOIT Accumulation Shared Compute Emulation Generate C",
            "AVBOIT Accumulation Shared Compute Emulation Raster C",
            "AVBOIT Accumulation Shared Compute Emulation Generate D",
            "AVBOIT Accumulation Shared Compute Emulation Raster D",
        };
        const usize accumulationSharedComputeEmulationPhaseCount =
            accumulationSharedComputeEmulationPlan.drawCount * 2u
        ;
        NWB_ASSERT(
            accumulationSharedComputeEmulationPlan.drawCount == 2u
            || accumulationSharedComputeEmulationPlan.drawCount == 3u
            || accumulationSharedComputeEmulationPlan.drawCount == 4u
        );
        NWB_ASSERT(
            accumulationSharedComputeEmulationPhaseCount
            <= LengthOf(accumulationSharedComputeEmulationPhaseIdentities)
        );
        Core::GpuTaskId accumulationSharedComputeEmulationDependency = accumulationDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < accumulationSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase = phaseIndex % 2u != 0u;
            m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseIndex] =
                addAccumulationSharedComputeEmulationPhase(
                    accumulationSharedComputeEmulationPhaseIdentities[phaseIndex],
                    accumulationSharedComputeEmulationPhaseMarkers[phaseIndex],
                    accumulationSharedComputeEmulationDependency,
                    isRasterPhase ? AccumulationSharedPhase::Raster : AccumulationSharedPhase::Generate,
                    phaseIndex / 2u,
                    phaseIndex == 0u,
                    phaseIndex + 1u == accumulationSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? accumulationSharedRasterResourceUses
                        : accumulationSharedGenerateResourceUses,
                    accumulationMaterialResourceSetUses,
                    accumulationMaterialResourceSetUseCount
                )
            ;
            if(!m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Accumulation shared compute-emulation phase"
                ));
                return;
            }
            accumulationSharedComputeEmulationDependency =
                m_deferredAvboitAccumulationSharedComputeEmulationTasks[phaseIndex];
        }
        m_deferredAvboitAccumulationSharedComputeEmulationTaskCount =
            accumulationSharedComputeEmulationPhaseCount;
        // The terminal raster is the existing Accumulation semantic endpoint: it feeds the unchanged finalizer,
        // timing ticket, state cache, record range, and accepted-token publication path.
        m_deferredAvboitAccumulationTask = accumulationSharedComputeEmulationDependency;
    }
    else{
        Core::GpuTaskDesc accumulationDesc;
        accumulationDesc
            .setIdentity(Name("render.avboit.accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitAccumulationScheduling)
            .setDependencies(&accumulationDependency, 1u)
            .setResourceUses(accumulationResourceUses.data(), accumulationResourceUses.size())
            .setResourceSetUses(
                accumulationMaterialResourceSetUseCount != 0u ? accumulationMaterialResourceSetUses : nullptr,
                accumulationMaterialResourceSetUseCount
            )
        ;
        m_deferredAvboitAccumulationTask = m_deferredLightingTaskGraph.addTask<AvboitAccumulationGraphTask>(
            accumulationDesc,
            Move(avboitAccumulationPayload)
        );
        if(!m_deferredAvboitAccumulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
            return;
        }
    }
    const Core::GpuTaskResourceUse accumulationFinalizeResourceUses[] = {
        ReadUse(avboitAccumColor, Core::ResourceStates::ShaderResource),
        ReadUse(avboitAccumExtinction, Core::ResourceStates::ShaderResource),
        ReadUse(depth, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint accumulationFinalizeScheduling;
    accumulationFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accumulationFinalizeScheduling.forceSubmissionBoundary = false;
    accumulationFinalizeScheduling.allowPacketMerge = true;
    accumulationFinalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc accumulationFinalizeDesc;
    accumulationFinalizeDesc
        .setIdentity(Name("render.avboit.accumulation_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(accumulationFinalizeScheduling)
        .setDependencies(&m_deferredAvboitAccumulationTask, 1u)
        .setResourceUses(accumulationFinalizeResourceUses, LengthOf(accumulationFinalizeResourceUses))
    ;
    m_deferredAvboitAccumulationFinalizeTask = m_deferredLightingTaskGraph.addTask<AvboitAccumulationFinalizeGraphTask>(
        accumulationFinalizeDesc,
        AvboitAccumulationFinalizeGraphTask::Payload{}
    );
    if(!m_deferredAvboitAccumulationFinalizeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation finalizer graph task"));
        return;
    }

    }
    const Core::GpuTaskId avboitFinalTask = hasTransparentRenderers
        ? m_deferredAvboitAccumulationFinalizeTask
        : m_deferredAvboitOccupancyTask
    ;

    const Core::GpuExternalCompletionId laggedLightingExternalDependencies[] = {
        m_deferredLightingHistoryCompletion,
    };
    const Core::GpuExternalCompletionId* const lightingExternalDependencies = useLaggedLightingHistory
        ? laggedLightingExternalDependencies
        : nullptr
    ;
    const usize lightingExternalDependencyCount = useLaggedLightingHistory
        ? LengthOf(laggedLightingExternalDependencies)
        : 0u
    ;
    // Live Lighting joins Shadow/Software, Surfel GI, AVBOIT, and Hardware Caustics through internal graph edges.
    // Active lagged Lighting instead reads history and stays independent from the current-frame producers.
    const Core::GpuTaskId hardwareLightingDependencies[] = {
        m_deferredShadowVisibilityTask,
        m_deferredSurfelGiTask,
        avboitFinalTask,
        m_deferredHardwareCausticsTask,
    };
    const Core::GpuTaskId softwareLightingDependencies[] = {
        m_deferredShadowVisibilityTask,
        m_deferredSoftwareCausticsTask,
        m_deferredSurfelGiTask,
        avboitFinalTask,
    };
    // Lagged Lighting normally reads its shared G-buffer inputs from the accepted prefix while history supplies the
    // temporal effects. Transparent AVBOIT accumulation temporarily binds current deferred depth as DepthRead, so
    // its finalizer must complete before that independent Compute reader can observe ShaderResource layout again.
    const Core::GpuTaskId laggedLightingDependencies[] = {
        m_graphicsPrefixTask,
        avboitFinalTask,
    };
    const usize laggedLightingDependencyCount = hasTransparentRenderers ? 2u : 1u;
    const Core::GpuTaskId laggedLightingSelectorUploadDependencies[] = { m_graphicsPrefixTask };
    const bool laggedBindlessSlotsGraphOwned = useLaggedLightingHistory && !history->slotsUploaded;
    if(laggedBindlessSlotsGraphOwned){
        const Core::GpuUploadBlobId laggedBindlessSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
            &history->slots,
            sizeof(history->slots),
            alignof(DeferredBindlessResourceSlots)
        );
        if(!laggedBindlessSlotsBlob.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain lagged lighting-history selector upload data"));
            return;
        }

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name("render.lagged_lighting.bindless_slots_upload"))
            .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
            .setQueue(ComputeUploadQueueRequest())
            .setScheduling(uploadScheduling)
            .setDependencies(
                laggedLightingSelectorUploadDependencies,
                LengthOf(laggedLightingSelectorUploadDependencies)
            )
        ;
        m_deferredLaggedLightingHistorySlotsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = laggedBindlessSlotsBlob,
                .destination = bindlessSlots,
                // Automatic-state selector buffers publish Common; Deferred Lighting owns the following
                // ConstantBuffer transition in this same externally gated packet.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_deferredLaggedLightingHistorySlotsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare lagged lighting-history selector upload"));
            return;
        }
    }
    const Core::GpuTaskId laggedLightingWithSelectorDependencies[] = {
        m_graphicsPrefixTask,
        m_deferredLaggedLightingHistorySlotsUploadTask,
        avboitFinalTask,
    };
    const Core::GpuTaskId* const lightingDependencies = declaresHardwareCaustics
        ? (useLaggedLightingHistory ? laggedLightingDependencies : hardwareLightingDependencies)
        : (useLaggedLightingHistory ? laggedLightingDependencies : softwareLightingDependencies)
    ;
    const Core::GpuTaskId* const resolvedLightingDependencies = laggedBindlessSlotsGraphOwned
        ? laggedLightingWithSelectorDependencies
        : lightingDependencies
    ;
    const usize lightingDependencyCount = laggedBindlessSlotsGraphOwned
        ? (hasTransparentRenderers ? 3u : 2u)
        : (useLaggedLightingHistory
            ? laggedLightingDependencyCount
            : LengthOf(hardwareLightingDependencies))
    ;
    // Active lagged Lighting receives its shared albedo/normal/world inputs directly from the accepted prefix
    // source while it reads history. Transparent depth is the explicit exception: the finalizer dependency above
    // orders its temporary AVBOIT DepthRead layout before Lighting samples ShaderResource state.
    const bool laggedReadsHaveIndependentStateSources = useLaggedLightingHistory;
    const bool laggedBindlessSlotsHaveIndependentStateSource =
        laggedReadsHaveIndependentStateSources && !laggedBindlessSlotsGraphOwned
    ;
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadTextureUse(
            albedo,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(
            normal,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer,
            laggedReadsHaveIndependentStateSources
        ),
        ReadUse(lights, Core::ResourceStates::ShaderResource, laggedReadsHaveIndependentStateSources),
        ReadUse(
            bindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            laggedBindlessSlotsHaveIndependentStateSource
        ),
        WriteTextureUse(opaqueColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = !laggedBindlessSlotsGraphOwned;
    scheduling.allowPacketMerge = laggedBindlessSlotsGraphOwned;
    scheduling.mergeWithPrevious = laggedBindlessSlotsGraphOwned;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(resolvedLightingDependencies, lightingDependencyCount)
        .setExternalDependencies(lightingExternalDependencies, lightingExternalDependencyCount)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredLightingTask = m_deferredSystem.declareDeferredLightingTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        useLaggedLightingHistory,
        currentBindlessSlotsGraphOwned,
        laggedBindlessSlotsGraphOwned,
        lightingTimingTicket
    );
    if(!m_deferredLightingTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }

    // Composite remains a distinct packet and joins both graph-owned AVBOIT and Lighting. It retains the current
    // bindless selector in lagged mode rather than inheriting Lighting's history selector.
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_composite.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId compositeBindlessSlots = currentBindlessSlots;
    if(
        !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !compositeColor.valid()
        || !compositeBindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse compositeResourceUses[] = {
        ReadUse(opaqueColor),
        ReadUse(avboitAccumColor),
        ReadUse(avboitAccumExtinction),
        ReadUse(
            compositeBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            useLaggedLightingHistory
        ),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Core::GpuTaskCostHint::Medium;
    compositeScheduling.avoidQueueCrossing = useLaggedLightingHistory;
    compositeScheduling.forceSubmissionBoundary = true;
    compositeScheduling.allowPacketMerge = false;
    const Core::GpuTaskId compositeDependencies[] = {
        m_deferredLightingTask,
        avboitFinalTask,
    };
    Core::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(ComputeQueueRequest())
        .setScheduling(compositeScheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeResourceUses, LengthOf(compositeResourceUses))
    ;
    m_deferredCompositeTask = m_deferredSystem.declareDeferredCompositeTask(
        m_deferredLightingTaskGraph,
        compositeDesc,
        deferredTargets,
        currentBindlessSlotsGraphOwned,
        compositeTimingTicket
    );
    if(!m_deferredCompositeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return;
    }

    const Core::GpuGraphResourceId backbuffer = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.deferred_present.backbuffer"), "Presentation Back Buffer")
    );
    if(!backbuffer.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse presentResourceUses[] = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(backbuffer, Core::ResourceStates::Present),
    };
    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    const Core::GpuTaskId presentDependencies[] = {
        m_deferredCompositeTask,
        m_deferredSurfelGiTask,
    };
    const usize presentDependencyCount = useLaggedLightingHistory ? LengthOf(presentDependencies) : 1u;
    Core::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(presentScheduling)
        .setDependencies(presentDependencies, presentDependencyCount)
        .setResourceUses(presentResourceUses, LengthOf(presentResourceUses))
    ;
    m_deferredPresentTask = m_deferredLightingTaskGraph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &deferredTargets,
            .presentationFramebuffer = presentationFramebuffer,
            .frameTimingTransaction = &frameTimingTransaction,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &presentTimingTicket,
            .shadowVisibilityTask = &m_deferredShadowVisibilityTask,
            .currentBindlessSlotsGraphOwned = currentBindlessSlotsGraphOwned,
        }
    );
    if(!m_deferredPresentTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return;
    }

    // UI/overlay work must be declared before the independent diagnostic and history-copy tails. Its explicit
    // dependency on Deferred Present therefore gives the shared graph the real final Graphics packet instead of
    // leaving Graphics::render() to submit a later untracked backbuffer write.
    m_deferredPresentationOverlayRequired =
        m_preparedTaskGraphPresentationContributor
        && m_preparedTaskGraphPresentationContributor->hasTaskGraphPresentationWork()
    ;
    if(m_deferredPresentationOverlayRequired){
        m_deferredPresentationOverlayTask = m_preparedTaskGraphPresentationContributor->declareTaskGraphPresentation(
            m_deferredLightingTaskGraph,
            presentationFramebuffer,
            backbuffer,
            m_deferredPresentTask
        );
        if(!m_deferredPresentationOverlayTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor did not declare its final graph task"));
            return;
        }
    }

    // Keep this diagnostic after the normal Present path in declaration order. Its only dependency is Surfel GI,
    // so it remains a late independent Transfer-preferred tail and does not delay lighting or presentation.
    declareDeferredSurfelCountReadbackTask();

    if(capturesLaggedLightingHistory){
        // The core built-in derives whole-resource CopySource/CopyDest declarations for these regions and retains
        // the imports itself. The array slices stay explicit only in the native copy body.
        Core::GpuCopyTextureTaskRegion historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 2u] = {};
        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::GpuCopyTextureTaskRegion& region = historyCopyRegions[shadowSlot];
            region.source = historyCopyShadowVisibility;
            region.destination = historyCopyDestinationShadowVisibility;
            region.sourceSlice.setArraySlice(shadowSlot);
            region.destinationSlice.setArraySlice(shadowSlot);
        }
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].source = historyCopyCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].destination = historyCopyDestinationCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].source = historyCopySurfelIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].destination = historyCopyDestinationSurfelIrradiance;
        Core::GpuTaskSchedulingHint historyCopyScheduling;
        historyCopyScheduling.cost = Core::GpuTaskCostHint::Medium;
        historyCopyScheduling.forceSubmissionBoundary = true;
        historyCopyScheduling.allowPacketMerge = false;
        const Core::GpuTaskId historyCopyDependencies[] = { m_deferredPresentTask };
        Core::GpuTaskDesc historyCopyDesc;
        historyCopyDesc
            .setIdentity(Name("render.lagged_history_copy"))
            .setMarkerLabel("Lagged Lighting History Copy")
            .setQueue(TransferQueueRequest())
            .setScheduling(historyCopyScheduling)
            .setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))
        ;
        m_deferredLaggedLightingHistoryTask = m_deferredLightingTaskGraph.addCopyTextureTask(
            historyCopyDesc,
            Core::GpuCopyTextureTaskDesc{
                .regions = historyCopyRegions,
                .regionCount = LengthOf(historyCopyRegions),
                .acceptedToken = &m_laggedLightingHistorySubmissionToken,
            }
        );
        if(!m_deferredLaggedLightingHistoryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred lagged-lighting history-copy task"));
            return;
        }
    }

    // Recovery is a late independent Graphics tail. It deliberately has no packet dependency on normal work: a
    // rejected suffix must not prevent it from retiring the accepted frame prefix. Its compiled packet asks the
    // graph transaction to join every accepted non-Graphics physical queue at submit time.
    const Core::GpuGraphResourceId recoveryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.frame_recovery.timing"), "Frame Recovery Timing")
    );
    if(!recoveryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred frame-recovery graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse recoveryResourceUses[] = {
        ReadWriteUse(recoveryDomain, Core::ResourceStates::Common),
    };
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Core::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("render.frame_recovery"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryResourceUses, LengthOf(recoveryResourceUses))
    ;
    m_deferredFrameRecoveryTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::FrameRecoveryGraphTask>(
        recoveryDesc,
        ECSRenderDetail::FrameRecoveryGraphTask::Payload{
            .frameTimingTransaction = &frameTimingTransaction,
            .armed = &m_deferredFrameRecoveryArmed,
            .retiresFrameTiming = &m_deferredFrameRecoveryRetiresTiming,
        }
    );
    if(!m_deferredFrameRecoveryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-recovery graph task"));
        return;
    }

    // The backend owns physical queue discovery and identity. The renderer consumes this immutable view directly so
    // graph packets can target multiple same-class native queues without rebuilding a class-shaped topology here.
    const Core::GpuTaskGraphQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: no native physical queue registry is available for the deferred graph"));
        return;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    Core::GpuTaskGraphCompileOptions compileOptions;
    // A graphics prefix can now split immediately after work that enables a different physical queue. This exposes
    // the true cross-queue frontier while preserving the compiler's declaration-derived dependency order.
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    if(!compiler.compile(
        m_deferredLightingTaskGraph,
        m_deferredLightingTaskGraphAnalysis,
        topology,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        scratchArena,
        compileOptions
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred AVBOIT/lighting/composite/present task graph"));
        return;
    }
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingTaskGraphValid = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

