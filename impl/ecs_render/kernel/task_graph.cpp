// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

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
        bool meshSwBvhBuildsGraphOwned = false;
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
                payload.meshSwBvhBuildsGraphOwned
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

        // The declared ShaderResource uses export every selected BLAS/SW-BVH input's exact graph-visible boundary
        // state before the following Prefix packet is seeded. Route-local build work remains inside this callback.
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
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
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
        bool csgReceiverSpanOutputImageStatesGraphOwned = false;
        bool csgRemovedIntervalOutputImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

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
                payload.materialGeometryStatesGraphOwned
            };
            if(regularDrawResourcesReady && !opaqueDrawItems.regular.empty()){
                Core::GpuTimingMeasure timing(
                    renderer.m_graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueRegular,
                    renderer.m_graphics.getDevice(),
                    commandList
                );
                renderer.m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
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
                payload.materialGeometryStatesGraphOwned
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
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(
                    commandList,
                    deferredTargets,
                    csgFrameData,
                    payload.csgReceiverSpanOutputImageStatesGraphOwned
                );
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                renderer.m_csgSystem.dispatchCsgIntervalCombine(
                    commandList,
                    deferredTargets,
                    csgFrameData,
                    payload.csgRemovedIntervalOutputImageStatesGraphOwned
                );
        }
        commandList.endRenderPass();
        return true;
    }
};


// Interval combine writes StorageImage-backed removed-interval outputs, while the following opaque material and cap
// draws load those same aliases. Keep the stages in their established Graphics submission when safe, but give the
// graph a task boundary at the required UAV fence instead of replaying it from the renderer thunk.
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
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty()
                || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        if(csgResourcesReady && csgDrawResourcesReady){
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
                payload.materialGeometryStatesGraphOwned
            };
            if(!opaqueDrawItems.csg.empty()){
                Core::GpuTimingMeasure timing(
                    renderer.m_graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueCsg,
                    renderer.m_graphics.getDevice(),
                    commandList
                );
                renderer.m_materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
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
};


