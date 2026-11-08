// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/kernel/frame_graph_runtime_statistics.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/kernel/renderer_device_diagnostics_label.h>
#include <impl/ecs_render/kernel/renderer_graph_runtime_label.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/frame_graph_nodes.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder){
    if(!m_frameTargets.valid())
        return false;

    const Core::GpuTaskGraph::DeclarationReadView deferredTaskGraphView(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView deferredCompiledPlan(m_deferredLightingCompiledGraph);

    using Handle = Core::Telemetry::FrameGraphNodeHandle;
    namespace Edge = Core::Telemetry::FrameGraphEdgeKind;

    const CsgFrameState& csgFrameState = m_preparedCsgFrameState;
    const b8 hasCsgFrameWork = m_preparedCsgFrameStateValid && !csgFrameState.empty();
    const b8 hasOpaqueCsgFrameWork = hasCsgFrameWork && (csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork);
    const b8 hasTransparentCsgFrameWork = hasCsgFrameWork && (csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork);
    const b8 hasTransparentRenderers = m_materialSystem.hasTransparentRenderers(RendererResourceLookupMode::PreparedOnly);
    const b8 hasAvboitWork = m_avboitSystem.shouldClearTargets(hasTransparentRenderers);

    Core::GpuTaskGraphRuntimeStatistics deferredRuntimeStatistics{};
    if(builder.frameIndex() == m_frameGraphSourceFrameIndex)
        deferredRuntimeStatistics = deferredTaskGraphRuntimeStatistics();
    Core::Telemetry::FrameGraphPassMetadata rendererFrameMetadata;
    rendererFrameMetadata.runtimeStatistics = ECSRenderDetail::BuildFrameGraphRuntimeStatistics(deferredRuntimeStatistics, builder.frameIndex(), m_frameGraphSourceFrameIndex);
    const Core::GpuPhysicalQueueTopology runtimeQueueTopology = deferredCompiledPlan.queueTopology();
    Core::Alloc::ScratchArena& scratchArena = builder.scratchArena();
    Vector< Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics, Core::Alloc::ScratchArena > physicalQueueRuntimeStatistics(scratchArena);
    Vector< Core::GpuTaskGraphPacketSubmissionStatistics, Core::Alloc::ScratchArena > packetSubmissionStatistics(scratchArena);
    if(rendererFrameMetadata.runtimeStatistics.present && (!runtimeQueueTopology.queues || runtimeQueueTopology.queueCount == 0u))
        rendererFrameMetadata.runtimeStatistics = {};
    if(rendererFrameMetadata.runtimeStatistics.present){
        physicalQueueRuntimeStatistics.reserve(runtimeQueueTopology.queueCount);
        for(usize queueIndex = 0u; queueIndex < runtimeQueueTopology.queueCount; ++queueIndex){
            const Core::GpuPhysicalQueueId queue = runtimeQueueTopology.queues[queueIndex].id;
            const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics queueStatistics =
                ECSRenderDetail::BuildFrameGraphPhysicalQueueRuntimeStatistics(
                    deferredCompiledPlan.physicalQueueCompileStatistics(queue),
                    m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics(m_deferredLightingCompiledGraph, deferredCompiledPlan, queue),
                    m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics(deferredCompiledPlan, queue)
                )
            ;
            if(!Core::Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(queueStatistics, rendererFrameMetadata.runtimeStatistics)){
                rendererFrameMetadata.runtimeStatistics = {};
                physicalQueueRuntimeStatistics.clear();
                break;
            }
            physicalQueueRuntimeStatistics.push_back(queueStatistics);
        }
    }
    if(rendererFrameMetadata.runtimeStatistics.present){
        packetSubmissionStatistics.reserve(deferredCompiledPlan.packetCount());
        for(usize packetIndex = 0u; packetIndex < deferredCompiledPlan.packetCount(); ++packetIndex){
            const Core::GpuSubmissionPacketId packet = deferredCompiledPlan.packetIdAt(packetIndex);
            if(!packet.valid()){
                rendererFrameMetadata.runtimeStatistics = {};
                physicalQueueRuntimeStatistics.clear();
                packetSubmissionStatistics.clear();
                break;
            }

            const Core::GpuTaskGraphPacketSubmissionStatistics packetStatistics = m_deferredLightingSubmissionTransaction.packetSubmissionStatistics(deferredCompiledPlan, packet);
            if(packetStatistics.valid())
                packetSubmissionStatistics.push_back(packetStatistics);
        }
        if(rendererFrameMetadata.runtimeStatistics.present && packetSubmissionStatistics.size() != deferredRuntimeStatistics.submission.nativeSubmissionCount){
            rendererFrameMetadata.runtimeStatistics = {};
            physicalQueueRuntimeStatistics.clear();
            packetSubmissionStatistics.clear();
        }
    }
    m_frameGraphRendererLabel.clear();
    if(!RendererGraphRuntimeLabel::append(deferredRuntimeStatistics, runtimeQueueTopology, physicalQueueRuntimeStatistics, deferredCompiledPlan, m_deferredLightingRecordedGraph, m_graphics.getDevice(), m_frameGraphRendererLabel))
        return false;

    RendererDeviceDiagnosticsLabel::append(m_graphics, m_frameGraphRendererLabel);

    const Handle rendererFrame = builder.addPass(Name("ecs_render/frame"), AStringView(m_frameGraphRendererLabel.data(), m_frameGraphRendererLabel.size()), rendererFrameMetadata);
    for(const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& queueStatistics : physicalQueueRuntimeStatistics){
        if(!builder.addPhysicalQueueRuntimeStatistics(rendererFrame, queueStatistics)){
            NWB_ASSERT(false);
            return false;
        }
    }
    for(const Core::GpuTaskGraphPacketSubmissionStatistics& packetStatistics : packetSubmissionStatistics){
        const Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecord telemetryStatistics = ECSRenderDetail::BuildFrameGraphPacketSubmissionStatistics(packetStatistics, rendererFrame.index);
        if(!builder.addPacketSubmissionStatistics(rendererFrame, telemetryStatistics)){
            NWB_ASSERT(false);
            return false;
        }
    }
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
            const Handle transparentCsgIntervals = appendPass(RendererGpuTimingScope::s_TransparentCsgIntervals, RendererGpuPassText::s_TransparentCsgIntervalsLabel);
            builder.addEdge(deferredTargets, transparentCsgIntervals, Edge::Reads);
            builder.addEdge(transparentCsgIntervals, avboitTargets, Edge::Writes);
        }

        const Handle avboitOccupancy = appendPass(RendererGpuTimingScope::s_AvboitOccupancy, "AVBOIT Occupancy");
        builder.addEdge(meshViewBuffer, avboitOccupancy, Edge::Reads);
        builder.addEdge(materialBuffers, avboitOccupancy, Edge::Reads);
        builder.addEdge(avboitOccupancy, avboitTargets, Edge::Writes);

        const Handle avboitDepthWarp = appendPass(RendererGpuTimingScope::s_AvboitDepthWarp, RendererGpuPassText::s_AvboitDepthWarpLabel);
        builder.addEdge(avboitTargets, avboitDepthWarp, Edge::Reads);
        builder.addEdge(avboitDepthWarp, avboitTargets, Edge::Writes);

        const Handle avboitExtinction = appendPass(RendererGpuTimingScope::s_AvboitExtinction, RendererGpuPassText::s_AvboitExtinctionLabel);
        builder.addEdge(avboitTargets, avboitExtinction, Edge::Reads);
        builder.addEdge(avboitExtinction, avboitTargets, Edge::Writes);

        const Handle avboitIntegration = appendPass(RendererGpuTimingScope::s_AvboitIntegration, RendererGpuPassText::s_AvboitIntegrationLabel);
        builder.addEdge(avboitTargets, avboitIntegration, Edge::Reads);
        builder.addEdge(avboitIntegration, avboitTargets, Edge::Writes);

        const Handle avboitAccumulate = appendPass(RendererGpuTimingScope::s_AvboitAccumulate, "AVBOIT Accumulate");
        builder.addEdge(avboitTargets, avboitAccumulate, Edge::Reads);
        builder.addEdge(avboitAccumulate, deferredTargets, Edge::Writes);
    }

    const Handle deferredLighting = appendPass(RendererGpuTimingScope::s_DeferredLighting, RendererGpuPassText::s_DeferredLightingLabel);
    builder.addEdge(deferredTargets, deferredLighting, Edge::Reads);
    builder.addEdge(deferredLighting, deferredTargets, Edge::Writes);

    const Handle deferredComposite = appendPass(RendererGpuTimingScope::s_DeferredComposite, RendererGpuPassText::s_DeferredCompositeLabel);
    builder.addEdge(deferredTargets, deferredComposite, Edge::Reads);

    const Handle deferredPresent = appendPass(RendererGpuTimingScope::s_DeferredPresent, RendererGpuPassText::s_DeferredPresentLabel);
    builder.addEdge(deferredComposite, deferredPresent, Edge::DependsOn);
    builder.addEdge(deferredPresent, backBuffer, Edge::Writes);

    if(m_deferredLightingTaskGraphScheduled){
        if(!m_deferredLightingTaskGraphQueueAssignmentTelemetry.update(deferredTaskGraphView, m_deferredLightingTaskGraphQueueAssignments, deferredCompiledPlan, m_deferredLightingSubmissionTransaction, scratchArena))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: accepted queue-assignment telemetry refresh failed; skipping detailed task graph export"));
        else{
            const Core::GpuTaskGraphTelemetryOptions deferredLightingTelemetryOptions{
                .queueAssignments = &m_deferredLightingTaskGraphQueueAssignments,
                .compiledPlan = &deferredCompiledPlan,
                .queueAssignmentTelemetry = &m_deferredLightingTaskGraphQueueAssignmentTelemetry,
            };
            if(!deferredTaskGraphView.appendFrameGraphTelemetry(m_deferredLightingTaskGraphAnalysis, scratchArena, builder, deferredLightingTelemetryOptions))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred-effects/lighting/composite/present graph telemetry export failed"));
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

