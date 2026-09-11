// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/graph_resource_import_builder.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredGraphResourceImportBuilder::DeferredGraphResourceImportBuilder(
    Core::GpuTaskGraph& graph
)
    : m_graph(graph){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool DeferredGraphResourceImportBuilder::declare(
    const DeferredGraphResourceImportInputs& inputs,
    DeferredGraphResourceImportResult& outResult
)const{
    outResult = DeferredGraphResourceImportResult{};
    if(
        !inputs.targets
        || !inputs.lightingResources
        || !inputs.frameBindings
        || !inputs.meshViewSnapshot
        || !inputs.csgResources
        || !inputs.rayTracingResources
    )
        return false;

    DeferredFrameTargets& deferredTargets = *inputs.targets;
    const DeferredLightingGraphResources& deferredLightingResources = *inputs.lightingResources;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings = *inputs.frameBindings;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = *inputs.meshViewSnapshot;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources = *inputs.csgResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources = *inputs.rayTracingResources;
    const DeferredLaggedLightingHistoryResources* const history = inputs.history;
    const DeferredLaggedLightingHistoryResources* const captureHistory = inputs.captureHistory;
    const bool clearAvboitTargets = inputs.clearAvboitTargets;
    const bool capturesLaggedLightingHistory = inputs.capturesLaggedLightingHistory;

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
        return clearAvboitTargets
            ? importFirstWriteTexture(texture, identity, label)
            : importTexture(texture, identity, label)
        ;
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, label));
    };
    const auto importCurrentBindlessSlots = [&](const Name& identity, const AStringView label){
        return m_graph.importBuffer(
            deferredTargets.bindless.slotsBuffer,
            RendererTaskGraphDetail::BufferResourceDesc(identity, label)
        );
    };

    outResult.albedo = importFirstWriteTexture(
        deferredTargets.albedo,
        Name("render.deferred_lighting.albedo"),
        "G-Buffer Albedo"
    );
    outResult.normal = importFirstWriteTexture(
        deferredTargets.normal,
        Name("render.deferred_lighting.normal"),
        "G-Buffer Normal"
    );
    outResult.worldPosition = importFirstWriteTexture(
        deferredTargets.worldPosition,
        Name("render.deferred_lighting.world_position"),
        "G-Buffer World Position"
    );
    outResult.specularRoughness = importFirstWriteTexture(
        deferredTargets.specularRoughness,
        Name("render.deferred_lighting.specular_roughness"),
        "G-Buffer Specular Roughness"
    );
    outResult.depth = importFirstWriteTexture(
        deferredTargets.depth,
        Name("render.deferred_lighting.depth"),
        "G-Buffer Depth"
    );


    // CSG working set declared here; wider target lifecycle stays in native producers.
    outResult.csgCapBackNormal = importTexture(
        deferredTargets.csgCapBackNormal,
        Name("render.deferred.csg_cap_back_normal"),
        "CSG Cap Back Normal"
    );
    outResult.csgIntervalDepth = importTexture(
        deferredTargets.csgIntervalDepth,
        Name("render.deferred.csg_interval_depth"),
        "CSG Interval Depth"
    );
    outResult.csgIntervalId = importTexture(
        deferredTargets.csgIntervalId,
        Name("render.deferred.csg_interval_id"),
        "CSG Interval ID"
    );
    outResult.csgReceiverEventData = importTexture(
        deferredTargets.csgReceiverEventData,
        Name("render.deferred.csg_receiver_event_data"),
        "CSG Receiver Event Data"
    );
    outResult.csgReceiverEventCount = importTexture(
        deferredTargets.csgReceiverEventCount,
        Name("render.deferred.csg_receiver_event_count"),
        "CSG Receiver Event Count"
    );
    outResult.csgReceiverSpanData = importTexture(
        deferredTargets.csgReceiverSpanData,
        Name("render.deferred.csg_receiver_span_data"),
        "CSG Receiver Span Data"
    );
    outResult.csgReceiverSpanCount = importTexture(
        deferredTargets.csgReceiverSpanCount,
        Name("render.deferred.csg_receiver_span_count"),
        "CSG Receiver Span Count"
    );
    outResult.csgRemovedIntervalDepth = importTexture(
        deferredTargets.csgRemovedIntervalDepth,
        Name("render.deferred.csg_removed_interval_depth"),
        "CSG Removed Interval Depth"
    );
    outResult.csgRemovedIntervalCapNormal = importTexture(
        deferredTargets.csgRemovedIntervalCapNormal,
        Name("render.deferred.csg_removed_interval_cap_normal"),
        "CSG Removed Interval Cap Normal"
    );
    outResult.csgRemovedIntervalData = importTexture(
        deferredTargets.csgRemovedIntervalData,
        Name("render.deferred.csg_removed_interval_data"),
        "CSG Removed Interval Data"
    );
    outResult.csgRemovedIntervalCount = importTexture(
        deferredTargets.csgRemovedIntervalCount,
        Name("render.deferred.csg_removed_interval_count"),
        "CSG Removed Interval Count"
    );
    outResult.shadowVisibility = importTexture(
        history ? history->shadowVisibility : deferredTargets.shadowVisibility,
        Name("render.deferred_lighting.shadow_visibility"),
        history ? "Lagged Shadow Visibility" : "Shadow Visibility"
    );
    outResult.causticIrradiance = importTexture(
        history ? history->causticIrradiance : deferredTargets.causticIrradiance,
        Name("render.deferred_lighting.caustic_irradiance"),
        history ? "Lagged Caustic Irradiance" : "Caustic Irradiance"
    );
    outResult.surfelIrradiance = importTexture(
        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,
        Name("render.deferred_lighting.surfel_irradiance"),
        history ? "Lagged Surfel Irradiance" : "Surfel Irradiance"
    );
    outResult.currentShadowVisibility = !history
        ? outResult.shadowVisibility
        : importTexture(
            deferredTargets.shadowVisibility,
            Name("render.deferred_shadow_visibility.current_output"),
            "Shadow Visibility"
        )
    ;
    outResult.currentCausticIrradiance = !history
        ? outResult.causticIrradiance
        : importTexture(
            deferredTargets.causticIrradiance,
            Name("render.deferred_effects.current_caustic_irradiance"),
            "Caustic Irradiance"
        )
    ;
    outResult.currentSurfelIrradiance = !history
        ? outResult.surfelIrradiance
        : importTexture(
            deferredTargets.surfelIrradiance,
            Name("render.deferred_surfel_gi.current_irradiance"),
            "Surfel Irradiance"
        )
    ;
    outResult.opaqueColor = importFirstWriteTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_lighting.opaque_color"),
        "Opaque Color"
    );
    outResult.sceneShading = importBuffer(
        deferredLightingResources.sceneShadingBuffer,
        Name("render.deferred_lighting.scene_shading"),
        "Scene Shading"
    );
    outResult.lights = importBuffer(
        deferredLightingResources.lightBuffer,
        Name("render.deferred_lighting.lights"),
        "Lights"
    );
    outResult.meshView = importBuffer(
        meshViewBufferSnapshot.buffer,
        Name("render.deferred.mesh_view"),
        "Mesh View"
    );
    outResult.materialInstances = frameBindings.instanceBuffer
        ? importBuffer(
            frameBindings.instanceBuffer,
            Name("render.deferred.material_instances"),
            "Material Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    outResult.materialTyped = frameBindings.materialTypedBuffer
        ? importBuffer(
            frameBindings.materialTypedBuffer,
            Name("render.deferred.material_typed"),
            "Material Typed Data"
        )
        : Core::GpuGraphResourceId{}
    ;
    outResult.csgReceiverRanges = csgResources.receiverRanges
        ? importBuffer(
            csgResources.receiverRanges,
            Name("render.deferred.csg_receiver_ranges"),
            "CSG Receiver Ranges"
        )
        : Core::GpuGraphResourceId{}
    ;
    outResult.csgCutters = csgResources.cutters
        ? importBuffer(
            csgResources.cutters,
            Name("render.deferred.csg_cutters"),
            "CSG Cutters"
        )
        : Core::GpuGraphResourceId{}
    ;
    outResult.csgClipContextSlots = csgResources.clipContextSlots
        ? importBuffer(
            csgResources.clipContextSlots,
            Name("render.deferred.csg_clip_context_slots"),
            "CSG Clip Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;
    outResult.csgIntervalSampleState = csgResources.intervalSampleState
        ? importBuffer(
            csgResources.intervalSampleState,
            Name("render.deferred.csg_interval_sample_state"),
            "CSG Interval Sample State"
        )
        : Core::GpuGraphResourceId{}
    ;
    outResult.bindlessSlots = history
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
    outResult.currentBindlessSlots =
        !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get()
            ? outResult.bindlessSlots
            : importCurrentBindlessSlots(
                Name("render.deferred_composite.bindless_slots"),
                "Deferred Bindless Slots"
            )
    ;
    outResult.materialContextSlots = rayTracingGraphResources.materialContextSlotsBuffer
        ? importBuffer(
            rayTracingGraphResources.materialContextSlotsBuffer,
            Name("render.deferred.material_context_slots"),
            "Ray Trace Material Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;


    // History copy declared after Present; reuse active-lighting identities for copy destinations.
    if(capturesLaggedLightingHistory){
        outResult.historyCopyShadowVisibility = outResult.currentShadowVisibility;
        outResult.historyCopyCausticIrradiance = outResult.currentCausticIrradiance;
        outResult.historyCopySurfelIrradiance = history
            ? outResult.currentSurfelIrradiance
            : outResult.surfelIrradiance
        ;
        outResult.historyCopyDestinationShadowVisibility = history
            ? outResult.shadowVisibility
            : importFirstWriteTexture(
                captureHistory->shadowVisibility,
                Name("render.lagged_history_copy.history_shadow_visibility"),
                "History Shadow Visibility"
            )
        ;
        outResult.historyCopyDestinationCausticIrradiance = history
            ? outResult.causticIrradiance
            : importFirstWriteTexture(
                captureHistory->causticIrradiance,
                Name("render.lagged_history_copy.history_caustic_irradiance"),
                "History Caustic Irradiance"
            )
        ;
        outResult.historyCopyDestinationSurfelIrradiance = history
            ? outResult.surfelIrradiance
            : importFirstWriteTexture(
                captureHistory->surfelIrradiance,
                Name("render.lagged_history_copy.history_surfel_irradiance"),
                "History Surfel Irradiance"
            )
        ;
    }


    // AVBOIT shares deferred G-buffer and imports; compiler owns state seeds through Lighting/Composite.
    outResult.avboitLowRaster = importAvboitTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    outResult.avboitAccumColor = importAvboitTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    outResult.avboitAccumExtinction = importAvboitTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    outResult.refractionDepth = importAvboitTexture(
        deferredTargets.avboit.refractionDepth, Name("render.avboit.refractionDepth"), "AVBOIT refractionDepth");
    outResult.refractionNormalIor = importAvboitTexture(
        deferredTargets.avboit.refractionNormalIor, Name("render.avboit.refractionNormalIor"), "AVBOIT refractionNormalIor");
    outResult.refractionTintCoverage = importAvboitTexture(
        deferredTargets.avboit.refractionTintCoverage, Name("render.avboit.refractionTintCoverage"), "AVBOIT refractionTintCoverage");
    outResult.refractionInstance = importAvboitTexture(
        deferredTargets.avboit.refractionInstance, Name("render.avboit.refractionInstance"), "AVBOIT refractionInstance");
    outResult.refractionSpecularRoughness = importAvboitTexture(
        deferredTargets.avboit.refractionSpecularRoughness, Name("render.avboit.refractionSpecularRoughness"), "AVBOIT Refraction Specular Roughness");
    outResult.refractionResolve = importAvboitTexture(
        deferredTargets.avboit.refractionResolve, Name("render.avboit.refractionResolve"), "AVBOIT refractionResolve");
    outResult.avboitForegroundColor = importAvboitTexture(
        deferredTargets.avboit.foregroundAccumColor, Name("render.avboit.avboitForegroundColor"), "AVBOIT avboitForegroundColor");
    outResult.avboitForegroundExtinction = importAvboitTexture(
        deferredTargets.avboit.foregroundAccumExtinction, Name("render.avboit.avboitForegroundExtinction"), "AVBOIT avboitForegroundExtinction");
    outResult.avboitTransmittance = importAvboitTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    outResult.avboitCoverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    outResult.avboitDepthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    outResult.avboitControl = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    outResult.avboitExtinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    outResult.avboitExtinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    outResult.avboitMaterialDomain = m_graph.importHazardDomain(
        RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    outResult.avboitCsgDomain = m_graph.importHazardDomain(
        RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !outResult.albedo.valid()
        || !outResult.normal.valid()
        || !outResult.worldPosition.valid()
        || !outResult.specularRoughness.valid()
        || !outResult.refractionSpecularRoughness.valid()
        || !outResult.depth.valid()
        || !outResult.csgCapBackNormal.valid()
        || !outResult.csgIntervalDepth.valid()
        || !outResult.csgIntervalId.valid()
        || !outResult.csgReceiverEventData.valid()
        || !outResult.csgReceiverEventCount.valid()
        || !outResult.csgReceiverSpanData.valid()
        || !outResult.csgReceiverSpanCount.valid()
        || !outResult.csgRemovedIntervalDepth.valid()
        || !outResult.csgRemovedIntervalCapNormal.valid()
        || !outResult.csgRemovedIntervalData.valid()
        || !outResult.csgRemovedIntervalCount.valid()
        || !outResult.shadowVisibility.valid()
        || !outResult.causticIrradiance.valid()
        || !outResult.surfelIrradiance.valid()
        || !outResult.currentShadowVisibility.valid()
        || !outResult.currentCausticIrradiance.valid()
        || !outResult.currentSurfelIrradiance.valid()
        || !outResult.opaqueColor.valid()
        || !outResult.sceneShading.valid()
        || !outResult.lights.valid()
        || !outResult.meshView.valid()
        || !outResult.bindlessSlots.valid()
        || !outResult.currentBindlessSlots.valid()
        || (rayTracingGraphResources.materialContextSlotsBuffer && !outResult.materialContextSlots.valid())
        || (capturesLaggedLightingHistory && (
            !outResult.historyCopyShadowVisibility.valid()
            || !outResult.historyCopyCausticIrradiance.valid()
            || !outResult.historyCopySurfelIrradiance.valid()
            || !outResult.historyCopyDestinationShadowVisibility.valid()
            || !outResult.historyCopyDestinationCausticIrradiance.valid()
            || !outResult.historyCopyDestinationSurfelIrradiance.valid()
        ))
        || !outResult.avboitLowRaster.valid()
        || !outResult.avboitAccumColor.valid()
        || !outResult.avboitAccumExtinction.valid()
        || !outResult.avboitTransmittance.valid()
        || !outResult.avboitCoverage.valid()
        || !outResult.avboitDepthWarp.valid()
        || !outResult.avboitControl.valid()
        || !outResult.avboitExtinction.valid()
        || !outResult.avboitExtinctionOverflow.valid()
        || !outResult.avboitMaterialDomain.valid()
        || !outResult.avboitCsgDomain.valid()
    )
        return false;
    outResult.csgPeelSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgPeelLayerCount
    );
    outResult.csgReceiverEventDataSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverEventLayerCount
    );
    outResult.csgReceiverEventCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    outResult.csgReceiverSpanDataSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverSpanLayerCount
    );
    outResult.csgReceiverSpanCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    outResult.csgRemovedIntervalSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgRemovedIntervalLayerCount
    );
    outResult.csgRemovedIntervalCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
