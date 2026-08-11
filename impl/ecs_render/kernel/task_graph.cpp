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
        const bool deferredBindlessResourcesReady = payload.currentBindlessSlotsGraphOwned
            ? payload.targets->bindless.valid()
            : renderer.m_deferredSystem.uploadDeferredBindlessFrameResources(commandList, *payload.targets)
        ;
        // Compatibility callers retain their direct selector update. The graph-owned path publishes Common from
        // its built-in upload and this task's declared ConstantBuffer read owns the transition.
        if(deferredBindlessResourcesReady && !payload.currentBindlessSlotsGraphOwned){
            commandList.setBufferState(
                payload.targets->bindless.slotsBuffer.get(),
                Core::ResourceStates::ConstantBuffer
            );
            commandList.commitBarriers();
        }
        const bool shadowResourcesPrepared = deferredBindlessResourcesReady
            && renderer.m_raytracingSystem.recordPreflightShadowVisibilityResources(
                commandList,
                *payload.targets,
                renderer.m_preparedShadowVisibilityReady,
                payload.causticEmissionTargetsGraphOwned,
                payload.surfelFrameConstantsGraphOwned,
                payload.shadowMaterialContextBatchGraphOwned
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

        // BLAS/SW-BVH record paths leave their inputs in route-dependent states. Export one exact graph-visible
        // boundary state for every retained selected buffer before the following Prefix packet is seeded.
        renderer.m_raytracingSystem.normalizePreparedShadowTraceGeometryBuffers(commandList);
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


// Deferred clear follows fully native setup. Its entry transitions are declaration-driven, so the recording body
// contains only clear commands and timing.
struct DeferredClearGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.renderer || !payload.targets || !payload.timingTicket || !*payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        payload.renderer->m_deferredSystem.clearDeferredTargets(
            commandList,
            *payload.targets
        );
        return true;
    }
};


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
        bool hasOpaqueCsgFrameWork = false;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;

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

        const Core::Rect opaqueCsgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        if(payload.hasOpaqueCsgFrameWork)
            renderer.m_deferredSystem.clearCsgIntervalTargets(commandList, deferredTargets, opaqueCsgClearRect);

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
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
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
                renderer.m_csgSystem.dispatchCsgIntervalPeels(commandList, deferredTargets, csgFrameData);
            const MaterialPassDrawContext opaqueDrawContext{
                commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState
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
                csgIntervalViewportState
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
                renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                renderer.m_csgSystem.dispatchCsgIntervalCombine(commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgDrawResourcesReady){
                if(!opaqueDrawItems.csg.empty()){
                    Core::GpuTimingMeasure timing(
                        renderer.m_graphics.gpuTiming(),
                        RendererGpuTimingScope::s_OpaqueCsg,
                        renderer.m_graphics.getDevice(),
                        commandList
                    );
                    renderer.m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
                }
                if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                    renderer.m_csgSystem.renderCsgIntervalCaps(commandList, deferredTargets, csgFrameData);
            }
        }
        commandList.endRenderPass();
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

// The terminal graphics-prefix task normalizes the state exported to the following graph packets.
struct PostGbufferNormalizeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
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
            || !payload.targets
            || !payload.timingTicket
            || !*payload.timingTicket
            || !shadowVisibilityQueue
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
        payload.raytracingSystem->normalizePostGbufferPacketResources(commandList, *payload.targets);
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


