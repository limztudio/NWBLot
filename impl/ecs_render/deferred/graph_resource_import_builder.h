// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayTracingDeferredGraphResourceSnapshot;


// Deferred graph resource imports own G-buffer, CSG, lighting-history, and AVBOIT target imports.
struct DeferredGraphResourceImportInputs{
    DeferredFrameTargets* targets = nullptr;
    const DeferredLightingGraphResources* lightingResources = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::MeshViewBufferSnapshot* meshViewSnapshot = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    const RayTracingDeferredGraphResourceSnapshot* rayTracingResources = nullptr;
    const DeferredLaggedLightingHistoryResources* history = nullptr;
    const DeferredLaggedLightingHistoryResources* captureHistory = nullptr;
    bool clearAvboitTargets = false;
    bool capturesLaggedLightingHistory = false;
};

struct DeferredGraphResourceImportResult{
    Core::GpuGraphResourceId albedo;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId specularRoughness;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId csgCapBackNormal;
    Core::GpuGraphResourceId csgIntervalDepth;
    Core::GpuGraphResourceId csgIntervalId;
    Core::GpuGraphResourceId csgReceiverEventData;
    Core::GpuGraphResourceId csgReceiverEventCount;
    Core::GpuGraphResourceId csgReceiverSpanData;
    Core::GpuGraphResourceId csgReceiverSpanCount;
    Core::GpuGraphResourceId csgRemovedIntervalDepth;
    Core::GpuGraphResourceId csgRemovedIntervalCapNormal;
    Core::GpuGraphResourceId csgRemovedIntervalData;
    Core::GpuGraphResourceId csgRemovedIntervalCount;
    Core::GpuGraphResourceId shadowVisibility;
    Core::GpuGraphResourceId causticIrradiance;
    Core::GpuGraphResourceId surfelIrradiance;
    Core::GpuGraphResourceId currentShadowVisibility;
    Core::GpuGraphResourceId currentCausticIrradiance;
    Core::GpuGraphResourceId currentSurfelIrradiance;
    Core::GpuGraphResourceId opaqueColor;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuGraphResourceId bindlessSlots;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId materialContextSlots;
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    Core::GpuGraphResourceId avboitLowRaster;
    Core::GpuGraphResourceId avboitAccumColor;
    Core::GpuGraphResourceId avboitAccumExtinction;
    Core::GpuGraphResourceId refractionDepth;
    Core::GpuGraphResourceId refractionNormalIor;
    Core::GpuGraphResourceId refractionTintCoverage;
    Core::GpuGraphResourceId refractionInstance;
    Core::GpuGraphResourceId refractionSpecularRoughness;
    Core::GpuGraphResourceId refractionResolve;
    Core::GpuGraphResourceId avboitForegroundColor;
    Core::GpuGraphResourceId avboitForegroundExtinction;
    Core::GpuGraphResourceId avboitTransmittance;
    Core::GpuGraphResourceId avboitCoverage;
    Core::GpuGraphResourceId avboitDepthWarp;
    Core::GpuGraphResourceId avboitControl;
    Core::GpuGraphResourceId avboitExtinction;
    Core::GpuGraphResourceId avboitExtinctionOverflow;
    Core::GpuGraphResourceId avboitMaterialDomain;
    Core::GpuGraphResourceId avboitCsgDomain;
    Core::TextureSubresourceSet csgPeelSubresources{};
    Core::TextureSubresourceSet csgReceiverEventDataSubresources{};
    Core::TextureSubresourceSet csgReceiverEventCountSubresources{};
    Core::TextureSubresourceSet csgReceiverSpanDataSubresources{};
    Core::TextureSubresourceSet csgReceiverSpanCountSubresources{};
    Core::TextureSubresourceSet csgRemovedIntervalSubresources{};
    Core::TextureSubresourceSet csgRemovedIntervalCountSubresources{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredGraphResourceImportBuilder final : NoCopy{
public:
    explicit DeferredGraphResourceImportBuilder(Core::GpuTaskGraph& graph);

public:
    [[nodiscard]] bool declare(
        const DeferredGraphResourceImportInputs& inputs,
        DeferredGraphResourceImportResult& outResult
    )const;

private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

