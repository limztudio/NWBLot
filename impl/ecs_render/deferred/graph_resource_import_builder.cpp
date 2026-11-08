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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

