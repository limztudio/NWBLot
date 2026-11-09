// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resource_import_builder.h"
#include "csg_texture_imports.h"
#include "avboit_resource_imports.h"
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <core/graphics/backend_selection/backend.h>
#include <impl/ecs_render/deferred/import_types.adhd.h>
#include "prefix_scene_upload_builder.h"
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <global/simd/memory.h>
#include "opaque_upload_chain_builder.h"
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <global/limit.h>
#include "opaque_gbuffer_material_resources.h"
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <core/common/log.h>
#include "opaque_gbuffer_resource_uses.h"
#include <impl/ecs_render/csg/types.adhd.h>
#include <impl/ecs_render/shared/frame_push_constants.adhd.h>
#include "opaque_compute_emulation_planner.h"
#include "opaque_compute_emulation_producers.h"
#include "opaque_shared_compute_emulation_chain.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredGraphResourceImportResult> DeferredGraphResourceImportBuilder::declare(const DeferredGraphResourceImportInputs& inputs)const{
    DeferredGraphResourceImportResult result{};

    DeferredFrameTargets& deferredTargets = inputs.targets;
    const DeferredLightingGraphResources& deferredLightingResources = inputs.lightingResources;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings = inputs.frameBindings;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = inputs.meshViewSnapshot;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources = inputs.csgResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources = inputs.rayTracingResources;
    const DeferredLaggedLightingHistoryResources* const history = inputs.history;
    const DeferredLaggedLightingHistoryResources* const captureHistory = inputs.captureHistory;
    const b8 clearAvboitTargets = inputs.clearAvboitTargets;
    const b8 capturesLaggedLightingHistory = inputs.capturesLaggedLightingHistory;
    const b8 hasCsgFrameWork = inputs.hasCsgFrameWork;

    // Outputs begin with a graph-owned write; fresh resources start Undefined.
    const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return RendererTaskGraphDetail::ImportExternalTexture(m_graph, texture, identity, label, Core::ResourceStates::Unknown);
    };
    const auto importCurrentBindlessSlots = [&](const Name& identity, const AStringView label){
        return RendererTaskGraphDetail::ImportExternalBuffer(m_graph, deferredTargets.bindless.slotsBuffer, identity, label);
    };

    Expected<Core::GpuGraphResourceId> albedoResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> normalResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> worldPositionResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> specularRoughnessResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> depthResult = MakeUnexpected(Failure{});
    DeferredCsgTextureImports csgTextureImports;
    DeferredAvboitResourceImports avboitImports;
    Expected<Core::GpuGraphResourceId> shadowVisibilityResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> causticIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> surfelIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> currentShadowVisibilityResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> currentCausticIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> currentSurfelIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> opaqueColorResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> sceneShadingResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> lightsResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshViewResult = MakeUnexpected(Failure{});
    Expected<Optional<Core::GpuGraphResourceId>> materialInstancesResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Optional<Core::GpuGraphResourceId>> materialTypedResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Optional<Core::GpuGraphResourceId>> csgReceiverRangesResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Optional<Core::GpuGraphResourceId>> csgCuttersResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Optional<Core::GpuGraphResourceId>> csgClipContextSlotsResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Optional<Core::GpuGraphResourceId>> csgIntervalSampleStateResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Core::GpuGraphResourceId> bindlessSlotsResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> currentBindlessSlotsResult = MakeUnexpected(Failure{});
    Expected<Optional<Core::GpuGraphResourceId>> materialContextSlotsResult = Optional<Core::GpuGraphResourceId>{};
    Expected<Core::GpuGraphResourceId> historyCopyShadowVisibilityResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> historyCopyCausticIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> historyCopySurfelIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> historyCopyDestinationShadowVisibilityResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> historyCopyDestinationCausticIrradianceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> historyCopyDestinationSurfelIrradianceResult = MakeUnexpected(Failure{});

    albedoResult = importFirstWriteTexture(deferredTargets.albedo, Name(s_DeferredImportNameRenderDeferredLightingAlbedo), "G-Buffer Albedo");
    normalResult = importFirstWriteTexture(deferredTargets.normal, Name(s_DeferredImportNameRenderDeferredLightingNormal), "G-Buffer Normal");
    worldPositionResult = importFirstWriteTexture(deferredTargets.worldPosition, Name(s_DeferredImportNameRenderDeferredLightingWorldPosition), "G-Buffer World Position");
    specularRoughnessResult = importFirstWriteTexture(deferredTargets.specularRoughness, Name(s_DeferredImportNameRenderDeferredLightingSpecularRoughness), "G-Buffer Specular Roughness");
    depthResult = importFirstWriteTexture(deferredTargets.depth, Name(s_DeferredImportNameRenderDeferredLightingDepth), "G-Buffer Depth");

    // CSG working set is declared only for frames with CSG work; wider target lifecycle stays in native producers.
    if(hasCsgFrameWork)
        csgTextureImports.capture(m_graph, deferredTargets);
    shadowVisibilityResult = RendererTaskGraphDetail::ImportExternalTexture(
        m_graph, history ? history->shadowVisibility : deferredTargets.shadowVisibility, Name(s_DeferredImportNameRenderDeferredLightingShadowVisibility), history ? "Lagged Shadow Visibility" : RendererGpuPassText::s_ShadowVisibilityLabel
    );
    causticIrradianceResult = RendererTaskGraphDetail::ImportExternalTexture(
        m_graph, history ? history->causticIrradiance : deferredTargets.causticIrradiance, Name(s_DeferredImportNameRenderDeferredLightingCausticIrradiance), history ? "Lagged Caustic Irradiance" : s_CausticIrradianceLabel
    );
    surfelIrradianceResult = RendererTaskGraphDetail::ImportExternalTexture(
        m_graph, history ? history->surfelIrradiance : deferredTargets.surfelIrradiance, Name(s_DeferredImportNameRenderDeferredLightingSurfelIrradiance), history ? "Lagged Surfel Irradiance" : s_SurfelIrradianceLabel
    );
    currentShadowVisibilityResult = !history
        ? shadowVisibilityResult
        : RendererTaskGraphDetail::ImportExternalTexture(m_graph, deferredTargets.shadowVisibility, Name(s_DeferredImportNameRenderDeferredShadowVisibilityCurrentOutput), RendererGpuPassText::s_ShadowVisibilityLabel);
    currentCausticIrradianceResult = !history ? causticIrradianceResult : RendererTaskGraphDetail::ImportExternalTexture(m_graph, deferredTargets.causticIrradiance, Name(s_DeferredImportNameRenderDeferredEffectsCurrentCausticIrradiance), s_CausticIrradianceLabel);
    currentSurfelIrradianceResult = !history ? surfelIrradianceResult : RendererTaskGraphDetail::ImportExternalTexture(m_graph, deferredTargets.surfelIrradiance, Name(s_DeferredImportNameRenderDeferredSurfelGiCurrentIrradiance), s_SurfelIrradianceLabel);
    opaqueColorResult = importFirstWriteTexture(deferredTargets.opaqueColor, Name(s_DeferredImportNameRenderDeferredLightingOpaqueColor), "Opaque Color");
    sceneShadingResult = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, deferredLightingResources.sceneShadingBuffer, Name(s_DeferredImportNameRenderDeferredLightingSceneShading), "Scene Shading");
    lightsResult = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, deferredLightingResources.lightBuffer, Name(s_DeferredImportNameRenderDeferredLightingLights), "Lights");
    meshViewResult = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, meshViewBufferSnapshot.buffer, Name(s_MeshViewName), s_MeshViewLabel);
    if(frameBindings.instanceBuffer){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, frameBindings.instanceBuffer, Name(s_DeferredImportNameRenderDeferredMaterialInstances), "Material Instances");
        if(!importedResource)
            materialInstancesResult = MakeUnexpected(importedResource.error());
        else
            materialInstancesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(frameBindings.materialTypedBuffer){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, frameBindings.materialTypedBuffer, Name(s_DeferredImportNameRenderDeferredMaterialTyped), "Material Typed Data");
        if(!importedResource)
            materialTypedResult = MakeUnexpected(importedResource.error());
        else
            materialTypedResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.receiverRanges){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, csgResources.receiverRanges, Name(s_DeferredImportNameRenderDeferredCsgReceiverRanges), "CSG Receiver Ranges");
        if(!importedResource)
            csgReceiverRangesResult = MakeUnexpected(importedResource.error());
        else
            csgReceiverRangesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.cutters){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, csgResources.cutters, Name(s_DeferredImportNameRenderDeferredCsgCutters), "CSG Cutters");
        if(!importedResource)
            csgCuttersResult = MakeUnexpected(importedResource.error());
        else
            csgCuttersResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.clipContextSlots){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, csgResources.clipContextSlots, Name(s_DeferredImportNameRenderDeferredCsgClipContextSlots), "CSG Clip Context Slots");
        if(!importedResource)
            csgClipContextSlotsResult = MakeUnexpected(importedResource.error());
        else
            csgClipContextSlotsResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.intervalSampleState){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, csgResources.intervalSampleState, Name(s_DeferredImportNameRenderDeferredCsgIntervalSampleState), "CSG Interval Sample State");
        if(!importedResource)
            csgIntervalSampleStateResult = MakeUnexpected(importedResource.error());
        else
            csgIntervalSampleStateResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    bindlessSlotsResult = history
        ? RendererTaskGraphDetail::ImportExternalBuffer(m_graph, history->slotsBuffer, s_LightingBindlessSlotsName, "Lagged Deferred Bindless Slots")
        : importCurrentBindlessSlots(s_LightingBindlessSlotsName, s_DeferredBindlessSlotsLabel);
    currentBindlessSlotsResult = !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get() ? bindlessSlotsResult : importCurrentBindlessSlots(Name(s_DeferredImportNameRenderDeferredCompositeBindlessSlots), s_DeferredBindlessSlotsLabel);
    if(rayTracingGraphResources.materialContextSlotsBuffer){
        const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(m_graph, rayTracingGraphResources.materialContextSlotsBuffer, Name(s_DeferredImportNameRenderDeferredMaterialContextSlots), "Ray Trace Material Context Slots");
        if(!importedResource)
            materialContextSlotsResult = MakeUnexpected(importedResource.error());
        else
            materialContextSlotsResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }

    // History copy declared after Present; reuse active-lighting identities for copy destinations.
    if(capturesLaggedLightingHistory){
        historyCopyShadowVisibilityResult = currentShadowVisibilityResult;
        historyCopyCausticIrradianceResult = currentCausticIrradianceResult;
        historyCopySurfelIrradianceResult = history ? currentSurfelIrradianceResult : surfelIrradianceResult;
        historyCopyDestinationShadowVisibilityResult = history ? shadowVisibilityResult : importFirstWriteTexture(captureHistory->shadowVisibility, Name(s_DeferredImportNameRenderLaggedHistoryCopyHistoryShadowVisibility), "History Shadow Visibility");
        historyCopyDestinationCausticIrradianceResult = history ? causticIrradianceResult : importFirstWriteTexture(captureHistory->causticIrradiance, Name(s_DeferredImportNameRenderLaggedHistoryCopyHistoryCausticIrradiance), "History Caustic Irradiance");
        historyCopyDestinationSurfelIrradianceResult = history ? surfelIrradianceResult : importFirstWriteTexture(captureHistory->surfelIrradiance, Name(s_DeferredImportNameRenderLaggedHistoryCopyHistorySurfelIrradiance), "History Surfel Irradiance");
    }

    // AVBOIT shares deferred G-buffer and imports; compiler owns state seeds through Lighting/Composite.
    avboitImports.capture(m_graph, deferredTargets, clearAvboitTargets);
    if(
        !csgIntervalSampleStateResult
        || !materialInstancesResult
        || !materialTypedResult
        || !csgReceiverRangesResult
        || !csgCuttersResult
        || !csgClipContextSlotsResult
        || !avboitImports.validRefractionInputs()
        || !albedoResult
        || !normalResult
        || !worldPositionResult
        || !specularRoughnessResult
        || !avboitImports.validRefractionSpecular()
        || !depthResult
        || (hasCsgFrameWork && !csgTextureImports.valid())
        || !shadowVisibilityResult
        || !causticIrradianceResult
        || !surfelIrradianceResult
        || !currentShadowVisibilityResult
        || !currentCausticIrradianceResult
        || !currentSurfelIrradianceResult
        || !opaqueColorResult
        || !sceneShadingResult
        || !lightsResult
        || !meshViewResult
        || !bindlessSlotsResult
        || !currentBindlessSlotsResult
        || (rayTracingGraphResources.materialContextSlotsBuffer && !materialContextSlotsResult)
        || (capturesLaggedLightingHistory && (
            !historyCopyShadowVisibilityResult
            || !historyCopyCausticIrradianceResult
            || !historyCopySurfelIrradianceResult
            || !historyCopyDestinationShadowVisibilityResult
            || !historyCopyDestinationCausticIrradianceResult
            || !historyCopyDestinationSurfelIrradianceResult
        ))
        || !avboitImports.validAvboitInputs()
    )
        return MakeUnexpected(Failure{});

    result.albedo = *albedoResult;
    result.normal = *normalResult;
    result.worldPosition = *worldPositionResult;
    result.specularRoughness = *specularRoughnessResult;
    result.depth = *depthResult;
    if(hasCsgFrameWork)
        csgTextureImports.publish(result);
    result.shadowVisibility = *shadowVisibilityResult;
    result.causticIrradiance = *causticIrradianceResult;
    result.surfelIrradiance = *surfelIrradianceResult;
    result.currentShadowVisibility = *currentShadowVisibilityResult;
    result.currentCausticIrradiance = *currentCausticIrradianceResult;
    result.currentSurfelIrradiance = *currentSurfelIrradianceResult;
    result.opaqueColor = *opaqueColorResult;
    result.sceneShading = *sceneShadingResult;
    result.lights = *lightsResult;
    result.meshView = *meshViewResult;
    if(*materialInstancesResult)
        result.materialInstances = **materialInstancesResult;
    if(*materialTypedResult)
        result.materialTyped = **materialTypedResult;
    if(*csgReceiverRangesResult)
        result.csgReceiverRanges = **csgReceiverRangesResult;
    if(*csgCuttersResult)
        result.csgCutters = **csgCuttersResult;
    if(*csgClipContextSlotsResult)
        result.csgClipContextSlots = **csgClipContextSlotsResult;
    if(*csgIntervalSampleStateResult)
        result.csgIntervalSampleState = **csgIntervalSampleStateResult;
    result.bindlessSlots = *bindlessSlotsResult;
    result.currentBindlessSlots = *currentBindlessSlotsResult;
    if(*materialContextSlotsResult)
        result.materialContextSlots = **materialContextSlotsResult;
    if(capturesLaggedLightingHistory){
        result.historyCopyShadowVisibility = *historyCopyShadowVisibilityResult;
        result.historyCopyCausticIrradiance = *historyCopyCausticIrradianceResult;
        result.historyCopySurfelIrradiance = *historyCopySurfelIrradianceResult;
        result.historyCopyDestinationShadowVisibility = *historyCopyDestinationShadowVisibilityResult;
        result.historyCopyDestinationCausticIrradiance = *historyCopyDestinationCausticIrradianceResult;
        result.historyCopyDestinationSurfelIrradiance = *historyCopyDestinationSurfelIrradianceResult;
    }
    avboitImports.publish(result);
    result.csgPeelSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, deferredTargets.csgPeelLayerCount);
    result.csgReceiverEventDataSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, deferredTargets.csgReceiverEventLayerCount);
    result.csgReceiverEventCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    result.csgReceiverSpanDataSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, deferredTargets.csgReceiverSpanLayerCount);
    result.csgReceiverSpanCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    result.csgRemovedIntervalSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, deferredTargets.csgRemovedIntervalLayerCount);
    result.csgRemovedIntervalCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeferredAvboitResourceImports::capture(Core::GpuTaskGraph& graph, const DeferredFrameTargets& deferredTargets, const b8 clearTargets){
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return clearTargets
            ? RendererTaskGraphDetail::ImportExternalTexture(graph, texture, identity, label, Core::ResourceStates::Unknown)
            : RendererTaskGraphDetail::ImportExternalTexture(graph, texture, identity, label)
        ;
    };
    m_avboitLowRaster = importTexture(deferredTargets.avboit.lowRasterTarget, Name(s_DeferredImportNameRenderAvboitLowRaster), "AVBOIT Low Raster");
    m_avboitAccumColor = importTexture(deferredTargets.avboit.accumColor, Name(s_DeferredImportNameRenderAvboitAccumColor), "AVBOIT Accumulated Color");
    m_avboitAccumExtinction = importTexture(deferredTargets.avboit.accumExtinction, Name(s_DeferredImportNameRenderAvboitAccumExtinction), "AVBOIT Accumulated Extinction");
    m_refractionDepth = importTexture(deferredTargets.avboit.refractionDepth, Name(s_DeferredImportNameRenderAvboitRefractionDepth), "AVBOIT refractionDepth");
    m_refractionNormalIor = importTexture(deferredTargets.avboit.refractionNormalIor, Name(s_DeferredImportNameRenderAvboitRefractionNormalIor), "AVBOIT refractionNormalIor");
    m_refractionTintCoverage = importTexture(deferredTargets.avboit.refractionTintCoverage, Name(s_DeferredImportNameRenderAvboitRefractionTintCoverage), "AVBOIT refractionTintCoverage");
    m_refractionInstance = importTexture(deferredTargets.avboit.refractionInstance, Name(s_DeferredImportNameRenderAvboitRefractionInstance), "AVBOIT refractionInstance");
    m_refractionSpecularRoughness = importTexture(deferredTargets.avboit.refractionSpecularRoughness, Name(s_DeferredImportNameRenderAvboitRefractionSpecularRoughness), "AVBOIT Refraction Specular Roughness");
    m_refractionResolve = importTexture(deferredTargets.avboit.refractionResolve, Name(s_DeferredImportNameRenderAvboitRefractionResolve), "AVBOIT refractionResolve");
    m_avboitForegroundColor = importTexture(deferredTargets.avboit.foregroundAccumColor, Name(s_DeferredImportNameRenderAvboitAvboitForegroundColor), "AVBOIT avboitForegroundColor");
    m_avboitForegroundExtinction = importTexture(deferredTargets.avboit.foregroundAccumExtinction, Name(s_DeferredImportNameRenderAvboitAvboitForegroundExtinction), "AVBOIT avboitForegroundExtinction");
    m_avboitTransmittance = importTexture(deferredTargets.avboit.transmittanceTexture, Name(s_DeferredImportNameRenderAvboitTransmittance), "AVBOIT Transmittance");
    m_avboitCoverage = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.coverageBuffer, Name(s_DeferredImportNameRenderAvboitCoverage), "AVBOIT Coverage");
    m_avboitDepthWarp = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.depthWarpBuffer, Name(RendererGpuPassText::s_AvboitDepthWarpName), RendererGpuPassText::s_AvboitDepthWarpLabel);
    m_avboitControl = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.controlBuffer, Name(s_DeferredImportNameRenderAvboitControl), "AVBOIT Control");
    m_avboitExtinction = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.extinctionBuffer, Name(RendererGpuPassText::s_AvboitExtinctionName), RendererGpuPassText::s_AvboitExtinctionLabel);
    m_avboitExtinctionOverflow = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.extinctionOverflowBuffer, Name(s_DeferredImportNameRenderAvboitExtinctionOverflow), "AVBOIT Extinction Overflow");
    m_avboitMaterialDomain = graph.importHazardDomain(RendererTaskGraphDetail::HazardDomainDesc(Name(s_DeferredImportNameRenderAvboitMaterialDomain), "Transparent Materials and Geometry"));
    m_avboitCsgDomain = graph.importHazardDomain(RendererTaskGraphDetail::HazardDomainDesc(Name(s_DeferredImportNameRenderAvboitCsgDomain), RendererGpuPassText::s_TransparentCsgIntervalsLabel));
}

