// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <core/graphics/frame_graph_nodes.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_export{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView PhysicalQueueClassLabel(const Core::CommandQueue::Enum queueClass)noexcept{
    switch(queueClass){
    case Core::CommandQueue::Graphics:
        return "Graphics";
    case Core::CommandQueue::Compute:
        return "Compute";
    case Core::CommandQueue::Transfer:
        return "Transfer";
    default:
        return "Unknown";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder){
    if(!m_deferredState.m_targets.valid())
        return false;

    using Handle = Core::Telemetry::FrameGraphNodeHandle;
    namespace Edge = Core::Telemetry::FrameGraphEdgeKind;

    const CsgFrameState& csgFrameState = m_preparedCsgFrameState;
    const bool hasCsgFrameWork = m_preparedCsgFrameStateValid && !csgFrameState.empty();
    const bool hasOpaqueCsgFrameWork =
        hasCsgFrameWork
        && (csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork)
    ;
    const bool hasTransparentCsgFrameWork =
        hasCsgFrameWork
        && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork)
    ;
    const bool hasTransparentRenderers = m_materialSystem.hasTransparentRenderers(RendererResourceLookupMode::PreparedOnly);
    const bool hasAvboitWork = hasTransparentRenderers || m_avboitState.m_targetsNeedClear;

    const Core::GpuTaskGraphRuntimeStatistics deferredRuntimeStatistics = deferredTaskGraphRuntimeStatistics();
    m_frameGraphRendererLabel.clear();
    if(deferredRuntimeStatistics.valid()){
        const Core::GpuTaskGraphCompileStatistics& compileStatistics = deferredRuntimeStatistics.compile;
        const Core::GpuTaskGraphRecordingStatistics& recordingStatistics = deferredRuntimeStatistics.recording;
        const Core::GpuTaskGraphSubmissionStatistics& submissionStatistics = deferredRuntimeStatistics.submission;
        StringAppendFormat(
            m_frameGraphRendererLabel,
            "Renderer Frame\n"
            "Task graph: tasks={} packets={} deps={} transitions={}\n"
            "Declarations: resource sets={} resource-set members={} direct uses={} declared set uses={} expanded set-member uses={} materialized uses={}\n"
            "Data: payload objects={} payload object bytes={} upload blobs={} upload blob bytes={}\n"
            "Recording: packets={} tasks={} command lists={} barriers={} parallel={}\n"
            "Submission: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} submissions={} command lists={} waits={} failed submissions={}\n"
            "CPU: declaration={:.3f} ms compile={:.3f} ms record={:.3f} ms submit={:.3f} ms\n"
            "CPU compile phases: analysis={:.3f} ms queue assignment={:.3f} ms planning={:.3f} ms\n"
            "CPU planning detail: packetization={:.3f} ms resource states={:.3f} ms packet dependencies={:.3f} ms\n"
            "CPU recording phases: command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task recording={:.3f} ms",
            compileStatistics.taskCount,
            compileStatistics.packetCount,
            compileStatistics.packetDependencyCount,
            compileStatistics.transitionBarrierCount,
            compileStatistics.resourceSetCount,
            compileStatistics.resourceSetMemberCount,
            compileStatistics.directResourceUseCount,
            compileStatistics.declaredResourceSetUseCount,
            compileStatistics.expandedResourceSetMemberUseCount,
            compileStatistics.resourceUseCount,
            compileStatistics.payloadObjectCount,
            compileStatistics.payloadObjectBytes,
            compileStatistics.uploadBlobCount,
            compileStatistics.uploadBlobBytes,
            recordingStatistics.packetCount,
            recordingStatistics.taskCount,
            recordingStatistics.commandListCount,
            recordingStatistics.barrierCount,
            recordingStatistics.parallelPacketCount,
            submissionStatistics.acceptedPacketCount,
            submissionStatistics.acceptedTaskCount,
            submissionStatistics.rejectedPacketCount,
            submissionStatistics.rejectedTaskCount,
            submissionStatistics.nativeSubmissionCount,
            submissionStatistics.nativeCommandListCount,
            submissionStatistics.timelineWaitCount,
            submissionStatistics.rejectedSubmissionCount,
            compileStatistics.declarationSeconds * 1000.0,
            compileStatistics.totalSeconds * 1000.0,
            recordingStatistics.recordingSeconds * 1000.0,
            submissionStatistics.submissionSeconds * 1000.0,
            compileStatistics.analysisSeconds * 1000.0,
            compileStatistics.queueAssignmentSeconds * 1000.0,
            compileStatistics.planningSeconds * 1000.0,
            compileStatistics.packetizationSeconds * 1000.0,
            compileStatistics.resourceStatePlanningSeconds * 1000.0,
            compileStatistics.packetDependencyPlanningSeconds * 1000.0,
            recordingStatistics.commandListAcquisitionSeconds * 1000.0,
            recordingStatistics.graphBarrierRecordingSeconds * 1000.0,
            recordingStatistics.taskRecordSeconds * 1000.0
        );

        // This borrows immutable compiled-plan topology and entries plus recorded packet slots under the same renderer-
        // side serialization contract as deferredTaskGraphRuntimeStatistics(). Native recording, including joined
        // ready-frontier workers, and compiled/recorded-graph reset/recompile must not overlap these value snapshots.
        // In particular, do not replace this with the mutable live Device registry while a later recompile can change
        // the packet plan that owns these transaction snapshots.
        const Core::GpuPhysicalQueueTopology queueTopology = m_deferredLightingCompiledGraph.queueTopology();
        for(usize queueIndex = 0u; queueIndex < queueTopology.queueCount; ++queueIndex){
            const Core::GpuPhysicalQueueInfo& queueInfo = queueTopology.queues[queueIndex];
            const Core::GpuTaskGraphPhysicalQueueSubmissionStatistics queueStatistics =
                m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics(
                    m_deferredLightingCompiledGraph,
                    queueInfo.id
                );
            if(
                !queueStatistics.valid()
                || (queueStatistics.acceptedPacketCount == 0u && queueStatistics.rejectedPacketCount == 0u)
            )
                continue;
            const Core::GpuTaskGraphPhysicalQueueCompileStatistics queueCompileStatistics =
                m_deferredLightingCompiledGraph.physicalQueueCompileStatistics(queueInfo.id)
            ;
            if(!queueCompileStatistics.valid())
                continue;
            const Core::GpuTaskGraphPhysicalQueueRecordingStatistics queueRecordingStatistics =
                m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics(
                    m_deferredLightingCompiledGraph,
                    queueInfo.id
                );
            if(!queueRecordingStatistics.valid())
                continue;
            const Core::GpuCommandArenaStatistics commandArenaStatistics =
                m_graphics.getDevice().getCommandArenaStatistics(queueInfo.id)
            ;
            if(!commandArenaStatistics.valid())
                continue;

            StringAppendFormat(
                m_frameGraphRendererLabel,
                "\nPhysical queue index={} generation={} class={} family index={} native queue index={} dedicated={}: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} native submissions={} rejected submit paths={} command lists={} planned waits={} same-queue elisions={} timeline waits={} merged timeline waits={} accepted frontier={} CPU={:.3f} ms"
                "\n  Compile plan: tasks={} packets={} merged tasks={} prologue barriers={} epilogue barriers={} ownership release barriers (subset)={} ownership acquire barriers (subset)={}"
                "\n  Recording: packets={} tasks={} command lists={} barriers={} parallel={} CPU command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task recording={:.3f} ms total={:.3f} ms"
                "\n  Command arena: workers={} epochs={} pending epochs={} command buffers current/high-water={}/{} reusable={} leased={} pending={} growth={} resets={} native handle storage lower bound={} bytes",
                queueStatistics.queue.index,
                queueStatistics.queue.deviceGeneration,
                __hidden_frame_graph_export::PhysicalQueueClassLabel(queueStatistics.queueClass),
                queueInfo.familyIndex,
                queueInfo.queueIndex,
                queueInfo.dedicated,
                queueStatistics.acceptedPacketCount,
                queueStatistics.acceptedTaskCount,
                queueStatistics.rejectedPacketCount,
                queueStatistics.rejectedTaskCount,
                queueStatistics.nativeSubmissionCount,
                queueStatistics.rejectedSubmissionCount,
                queueStatistics.nativeCommandListCount,
                queueStatistics.plannedWaitTokenCount,
                queueStatistics.sameQueueWaitElisionCount,
                queueStatistics.timelineWaitCount,
                queueStatistics.mergedTimelineWaitCount,
                queueStatistics.acceptedFrontierSubmissionCount,
                queueStatistics.submissionSeconds * 1000.0,
                queueCompileStatistics.taskCount,
                queueCompileStatistics.packetCount,
                queueCompileStatistics.mergedTaskCount,
                queueCompileStatistics.prologueBarrierCount,
                queueCompileStatistics.epilogueBarrierCount,
                queueCompileStatistics.ownershipReleaseBarrierCount,
                queueCompileStatistics.ownershipAcquireBarrierCount,
                queueRecordingStatistics.packetCount,
                queueRecordingStatistics.taskCount,
                queueRecordingStatistics.commandListCount,
                queueRecordingStatistics.barrierCount,
                queueRecordingStatistics.parallelPacketCount,
                queueRecordingStatistics.commandListAcquisitionSeconds * 1000.0,
                queueRecordingStatistics.graphBarrierRecordingSeconds * 1000.0,
                queueRecordingStatistics.taskRecordSeconds * 1000.0,
                queueRecordingStatistics.recordingSeconds * 1000.0,
                commandArenaStatistics.workerArenaCount,
                commandArenaStatistics.commandPoolEpochCount,
                commandArenaStatistics.pendingCommandPoolEpochCount,
                commandArenaStatistics.currentCommandBufferCount,
                commandArenaStatistics.highWaterCommandBufferCount,
                commandArenaStatistics.reusableCommandBufferCount,
                commandArenaStatistics.leasedCommandBufferCount,
                commandArenaStatistics.pendingCommandBufferCount,
                commandArenaStatistics.growthEventCount,
                commandArenaStatistics.resetEventCount,
                commandArenaStatistics.nativeHandleStorageLowerBoundBytes
            );
        }
    }
    else
        m_frameGraphRendererLabel += "Renderer Frame";

    const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics =
        m_graphics.getDevice().getDescriptorHeap().lifecycleStatistics()
    ;
    StringAppendFormat(
        m_frameGraphRendererLabel,
        "\nDescriptor heap lifecycle (device-wide current): initialized={} "
        "resource live/capacity={}/{} sampler live/capacity={}/{} "
        "acceleration structure live/capacity={}/{} pending retired slots={} "
        "accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}",
        descriptorHeapLifecycleStatistics.initialized,
        descriptorHeapLifecycleStatistics.resourceLiveSlotCount,
        descriptorHeapLifecycleStatistics.resourceCapacity,
        descriptorHeapLifecycleStatistics.samplerLiveSlotCount,
        descriptorHeapLifecycleStatistics.samplerCapacity,
        descriptorHeapLifecycleStatistics.accelStructLiveSlotCount,
        descriptorHeapLifecycleStatistics.accelStructCapacity,
        descriptorHeapLifecycleStatistics.pendingRetiredSlotCount,
        descriptorHeapLifecycleStatistics.acceptedHeapUseCount,
        descriptorHeapLifecycleStatistics.unsubmittedHeapUseCount,
        descriptorHeapLifecycleStatistics.abandonedHeapUseCount
    );

    const Handle rendererFrame = builder.addPass(
        Name("ecs_render/frame"),
        AStringView(m_frameGraphRendererLabel.data(), m_frameGraphRendererLabel.size())
    );
    const Handle frameSetup = builder.addPass(Name("ecs_render/frame_setup"), "Frame Setup");
    const Handle deferredTargets = builder.addResource(Name("ecs_render/deferred_targets"), "Deferred Targets");
    const Handle meshViewBuffer = builder.addResource(Name("ecs_render/mesh_view_buffer"), "Mesh View Buffer");
    const Handle sceneShadingBuffer = builder.addResource(Name("ecs_render/scene_shading_buffer"), "Scene Shading Buffer");
    const Handle materialBuffers = builder.addResource(Name("ecs_render/material_draw_buffers"), "Material Draw Buffers");
    const Handle backBuffer = builder.addExternal(Core::GraphicsFrameGraphNodes::s_Backbuffer, "Back Buffer");

    Handle lastPass = frameSetup;
    auto appendPass = [&](const Core::GpuTimingScopeDefinition& scope, const AStringView label) -> Handle{
        const Handle pass = builder.addPass(scope.identity, label);
        builder.addEdge(lastPass, pass, Edge::DependsOn);
        lastPass = pass;
        return pass;
    };

    builder.addEdge(rendererFrame, frameSetup, Edge::DependsOn);
    builder.addEdge(frameSetup, meshViewBuffer, Edge::Writes);
    builder.addEdge(frameSetup, sceneShadingBuffer, Edge::Writes);

    const Handle deferredClear = appendPass(RendererGpuTimingScope::s_DeferredClear, "Deferred Clear");
    builder.addEdge(deferredClear, deferredTargets, Edge::Writes);

    const Handle materialUpload = appendPass(RendererGpuTimingScope::s_MaterialUpload, "Material Upload");
    builder.addEdge(materialUpload, materialBuffers, Edge::Writes);

    Handle csgIntervalTargets;
    if(hasOpaqueCsgFrameWork){
        csgIntervalTargets = builder.addResource(Name("ecs_render/csg_interval_targets"), "CSG Interval Targets");

        const Handle csgUpload = appendPass(RendererGpuTimingScope::s_CsgUpload, "CSG Upload");
        builder.addEdge(csgUpload, csgIntervalTargets, Edge::Writes);

        const Handle csgSampleStateUpload = appendPass(RendererGpuTimingScope::s_CsgSampleStateUpload, "CSG Sample State Upload");
        builder.addEdge(csgSampleStateUpload, csgIntervalTargets, Edge::Writes);

        const Handle csgIntervalPeel = appendPass(RendererGpuTimingScope::s_CsgIntervalPeel, "CSG Interval Peel");
        builder.addEdge(csgIntervalPeel, csgIntervalTargets, Edge::Writes);

        const Handle csgReceiverSurface = appendPass(RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, "Opaque CSG Receiver Surface");
        builder.addEdge(meshViewBuffer, csgReceiverSurface, Edge::Reads);
        builder.addEdge(csgReceiverSurface, csgIntervalTargets, Edge::Writes);

        const Handle csgReceiverSpanBuild = appendPass(RendererGpuTimingScope::s_CsgReceiverSpanBuild, "CSG Receiver Span Build");
        builder.addEdge(csgIntervalTargets, csgReceiverSpanBuild, Edge::Reads);
        builder.addEdge(csgReceiverSpanBuild, csgIntervalTargets, Edge::Writes);

        const Handle csgIntervalCombine = appendPass(RendererGpuTimingScope::s_CsgIntervalCombine, "CSG Interval Combine");
        builder.addEdge(csgIntervalTargets, csgIntervalCombine, Edge::Reads);
        builder.addEdge(csgIntervalCombine, csgIntervalTargets, Edge::Writes);
    }

    const Handle opaqueRegular = appendPass(RendererGpuTimingScope::s_OpaqueRegular, "Opaque Regular");
    builder.addEdge(meshViewBuffer, opaqueRegular, Edge::Reads);
    builder.addEdge(sceneShadingBuffer, opaqueRegular, Edge::Reads);
    builder.addEdge(materialBuffers, opaqueRegular, Edge::Reads);
    builder.addEdge(opaqueRegular, deferredTargets, Edge::Writes);

    if(hasOpaqueCsgFrameWork){
        const Handle opaqueCsg = appendPass(RendererGpuTimingScope::s_OpaqueCsg, "Opaque CSG");
        builder.addEdge(meshViewBuffer, opaqueCsg, Edge::Reads);
        builder.addEdge(materialBuffers, opaqueCsg, Edge::Reads);
        builder.addEdge(csgIntervalTargets, opaqueCsg, Edge::Reads);
        builder.addEdge(opaqueCsg, deferredTargets, Edge::Writes);

        const Handle csgCapFill = appendPass(RendererGpuTimingScope::s_CsgCapFill, "CSG Cap Fill");
        builder.addEdge(csgIntervalTargets, csgCapFill, Edge::Reads);
        builder.addEdge(csgCapFill, deferredTargets, Edge::Writes);
    }

    Handle avboitTargets;
    if(hasAvboitWork){
        avboitTargets = builder.addResource(Name("ecs_render/avboit_targets"), "AVBOIT Targets");

        const Handle avboitClear = appendPass(RendererGpuTimingScope::s_AvboitClear, "AVBOIT Clear");
        builder.addEdge(avboitClear, avboitTargets, Edge::Writes);
    }
    if(hasTransparentRenderers){
        if(hasTransparentCsgFrameWork){
            const Handle transparentCsgIntervals = appendPass(RendererGpuTimingScope::s_TransparentCsgIntervals, "Transparent CSG Intervals");
            builder.addEdge(deferredTargets, transparentCsgIntervals, Edge::Reads);
            builder.addEdge(transparentCsgIntervals, avboitTargets, Edge::Writes);
        }

        const Handle avboitOccupancy = appendPass(RendererGpuTimingScope::s_AvboitOccupancy, "AVBOIT Occupancy");
        builder.addEdge(meshViewBuffer, avboitOccupancy, Edge::Reads);
        builder.addEdge(materialBuffers, avboitOccupancy, Edge::Reads);
        builder.addEdge(avboitOccupancy, avboitTargets, Edge::Writes);

        const Handle avboitDepthWarp = appendPass(RendererGpuTimingScope::s_AvboitDepthWarp, "AVBOIT Depth Warp");
        builder.addEdge(avboitTargets, avboitDepthWarp, Edge::Reads);
        builder.addEdge(avboitDepthWarp, avboitTargets, Edge::Writes);

        const Handle avboitExtinction = appendPass(RendererGpuTimingScope::s_AvboitExtinction, "AVBOIT Extinction");
        builder.addEdge(avboitTargets, avboitExtinction, Edge::Reads);
        builder.addEdge(avboitExtinction, avboitTargets, Edge::Writes);

        const Handle avboitIntegration = appendPass(RendererGpuTimingScope::s_AvboitIntegration, "AVBOIT Integration");
        builder.addEdge(avboitTargets, avboitIntegration, Edge::Reads);
        builder.addEdge(avboitIntegration, avboitTargets, Edge::Writes);

        const Handle avboitAccumulate = appendPass(RendererGpuTimingScope::s_AvboitAccumulate, "AVBOIT Accumulate");
        builder.addEdge(avboitTargets, avboitAccumulate, Edge::Reads);
        builder.addEdge(avboitAccumulate, deferredTargets, Edge::Writes);
    }

    const Handle deferredLighting = appendPass(RendererGpuTimingScope::s_DeferredLighting, "Deferred Lighting");
    builder.addEdge(deferredTargets, deferredLighting, Edge::Reads);
    builder.addEdge(deferredLighting, deferredTargets, Edge::Writes);

    const Handle deferredComposite = appendPass(RendererGpuTimingScope::s_DeferredComposite, "Deferred Composite");
    builder.addEdge(deferredTargets, deferredComposite, Edge::Reads);

    const Handle deferredPresent = appendPass(RendererGpuTimingScope::s_DeferredPresent, "Deferred Present");
    builder.addEdge(deferredComposite, deferredPresent, Edge::DependsOn);
    builder.addEdge(deferredPresent, backBuffer, Edge::Writes);

    if(m_deferredLightingTaskGraphValid){
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
        const Core::GpuTaskGraphTelemetryOptions deferredLightingTelemetryOptions{
            .queueAssignments = &m_deferredLightingTaskGraphQueueAssignments,
        };
        if(!m_deferredLightingTaskGraph.appendFrameGraphTelemetry(
            builder,
            m_deferredLightingTaskGraphAnalysis,
            scratchArena,
            deferredLightingTelemetryOptions
        ))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred-effects/lighting/composite/present graph telemetry export failed"));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

