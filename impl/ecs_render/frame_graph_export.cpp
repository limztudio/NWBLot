// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_frame_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u32 AddNode(
    Core::Telemetry::FrameGraphNodeDescs& nodes,
    const Name& name,
    const AStringView label,
    const Core::Telemetry::FrameGraphNodeKind::Enum kind
){
    const u32 index = static_cast<u32>(nodes.size());
    nodes.push_back(Core::Telemetry::FrameGraphNodeDesc{
        .name = name,
        .label = label,
        .kind = kind,
    });
    return index;
}

static void AddEdge(
    Core::Telemetry::FrameGraphEdgeDescs& edges,
    const u32 fromNodeIndex,
    const u32 toNodeIndex,
    const Core::Telemetry::FrameGraphEdgeKind::Enum kind
){
    edges.push_back(Core::Telemetry::FrameGraphEdgeDesc{
        .fromNodeIndex = fromNodeIndex,
        .toNodeIndex = toNodeIndex,
        .kind = kind,
    });
}

[[nodiscard]] static u32 AddPass(Core::Telemetry::FrameGraphNodeDescs& nodes, const Name& name, const AStringView label){
    return AddNode(nodes, name, label, Core::Telemetry::FrameGraphNodeKind::Pass);
}

[[nodiscard]] static u32 AddResource(Core::Telemetry::FrameGraphNodeDescs& nodes, const Name& name, const AStringView label){
    return AddNode(nodes, name, label, Core::Telemetry::FrameGraphNodeKind::Resource);
}