b8 DeferredAvboitResourceImports::validRefractionInputs()const noexcept{
    return
        m_refractionDepth.has_value()
        && m_refractionNormalIor.has_value()
        && m_refractionTintCoverage.has_value()
        && m_refractionInstance.has_value()
        && m_refractionResolve.has_value()
        && m_avboitForegroundColor.has_value()
        && m_avboitForegroundExtinction.has_value()
    ;
}

b8 DeferredAvboitResourceImports::validRefractionSpecular()const noexcept{
    return m_refractionSpecularRoughness.has_value();
}

b8 DeferredAvboitResourceImports::validAvboitInputs()const noexcept{
    return
        m_avboitLowRaster.has_value()
        && m_avboitAccumColor.has_value()
        && m_avboitAccumExtinction.has_value()
        && m_avboitTransmittance.has_value()
        && m_avboitCoverage.has_value()
        && m_avboitDepthWarp.has_value()
        && m_avboitControl.has_value()
        && m_avboitExtinction.has_value()
        && m_avboitExtinctionOverflow.has_value()
        && m_avboitMaterialDomain.has_value()
        && m_avboitCsgDomain.has_value()
    ;
}

void DeferredAvboitResourceImports::publish(DeferredGraphResourceImportResult& result)const noexcept{
    result.avboitLowRaster = *m_avboitLowRaster;
    result.avboitAccumColor = *m_avboitAccumColor;
    result.avboitAccumExtinction = *m_avboitAccumExtinction;
    result.refractionDepth = *m_refractionDepth;
    result.refractionNormalIor = *m_refractionNormalIor;
    result.refractionTintCoverage = *m_refractionTintCoverage;
    result.refractionInstance = *m_refractionInstance;
    result.refractionSpecularRoughness = *m_refractionSpecularRoughness;
    result.refractionResolve = *m_refractionResolve;
    result.avboitForegroundColor = *m_avboitForegroundColor;
    result.avboitForegroundExtinction = *m_avboitForegroundExtinction;
    result.avboitTransmittance = *m_avboitTransmittance;
    result.avboitCoverage = *m_avboitCoverage;
    result.avboitDepthWarp = *m_avboitDepthWarp;
    result.avboitControl = *m_avboitControl;
    result.avboitExtinction = *m_avboitExtinction;
    result.avboitExtinctionOverflow = *m_avboitExtinctionOverflow;
    result.avboitMaterialDomain = *m_avboitMaterialDomain;
    result.avboitCsgDomain = *m_avboitCsgDomain;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeferredCsgTextureImports::capture(Core::GpuTaskGraph& graph, const DeferredFrameTargets& deferredTargets){
    m_csgCapBackNormal = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgCapBackNormal, Name(s_DeferredImportNameRenderDeferredCsgCapBackNormal), "CSG Cap Back Normal");
    m_csgIntervalDepth = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgIntervalDepth, Name(s_DeferredImportNameRenderDeferredCsgIntervalDepth), "CSG Interval Depth");
    m_csgIntervalId = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgIntervalId, Name(s_DeferredImportNameRenderDeferredCsgIntervalId), "CSG Interval ID");
    m_csgReceiverEventData = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverEventData, Name(s_DeferredImportNameRenderDeferredCsgReceiverEventData), "CSG Receiver Event Data");
    m_csgReceiverEventCount = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverEventCount, Name(s_DeferredImportNameRenderDeferredCsgReceiverEventCount), "CSG Receiver Event Count");
    m_csgReceiverSpanData = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverSpanData, Name(s_DeferredImportNameRenderDeferredCsgReceiverSpanData), "CSG Receiver Span Data");
    m_csgReceiverSpanCount = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverSpanCount, Name(s_DeferredImportNameRenderDeferredCsgReceiverSpanCount), "CSG Receiver Span Count");
    m_csgRemovedIntervalDepth = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalDepth, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalDepth), "CSG Removed Interval Depth");
    m_csgRemovedIntervalCapNormal = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalCapNormal, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalCapNormal), "CSG Removed Interval Cap Normal");
    m_csgRemovedIntervalData = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalData, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalData), "CSG Removed Interval Data");
    m_csgRemovedIntervalCount = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalCount, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalCount), "CSG Removed Interval Count");
}