// The two CSG values that carry state across a work region are reset by a distinct graph task.  The remaining
// interval images are fully overwritten or count-gated by their native producers, which retain their established
// compatibility state setup.  Keeping this task narrow lets the graph own the actual CopyDest clear contract
// without broadening this tranche into the full CSG image-lifecycle migration.
struct CsgIntervalRectClearGraphTask{
    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::Rect clearRect;
        // Prefix timing tickets are rebound after packet compilation; AVBOIT owns a stable packet-local ticket.
        Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        Core::GpuTimingSubmissionTicket* const timingTicket = payload.rebindableTimingTicket
            ? *payload.rebindableTimingTicket
            : payload.timingTicket
        ;
        if(!payload.deferredSystem || !payload.targets || !timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
        payload.deferredSystem->clearGraphOwnedCsgIntervalTargets(commandList, *payload.targets, payload.clearRect);
        return true;
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


// The AVBOIT targets have no semantic lifetime before the first transparent phase. Keep their clear as one
// graph-declared CopyDest operation so its exact nine writes and following producer transitions stay visible to the
// compiler, while the clear thunk itself remains a value-only native operation with its established timing scope.
struct AvboitClearGraphTask{
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
        payload.avboitSystem->clearGraphOwnedAvboitTargets(commandList, *payload.targets);
        return true;
    }
};


struct AvboitPreGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool hasTransparentRenderers = false;
        ECSRenderDetail::TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool transparentCsgStreamsUploaded = false;
        bool transparentCsgIntervalTargetsGraphOwned = false;
        bool transparentCsgIntervalPeelTargetStatesGraphOwned = false;
        bool transparentCsgReceiverSurfaceImageStatesGraphOwned = false;
        bool transparentCsgReceiverSpanOutputImageStatesGraphOwned = false;
        bool transparentCsgRemovedIntervalOutputImageStatesGraphOwned = false;
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
                payload.transparentCsgMaterialGeometryStatesGraphOwned
            );
        }
        return true;
    }
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
        bool splitStages = false;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot occupancySnapshot;
        bool occupancyPhasePrepared = false;
        bool occupancyStreamsUploaded = false;
        bool occupancyCsgIntervalSampleImageStatesGraphOwned = false;
        bool occupancyCsgClipBufferStatesGraphOwned = false;
        bool occupancyMaterialFrameStatesGraphOwned = false;
        bool occupancyMaterialGeometryStatesGraphOwned = false;

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
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
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
                payload.occupancyMaterialGeometryStatesGraphOwned
            );
        }
        // The declared sampled G-buffer uses remain authoritative here. Occupancy's low-resolution framebuffer
        // does not attach any deferred target, so the graph-established states remain valid for either continuation.
        return true;
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
        bool hasTransparentRenderers = false;
        bool splitStages = false;

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
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
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
            if(payload.splitStages){
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
                    payload.extinctionMaterialGeometryStatesGraphOwned
                );
            }
            else{
                payload.avboitSystem->renderAvboitPostOccupancyPreAccumulationPasses(
                    commandList,
                    *payload.targets,
                    *payload.csgFrameState,
                    preparedExtinctionDrawItems,
                    preparedExtinctionCsgFrameData,
                    preparedExtinctionInstanceCount,
                    preparedExtinctionMaterialTypedByteCount,
                    payload.extinctionCsgIntervalSampleImageStatesGraphOwned,
                    payload.extinctionCsgClipBufferStatesGraphOwned,
                    payload.extinctionMaterialFrameStatesGraphOwned,
                    payload.extinctionMaterialGeometryStatesGraphOwned
                );
            }
        }
        return true;
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
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
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
                payload.accumulationMaterialGeometryStatesGraphOwned
            );
        }
        return true;
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
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredShadowPrepareTask = {};
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

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(
        19u
        + shadowTraceGeometryResourceCount
        + softwareBvhBuildStateResourceCount
        + meshState().m_meshes.size() * 2u
    );
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
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = softwareBvhBuildStateResources[resourceIndex];
        if(!resource.valid())
            return false;
        // Parent links and global build scratch retain their native UAV close state. They are state-only graph
        // resources: later traversal keeps its narrower node/geometry declarations.
        resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
    }

    bool resourcesImported = true;
    if(m_rayTracingState.m_tlas){
        const Core::GpuGraphResourceId tlas = m_deferredLightingTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
        );
        const Core::GpuGraphResourceId tlasBacking = importBuffer(
            m_rayTracingState.m_tlas->getBackingBufferHandle(),
            Name("render.deferred_effects.tlas_backing"),
            "Scene TLAS Backing"
        );
        resourcesImported = resourcesImported && tlas.valid() && tlasBacking.valid();
        if(tlas.valid() && tlasBacking.valid()){
            // The native TLAS builder explicitly transitions Write -> Read inside Shadow Preparation. Keep this
            // graph-visible final read state as the accepting packet's cross-queue and cross-frame handoff.
            resourceUses.push_back(ReadWriteUse(tlas, Core::ResourceStates::AccelStructRead));
            resourceUses.push_back(WriteUse(tlasBacking, Core::ResourceStates::AccelStructRead));
        }
    }
    for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
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

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
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
            .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
        }
    );
    if(!m_deferredShadowPrepareTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation task"));
        return false;
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
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
    Core::GpuTimingSubmissionTicket** const timingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket
){
    using namespace __hidden_renderer_task_graph;
    using PrefixTimingSlot = ECSRenderDetail::DeferredGraphicsPrefixTimingSlot;

    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;

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
    ECSRenderDetail::CsgIntervalSampleGraphTask::Payload csgIntervalSamplePayload{ m_arena };
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
    // The three peel targets plus receiver-event/span data/count pairs and removed-interval outputs are declared
    // by the paired G-buffer producer and opaque sample tasks below whenever this semantic CSG producer exists.
    // Their native thunks consume graph-owned StorageImage state without staging the initial target transitions.
    gbufferPayload.csgIntervalPeelTargetStatesGraphOwned = hasOpaqueCsgFrameWork;
    gbufferPayload.csgReceiverSurfaceImageStatesGraphOwned = hasOpaqueCsgFrameWork;
    gbufferPayload.csgReceiverSpanOutputImageStatesGraphOwned = hasOpaqueCsgFrameWork;
    gbufferPayload.csgRemovedIntervalOutputImageStatesGraphOwned = hasOpaqueCsgFrameWork;
    // Match the actual graph declarations above; semantic CSG work may have no gathered GPU frame data.
    gbufferPayload.csgClipBufferStatesGraphOwned = hasCsgFrameGpuWork;

    // The clear is intentionally keyed to the semantic opaque-CSG frame flag, rather than the later native
    // readiness checks. This preserves the old defensive clear timing while making its two actual CopyDest writes
    // and the following UAV handoff visible to the graph.
    Core::GpuTaskId csgIntervalClearTask = csgFrameUploadTask;
    if(hasOpaqueCsgFrameWork){
        const Core::GpuTaskResourceUse csgIntervalClearResourceUses[] = {
            WriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::CopyDest),
            WriteTextureUse(
                csgReceiverEventCount,
                csgReceiverEventCountSubresources,
                Core::ResourceStates::CopyDest
            ),
        };
        Core::GpuTaskSchedulingHint csgIntervalClearScheduling;
        csgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        csgIntervalClearScheduling.forceSubmissionBoundary = false;
        csgIntervalClearScheduling.allowPacketMerge = true;
        csgIntervalClearScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgIntervalClearDesc;
        csgIntervalClearDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_clear"))
            .setMarkerLabel("CSG Interval Clear")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(csgIntervalClearScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
            .setResourceUses(csgIntervalClearResourceUses, LengthOf(csgIntervalClearResourceUses))
        ;
        csgIntervalClearTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::CsgIntervalRectClearGraphTask>(
            csgIntervalClearDesc,
            ECSRenderDetail::CsgIntervalRectClearGraphTask::Payload{
                .deferredSystem = &m_deferredSystem,
                .targets = &deferredTargets,
                .clearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height),
                .rebindableTimingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer),
            }
        );
        if(!csgIntervalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval clear"));
            return false;
        }
    }

    Core::Alloc::ScratchArena gbufferResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferResourceUses{ gbufferResourceScratch };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferMaterialGeometryUses{ gbufferResourceScratch };
    const MaterialPassDrawItems* const gbufferMaterialGeometryDrawSets[] = {
        &opaqueDrawItems.regular,
        &opaqueDrawItems.csgReceiverSurface,
    };
    const bool gbufferUsesMaterialGeometry =
        !opaqueDrawItems.regular.empty()
        || !opaqueDrawItems.csgReceiverSurface.empty()
    ;
    gbufferPayload.materialGeometryStatesGraphOwned = gbufferUsesMaterialGeometry
        && GatherPreparedMaterialGeometryUses(
            m_meshSystem,
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            gbufferResourceScratch,
            gbufferMaterialGeometryUses
        )
    ;
    if(gbufferUsesMaterialGeometry && !gbufferPayload.materialGeometryStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material geometry states"));
    gbufferResourceUses.reserve((hasOpaqueDrawItems ? 7u : 5u) + (hasCsgFrameGpuWork ? 5u : 0u) + (hasOpaqueCsgFrameWork ? 11u : 0u));
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
        gbufferResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    gbufferResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));
    for(const Core::GpuTaskResourceUse& use : gbufferMaterialGeometryUses)
        gbufferResourceUses.push_back(use);
    Core::GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = Core::GpuTaskCostHint::Medium;
    gbufferScheduling.forceSubmissionBoundary = false;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("render.graphics_prefix.gbuffer"))
        .setMarkerLabel("Opaque G-Buffer")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(gbufferScheduling)
        .setDependencies(&csgIntervalClearTask, 1u)
        .setResourceUses(gbufferResourceUses.data(), gbufferResourceUses.size())
    ;
    m_graphicsPrefixGbufferTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::GbufferGraphTask>(
        gbufferDesc,
        Move(gbufferPayload)
    );
    if(!m_graphicsPrefixGbufferTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque G-buffer task"));
        return false;
    }

    Core::GpuTaskId gbufferCompletionTask = m_graphicsPrefixGbufferTask;
    if(hasOpaqueCsgFrameWork){
        Core::Alloc::ScratchArena csgIntervalSampleResourceScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalSampleResourceUses{
            csgIntervalSampleResourceScratch
        };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalSampleMaterialGeometryUses{
            csgIntervalSampleResourceScratch
        };
        const MaterialPassDrawItems* const csgIntervalSampleMaterialGeometryDrawSets[] = { &opaqueDrawItems.csg };
        const bool csgIntervalSampleUsesMaterialGeometry = !opaqueDrawItems.csg.empty();
        csgIntervalSamplePayload.materialGeometryStatesGraphOwned = csgIntervalSampleUsesMaterialGeometry
            && GatherPreparedMaterialGeometryUses(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                csgIntervalSampleMaterialGeometryDrawSets,
                LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
                csgIntervalSampleResourceScratch,
                csgIntervalSampleMaterialGeometryUses
            )
        ;
        if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSamplePayload.materialGeometryStatesGraphOwned)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material geometry states"));
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
        for(const Core::GpuTaskResourceUse& use : csgIntervalSampleMaterialGeometryUses)
            csgIntervalSampleResourceUses.push_back(use);

        Core::GpuTaskSchedulingHint csgIntervalSampleScheduling;
        csgIntervalSampleScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgIntervalSampleScheduling.forceSubmissionBoundary = false;
        csgIntervalSampleScheduling.allowPacketMerge = true;
        csgIntervalSampleScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgIntervalSampleDesc;
        csgIntervalSampleDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_sample"))
            .setMarkerLabel("Opaque CSG Interval Sample")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(csgIntervalSampleScheduling)
            .setDependencies(&m_graphicsPrefixGbufferTask, 1u)
            .setResourceUses(csgIntervalSampleResourceUses.data(), csgIntervalSampleResourceUses.size())
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
    normalizeResourceUses.reserve(8u + shadowTraceGeometryResourceCount);
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
        normalizeResourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
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
    const Core::GpuTaskId prefixTask,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

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
    resourceUses.reserve(40u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    // Shadow visibility samples the bindless depth image, so its declared layout must match the native shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Hybrid transparent shadows multiply onto the opaque result, so this remains a read/write declaration even
    // when the hardware-only path overwrites it.
    resourceUses.push_back(ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
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
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B"
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
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_swShadowEdgeStatsReadback,
            Name("render.shadow_visibility.edge_stats_readback"),
            "Shadow Edge Statistics Readback",
            Core::ResourceStates::CopyDest
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
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = softwareTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&prefixTask, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_deferredShadowVisibilityTask = m_raytracingSystem.declareShadowVisibilityTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        &m_preparedShadowVisibilityReady,
        hardwareShadowSupported,
        timingTicket,
        true
    );
    if(!m_deferredShadowVisibilityTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred shadow-visibility graph task"));
        return false;
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
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming
){
    using namespace __hidden_renderer_task_graph;

    m_deferredSoftwareCausticsTask = {};
    m_deferredCausticIrradianceClearTask = {};
    m_deferredCausticAccumulatorBootstrapClearTask = {};
    m_deferredCausticAccumulatorDecayTask = {};
    m_deferredCausticAccumulatorBootstrapProducerDispatched = false;

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
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(25u + softwareTraceGeometryResourceCount);
    resourceUses.push_back(ReadUse(worldPosition));
    // Software caustics samples the bindless depth image, so its declared layout must match the shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(causticIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
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
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(usize resourceIndex = 0u; resourceIndex < softwareTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = softwareTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

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
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(ComputeQueueRequest())
        .setScheduling(causticsScheduling)
        .setDependencies(&causticsDependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_deferredSoftwareCausticsTask = m_raytracingSystem.declareSoftwareCausticsTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        &m_preparedShadowVisibilityReady,
        timingTicket,
        true,
        graphOwnsAccumulatorBootstrapClear,
        graphOwnsAccumulatorDecay,
        &causticPhotonTiming,
        graphOwnsAccumulatorBootstrapClear
            ? &m_deferredCausticAccumulatorBootstrapProducerDispatched
            : nullptr
    );
    if(!m_deferredSoftwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics graph task"));
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
    const Core::GpuTaskId effectsTask,
    const Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
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
    resourceUses.reserve(28u + traceGeometryResourceCount);
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
    Core::GpuGraphResourceId surfelPool;
    Core::GpuGraphResourceId surfelCellHead;
    Core::GpuGraphResourceId surfelCounter;
    Core::GpuGraphResourceId surfelFreeList;
    Core::GpuGraphResourceId surfelPoolSnapshot;
    Core::GpuGraphResourceId surfelCellHeadSnapshot;
    bool optionalResourcesImported =
        appendOptionalReadBuffer(
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
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants",
            Core::ResourceStates::ConstantBuffer
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
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
            Name("render.surfel_gi.trace_args"),
            "Surfel Trace Arguments",
            Core::ResourceStates::UnorderedAccess
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
    if(optionalResourcesImported && !useHwTrace){
        optionalResourcesImported =
            appendOptionalReadBuffer(
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
        ;
    }
    if(optionalResourcesImported && useHwTrace){
        if(!m_rayTracingState.m_tlas)
            optionalResourcesImported = false;
        else{
            const Core::GpuGraphResourceId tlas = m_deferredLightingTaskGraph.importAccelStruct(
                m_rayTracingState.m_tlas,
                AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
            );
            const Core::GpuGraphResourceId tlasBackingBuffer = importBuffer(
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

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuExternalCompletionId* const surfelGiExternalDependencies =
        surfelCounterReadbackCompletion.valid() ? &surfelCounterReadbackCompletion : nullptr
    ;
    const usize surfelGiExternalDependencyCount = surfelCounterReadbackCompletion.valid() ? 1u : 0u;

    // A fresh persistent field must clear before the snapshot reads it. Once initialized, the two fixed regions
    // become one Transfer-preferred graph task; compiler declarations own CopySource/CopyDest transitions and any
    // Compute/Transfer/Graphics handoff.
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
            const Core::GpuTaskResourceUse initializationResourceUses[] = {
                WriteUse(surfelPool, Core::ResourceStates::CopyDest),
                WriteUse(surfelCellHead, Core::ResourceStates::CopyDest),
                WriteUse(surfelCounter, Core::ResourceStates::CopyDest),
                WriteUse(surfelFreeList, Core::ResourceStates::CopyDest),
            };
            Core::GpuTaskSchedulingHint initializationScheduling;
            initializationScheduling.cost = Core::GpuTaskCostHint::Medium;
            initializationScheduling.forceSubmissionBoundary = true;
            initializationScheduling.allowPacketMerge = false;
            Core::GpuTaskDesc initializationDesc;
            initializationDesc
                .setIdentity(Name("render.surfel_gi.initialize"))
                .setMarkerLabel("Surfel GI Initialize")
                .setQueue(ComputeQueueRequest())
                .setScheduling(initializationScheduling)
                .setDependencies(&surfelGiDependency, 1u)
                .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
                .setResourceUses(initializationResourceUses, LengthOf(initializationResourceUses))
            ;
            m_deferredSurfelGiPreparationTask = m_raytracingSystem.declareSurfelResourceInitializationTask(
                m_deferredLightingTaskGraph,
                initializationDesc,
                true
            );
            if(!m_deferredSurfelGiPreparationTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI initialization task"));
                return false;
            }
            surfelGiDependency = m_deferredSurfelGiPreparationTask;
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
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(ComputeQueueRequest())
        .setScheduling(surfelGiScheduling)
        .setDependencies(&surfelGiDependency, 1u)
        .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_deferredSurfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        timingTicket,
        true
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
    Core::GpuTimingSubmissionTicket& shadowPrepareTimingTicket,
    Core::GpuTimingSubmissionTicket** const graphicsPrefixTimingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
    Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
    Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket,
    Core::GpuTimingSubmissionTicket& surfelGiTimingTicket,
    Core::GpuTimingSubmissionTicket& hardwareCausticsTimingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
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
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_deferredShadowVisibilityTask = {};
    m_deferredSoftwareCausticsTask = {};
    m_deferredCausticIrradianceClearTask = {};
    m_deferredCausticAccumulatorBootstrapClearTask = {};
    m_deferredCausticAccumulatorDecayTask = {};
    m_deferredCausticAccumulatorBootstrapProducerDispatched = false;
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitClearTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
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
    Core::Alloc::ScratchArena traceGeometryScratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> traceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceAttributeResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareTraceGeometryResources{ traceGeometryScratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareBvhBuildStateResources{ traceGeometryScratchArena };
    Vector<Core::Buffer*, Core::Alloc::ScratchArena> softwareBvhBuildStateBuffers{ traceGeometryScratchArena };
    traceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceAttributeResources.reserve(preparedTraceGeometry.size());
    softwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
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
        shadowPrepareTimingTicket
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation packet"));
        return;
    }
    const bool currentBindlessSlotsGraphOwned = m_deferredBindlessSlotsUploadTask.valid();

    if(!declareDeferredGraphicsPrefixTasks(
        deferredTargets,
        m_deferredShadowPrepareTask,
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
        frameTimingTransaction,
        asyncPrefixTiming,
        deferredClearTiming,
        deferredClearTimingState,
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
        m_graphicsPrefixTask,
        shadowVisibilityTimingTicket
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
        softwareCausticsTimingTicket,
        causticPhotonTiming
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
        effectsTask,
        m_deferredSurfelGiCounterReadbackCompletion,
        surfelGiTimingTicket
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
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResourceUses{ hardwareCausticsScratchArena };
        hardwareResourceUses.reserve(20u + hardwareTraceAttributeResources.size());
        hardwareResourceUses.push_back(ReadUse(
            worldPosition,
            Core::ResourceStates::ShaderResource,
            true
        ));
        hardwareResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        hardwareResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        hardwareResourceUses.push_back(ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        hardwareResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource, true));
        hardwareResourceUses.push_back(ReadUse(sceneGeometryDomain));
        hardwareResourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(WriteUse(currentCausticIrradiance, Core::ResourceStates::UnorderedAccess));

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
            hardwareResourceUses.push_back(ReadUse(resource, state));
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
            hardwareResourceUses.push_back(ReadUse(
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
            hardwareResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
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
                hardwareResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
                hardwareResourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
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
                .setQueue(GraphicsQueueRequest())
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
        Core::GpuTaskDesc hardwareDesc;
        hardwareDesc
            .setIdentity(Name("render.hardware_caustics"))
            .setMarkerLabel("Hardware Caustics")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(hardwareCausticsScheduling)
            .setDependencies(&causticsDependency, 1u)
            .setResourceUses(hardwareResourceUses.data(), hardwareResourceUses.size())
        ;
        m_deferredHardwareCausticsTask = m_raytracingSystem.declareHardwareCausticsTask(
            m_deferredLightingTaskGraph,
            hardwareDesc,
            deferredTargets,
            &m_preparedShadowVisibilityReady,
            hardwareCausticsTimingTicket,
            true,
            graphOwnsAccumulatorBootstrapClear,
            graphOwnsAccumulatorDecay,
            &causticPhotonTiming,
            graphOwnsAccumulatorBootstrapClear
                ? &m_deferredCausticAccumulatorBootstrapProducerDispatched
                : nullptr
        );
        if(!m_deferredHardwareCausticsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics graph task"));
            return;
        }
    }

    AvboitPreGraphTask::Payload avboitPrePayload{ m_arena };
    avboitPrePayload.avboitSystem = &m_avboitSystem;
    avboitPrePayload.targets = &deferredTargets;
    avboitPrePayload.csgFrameState = &csgFrameState;
    avboitPrePayload.timingTicket = &avboitPreTimingTicket;
    avboitPrePayload.hasTransparentRenderers = hasTransparentRenderers;

    // Freeze the transparent CSG interval producer before AVBOIT native recording.  Its shared instance/material
    // and CSG buffers are intentionally overwritten by the later occupancy/extinction/accumulation compatibility
    // paths, so this snapshot applies only to the receiver-surface interval work immediately before occupancy.
    Core::GpuTaskId transparentCsgUploadTask = m_graphicsPrefixTask;
    Core::Alloc::ScratchArena transparentCsgMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentCsgMaterialGeometryUses{
        transparentCsgMaterialGeometryScratch
    };
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
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryUses(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                transparentCsgMaterialGeometryDrawSets,
                LengthOf(transparentCsgMaterialGeometryDrawSets),
                transparentCsgMaterialGeometryScratch,
                transparentCsgMaterialGeometryUses
            );
            if(!avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));

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

            Core::GpuTaskDesc transparentCsgInstanceUploadDesc;
            transparentCsgInstanceUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_instances_upload"))
                .setMarkerLabel("Transparent CSG Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
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
            avboitPrePayload.transparentCsgStreamsUploaded = true;
        }
    }

    // Prepared transparent CSG uses the same persistent interval values and peel targets as opaque CSG. Place its
    // frozen rect clear immediately after immutable stream uploads so the graph owns CopyDest -> UAV ordering,
    // then declare the CSG StorageImage working set on the producer task. An unprepared compatibility path
    // continues to call the legacy all-target helper.
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        const Core::GpuTaskResourceUse transparentCsgIntervalClearResourceUses[] = {
            WriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::CopyDest),
            WriteTextureUse(
                csgReceiverEventCount,
                csgReceiverEventCountSubresources,
                Core::ResourceStates::CopyDest
            ),
        };
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        transparentCsgIntervalClearScheduling.forceSubmissionBoundary = false;
        transparentCsgIntervalClearScheduling.allowPacketMerge = true;
        transparentCsgIntervalClearScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc transparentCsgIntervalClearDesc;
        transparentCsgIntervalClearDesc
            .setIdentity(Name("render.avboit.transparent_csg.interval_clear"))
            .setMarkerLabel("Transparent CSG Interval Clear")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(transparentCsgIntervalClearScheduling)
            .setDependencies(&transparentCsgUploadTask, 1u)
            .setResourceUses(
                transparentCsgIntervalClearResourceUses,
                LengthOf(transparentCsgIntervalClearResourceUses)
            )
        ;
        transparentCsgUploadTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::CsgIntervalRectClearGraphTask>(
            transparentCsgIntervalClearDesc,
            ECSRenderDetail::CsgIntervalRectClearGraphTask::Payload{
                .deferredSystem = &m_deferredSystem,
                .targets = &deferredTargets,
                .clearRect = avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(
                    deferredTargets.width,
                    deferredTargets.height
                ),
                .timingTicket = &avboitPreTimingTicket,
            }
        );
        if(!transparentCsgUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval clear"));
            return;
        }
        avboitPrePayload.transparentCsgIntervalTargetsGraphOwned = true;
        avboitPrePayload.transparentCsgIntervalPeelTargetStatesGraphOwned = true;
        avboitPrePayload.transparentCsgReceiverSurfaceImageStatesGraphOwned = true;
        avboitPrePayload.transparentCsgReceiverSpanOutputImageStatesGraphOwned = true;
        avboitPrePayload.transparentCsgRemovedIntervalOutputImageStatesGraphOwned = true;
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
    avboitIntervalResourceUses.reserve(22u);
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
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    for(const Core::GpuTaskResourceUse& use : transparentCsgMaterialGeometryUses)
        avboitIntervalResourceUses.push_back(use);
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
        .setQueue(GraphicsQueueRequest())
        .setScheduling(avboitIntervalScheduling)
        .setDependencies(&transparentCsgUploadTask, 1u)
        .setResourceUses(avboitIntervalResourceUses.data(), avboitIntervalResourceUses.size())
    ;
    const bool avboitIntervalOutputsGraphOwned =
        avboitPrePayload.transparentCsgStreamsUploaded
        && avboitPrePayload.transparentCsgSnapshot.captured
        && avboitPrePayload.transparentCsgRemovedIntervalOutputImageStatesGraphOwned
    ;
    m_deferredAvboitPreTask = m_deferredLightingTaskGraph.addTask<AvboitPreGraphTask>(
        avboitIntervalDesc,
        Move(avboitPrePayload)
    );
    if(!m_deferredAvboitPreTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph task"));
        return;
    }

    AvboitOccupancyGraphTask::Payload avboitOccupancyPayload{ m_arena };
    avboitOccupancyPayload.avboitSystem = &m_avboitSystem;
    avboitOccupancyPayload.targets = &deferredTargets;
    avboitOccupancyPayload.csgFrameState = &csgFrameState;
    avboitOccupancyPayload.timingTicket = &avboitPreTimingTicket;
    avboitOccupancyPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitOccupancyPayload.splitStages = splitAvboitStages;

    Core::GpuTaskId occupancyUploadTask = m_deferredAvboitPreTask;
    bool occupancyCsgStreamsUploaded = false;
    Core::Alloc::ScratchArena occupancyMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancyMaterialGeometryUses{
        occupancyMaterialGeometryScratch
    };
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
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryUses(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                occupancyMaterialGeometryDrawSets,
                LengthOf(occupancyMaterialGeometryDrawSets),
                occupancyMaterialGeometryScratch,
                occupancyMaterialGeometryUses
            );
            if(!avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT occupancy material geometry states"));

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

    // Preserve the native order: phase-local material/CSG uploads first, then the one AVBOIT target clear, then
    // occupancy. The clear's real CopyDest contract is now separate from the raster consumer's declared state.
    Core::GpuTaskId avboitClearTask = occupancyUploadTask;
    if(clearAvboitTargets){
        const Core::GpuTaskResourceUse avboitClearResourceUses[] = {
            WriteTextureUse(avboitLowRaster, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest),
            WriteTextureUse(avboitAccumColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest),
            WriteTextureUse(
                avboitAccumExtinction,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::CopyDest
            ),
            WriteTextureUse(
                avboitTransmittance,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::CopyDest
            ),
            WriteUse(avboitCoverage, Core::ResourceStates::CopyDest),
            WriteUse(avboitDepthWarp, Core::ResourceStates::CopyDest),
            WriteUse(avboitControl, Core::ResourceStates::CopyDest),
            WriteUse(avboitExtinction, Core::ResourceStates::CopyDest),
            WriteUse(avboitExtinctionOverflow, Core::ResourceStates::CopyDest),
        };
        Core::GpuTaskSchedulingHint avboitClearScheduling;
        avboitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        avboitClearScheduling.forceSubmissionBoundary = false;
        avboitClearScheduling.allowPacketMerge = true;
        avboitClearScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitClearDesc;
        avboitClearDesc
            .setIdentity(Name("render.avboit.clear"))
            .setMarkerLabel("AVBOIT Clear")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(avboitClearScheduling)
            .setDependencies(&occupancyUploadTask, 1u)
            .setResourceUses(avboitClearResourceUses, LengthOf(avboitClearResourceUses))
        ;
        avboitClearTask = m_deferredLightingTaskGraph.addTask<AvboitClearGraphTask>(
            avboitClearDesc,
            AvboitClearGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitPreTimingTicket,
            }
        );
        if(!avboitClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT target clear"));
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
    for(const Core::GpuTaskResourceUse& use : occupancyMaterialGeometryUses)
        avboitPreResourceUses.push_back(use);
    avboitPreResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer, true));
    avboitPreResourceUses.push_back(ReadUse(avboitMaterialDomain));
    avboitPreResourceUses.push_back(ReadUse(avboitCsgDomain, Core::ResourceStates::ShaderResource));

    Core::GpuTaskSchedulingHint avboitOccupancyScheduling;
    avboitOccupancyScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitOccupancyScheduling.forceSubmissionBoundary = false;
    avboitOccupancyScheduling.allowPacketMerge = true;
    avboitOccupancyScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc avboitOccupancyDesc;
    avboitOccupancyDesc
        .setIdentity(Name("render.avboit.pre"))
        .setMarkerLabel(splitAvboitStages ? "AVBOIT Pre" : "AVBOIT")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(avboitOccupancyScheduling)
        .setDependencies(&avboitClearTask, 1u)
        .setResourceUses(avboitPreResourceUses.data(), avboitPreResourceUses.size())
    ;
    m_deferredAvboitOccupancyTask = m_deferredLightingTaskGraph.addTask<AvboitOccupancyGraphTask>(
        avboitOccupancyDesc,
        Move(avboitOccupancyPayload)
    );
    if(!m_deferredAvboitOccupancyTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
        return;
    }

    Core::GpuTaskSchedulingHint avboitComputeScheduling;
    avboitComputeScheduling.cost = Core::GpuTaskCostHint::Medium;
    avboitComputeScheduling.forceSubmissionBoundary = true;
    avboitComputeScheduling.allowPacketMerge = false;
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
    }

    if(hasTransparentRenderers){
    // Extinction is a distinct shared-buffer write point. Snapshot and publish it only after occupancy, or after
    // the split Compute depth warp, so neither phase can overwrite the other phase's instance/typed/CSG stream.
    AvboitExtinctionGraphTask::Payload avboitExtinctionPayload{ m_arena };
    avboitExtinctionPayload.avboitSystem = &m_avboitSystem;
    avboitExtinctionPayload.targets = &deferredTargets;
    avboitExtinctionPayload.csgFrameState = &csgFrameState;
    avboitExtinctionPayload.timingTicket = splitAvboitStages
        ? &avboitExtinctionTimingTicket
        : &avboitPreTimingTicket
    ;
    avboitExtinctionPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitExtinctionPayload.splitStages = splitAvboitStages;

    Core::GpuTaskId extinctionUploadTask = splitAvboitStages
        ? m_deferredAvboitDepthWarpTask
        : m_deferredAvboitOccupancyTask
    ;
    bool extinctionStreamsUploaded = false;
    bool extinctionCsgStreamsUploaded = false;
    Core::Alloc::ScratchArena extinctionMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionMaterialGeometryUses{
        extinctionMaterialGeometryScratch
    };
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
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryUses(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                extinctionMaterialGeometryDrawSets,
                LengthOf(extinctionMaterialGeometryDrawSets),
                extinctionMaterialGeometryScratch,
                extinctionMaterialGeometryUses
            );
            if(!avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT extinction material geometry states"));

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
        // This tail owns depth warp, extinction, and integration on the one Graphics AVBOIT packet. The following
        // accumulation task owns its separately frozen raster stream and render-target transition.
        extinctionResourceUses.push_back(ReadUse(albedo));
        extinctionResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource, true));
        extinctionResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true));
        extinctionResourceUses.push_back(ReadUse(depth));
        extinctionResourceUses.push_back(ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
        extinctionResourceUses.push_back(ReadWriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess));
        extinctionResourceUses.push_back(ReadWriteUse(avboitCoverage, Core::ResourceStates::UnorderedAccess));
        extinctionResourceUses.push_back(ReadWriteUse(avboitDepthWarp, Core::ResourceStates::UnorderedAccess));
        extinctionResourceUses.push_back(ReadWriteUse(avboitControl, Core::ResourceStates::UnorderedAccess));
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
    for(const Core::GpuTaskResourceUse& use : extinctionMaterialGeometryUses)
        extinctionResourceUses.push_back(use);
    extinctionResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    extinctionResourceUses.push_back(ReadUse(avboitMaterialDomain));
    extinctionResourceUses.push_back(ReadUse(avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitExtinctionScheduling;
    avboitExtinctionScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitExtinctionScheduling.forceSubmissionBoundary = false;
    avboitExtinctionScheduling.allowPacketMerge = true;
    avboitExtinctionScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("render.avboit.extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(avboitExtinctionScheduling)
        .setDependencies(&extinctionUploadTask, 1u)
        .setResourceUses(extinctionResourceUses.data(), extinctionResourceUses.size())
    ;
    m_deferredAvboitExtinctionTask = m_deferredLightingTaskGraph.addTask<AvboitExtinctionGraphTask>(
        extinctionDesc,
        Move(avboitExtinctionPayload)
    );
    if(!m_deferredAvboitExtinctionTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return;
    }
    if(extinctionStreamsUploaded)
        m_deferredAvboitExtinctionStreamTask = extinctionUploadTask;

    if(splitAvboitStages){
        const Core::GpuTaskResourceUse integrationResourceUses[] = {
            ReadUse(avboitExtinction),
            ReadUse(avboitControl),
            ReadUse(avboitExtinctionOverflow),
            ReadWriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId extinctionDependency[] = { m_deferredAvboitExtinctionTask };
        Core::GpuTaskDesc integrationDesc;
        integrationDesc
            .setIdentity(Name("render.avboit.integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
            .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
            .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
        ;
        m_deferredAvboitIntegrationTask = m_deferredLightingTaskGraph.addTask<AvboitIntegrationGraphTask>(
            integrationDesc,
            AvboitIntegrationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitIntegrationTimingTicket,
            }
        );
        if(!m_deferredAvboitIntegrationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
            return;
        }

    }

    // Accumulation is another independent write point for the shared material/CSG buffers. Freeze and publish its
    // bytes after integration, rather than letting native recording re-gather mutable scene state after extinction.
    AvboitAccumulationGraphTask::Payload avboitAccumulationPayload{ m_arena };
    avboitAccumulationPayload.avboitSystem = &m_avboitSystem;
    avboitAccumulationPayload.targets = &deferredTargets;
    avboitAccumulationPayload.csgFrameState = &csgFrameState;
    avboitAccumulationPayload.timingTicket = splitAvboitStages
        ? &avboitAccumulationTimingTicket
        : &avboitPreTimingTicket
    ;
    avboitAccumulationPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitAccumulationPayload.splitStages = splitAvboitStages;

    Core::GpuTaskId accumulationUploadTask = splitAvboitStages
        ? m_deferredAvboitIntegrationTask
        : m_deferredAvboitExtinctionTask
    ;
    bool accumulationStreamsUploaded = false;
    bool accumulationCsgStreamsUploaded = false;
    Core::Alloc::ScratchArena accumulationMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationMaterialGeometryUses{
        accumulationMaterialGeometryScratch
    };
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
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryUses(
                m_meshSystem,
                m_deferredLightingTaskGraph,
                accumulationMaterialGeometryDrawSets,
                LengthOf(accumulationMaterialGeometryDrawSets),
                accumulationMaterialGeometryScratch,
                accumulationMaterialGeometryUses
            );
            if(!avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT accumulation material geometry states"));

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
    for(const Core::GpuTaskResourceUse& use : accumulationMaterialGeometryUses)
        accumulationResourceUses.push_back(use);
    accumulationResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    accumulationResourceUses.push_back(ReadUse(avboitMaterialDomain));
    accumulationResourceUses.push_back(ReadUse(avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitAccumulationScheduling;
    avboitAccumulationScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitAccumulationScheduling.forceSubmissionBoundary = false;
    avboitAccumulationScheduling.allowPacketMerge = true;
    avboitAccumulationScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("render.avboit.accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(avboitAccumulationScheduling)
        .setDependencies(&accumulationUploadTask, 1u)
        .setResourceUses(accumulationResourceUses.data(), accumulationResourceUses.size())
    ;
    m_deferredAvboitAccumulationTask = m_deferredLightingTaskGraph.addTask<AvboitAccumulationGraphTask>(
        accumulationDesc,
        Move(avboitAccumulationPayload)
    );
    if(!m_deferredAvboitAccumulationTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
        return;
    }
    if(accumulationStreamsUploaded)
        m_deferredAvboitAccumulationStreamTask = accumulationUploadTask;

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