// AVBOIT remains explicitly staged because its raster/compute alternation is a real dependency chain.  The graph
// owns those packets now; manual state handoffs only seed native recording until automatic graph barriers arrive.
static void RestoreAvboitGbufferInputs(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setTextureState(
        targets.albedo.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.normal.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.worldPosition.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.depth.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
}


struct AvboitPreGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool hasTransparentRenderers = false;
        ECSRenderDetail::TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool transparentCsgStreamsUploaded = false;

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
                preparedTransparentCsgMaterialTypedByteCount
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
        bool clearTargets = false;
        bool hasTransparentRenderers = false;
        bool splitStages = false;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot occupancySnapshot;
        bool occupancyPhasePrepared = false;
        bool occupancyStreamsUploaded = false;

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
        if(payload.clearTargets)
            payload.avboitSystem->clearAvboitTargets(commandList, payload.targets->avboit);
        if(payload.hasTransparentRenderers){
            payload.avboitSystem->renderAvboitOccupancyPass(
                commandList,
                *payload.targets,
                *payload.csgFrameState,
                preparedOccupancyDrawItems,
                preparedOccupancyCsgFrameData,
                preparedOccupancyInstanceCount,
                preparedOccupancyMaterialTypedByteCount
            );
        }
        // The Graphics-only path records the remaining AVBOIT phases in the mergeable extinction tail so its
        // phase-local immutable uploads land after occupancy. The split path retains the historical early restore
        // before the Compute depth warp packet.
        if(payload.splitStages || !payload.hasTransparentRenderers)
            RestoreAvboitGbufferInputs(commandList, *payload.targets);
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
                    preparedExtinctionMaterialTypedByteCount
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
                    preparedExtinctionMaterialTypedByteCount
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
                preparedAccumulationMaterialTypedByteCount
            );
        }
        if(!payload.splitStages)
            RestoreAvboitGbufferInputs(commandList, *payload.targets);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::declareDeferredShadowPrepareTask(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
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
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_raytracingSystem.shadowVisibilityResourcesPreflighted()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
        || (shadowTraceGeometryResourceCount != 0u && !shadowTraceGeometryResources)
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
                // The selector buffer restores Common when its command list closes. Shadow Preparation owns the
                // following Common -> ConstantBuffer transition through its declared read use.
                .finalState = Core::ResourceStates::Common,
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

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(17u + shadowTraceGeometryResourceCount);
    // Shadow Preparation owns each preflight input's post-transition packet boundary. This deliberately supersedes
    // preceding immutable uploads as graph producers, so later Compute readers wait on this first Graphics packet
    // rather than forcing FrontierSafe packetization to split an upload away from its accepting consumer.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as the graph producer for this selector. Its direct compatibility path writes the
    // same bytes natively; on the graph path this WAW handoff retires the immutable upload before later Compute reads.
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
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }

    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    bool resourcesImported =
        appendOptionalWriteBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances",
            Core::ResourceStates::ShaderResource
        )
    ;
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
            resourceUses.push_back(ReadWriteUse(tlas, Core::ResourceStates::AccelStructRead));
            resourceUses.push_back(WriteUse(tlasBacking, Core::ResourceStates::AccelStructRead));
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
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Core::GpuTimingSubmissionTicket** const timingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket
){
    using namespace __hidden_renderer_task_graph;
    using PrefixTimingSlot = ECSRenderDetail::DeferredGraphicsPrefixTimingSlot;

    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
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
        || !timingTickets
        || !asyncPrefixTimingSpansOnePacket
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

    {
        Core::Alloc::ScratchArena clearResourceScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> clearResourceUses{ clearResourceScratch };
        clearResourceUses.reserve(5u);
        clearResourceUses.push_back(WriteTextureUse(
            albedo,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            normal,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            opaqueColor,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        Core::GpuTaskSchedulingHint clearScheduling;
        clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        clearScheduling.forceSubmissionBoundary = false;
        clearScheduling.allowPacketMerge = true;
        clearScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(Name("render.graphics_prefix.deferred_clear"))
            .setMarkerLabel("Deferred Clear")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(clearScheduling)
            .setDependencies(&m_graphicsPrefixSceneShadingSetupTask, 1u)
            .setResourceUses(clearResourceUses.data(), clearResourceUses.size())
        ;
        m_graphicsPrefixDeferredClearTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::DeferredClearGraphTask>(
            clearDesc,
            ECSRenderDetail::DeferredClearGraphTask::Payload{
                .renderer = this,
                .targets = &deferredTargets,
                .timingTicket = timingTicketSlot(PrefixTimingSlot::DeferredClear),
            }
        );
    }
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
    gbufferPayload.hasOpaqueCsgFrameWork = hasOpaqueCsgFrameWork;
    gbufferPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
    gbufferPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;

    const bool hasOpaqueDrawItems = !opaqueDrawItems.empty();
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

    Core::Alloc::ScratchArena gbufferResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferResourceUses{ gbufferResourceScratch };
    gbufferResourceUses.reserve((hasOpaqueDrawItems ? 7u : 5u) + (hasCsgFrameGpuWork ? 5u : 0u));
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
    gbufferResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));
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
        .setDependencies(&csgFrameUploadTask, 1u)
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
        .setDependencies(&m_graphicsPrefixGbufferTask, 1u)
        .setResourceUses(normalizeResourceUses.data(), normalizeResourceUses.size())
    ;
    m_graphicsPrefixTask = m_deferredLightingTaskGraph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{
            .raytracingSystem = &m_raytracingSystem,
            .targets = &deferredTargets,
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
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
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
        timingTicket
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
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredSoftwareCausticsTask = {};

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
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
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
    const Core::GpuTaskId dependencies[] = { m_deferredShadowVisibilityTask };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, LengthOf(dependencies))
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_deferredSoftwareCausticsTask = m_raytracingSystem.declareSoftwareCausticsTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        &m_preparedShadowVisibilityReady,
        timingTicket
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
                initializationDesc
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

    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&surfelGiDependency, 1u)
        .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_deferredSurfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        timingTicket
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
    m_deferredLaggedLightingHistorySlotsUploadTask = {};
    m_deferredShadowPrepareTask = {};
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixTask = {};
    m_deferredShadowVisibilityTask = {};
    m_deferredSoftwareCausticsTask = {};
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitOccupancyTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionStreamTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationStreamTask = {};
    m_deferredAvboitAccumulationTask = {};
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
    traceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceGeometryResources.reserve(preparedTraceGeometry.size());
    hardwareTraceAttributeResources.reserve(preparedTraceGeometry.size());
    softwareTraceGeometryResources.reserve(preparedTraceGeometry.size());

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const auto importCurrentBindlessSlots = [&](const Name& identity, const AStringView label){
        Core::GpuGraphResourceDesc desc = BufferResourceDesc(identity, label);
        desc.setInitialState(
            deferredTargets.bindless.slotsUploaded
                ? Core::ResourceStates::ConstantBuffer
                : deferredTargets.bindless.slotsBuffer->getDescription().initialState
        );
        return m_deferredLightingTaskGraph.importBuffer(deferredTargets.bindless.slotsBuffer, desc);
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

    if(!declareDeferredShadowPrepareTask(
        deferredTargets,
        currentBindlessSlots,
        materialContextSlots,
        traceGeometryResources.data(),
        traceGeometryResources.size(),
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
        currentBindlessSlots,
        materialContextSlots,
        traceGeometryResources.data(),
        traceGeometryResources.size(),
        frameTimingTransaction,
        asyncPrefixTiming,
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
        softwareCausticsTimingTicket
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
        Core::GpuTaskDesc hardwareDesc;
        hardwareDesc
            .setIdentity(Name("render.hardware_caustics"))
            .setMarkerLabel("Hardware Caustics")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(hardwareScheduling)
            .setDependencies(hardwareDependencies, LengthOf(hardwareDependencies))
            .setExternalDependencies(hardwareExternalDependencies, hardwareExternalDependencyCount)
            .setResourceUses(hardwareResourceUses.data(), hardwareResourceUses.size())
        ;
        m_deferredHardwareCausticsTask = m_raytracingSystem.declareHardwareCausticsTask(
            m_deferredLightingTaskGraph,
            hardwareDesc,
            deferredTargets,
            &m_preparedShadowVisibilityReady,
            hardwareCausticsTimingTicket
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

    // The interval producer consumes the first frozen transparent CSG stream. Its graph-visible states must be
    // declared here, before its native work records, rather than on the later occupancy task.
    Core::Alloc::ScratchArena avboitIntervalResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ avboitIntervalResourceScratch };
    avboitIntervalResourceUses.reserve(11u);
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        avboitIntervalResourceUses.push_back(ReadUse(depth));
        avboitIntervalResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
        avboitIntervalResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
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
        .setQueue(GraphicsQueueRequest())
        .setScheduling(avboitIntervalScheduling)
        .setDependencies(&transparentCsgUploadTask, 1u)
        .setResourceUses(avboitIntervalResourceUses.data(), avboitIntervalResourceUses.size())
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
    avboitOccupancyPayload.clearTargets = clearAvboitTargets;
    avboitOccupancyPayload.hasTransparentRenderers = hasTransparentRenderers;
    avboitOccupancyPayload.splitStages = splitAvboitStages;

    Core::GpuTaskId occupancyUploadTask = m_deferredAvboitPreTask;
    bool occupancyCsgStreamsUploaded = false;
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

    Core::Alloc::ScratchArena avboitPreResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitPreResourceUses{ avboitPreResourceScratch };
    avboitPreResourceUses.reserve(20u + (avboitOccupancyPayload.occupancyStreamsUploaded ? 7u : 0u));
    avboitPreResourceUses.push_back(ReadUse(albedo));
    avboitPreResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource, true));
    avboitPreResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true));
    avboitPreResourceUses.push_back(ReadUse(depth));
    avboitPreResourceUses.push_back(ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget));
    // Clear owns the untouched post-occupancy targets here; the extinction tail later declares their actual
    // raster/compute states. Keeping only CopyDest for this clear avoids retaining a false post-phase producer.
    avboitPreResourceUses.push_back(WriteUse(avboitAccumColor, Core::ResourceStates::CopyDest));
    avboitPreResourceUses.push_back(WriteUse(avboitAccumExtinction, Core::ResourceStates::CopyDest));
    avboitPreResourceUses.push_back(WriteUse(avboitTransmittance, Core::ResourceStates::CopyDest));
    avboitPreResourceUses.push_back(ReadWriteUse(avboitCoverage, Core::ResourceStates::UnorderedAccess));
    avboitPreResourceUses.push_back(WriteUse(avboitDepthWarp, Core::ResourceStates::CopyDest));
    avboitPreResourceUses.push_back(WriteUse(avboitControl, Core::ResourceStates::CopyDest));
    avboitPreResourceUses.push_back(WriteUse(avboitExtinction, Core::ResourceStates::CopyDest));
    avboitPreResourceUses.push_back(WriteUse(avboitExtinctionOverflow, Core::ResourceStates::CopyDest));
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
        .setDependencies(&occupancyUploadTask, 1u)
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
    Core::Alloc::ScratchArena extinctionResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionResourceUses{ extinctionResourceScratch };
    extinctionResourceUses.reserve((splitAvboitStages ? 9u : 16u) + (extinctionStreamsUploaded ? 7u : 0u));
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
        }
    }
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

    Core::Alloc::ScratchArena accumulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationResourceUses{ accumulationResourceScratch };
    accumulationResourceUses.reserve((splitAvboitStages ? 9u : 12u) + (accumulationStreamsUploaded ? 7u : 0u));
    if(!splitAvboitStages){
        // Normal Graphics recording restores these native G-buffer inputs after the final accumulation raster pass.
        accumulationResourceUses.push_back(ReadUse(albedo));
        accumulationResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource, true));
        accumulationResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true));
    }
    accumulationResourceUses.push_back(ReadUse(depth));
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
        }
    }
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

    }
    const Core::GpuTaskId avboitFinalTask = hasTransparentRenderers
        ? m_deferredAvboitAccumulationTask
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
    const Core::GpuTaskId laggedLightingDependencies[] = { m_graphicsPrefixTask };
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
            .setDependencies(laggedLightingDependencies, LengthOf(laggedLightingDependencies))
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
        ? LengthOf(laggedLightingWithSelectorDependencies)
        : (useLaggedLightingHistory
            ? LengthOf(laggedLightingDependencies)
            : LengthOf(hardwareLightingDependencies))
    ;
    // Active lagged Lighting receives these shared read-only states directly from the accepted prefix source. This
    // lets it avoid importing the recorded snapshots from Hardware Caustics and AVBOIT Pre while it reads history.
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