b8 DeferredCsgTextureImports::valid()const noexcept{
    return
        m_csgCapBackNormal.has_value()
        && m_csgIntervalDepth.has_value()
        && m_csgIntervalId.has_value()
        && m_csgReceiverEventData.has_value()
        && m_csgReceiverEventCount.has_value()
        && m_csgReceiverSpanData.has_value()
        && m_csgReceiverSpanCount.has_value()
        && m_csgRemovedIntervalDepth.has_value()
        && m_csgRemovedIntervalCapNormal.has_value()
        && m_csgRemovedIntervalData.has_value()
        && m_csgRemovedIntervalCount.has_value()
    ;
}

void DeferredCsgTextureImports::publish(DeferredGraphResourceImportResult& result)const noexcept{
    result.csgCapBackNormal = *m_csgCapBackNormal;
    result.csgIntervalDepth = *m_csgIntervalDepth;
    result.csgIntervalId = *m_csgIntervalId;
    result.csgReceiverEventData = *m_csgReceiverEventData;
    result.csgReceiverEventCount = *m_csgReceiverEventCount;
    result.csgReceiverSpanData = *m_csgReceiverSpanData;
    result.csgReceiverSpanCount = *m_csgReceiverSpanCount;
    result.csgRemovedIntervalDepth = *m_csgRemovedIntervalDepth;
    result.csgRemovedIntervalCapNormal = *m_csgRemovedIntervalCapNormal;
    result.csgRemovedIntervalData = *m_csgRemovedIntervalData;
    result.csgRemovedIntervalCount = *m_csgRemovedIntervalCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SceneShadingSetupGraphTask::Record(const Payload& payload, [[maybe_unused]] Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept{
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    payload.ready = true;
    return true;
}

void SceneShadingSetupGraphTask::Accepted(Payload& payload, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    payload.deferredSystem.confirmSceneShadingBufferUploads(
        Span<const SceneLightGpuData>(payload.lightData.data(), payload.lightCount),
        payload.lightUploadRequired,
        payload.sceneShadingState,
        payload.sceneShadingUploadRequired
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PrefixSceneUploadResult> PrefixSceneUploadBuilder::declare(const PrefixSceneUploadInputs& inputs){
    PrefixSceneUploadResult result{};
    if(!inputs.meshView.valid() || !inputs.lights.valid() || !inputs.sceneShading.valid() || !inputs.shadowPrepareTask.valid())
        return MakeUnexpected(Failure{});

    const MeshViewGpuData& meshViewState = inputs.meshViewState;
    const b8 meshViewUploadRequired = inputs.meshViewUploadRequired;

    const SceneShadingBufferUploads uploads = m_deferredSystem.prepareSceneShadingBufferUploads(inputs.meshViewAspectRatio, inputs.rayTracingLightingInput);
    const auto& sceneLightData = uploads.lightData;
    const auto& sceneShadingState = uploads.sceneShadingState;
    const u32 sceneLightCount = uploads.lightCount;
    const auto& rayTracingLightingClassification = uploads.lightingClassification;
    const b8 sceneLightUploadRequired = uploads.lightUploadRequired;
    const b8 sceneShadingUploadRequired = uploads.sceneShadingUploadRequired;
    result.sceneLightingContentHash = ComputeSceneLightingContentHash(sceneShadingState, Span<const SceneLightGpuData>(sceneLightData.data(), sceneLightCount));

    Core::GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.cost = Core::GpuTaskCostHint::Medium;
    meshViewSetupScheduling.forceSubmissionBoundary = false;
    meshViewSetupScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixMeshViewSetup)).setMarkerLabel("Mesh View Setup").setScheduling(meshViewSetupScheduling).setDependencies({ &inputs.shadowPrepareTask, 1u });
    const auto meshViewSetupTaskResult = m_graph.addTask<ECSRenderDetail::MeshViewSetupGraphTask>(
        meshViewSetupDesc,
        ECSRenderDetail::MeshViewSetupGraphTask::Payload{
            .graphics = m_graphics,
            .asyncPrefixTiming = inputs.asyncPrefixTiming,
            .timingTicket = inputs.meshViewSetupTimingTicket,
            .asyncPrefixTimingSpansOnePacket = inputs.asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = inputs.shadowVisibilityTask,
        });
    if(!meshViewSetupTaskResult){
        result.meshViewSetupTask = {};
        return MakeUnexpected(Failure{});
    }
    result.meshViewSetupTask = *meshViewSetupTaskResult;

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskId meshViewUploadTask = result.meshViewSetupTask;
    if(meshViewUploadRequired){
        const auto meshViewBlobResult = m_graph.copyUploadData(&meshViewState, sizeof(meshViewState), alignof(MeshViewGpuData));
        Core::GpuTaskDesc meshViewUploadDesc;
        meshViewUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixMeshViewUpload)).setMarkerLabel("Mesh View Upload").setScheduling(immutableUploadScheduling).setDependencies({ &result.meshViewSetupTask, 1u });
        if(!meshViewBlobResult)
            return MakeUnexpected(meshViewBlobResult.error());
        const Core::GpuUploadBlobId meshViewBlob = *meshViewBlobResult;
        const auto meshViewUploadTaskResult = m_graph.addUploadBufferTask(
            meshViewUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = meshViewBlob,
                .destination = inputs.meshView,
                // The backing buffer deliberately restores Common when a native packet closes.
                // Declare that exact graph-visible boundary here; the G-buffer consumer below owns the Common -> ConstantBuffer transition.
                .finalState = Core::ResourceStates::Common,
            });
        if(!meshViewUploadTaskResult){
            meshViewUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        meshViewUploadTask = *meshViewUploadTaskResult;
    }

    Core::GpuTaskSchedulingHint meshViewCommitScheduling = immutableUploadScheduling;
    Core::GpuTaskDesc meshViewCommitDesc;
    meshViewCommitDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixMeshViewUploadCommit)).setMarkerLabel("Mesh View Upload Commit").setScheduling(meshViewCommitScheduling).setDependencies({ &meshViewUploadTask, 1u });
    const auto meshViewCommitTaskResult = m_graph.addTask<ECSRenderDetail::MeshViewUploadCommitGraphTask>(
        meshViewCommitDesc,
        ECSRenderDetail::MeshViewUploadCommitGraphTask::Payload{ .meshSystem = m_meshSystem, .viewState = meshViewState, .uploadRequired = meshViewUploadRequired, .ready = inputs.meshViewSetupReady, });
    if(!meshViewCommitTaskResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId meshViewCommitTask = *meshViewCommitTaskResult;

    Core::GpuTaskId sceneUploadTask = meshViewCommitTask;
    if(sceneLightUploadRequired){
        const usize sceneLightByteCount = static_cast<usize>(sceneLightCount) * sizeof(sceneLightData[0u]);
        const auto sceneLightBlobResult = m_graph.copyUploadData(sceneLightData.data(), sceneLightByteCount, alignof(SceneLightGpuData));
        Core::GpuTaskDesc sceneLightUploadDesc;
        sceneLightUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixSceneLightsUpload)).setMarkerLabel("Scene Lights Upload").setScheduling(immutableUploadScheduling).setDependencies({ &sceneUploadTask, 1u });
        if(!sceneLightBlobResult)
            return MakeUnexpected(sceneLightBlobResult.error());
        const Core::GpuUploadBlobId sceneLightBlob = *sceneLightBlobResult;
        const auto sceneUploadTaskResult = m_graph.addUploadBufferTask(
            sceneLightUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneLightBlob,
                .destination = inputs.lights,
                // These shared frame buffers retain Common between native packets.
                // The first declared reader owns the transition to ShaderResource.
                .finalState = Core::ResourceStates::Common,
            });
        if(!sceneUploadTaskResult){
            sceneUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        sceneUploadTask = *sceneUploadTaskResult;
    }

    if(sceneShadingUploadRequired){
        const auto sceneShadingBlobResult = m_graph.copyUploadData(&sceneShadingState, sizeof(sceneShadingState), alignof(SceneShadingGpuData));
        Core::GpuTaskDesc sceneShadingUploadDesc;
        sceneShadingUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixSceneShadingUpload)).setMarkerLabel("Scene Shading Upload").setScheduling(immutableUploadScheduling).setDependencies({ &sceneUploadTask, 1u });
        if(!sceneShadingBlobResult)
            return MakeUnexpected(sceneShadingBlobResult.error());
        const Core::GpuUploadBlobId sceneShadingBlob = *sceneShadingBlobResult;
        const auto sceneUploadTaskResult2 = m_graph.addUploadBufferTask(
            sceneShadingUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneShadingBlob,
                .destination = inputs.sceneShading,
                // Like the light upload: preserve the automatic Common boundary; the first reader lowers the ConstantBuffer transition.
                .finalState = Core::ResourceStates::Common,
            });
        if(!sceneUploadTaskResult2){
            sceneUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        sceneUploadTask = *sceneUploadTaskResult2;
    }

    Core::GpuTaskSchedulingHint sceneShadingSetupScheduling;
    sceneShadingSetupScheduling.cost = Core::GpuTaskCostHint::Tiny;
    sceneShadingSetupScheduling.forceSubmissionBoundary = false;
    sceneShadingSetupScheduling.allowPacketMerge = true;
    sceneShadingSetupScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc sceneShadingSetupDesc;
    sceneShadingSetupDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixSceneShadingSetup)).setMarkerLabel("Scene Shading Setup").setScheduling(sceneShadingSetupScheduling).setDependencies({ &sceneUploadTask, 1u });
    ECSRenderDetail::SceneShadingSetupGraphTask::Payload sceneShadingSetupPayload{ .deferredSystem = m_deferredSystem, .timingTicket = inputs.sceneShadingSetupTimingTicket, .ready = inputs.sceneShadingSetupReady, .sceneShadingState = sceneShadingState, };
    NWB_SIMD_MEMCPY(sceneShadingSetupPayload.lightData.data(), sizeof(sceneShadingSetupPayload.lightData), sceneLightData.data(), sizeof(sceneLightData));
    sceneShadingSetupPayload.lightCount = sceneLightCount;
    sceneShadingSetupPayload.lightUploadRequired = sceneLightUploadRequired;
    sceneShadingSetupPayload.sceneShadingUploadRequired = sceneShadingUploadRequired;
    const auto sceneShadingSetupTaskResult = m_graph.addTask<ECSRenderDetail::SceneShadingSetupGraphTask>(sceneShadingSetupDesc, Move(sceneShadingSetupPayload));
    if(!sceneShadingSetupTaskResult){
        result.sceneShadingSetupTask = {};
        return MakeUnexpected(Failure{});
    }
    result.sceneShadingSetupTask = *sceneShadingSetupTaskResult;
    result.lightingClassification = rayTracingLightingClassification;
    NWB_SIMD_MEMCPY(result.lightData.data(), sizeof(result.lightData), sceneLightData.data(), sizeof(sceneLightData));
    result.lightCount = sceneLightCount;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpaqueUploadChainResult> OpaqueUploadChainBuilder::declare(const OpaqueUploadChainInputs& inputs){
    OpaqueUploadChainResult result{};
    if(!inputs.dependencyTask.valid())
        return MakeUnexpected(Failure{});

    DeferredFrameTargets& deferredTargets = inputs.targets;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings = inputs.frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources = inputs.csgResources;
    const MaterialPassDrawItemPartitionsView& opaqueDrawItems = inputs.drawItems;
    const Span<InstanceGpuData> instanceData = inputs.instanceData;
    const CsgFrameGpuDataView& csgFrameData = inputs.csgFrameData;
    const Span<const u8> materialTypedBytes = inputs.materialTypedBytes;

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    const b8 hasOpaqueDrawItems = !opaqueDrawItems.empty();
    Core::GpuTaskId materialDrawUploadTask = inputs.dependencyTask;
    if(hasOpaqueDrawItems){
        if(!inputs.materialInstances.valid() || !inputs.materialTyped.valid() || !frameBindings.frameReady(instanceData.size(), materialTypedBytes.size()))
            return MakeUnexpected(Failure{});
        m_materialSystem.prepareMaterialPassInstanceUploadData(csgResources, instanceData);
#if defined(NWB_DEBUG)
        if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData))
            return MakeUnexpected(Failure{});
        NWB_ASSERT(instanceData.size() == inputs.materialTypedRanges.size());
        ValidateMaterialTypedUploadRanges(inputs.materialTypedRanges, materialTypedBytes);