[[nodiscard]] static u32 AddExternal(Core::Telemetry::FrameGraphNodeDescs& nodes, const Name& name, const AStringView label){
    return AddNode(nodes, name, label, Core::Telemetry::FrameGraphNodeKind::External);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::appendFrameGraph(
    Core::Telemetry::FrameGraphNodeDescs& nodes,
    Core::Telemetry::FrameGraphEdgeDescs& edges,
    u32& outEntryNodeIndex,
    u32& outExitNodeIndex
){
    outEntryNodeIndex = 0u;
    outExitNodeIndex = 0u;

    if(!m_deferredState.m_targets.valid())
        return false;

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

    const u32 rendererFrame = __hidden_renderer_frame_graph::AddPass(nodes, Name("ecs_render/frame"), "Renderer Frame");
    const u32 frameSetup = __hidden_renderer_frame_graph::AddPass(nodes, Name("ecs_render/frame_setup"), "Frame Setup");
    const u32 deferredTargets = __hidden_renderer_frame_graph::AddResource(nodes, Name("ecs_render/deferred_targets"), "Deferred Targets");
    const u32 meshViewBuffer = __hidden_renderer_frame_graph::AddResource(nodes, Name("ecs_render/mesh_view_buffer"), "Mesh View Buffer");
    const u32 sceneShadingBuffer = __hidden_renderer_frame_graph::AddResource(nodes, Name("ecs_render/scene_shading_buffer"), "Scene Shading Buffer");
    const u32 materialBuffers = __hidden_renderer_frame_graph::AddResource(nodes, Name("ecs_render/material_draw_buffers"), "Material Draw Buffers");
    const u32 backBuffer = __hidden_renderer_frame_graph::AddExternal(nodes, Name("graphics/backbuffer"), "Back Buffer");

    outEntryNodeIndex = rendererFrame;
    u32 lastPass = rendererFrame;
    auto appendPass = [&](const Name& name, const AStringView label) -> u32{
        const u32 pass = __hidden_renderer_frame_graph::AddPass(nodes, name, label);
        __hidden_renderer_frame_graph::AddEdge(edges, lastPass, pass, Core::Telemetry::FrameGraphEdgeKind::DependsOn);
        lastPass = pass;
        return pass;
    };

    __hidden_renderer_frame_graph::AddEdge(edges, rendererFrame, frameSetup, Core::Telemetry::FrameGraphEdgeKind::DependsOn);
    lastPass = frameSetup;
    __hidden_renderer_frame_graph::AddEdge(edges, frameSetup, meshViewBuffer, Core::Telemetry::FrameGraphEdgeKind::Writes);
    __hidden_renderer_frame_graph::AddEdge(edges, frameSetup, sceneShadingBuffer, Core::Telemetry::FrameGraphEdgeKind::Writes);

    const u32 deferredClear = appendPass(RendererGpuTimingScope::s_DeferredClear, "Deferred Clear");
    __hidden_renderer_frame_graph::AddEdge(edges, deferredClear, deferredTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

    const u32 materialUpload = appendPass(RendererGpuTimingScope::s_MaterialUpload, "Material Upload");
    __hidden_renderer_frame_graph::AddEdge(edges, materialUpload, materialBuffers, Core::Telemetry::FrameGraphEdgeKind::Writes);

    u32 csgIntervalTargets = 0u;
    if(hasOpaqueCsgFrameWork){
        csgIntervalTargets = __hidden_renderer_frame_graph::AddResource(nodes, Name("ecs_render/csg_interval_targets"), "CSG Interval Targets");

        const u32 csgUpload = appendPass(RendererGpuTimingScope::s_CsgUpload, "CSG Upload");
        __hidden_renderer_frame_graph::AddEdge(edges, csgUpload, csgIntervalTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 csgSampleStateUpload = appendPass(RendererGpuTimingScope::s_CsgSampleStateUpload, "CSG Sample State Upload");
        __hidden_renderer_frame_graph::AddEdge(edges, csgSampleStateUpload, csgIntervalTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 csgIntervalPeel = appendPass(RendererGpuTimingScope::s_CsgIntervalPeel, "CSG Interval Peel");
        __hidden_renderer_frame_graph::AddEdge(edges, csgIntervalPeel, csgIntervalTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 csgReceiverSurface = appendPass(RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, "Opaque CSG Receiver Surface");
        __hidden_renderer_frame_graph::AddEdge(edges, meshViewBuffer, csgReceiverSurface, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, csgReceiverSurface, csgIntervalTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 csgReceiverSpanBuild = appendPass(RendererGpuTimingScope::s_CsgReceiverSpanBuild, "CSG Receiver Span Build");
        __hidden_renderer_frame_graph::AddEdge(edges, csgIntervalTargets, csgReceiverSpanBuild, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, csgReceiverSpanBuild, csgIntervalTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 csgIntervalCombine = appendPass(RendererGpuTimingScope::s_CsgIntervalCombine, "CSG Interval Combine");
        __hidden_renderer_frame_graph::AddEdge(edges, csgIntervalTargets, csgIntervalCombine, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, csgIntervalCombine, csgIntervalTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);
    }

    const u32 opaqueRegular = appendPass(RendererGpuTimingScope::s_OpaqueRegular, "Opaque Regular");
    __hidden_renderer_frame_graph::AddEdge(edges, meshViewBuffer, opaqueRegular, Core::Telemetry::FrameGraphEdgeKind::Reads);
    __hidden_renderer_frame_graph::AddEdge(edges, sceneShadingBuffer, opaqueRegular, Core::Telemetry::FrameGraphEdgeKind::Reads);
    __hidden_renderer_frame_graph::AddEdge(edges, materialBuffers, opaqueRegular, Core::Telemetry::FrameGraphEdgeKind::Reads);
    __hidden_renderer_frame_graph::AddEdge(edges, opaqueRegular, deferredTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

    if(hasOpaqueCsgFrameWork){
        const u32 opaqueCsg = appendPass(RendererGpuTimingScope::s_OpaqueCsg, "Opaque CSG");
        __hidden_renderer_frame_graph::AddEdge(edges, meshViewBuffer, opaqueCsg, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, materialBuffers, opaqueCsg, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, csgIntervalTargets, opaqueCsg, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, opaqueCsg, deferredTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 csgCapFill = appendPass(RendererGpuTimingScope::s_CsgCapFill, "CSG Cap Fill");
        __hidden_renderer_frame_graph::AddEdge(edges, csgIntervalTargets, csgCapFill, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, csgCapFill, deferredTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);
    }

    const u32 deferredLighting = appendPass(RendererGpuTimingScope::s_DeferredLighting, "Deferred Lighting");
    __hidden_renderer_frame_graph::AddEdge(edges, deferredTargets, deferredLighting, Core::Telemetry::FrameGraphEdgeKind::Reads);
    __hidden_renderer_frame_graph::AddEdge(edges, deferredLighting, deferredTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

    u32 avboitTargets = 0u;
    if(hasAvboitWork){
        avboitTargets = __hidden_renderer_frame_graph::AddResource(nodes, Name("ecs_render/avboit_targets"), "AVBOIT Targets");

        const u32 avboitClear = appendPass(RendererGpuTimingScope::s_AvboitClear, "AVBOIT Clear");
        __hidden_renderer_frame_graph::AddEdge(edges, avboitClear, avboitTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);
    }
    if(hasTransparentRenderers){
        if(hasTransparentCsgFrameWork){
            const u32 transparentCsgIntervals = appendPass(RendererGpuTimingScope::s_TransparentCsgIntervals, "Transparent CSG Intervals");
            __hidden_renderer_frame_graph::AddEdge(edges, deferredTargets, transparentCsgIntervals, Core::Telemetry::FrameGraphEdgeKind::Reads);
            __hidden_renderer_frame_graph::AddEdge(edges, transparentCsgIntervals, avboitTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);
        }

        const u32 avboitOccupancy = appendPass(RendererGpuTimingScope::s_AvboitOccupancy, "AVBOIT Occupancy");
        __hidden_renderer_frame_graph::AddEdge(edges, meshViewBuffer, avboitOccupancy, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, materialBuffers, avboitOccupancy, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, avboitOccupancy, avboitTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 avboitDepthWarp = appendPass(RendererGpuTimingScope::s_AvboitDepthWarp, "AVBOIT Depth Warp");
        __hidden_renderer_frame_graph::AddEdge(edges, avboitTargets, avboitDepthWarp, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, avboitDepthWarp, avboitTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 avboitExtinction = appendPass(RendererGpuTimingScope::s_AvboitExtinction, "AVBOIT Extinction");
        __hidden_renderer_frame_graph::AddEdge(edges, avboitTargets, avboitExtinction, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, avboitExtinction, avboitTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 avboitIntegration = appendPass(RendererGpuTimingScope::s_AvboitIntegration, "AVBOIT Integration");
        __hidden_renderer_frame_graph::AddEdge(edges, avboitTargets, avboitIntegration, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, avboitIntegration, avboitTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);

        const u32 avboitAccumulate = appendPass(RendererGpuTimingScope::s_AvboitAccumulate, "AVBOIT Accumulate");
        __hidden_renderer_frame_graph::AddEdge(edges, avboitTargets, avboitAccumulate, Core::Telemetry::FrameGraphEdgeKind::Reads);
        __hidden_renderer_frame_graph::AddEdge(edges, avboitAccumulate, deferredTargets, Core::Telemetry::FrameGraphEdgeKind::Writes);
    }

    const u32 deferredComposite = appendPass(RendererGpuTimingScope::s_DeferredComposite, "Deferred Composite");
    __hidden_renderer_frame_graph::AddEdge(edges, deferredTargets, deferredComposite, Core::Telemetry::FrameGraphEdgeKind::Reads);
    __hidden_renderer_frame_graph::AddEdge(edges, deferredComposite, backBuffer, Core::Telemetry::FrameGraphEdgeKind::Writes);

    outExitNodeIndex = deferredComposite;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

