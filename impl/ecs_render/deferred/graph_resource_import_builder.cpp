// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resource_import_builder.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredGraphResourceImportBuilder::DeferredGraphResourceImportBuilder(Core::GpuTaskGraph& graph)noexcept
    : m_graph(graph){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<DeferredGraphResourceImportResult> DeferredGraphResourceImportBuilder::declare(const DeferredGraphResourceImportInputs& inputs)const{
    DeferredGraphResourceImportResult result{};

    DeferredFrameTargets& deferredTargets = inputs.targets;
    const DeferredLightingGraphResources& deferredLightingResources = inputs.lightingResources;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings = inputs.frameBindings;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = inputs.meshViewSnapshot;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources = inputs.csgResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources = inputs.rayTracingResources;
    const DeferredLaggedLightingHistoryResources* const history = inputs.history;
    const DeferredLaggedLightingHistoryResources* const captureHistory = inputs.captureHistory;
    const bool clearAvboitTargets = inputs.clearAvboitTargets;
    const bool capturesLaggedLightingHistory = inputs.capturesLaggedLightingHistory;

    constexpr AStringView s_CausticIrradianceLabel = "Caustic Irradiance";
    constexpr AStringView s_SurfelIrradianceLabel = "Surfel Irradiance";
    constexpr AStringView s_LightingBindlessSlotsName = "render.deferred_lighting.bindless_slots";
    constexpr AStringView s_BindlessSlotsLabel = "Deferred Bindless Slots";

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, label));
    };
    // Outputs begin with a graph-owned write; fresh resources start Undefined.
    const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        Core::GpuGraphResourceDesc desc = RendererTaskGraphDetail::TextureResourceDesc(identity, label);
        desc.setInitialState(Core::ResourceStates::Unknown);
        return m_graph.importTexture(texture, desc);
    };
    // Clears may start Undefined; no-clear frames import retained Common.
    const auto importAvboitTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return clearAvboitTargets ? importFirstWriteTexture(texture, identity, label) : importTexture(texture, identity, label);
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, label));
    };
    const auto importCurrentBindlessSlots = [&](const Name& identity, const AStringView label){
        return m_graph.importBuffer(deferredTargets.bindless.slotsBuffer, RendererTaskGraphDetail::BufferResourceDesc(identity, label));
    };

    Expected<Core::GpuGraphResourceId> albedoResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> normalResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> worldPositionResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> specularRoughnessResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> depthResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgCapBackNormalResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgIntervalDepthResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgIntervalIdResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgReceiverEventDataResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgReceiverEventCountResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgReceiverSpanDataResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgReceiverSpanCountResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgRemovedIntervalDepthResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgRemovedIntervalCapNormalResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgRemovedIntervalDataResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> csgRemovedIntervalCountResult = MakeUnexpected(Failure{});
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
    Expected<Core::GpuGraphResourceId> avboitLowRasterResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitAccumColorResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitAccumExtinctionResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> refractionDepthResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> refractionNormalIorResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> refractionTintCoverageResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> refractionInstanceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> refractionSpecularRoughnessResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> refractionResolveResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitForegroundColorResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitForegroundExtinctionResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitTransmittanceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitCoverageResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitDepthWarpResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitControlResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitExtinctionResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitExtinctionOverflowResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitMaterialDomainResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> avboitCsgDomainResult = MakeUnexpected(Failure{});

    albedoResult = importFirstWriteTexture(deferredTargets.albedo, Name("render.deferred_lighting.albedo"), "G-Buffer Albedo");
    normalResult = importFirstWriteTexture(deferredTargets.normal, Name("render.deferred_lighting.normal"), "G-Buffer Normal");
    worldPositionResult = importFirstWriteTexture(deferredTargets.worldPosition, Name("render.deferred_lighting.world_position"), "G-Buffer World Position");
    specularRoughnessResult = importFirstWriteTexture(deferredTargets.specularRoughness, Name("render.deferred_lighting.specular_roughness"), "G-Buffer Specular Roughness");
    depthResult = importFirstWriteTexture(deferredTargets.depth, Name("render.deferred_lighting.depth"), "G-Buffer Depth");


    // CSG working set declared here; wider target lifecycle stays in native producers.
    csgCapBackNormalResult = importTexture(deferredTargets.csgCapBackNormal, Name("render.deferred.csg_cap_back_normal"), "CSG Cap Back Normal");
    csgIntervalDepthResult = importTexture(deferredTargets.csgIntervalDepth, Name("render.deferred.csg_interval_depth"), "CSG Interval Depth");
    csgIntervalIdResult = importTexture(deferredTargets.csgIntervalId, Name("render.deferred.csg_interval_id"), "CSG Interval ID");
    csgReceiverEventDataResult = importTexture(deferredTargets.csgReceiverEventData, Name("render.deferred.csg_receiver_event_data"), "CSG Receiver Event Data");
    csgReceiverEventCountResult = importTexture(deferredTargets.csgReceiverEventCount, Name("render.deferred.csg_receiver_event_count"), "CSG Receiver Event Count");
    csgReceiverSpanDataResult = importTexture(deferredTargets.csgReceiverSpanData, Name("render.deferred.csg_receiver_span_data"), "CSG Receiver Span Data");
    csgReceiverSpanCountResult = importTexture(deferredTargets.csgReceiverSpanCount, Name("render.deferred.csg_receiver_span_count"), "CSG Receiver Span Count");
    csgRemovedIntervalDepthResult = importTexture(deferredTargets.csgRemovedIntervalDepth, Name("render.deferred.csg_removed_interval_depth"), "CSG Removed Interval Depth");
    csgRemovedIntervalCapNormalResult = importTexture(deferredTargets.csgRemovedIntervalCapNormal, Name("render.deferred.csg_removed_interval_cap_normal"), "CSG Removed Interval Cap Normal");
    csgRemovedIntervalDataResult = importTexture(deferredTargets.csgRemovedIntervalData, Name("render.deferred.csg_removed_interval_data"), "CSG Removed Interval Data");
    csgRemovedIntervalCountResult = importTexture(deferredTargets.csgRemovedIntervalCount, Name("render.deferred.csg_removed_interval_count"), "CSG Removed Interval Count");
    shadowVisibilityResult = importTexture(history ? history->shadowVisibility : deferredTargets.shadowVisibility, Name("render.deferred_lighting.shadow_visibility"), history ? "Lagged Shadow Visibility" : RendererGpuPassText::s_ShadowVisibilityLabel);
    causticIrradianceResult = importTexture(history ? history->causticIrradiance : deferredTargets.causticIrradiance, Name("render.deferred_lighting.caustic_irradiance"), history ? "Lagged Caustic Irradiance" : s_CausticIrradianceLabel);
    surfelIrradianceResult = importTexture(history ? history->surfelIrradiance : deferredTargets.surfelIrradiance, Name("render.deferred_lighting.surfel_irradiance"), history ? "Lagged Surfel Irradiance" : s_SurfelIrradianceLabel);
    currentShadowVisibilityResult = !history ? shadowVisibilityResult : importTexture(deferredTargets.shadowVisibility, Name("render.deferred_shadow_visibility.current_output"), RendererGpuPassText::s_ShadowVisibilityLabel);
    currentCausticIrradianceResult = !history ? causticIrradianceResult : importTexture(deferredTargets.causticIrradiance, Name("render.deferred_effects.current_caustic_irradiance"), s_CausticIrradianceLabel);
    currentSurfelIrradianceResult = !history ? surfelIrradianceResult : importTexture(deferredTargets.surfelIrradiance, Name("render.deferred_surfel_gi.current_irradiance"), s_SurfelIrradianceLabel);
    opaqueColorResult = importFirstWriteTexture(deferredTargets.opaqueColor, Name("render.deferred_lighting.opaque_color"), "Opaque Color");
    sceneShadingResult = importBuffer(deferredLightingResources.sceneShadingBuffer, Name("render.deferred_lighting.scene_shading"), "Scene Shading");
    lightsResult = importBuffer(deferredLightingResources.lightBuffer, Name("render.deferred_lighting.lights"), "Lights");
    meshViewResult = importBuffer(meshViewBufferSnapshot.buffer, Name(RendererFramePipelineDetail::s_MeshViewName), RendererFramePipelineDetail::s_MeshViewLabel);
    if(frameBindings.instanceBuffer){
        const auto importedResource = importBuffer(frameBindings.instanceBuffer, Name("render.deferred.material_instances"), "Material Instances");
        if(!importedResource)
            materialInstancesResult = MakeUnexpected(importedResource.error());
        else
            materialInstancesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(frameBindings.materialTypedBuffer){
        const auto importedResource = importBuffer(frameBindings.materialTypedBuffer, Name("render.deferred.material_typed"), "Material Typed Data");
        if(!importedResource)
            materialTypedResult = MakeUnexpected(importedResource.error());
        else
            materialTypedResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.receiverRanges){
        const auto importedResource = importBuffer(csgResources.receiverRanges, Name("render.deferred.csg_receiver_ranges"), "CSG Receiver Ranges");
        if(!importedResource)
            csgReceiverRangesResult = MakeUnexpected(importedResource.error());
        else
            csgReceiverRangesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.cutters){
        const auto importedResource = importBuffer(csgResources.cutters, Name("render.deferred.csg_cutters"), "CSG Cutters");
        if(!importedResource)
            csgCuttersResult = MakeUnexpected(importedResource.error());
        else
            csgCuttersResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.clipContextSlots){
        const auto importedResource = importBuffer(csgResources.clipContextSlots, Name("render.deferred.csg_clip_context_slots"), "CSG Clip Context Slots");
        if(!importedResource)
            csgClipContextSlotsResult = MakeUnexpected(importedResource.error());
        else
            csgClipContextSlotsResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(csgResources.intervalSampleState){
        const auto importedResource = importBuffer(csgResources.intervalSampleState, Name("render.deferred.csg_interval_sample_state"), "CSG Interval Sample State");
        if(!importedResource)
            csgIntervalSampleStateResult = MakeUnexpected(importedResource.error());
        else
            csgIntervalSampleStateResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    bindlessSlotsResult = history ? importBuffer(history->slotsBuffer, Name(s_LightingBindlessSlotsName), "Lagged Deferred Bindless Slots") : importCurrentBindlessSlots(Name(s_LightingBindlessSlotsName), s_BindlessSlotsLabel);
    currentBindlessSlotsResult = !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get() ? bindlessSlotsResult : importCurrentBindlessSlots(Name("render.deferred_composite.bindless_slots"), s_BindlessSlotsLabel);
    if(rayTracingGraphResources.materialContextSlotsBuffer){
        const auto importedResource = importBuffer(rayTracingGraphResources.materialContextSlotsBuffer, Name("render.deferred.material_context_slots"), "Ray Trace Material Context Slots");
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
        historyCopyDestinationShadowVisibilityResult = history ? shadowVisibilityResult : importFirstWriteTexture(captureHistory->shadowVisibility, Name("render.lagged_history_copy.history_shadow_visibility"), "History Shadow Visibility");
        historyCopyDestinationCausticIrradianceResult = history ? causticIrradianceResult : importFirstWriteTexture(captureHistory->causticIrradiance, Name("render.lagged_history_copy.history_caustic_irradiance"), "History Caustic Irradiance");
        historyCopyDestinationSurfelIrradianceResult = history ? surfelIrradianceResult : importFirstWriteTexture(captureHistory->surfelIrradiance, Name("render.lagged_history_copy.history_surfel_irradiance"), "History Surfel Irradiance");
    }


    // AVBOIT shares deferred G-buffer and imports; compiler owns state seeds through Lighting/Composite.
    avboitLowRasterResult = importAvboitTexture(deferredTargets.avboit.lowRasterTarget, Name("render.avboit.low_raster"), "AVBOIT Low Raster");
    avboitAccumColorResult = importAvboitTexture(deferredTargets.avboit.accumColor, Name("render.avboit.accum_color"), "AVBOIT Accumulated Color");
    avboitAccumExtinctionResult = importAvboitTexture(deferredTargets.avboit.accumExtinction, Name("render.avboit.accum_extinction"), "AVBOIT Accumulated Extinction");
    refractionDepthResult = importAvboitTexture(deferredTargets.avboit.refractionDepth, Name("render.avboit.refractionDepth"), "AVBOIT refractionDepth");
    refractionNormalIorResult = importAvboitTexture(deferredTargets.avboit.refractionNormalIor, Name("render.avboit.refractionNormalIor"), "AVBOIT refractionNormalIor");
    refractionTintCoverageResult = importAvboitTexture(deferredTargets.avboit.refractionTintCoverage, Name("render.avboit.refractionTintCoverage"), "AVBOIT refractionTintCoverage");
    refractionInstanceResult = importAvboitTexture(deferredTargets.avboit.refractionInstance, Name("render.avboit.refractionInstance"), "AVBOIT refractionInstance");
    refractionSpecularRoughnessResult = importAvboitTexture(deferredTargets.avboit.refractionSpecularRoughness, Name("render.avboit.refractionSpecularRoughness"), "AVBOIT Refraction Specular Roughness");
    refractionResolveResult = importAvboitTexture(deferredTargets.avboit.refractionResolve, Name("render.avboit.refractionResolve"), "AVBOIT refractionResolve");
    avboitForegroundColorResult = importAvboitTexture(deferredTargets.avboit.foregroundAccumColor, Name("render.avboit.avboitForegroundColor"), "AVBOIT avboitForegroundColor");
    avboitForegroundExtinctionResult = importAvboitTexture(deferredTargets.avboit.foregroundAccumExtinction, Name("render.avboit.avboitForegroundExtinction"), "AVBOIT avboitForegroundExtinction");
    avboitTransmittanceResult = importAvboitTexture(deferredTargets.avboit.transmittanceTexture, Name("render.avboit.transmittance"), "AVBOIT Transmittance");
    avboitCoverageResult = importBuffer(deferredTargets.avboit.coverageBuffer, Name("render.avboit.coverage"), "AVBOIT Coverage");
    avboitDepthWarpResult = importBuffer(deferredTargets.avboit.depthWarpBuffer, Name(RendererGpuPassText::s_AvboitDepthWarpName), RendererGpuPassText::s_AvboitDepthWarpLabel);
    avboitControlResult = importBuffer(deferredTargets.avboit.controlBuffer, Name("render.avboit.control"), "AVBOIT Control");
    avboitExtinctionResult = importBuffer(deferredTargets.avboit.extinctionBuffer, Name(RendererGpuPassText::s_AvboitExtinctionName), RendererGpuPassText::s_AvboitExtinctionLabel);
    avboitExtinctionOverflowResult = importBuffer(deferredTargets.avboit.extinctionOverflowBuffer, Name("render.avboit.extinction_overflow"), "AVBOIT Extinction Overflow");
    avboitMaterialDomainResult = m_graph.importHazardDomain(RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry"));
    avboitCsgDomainResult = m_graph.importHazardDomain(RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.csg_domain"), RendererGpuPassText::s_TransparentCsgIntervalsLabel));
    if(
        !csgIntervalSampleStateResult
        || !materialInstancesResult
        || !materialTypedResult
        || !csgReceiverRangesResult
        || !csgCuttersResult
        || !csgClipContextSlotsResult
        || !refractionDepthResult
        || !refractionNormalIorResult
        || !refractionTintCoverageResult
        || !refractionInstanceResult
        || !refractionResolveResult
        || !avboitForegroundColorResult
        || !avboitForegroundExtinctionResult
        || !albedoResult
        || !normalResult
        || !worldPositionResult
        || !specularRoughnessResult
        || !refractionSpecularRoughnessResult
        || !depthResult
        || !csgCapBackNormalResult
        || !csgIntervalDepthResult
        || !csgIntervalIdResult
        || !csgReceiverEventDataResult
        || !csgReceiverEventCountResult
        || !csgReceiverSpanDataResult
        || !csgReceiverSpanCountResult
        || !csgRemovedIntervalDepthResult
        || !csgRemovedIntervalCapNormalResult
        || !csgRemovedIntervalDataResult
        || !csgRemovedIntervalCountResult
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
        || !avboitLowRasterResult
        || !avboitAccumColorResult
        || !avboitAccumExtinctionResult
        || !avboitTransmittanceResult
        || !avboitCoverageResult
        || !avboitDepthWarpResult
        || !avboitControlResult
        || !avboitExtinctionResult
        || !avboitExtinctionOverflowResult
        || !avboitMaterialDomainResult
        || !avboitCsgDomainResult
    )
        return MakeUnexpected(Failure{});

    result.albedo = *albedoResult;
    result.normal = *normalResult;
    result.worldPosition = *worldPositionResult;
    result.specularRoughness = *specularRoughnessResult;
    result.depth = *depthResult;
    result.csgCapBackNormal = *csgCapBackNormalResult;
    result.csgIntervalDepth = *csgIntervalDepthResult;
    result.csgIntervalId = *csgIntervalIdResult;
    result.csgReceiverEventData = *csgReceiverEventDataResult;
    result.csgReceiverEventCount = *csgReceiverEventCountResult;
    result.csgReceiverSpanData = *csgReceiverSpanDataResult;
    result.csgReceiverSpanCount = *csgReceiverSpanCountResult;
    result.csgRemovedIntervalDepth = *csgRemovedIntervalDepthResult;
    result.csgRemovedIntervalCapNormal = *csgRemovedIntervalCapNormalResult;
    result.csgRemovedIntervalData = *csgRemovedIntervalDataResult;
    result.csgRemovedIntervalCount = *csgRemovedIntervalCountResult;
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
    if(capturesLaggedLightingHistory)
        result.historyCopyShadowVisibility = *historyCopyShadowVisibilityResult;
    if(capturesLaggedLightingHistory)
        result.historyCopyCausticIrradiance = *historyCopyCausticIrradianceResult;
    if(capturesLaggedLightingHistory)
        result.historyCopySurfelIrradiance = *historyCopySurfelIrradianceResult;
    if(capturesLaggedLightingHistory)
        result.historyCopyDestinationShadowVisibility = *historyCopyDestinationShadowVisibilityResult;
    if(capturesLaggedLightingHistory)
        result.historyCopyDestinationCausticIrradiance = *historyCopyDestinationCausticIrradianceResult;
    if(capturesLaggedLightingHistory)
        result.historyCopyDestinationSurfelIrradiance = *historyCopyDestinationSurfelIrradianceResult;
    result.avboitLowRaster = *avboitLowRasterResult;
    result.avboitAccumColor = *avboitAccumColorResult;
    result.avboitAccumExtinction = *avboitAccumExtinctionResult;
    result.refractionDepth = *refractionDepthResult;
    result.refractionNormalIor = *refractionNormalIorResult;
    result.refractionTintCoverage = *refractionTintCoverageResult;
    result.refractionInstance = *refractionInstanceResult;
    result.refractionSpecularRoughness = *refractionSpecularRoughnessResult;
    result.refractionResolve = *refractionResolveResult;
    result.avboitForegroundColor = *avboitForegroundColorResult;
    result.avboitForegroundExtinction = *avboitForegroundExtinctionResult;
    result.avboitTransmittance = *avboitTransmittanceResult;
    result.avboitCoverage = *avboitCoverageResult;
    result.avboitDepthWarp = *avboitDepthWarpResult;
    result.avboitControl = *avboitControlResult;
    result.avboitExtinction = *avboitExtinctionResult;
    result.avboitExtinctionOverflow = *avboitExtinctionOverflowResult;
    result.avboitMaterialDomain = *avboitMaterialDomainResult;
    result.avboitCsgDomain = *avboitCsgDomainResult;
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