#endif

        const auto instanceBlobResult = m_graph.copyUploadData(instanceData.data(), instanceData.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData));
        const auto materialTypedBlobResult = m_graph.copyUploadData(materialTypedBytes.data(), materialTypedBytes.size(), alignof(u32));
        if(!instanceBlobResult || !materialTypedBlobResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId instanceBlob = *instanceBlobResult;
        const Core::GpuUploadBlobId materialTypedBlob = *materialTypedBlobResult;

        Core::GpuTaskDesc instanceUploadDesc;
        instanceUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixMaterialInstancesUpload)).setMarkerLabel("Material Instances Upload").setScheduling(immutableUploadScheduling).setDependencies({ &materialDrawUploadTask, 1u });
        const auto materialDrawUploadTaskResult = m_graph.addUploadBufferTask(
            instanceUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = instanceBlob,
                .destination = inputs.materialInstances,
                // Both draw buffers use automatic Common restoration when the native packet closes.
                // Keep that graph-visible boundary exact; the G-buffer read below owns the transient SRV transition.
                .finalState = Core::ResourceStates::Common,
            });
        if(!materialDrawUploadTaskResult){
            materialDrawUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        materialDrawUploadTask = *materialDrawUploadTaskResult;

        Core::GpuTaskDesc materialTypedUploadDesc;
        materialTypedUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixMaterialTypedUpload)).setMarkerLabel("Material Typed Upload").setScheduling(immutableUploadScheduling).setDependencies({ &materialDrawUploadTask, 1u });
        const auto materialDrawUploadTaskResult2 = m_graph.addUploadBufferTask(materialTypedUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = materialTypedBlob, .destination = inputs.materialTyped, .finalState = Core::ResourceStates::Common, });
        if(!materialDrawUploadTaskResult2){
            materialDrawUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        materialDrawUploadTask = *materialDrawUploadTaskResult2;
        result.materialDrawBuffersUploaded = true;
    }

    // Freeze every opaque CSG upload byte after preflight fixed the buffer, descriptor, and target generations.
    // Native G-buffer recording consumes these values without rebuilding either CSG uniform payload from live state.
    const b8 hasCsgFrameGpuWork = csgFrameData.hasWork();
    result.hasCsgFrameGpuWork = hasCsgFrameGpuWork;
    Core::GpuTaskId csgFrameUploadTask = materialDrawUploadTask;
    if(hasCsgFrameGpuWork){
        if(!inputs.csgReceiverRanges.valid() || !inputs.csgCutters.valid() || !inputs.csgClipContextSlots.valid() || !inputs.csgIntervalSampleState.valid() || !csgResources.frameReady(csgFrameData))
            return MakeUnexpected(Failure{});
#if defined(NWB_DEBUG)
        if(csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData) || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData))
            return MakeUnexpected(Failure{});
#endif

        const auto csgClipContextSlotData = m_csgSystem.prepareCsgClipContextSlotData(deferredTargets, csgFrameData, csgResources, frameBindings);
        if(!csgClipContextSlotData)
            return MakeUnexpected(Failure{});
        const auto csgIntervalSampleStateData = m_csgSystem.prepareCsgIntervalSampleStateData(deferredTargets, csgFrameData, csgResources, frameBindings);
        if(!csgIntervalSampleStateData)
            return MakeUnexpected(Failure{});

        const auto receiverRangesBlobResult = m_graph.copyUploadData(csgFrameData.receiverRanges.data(), csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData));
        const auto cuttersBlobResult = m_graph.copyUploadData(csgFrameData.cutters.data(), csgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData));
        const auto clipContextSlotsBlobResult = m_graph.copyUploadData(&*csgClipContextSlotData, sizeof(*csgClipContextSlotData), alignof(CsgClipContextSlots));
        const auto intervalSampleStateBlobResult = m_graph.copyUploadData(&*csgIntervalSampleStateData, sizeof(*csgIntervalSampleStateData), alignof(CsgIntervalSampleStateGpuData));
        if(!receiverRangesBlobResult || !cuttersBlobResult || !clipContextSlotsBlobResult || !intervalSampleStateBlobResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId receiverRangesBlob = *receiverRangesBlobResult;
        const Core::GpuUploadBlobId cuttersBlob = *cuttersBlobResult;
        const Core::GpuUploadBlobId clipContextSlotsBlob = *clipContextSlotsBlobResult;
        const Core::GpuUploadBlobId intervalSampleStateBlob = *intervalSampleStateBlobResult;

        Core::GpuTaskDesc receiverRangesUploadDesc;
        receiverRangesUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgReceiverRangesUpload)).setMarkerLabel("CSG Receiver Ranges Upload").setScheduling(immutableUploadScheduling).setDependencies({ &csgFrameUploadTask, 1u });
        const auto csgFrameUploadTaskResult = m_graph.addUploadBufferTask(
            receiverRangesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = receiverRangesBlob,
                .destination = inputs.csgReceiverRanges,
                // CSG buffers restore Common at packet close; G-buffer owns their transient SRV state like the material streams.
                .finalState = Core::ResourceStates::Common,
            });
        if(!csgFrameUploadTaskResult){
            csgFrameUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        csgFrameUploadTask = *csgFrameUploadTaskResult;

        Core::GpuTaskDesc cuttersUploadDesc;
        cuttersUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgCuttersUpload)).setMarkerLabel("CSG Cutters Upload").setScheduling(immutableUploadScheduling).setDependencies({ &csgFrameUploadTask, 1u });
        const auto csgFrameUploadTaskResult2 = m_graph.addUploadBufferTask(cuttersUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = cuttersBlob, .destination = inputs.csgCutters, .finalState = Core::ResourceStates::Common, });
        if(!csgFrameUploadTaskResult2){
            csgFrameUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        csgFrameUploadTask = *csgFrameUploadTaskResult2;

        Core::GpuTaskDesc clipContextSlotsUploadDesc;
        clipContextSlotsUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgClipContextSlotsUpload)).setMarkerLabel("CSG Clip Context Slots Upload").setScheduling(immutableUploadScheduling).setDependencies({ &csgFrameUploadTask, 1u });
        const auto csgFrameUploadTaskResult3 = m_graph.addUploadBufferTask(clipContextSlotsUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = clipContextSlotsBlob, .destination = inputs.csgClipContextSlots, .finalState = Core::ResourceStates::Common, });
        if(!csgFrameUploadTaskResult3){
            csgFrameUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        csgFrameUploadTask = *csgFrameUploadTaskResult3;

        Core::GpuTaskDesc intervalSampleStateUploadDesc;
        intervalSampleStateUploadDesc.setIdentity(Name(s_RenderNameRenderGraphicsPrefixCsgIntervalSampleStateUpload)).setMarkerLabel("CSG Interval Sample State Upload").setScheduling(immutableUploadScheduling).setDependencies({ &csgFrameUploadTask, 1u });
        const auto csgFrameUploadTaskResult4 = m_graph.addUploadBufferTask(
            intervalSampleStateUploadDesc,
            Core::GpuUploadBufferTaskDesc{ .source = intervalSampleStateBlob, .destination = inputs.csgIntervalSampleState, .finalState = Core::ResourceStates::Common, });
        if(!csgFrameUploadTaskResult4){
            csgFrameUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        csgFrameUploadTask = *csgFrameUploadTaskResult4;
        result.csgFrameBuffersUploaded = true;
    }
    result.csgUploadTask = csgFrameUploadTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpaqueGbufferMaterialResources> OpaqueGbufferMaterialResourceCollector::gather(const MaterialPassDrawItemPartitions& drawItems){
    using namespace RendererTaskGraphDetail;

    OpaqueGbufferMaterialResources resources;
    const Array<MaterialPassDrawItemsView, 2> drawSets = { drawItems.regular, drawItems.csgReceiverSurface, };
    const b8 usesMaterialGeometry = !drawItems.regular.empty() || !drawItems.csgReceiverSurface.empty();
    if(usesMaterialGeometry){
        const auto geometrySet = GatherPreparedMaterialGeometryResourceSet(drawSets, Name(s_GbufferMaterialGeometrySet), "Opaque Material Geometry", m_graph, m_scratchArena);
        resources.geometryStatesGraphOwned = geometrySet.has_value();
        if(geometrySet)
            resources.geometrySet = *geometrySet;
    }
    if(usesMaterialGeometry && !resources.geometryStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material geometry states"));
        // Frozen heap slots require a complete graph declaration before this material callback can record.
        return MakeUnexpected(Failure{});
    }
    if(usesMaterialGeometry){
        const auto sampledTextureSet = GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            drawSets,
            Name(s_FrameGraphNameRenderGraphicsPrefixGbufferMaterialSampledTextures),
            "Opaque Material Sampled Textures",
            m_graph,
            m_scratchArena
        );
        resources.sampledTexturesCollected = sampledTextureSet.has_value();
        if(sampledTextureSet)
            resources.sampledTextureSet = *sampledTextureSet;
    }
    if(usesMaterialGeometry && !resources.sampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material sampled textures"));
        return MakeUnexpected(Failure{});
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueGbufferResourceUseBuilder::Append(const OpaqueGbufferResourceUseInputs& inputs, Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& uses){
    using namespace RendererTaskGraphDetail;

    uses.reserve((inputs.hasOpaqueDrawItems ? s_OpaqueDrawResourceUseCount : s_NoOpaqueDrawResourceUseCount) + (inputs.hasCsgFrameGpuWork ? s_OpaqueCsgFrameResourceUseCount : 0u) + (inputs.hasOpaqueCsgFrameWork ? s_OpaqueCsgFrameResourceUseCount : 0u));
    uses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    if(inputs.hasOpaqueDrawItems){
        uses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
        uses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
    }
    if(inputs.hasCsgFrameGpuWork){
        uses.push_back(ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange));
        uses.push_back(ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange));
        uses.push_back(ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        uses.push_back(ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // CSG resolves target-specific images through the deferred bindless-slot buffer selected in its context.
        uses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    if(inputs.hasOpaqueCsgFrameWork){
        // Payloads and events never load priors. Interval ID + event count keep sparse-write validity after clears; only they need ReadWrite.
        // Keeping the payloads write-only also permits their first graph use to begin at a fresh texture's native Unknown state.
        uses.push_back(WriteTextureUse(inputs.resources.csgCapBackNormal, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(WriteTextureUse(inputs.resources.csgIntervalDepth, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteTextureUse(inputs.resources.csgIntervalId, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(WriteTextureUse(inputs.resources.csgReceiverEventData, inputs.csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteTextureUse(inputs.resources.csgReceiverEventCount, inputs.csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess));
    }
    uses.push_back(WriteUse(inputs.resources.albedo, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.normal, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.worldPosition, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.specularRoughness, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.depth, Core::ResourceStates::DepthWrite));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueComputeEmulationPlanner::OpaqueComputeEmulationPlanner(
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystem,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket*& timingTicket,
    const b8& meshViewSetupReady,
    const b8& sceneShadingSetupReady
)noexcept
    : m_regularPayload(arena, materialSystem, targets, timingTicket, meshViewSetupReady, sceneShadingSetupReady)
    , m_csgPayload(arena, materialSystem, targets, timingTicket, meshViewSetupReady, sceneShadingSetupReady)
{}

void OpaqueComputeEmulationPlanner::capture(
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena,
    const MaterialPassDrawItemPartitions& opaqueDrawItems,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const b8 hasOpaqueCsgFrameWork,
    const b8 hasCsgFrameGpuWork,
    const b8 gbufferMaterialSampledTexturesCollected,
    ECSRenderDetail::GbufferGraphTask::Payload& gbufferPayload,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming
){
    using namespace RendererTaskGraphDetail;

    // Generated-vertex buffers persist per mesh, so only the alias-free regular opaque case hoists compute ahead of raster.
    m_regularPayload.frameBindings = frameBindings;
    const b8 opaqueComputeEmulationPlanCaptured = gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && m_regularPayload.plan.capture(opaqueDrawItems.regular, scratchArena)
    ;
    if(opaqueComputeEmulationPlanCaptured){
        const auto opaqueComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            m_regularPayload.plan,
            scratchArena,
            Name(s_FrameGraphNameRenderGraphicsPrefixOpaqueComputeEmulationOutputs),
            "Opaque Compute Emulation Outputs"
        );
        m_regularOutputOwned = opaqueComputeEmulationOutputSetResult.has_value();
        if(opaqueComputeEmulationOutputSetResult)
            m_regularOutputSet = *opaqueComputeEmulationOutputSetResult;
    }
    if(opaqueComputeEmulationPlanCaptured && !m_regularOutputOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque compute-emulation output states"));
    gbufferPayload.regularComputeEmulationOutputStatesGraphOwned = m_regularOutputOwned;

    // A persistent generated-vertex buffer is normally a local dispatch/raster bridge.
    // Handle the small alias classes explicitly: two through five regular opaque compute items sharing one frozen output and heap slot.
    // Opaque CSG remains out of scope so no CSG producer/raster phase can observe this alternation.
    const b8 opaqueSharedComputeEmulationPlanCaptured =
        !hasOpaqueCsgFrameWork
        && !m_regularOutputOwned
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && m_sharedPlan.capture(opaqueDrawItems.regular, s_SharedComputeEmulationMaximumDrawCount)
    ;
    if(opaqueSharedComputeEmulationPlanCaptured){
        const auto opaqueSharedComputeEmulationOutputResult = GatherRegularSharedComputeEmulationResource(graph, m_sharedPlan, "Opaque Shared Compute Emulation Output");
        m_sharedOutputOwned = opaqueSharedComputeEmulationOutputResult.has_value();
        if(opaqueSharedComputeEmulationOutputResult)
            m_sharedOutput = *opaqueSharedComputeEmulationOutputResult;
    }
    if(opaqueSharedComputeEmulationPlanCaptured && !m_sharedOutputOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned shared opaque compute-emulation output state"));
    gbufferPayload.regularSharedComputeEmulationDrawsGraphOwned = m_sharedOutputOwned;
    gbufferPayload.regularSharedComputeEmulationTiming = m_sharedOutputOwned ? &opaqueRegularSharedComputeEmulationTiming : nullptr;

    m_csgPayload.frameBindings = frameBindings;
    const b8 opaqueCsgReceiverComputeEmulationPlanCaptured = hasOpaqueCsgFrameWork
        && hasCsgFrameGpuWork
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && m_csgPayload.plan.capture(opaqueDrawItems.csgReceiverSurface, opaqueDrawItems.regular, csgFrameData, scratchArena)
    ;
    if(opaqueCsgReceiverComputeEmulationPlanCaptured){
        const auto opaqueCsgReceiverComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            m_csgPayload.plan,
            scratchArena,
            Name(s_FrameGraphNameRenderGraphicsPrefixOpaqueCsgReceiverComputeEmulationOutputs),
            "Opaque CSG Receiver Compute Emulation Outputs"
        );
        m_csgOutputOwned = opaqueCsgReceiverComputeEmulationOutputSetResult.has_value();
        if(opaqueCsgReceiverComputeEmulationOutputSetResult)
            m_csgOutputSet = *opaqueCsgReceiverComputeEmulationOutputSetResult;
    }
    if(opaqueCsgReceiverComputeEmulationPlanCaptured && !m_csgOutputOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver compute-emulation output states"));
    gbufferPayload.csgReceiverComputeEmulationOutputStatesGraphOwned = m_csgOutputOwned;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> OpaqueComputeEmulationProducerBuilder::declareRegular(
    const OpaqueComputeEmulationProducerInputs& inputs,
    const Core::GpuTaskResourceSetUse& outputUavSetUse,
    const Core::GpuTaskId& dependency,
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload& payload
){
    using namespace RendererTaskGraphDetail;

    // G-buffer's semantic timing ticket spans its preparatory compute half and its raster half in one packet.
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;

    const Array<Core::GpuTaskResourceUse, 3u> resourceUses = {
        ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer),
        ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange),
        ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange),
    };
    Array<Core::GpuTaskResourceSetUse, s_OpaqueProducerResourceSetUseCapacity> opaqueComputeEmulationResourceSetUses = {};
    usize opaqueComputeEmulationResourceSetUseCount = 0u;
    opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid())
        opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = inputs.materialSampledTextureSetUse;
    opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = outputUavSetUse;
    Core::GpuTaskSchedulingHint opaqueComputeEmulationScheduling;
    opaqueComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
    opaqueComputeEmulationScheduling.forceSubmissionBoundary = false;
    opaqueComputeEmulationScheduling.allowPacketMerge = true;
    opaqueComputeEmulationScheduling.mergeWithPrevious = true;
    // Stays with the Graphics prefix: CSG-clear timing and the G-buffer range need one list under FrontierSafe.
    opaqueComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc opaqueComputeEmulationDesc;
    opaqueComputeEmulationDesc
        .setIdentity(Name(s_RenderNameRenderGraphicsPrefixOpaqueComputeEmulation))
        .setMarkerLabel("Opaque Compute Emulation")
        .setScheduling(opaqueComputeEmulationScheduling)
        .setDependencies({ &dependency, 1u })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ opaqueComputeEmulationResourceSetUses.data(), opaqueComputeEmulationResourceSetUseCount })
    ;
    const auto taskResult = m_graph.addTask< ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask >(opaqueComputeEmulationDesc, Move(payload));
    if(!taskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque compute-emulation producer"));
        return MakeUnexpected(Failure{});
    }
    return *taskResult;
}

Expected<Core::GpuTaskId> OpaqueComputeEmulationProducerBuilder::declareCsgReceiver(
    const OpaqueComputeEmulationProducerInputs& inputs,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const Core::GpuTaskResourceSetUse& outputUavSetUse,
    const Core::GpuTaskId& dependency,
    ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload& payload
){
    using namespace RendererTaskGraphDetail;

    payload.csgResources = csgResources;
    // This producer is early G-buffer submission; dispatch shares its ticket while receiver raster keeps its own scope.
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.csgFrameBuffersUploaded = inputs.csgFrameBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;

    // The compute pipeline shares CSG's clip context. Receiver-event images stay G-buffer raster outputs, absent here.
    const Array<Core::GpuTaskResourceUse, 8u> resourceUses = {
        ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer),
        ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange),
        ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange),
        ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange),
        ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange),
        ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
        ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer),
        ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Array<Core::GpuTaskResourceSetUse, s_OpaqueProducerResourceSetUseCapacity> opaqueCsgReceiverComputeEmulationResourceSetUses = {};
    usize opaqueCsgReceiverComputeEmulationResourceSetUseCount = 0u;
    opaqueCsgReceiverComputeEmulationResourceSetUses[opaqueCsgReceiverComputeEmulationResourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid())
        opaqueCsgReceiverComputeEmulationResourceSetUses[opaqueCsgReceiverComputeEmulationResourceSetUseCount++] = inputs.materialSampledTextureSetUse;
    opaqueCsgReceiverComputeEmulationResourceSetUses[opaqueCsgReceiverComputeEmulationResourceSetUseCount++] = outputUavSetUse;
    Core::GpuTaskSchedulingHint opaqueCsgReceiverComputeEmulationScheduling;
    opaqueCsgReceiverComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
    opaqueCsgReceiverComputeEmulationScheduling.forceSubmissionBoundary = false;
    opaqueCsgReceiverComputeEmulationScheduling.allowPacketMerge = true;
    opaqueCsgReceiverComputeEmulationScheduling.mergeWithPrevious = true;
    // Stays with G-buffer under FrontierSafe: output boundary, timing ticket, and range stay one Graphics packet.
    opaqueCsgReceiverComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc opaqueCsgReceiverComputeEmulationDesc;
    opaqueCsgReceiverComputeEmulationDesc
        .setIdentity(Name(s_RenderNameRenderGraphicsPrefixOpaqueCsgReceiverComputeEmulation))
        .setMarkerLabel("Opaque CSG Receiver Compute Emulation")
        .setScheduling(opaqueCsgReceiverComputeEmulationScheduling)
        .setDependencies({ &dependency, 1u })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ opaqueCsgReceiverComputeEmulationResourceSetUses.data(), opaqueCsgReceiverComputeEmulationResourceSetUseCount })
    ;
    const auto taskResult = m_graph.addTask< ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask >(opaqueCsgReceiverComputeEmulationDesc, Move(payload));
    if(!taskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver compute-emulation producer"));
        return MakeUnexpected(Failure{});
    }
    return *taskResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpaqueSharedComputeEmulationChainResult> OpaqueSharedComputeEmulationChainBuilder::declare(const OpaqueSharedComputeEmulationChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena, const Span<Core::GpuTaskId> tasks){
    using namespace RendererTaskGraphDetail;

    const DeferredGraphResourceImportResult& resources = inputs.resources;
    const Array<Core::GpuTaskResourceUse, 4u> generateResourceUses = {
        ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer),
        ReadBufferUse(resources.materialInstances, inputs.materialInstanceRange),
        ReadBufferUse(resources.materialTyped, inputs.materialTypedRange),
        WriteUse(inputs.sharedOutput, Core::ResourceStates::UnorderedAccess),
    };

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> rasterResourceUses{ scratchArena };
    rasterResourceUses.reserve(inputs.objectGeometryReads.size() + s_OpaqueSharedRasterResourceUseCapacity);
    rasterResourceUses.insert(rasterResourceUses.end(), inputs.objectGeometryReads.begin(), inputs.objectGeometryReads.end());
    rasterResourceUses.push_back(ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer));
    rasterResourceUses.push_back(ReadBufferUse(resources.materialInstances, inputs.materialInstanceRange));
    rasterResourceUses.push_back(ReadBufferUse(resources.materialTyped, inputs.materialTypedRange));
    rasterResourceUses.push_back(ReadUse(inputs.sharedOutput, ECSRenderDetail::s_ObjectGeometryRasterState));
    rasterResourceUses.push_back(ReadWriteUse(resources.albedo, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.normal, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.worldPosition, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.specularRoughness, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.depth, Core::ResourceStates::DepthWrite));

    Array<Core::GpuTaskResourceSetUse, s_OpaqueSharedResourceSetUseCapacity> resourceSetUses = {};
    usize resourceSetUseCount = 0u;
    resourceSetUses[resourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid())
        resourceSetUses[resourceSetUseCount++] = inputs.materialSampledTextureSetUse;

    NWB_DEFINE_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.graphics_prefix.opaque", "Opaque");
    const usize phaseCount = SharedComputeEmulationPhaseCountForDrawCount(inputs.plan.drawCount);
    if(!IsSupportedSharedComputeEmulationPhaseCount(phaseCount) || phaseCount > tasks.size()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid shared opaque compute-emulation phase count"));
        return MakeUnexpected(Failure{});
    }
    Core::GpuTaskId dependency = inputs.gbufferTask;
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUseSpan(resourceSetUses.data(), resourceSetUseCount);
    for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
        const b8 isRasterPhase = phaseIndex % s_SharedComputeEmulationPhasesPerDraw != 0u;
        const auto phaseTask = declarePhase(
            inputs,
            sharedPhaseIdentities[phaseIndex],
            sharedPhaseMarkers[phaseIndex],
            dependency,
            isRasterPhase ? SharedComputeEmulationPhase::Raster : SharedComputeEmulationPhase::Generate,
            phaseIndex / s_SharedComputeEmulationPhasesPerDraw,
            phaseIndex + 1u == phaseCount,
            isRasterPhase ? Span<const Core::GpuTaskResourceUse>(rasterResourceUses) : Span<const Core::GpuTaskResourceUse>(generateResourceUses),
            resourceSetUseSpan
        );
        if(!phaseTask){
            tasks[phaseIndex] = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared opaque compute-emulation phase"));
            return MakeUnexpected(Failure{});
        }
        tasks[phaseIndex] = *phaseTask;
        dependency = *phaseTask;
    }
    return OpaqueSharedComputeEmulationChainResult(dependency, phaseCount);
}


Expected<Core::GpuTaskId> OpaqueSharedComputeEmulationChainBuilder::declarePhase(
    const OpaqueSharedComputeEmulationChainInputs& inputs,
    const Name& identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const SharedComputeEmulationPhase::Enum phase,
    const usize drawIndex,
    const b8 finishTiming,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses
){
    Core::GpuTaskDesc desc;
    RendererTaskGraphDetail::MakeSharedComputeEmulationPhaseTaskDesc(identity, markerLabel, dependency, resourceUses, resourceSetUses, desc);
    ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Payload payload{
        m_materialSystem,
        inputs.targets,
        inputs.timingTicket,
        inputs.meshViewSetupReady,
        inputs.sceneShadingSetupReady,
        inputs.sharedComputeEmulationTiming,
        inputs.frameBindings,
        {},
    };
    payload.plan = inputs.plan;
    payload.drawIndex = drawIndex;
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;
    payload.finishTiming = finishTiming;
    payload.phase = phase;
    return m_graph.addTask<ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask>(desc, Move(payload));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
