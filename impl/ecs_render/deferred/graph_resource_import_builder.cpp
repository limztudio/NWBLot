// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/graph_resource_import_builder.h>

#include <core/graphics/backend_selection/backend.h>

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


[[nodiscard]] Expected<DeferredGraphResourceImportResult> DeferredGraphResourceImportBuilder::declare(
    const DeferredGraphResourceImportInputs& inputs
)const{
    DeferredGraphResourceImportResult result{};
    if(
        !inputs.targets
        || !inputs.lightingResources
        || !inputs.frameBindings
        || !inputs.meshViewSnapshot
        || !inputs.csgResources
        || !inputs.rayTracingResources
    )
        return MakeUnexpected(Failure{});

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

    result.albedo = importFirstWriteTexture(
        deferredTargets.albedo,
        Name("render.deferred_lighting.albedo"),
        "G-Buffer Albedo"
    );
    result.normal = importFirstWriteTexture(
        deferredTargets.normal,
        Name("render.deferred_lighting.normal"),
        "G-Buffer Normal"
    );
    result.worldPosition = importFirstWriteTexture(
        deferredTargets.worldPosition,
        Name("render.deferred_lighting.world_position"),
        "G-Buffer World Position"
    );
    result.specularRoughness = importFirstWriteTexture(
        deferredTargets.specularRoughness,
        Name("render.deferred_lighting.specular_roughness"),
        "G-Buffer Specular Roughness"
    );
    result.depth = importFirstWriteTexture(
        deferredTargets.depth,
        Name("render.deferred_lighting.depth"),
        "G-Buffer Depth"
    );


    // CSG working set declared here; wider target lifecycle stays in native producers.
    result.csgCapBackNormal = importTexture(
        deferredTargets.csgCapBackNormal,
        Name("render.deferred.csg_cap_back_normal"),
        "CSG Cap Back Normal"
    );
    result.csgIntervalDepth = importTexture(
        deferredTargets.csgIntervalDepth,
        Name("render.deferred.csg_interval_depth"),
        "CSG Interval Depth"
    );
    result.csgIntervalId = importTexture(
        deferredTargets.csgIntervalId,
        Name("render.deferred.csg_interval_id"),
        "CSG Interval ID"
    );
    result.csgReceiverEventData = importTexture(
        deferredTargets.csgReceiverEventData,
        Name("render.deferred.csg_receiver_event_data"),
        "CSG Receiver Event Data"
    );
    result.csgReceiverEventCount = importTexture(
        deferredTargets.csgReceiverEventCount,
        Name("render.deferred.csg_receiver_event_count"),
        "CSG Receiver Event Count"
    );
    result.csgReceiverSpanData = importTexture(
        deferredTargets.csgReceiverSpanData,
        Name("render.deferred.csg_receiver_span_data"),
        "CSG Receiver Span Data"
    );
    result.csgReceiverSpanCount = importTexture(
        deferredTargets.csgReceiverSpanCount,
        Name("render.deferred.csg_receiver_span_count"),
        "CSG Receiver Span Count"
    );
    result.csgRemovedIntervalDepth = importTexture(
        deferredTargets.csgRemovedIntervalDepth,
        Name("render.deferred.csg_removed_interval_depth"),
        "CSG Removed Interval Depth"
    );
    result.csgRemovedIntervalCapNormal = importTexture(
        deferredTargets.csgRemovedIntervalCapNormal,
        Name("render.deferred.csg_removed_interval_cap_normal"),
        "CSG Removed Interval Cap Normal"
    );
    result.csgRemovedIntervalData = importTexture(
        deferredTargets.csgRemovedIntervalData,
        Name("render.deferred.csg_removed_interval_data"),
        "CSG Removed Interval Data"
    );
    result.csgRemovedIntervalCount = importTexture(
        deferredTargets.csgRemovedIntervalCount,
        Name("render.deferred.csg_removed_interval_count"),
        "CSG Removed Interval Count"
    );
    result.shadowVisibility = importTexture(
        history ? history->shadowVisibility : deferredTargets.shadowVisibility,
        Name("render.deferred_lighting.shadow_visibility"),
        history ? "Lagged Shadow Visibility" : "Shadow Visibility"
    );
    result.causticIrradiance = importTexture(
        history ? history->causticIrradiance : deferredTargets.causticIrradiance,
        Name("render.deferred_lighting.caustic_irradiance"),
        history ? "Lagged Caustic Irradiance" : "Caustic Irradiance"
    );
    result.surfelIrradiance = importTexture(
        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,
        Name("render.deferred_lighting.surfel_irradiance"),
        history ? "Lagged Surfel Irradiance" : "Surfel Irradiance"
    );
    result.currentShadowVisibility = !history
        ? result.shadowVisibility
        : importTexture(
            deferredTargets.shadowVisibility,
            Name("render.deferred_shadow_visibility.current_output"),
            "Shadow Visibility"
        )
    ;
    result.currentCausticIrradiance = !history
        ? result.causticIrradiance
        : importTexture(
            deferredTargets.causticIrradiance,
            Name("render.deferred_effects.current_caustic_irradiance"),
            "Caustic Irradiance"
        )
    ;
    result.currentSurfelIrradiance = !history
        ? result.surfelIrradiance
        : importTexture(
            deferredTargets.surfelIrradiance,
            Name("render.deferred_surfel_gi.current_irradiance"),
            "Surfel Irradiance"
        )
    ;
    result.opaqueColor = importFirstWriteTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_lighting.opaque_color"),
        "Opaque Color"
    );
    result.sceneShading = importBuffer(
        deferredLightingResources.sceneShadingBuffer,
        Name("render.deferred_lighting.scene_shading"),
        "Scene Shading"
    );
    result.lights = importBuffer(
        deferredLightingResources.lightBuffer,
        Name("render.deferred_lighting.lights"),
        "Lights"
    );
    result.meshView = importBuffer(
        meshViewBufferSnapshot.buffer,
        Name("render.deferred.mesh_view"),
        "Mesh View"
    );
    result.materialInstances = frameBindings.instanceBuffer
        ? importBuffer(
            frameBindings.instanceBuffer,
            Name("render.deferred.material_instances"),
            "Material Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    result.materialTyped = frameBindings.materialTypedBuffer
        ? importBuffer(
            frameBindings.materialTypedBuffer,
            Name("render.deferred.material_typed"),
            "Material Typed Data"
        )
        : Core::GpuGraphResourceId{}
    ;
    result.csgReceiverRanges = csgResources.receiverRanges
        ? importBuffer(
            csgResources.receiverRanges,
            Name("render.deferred.csg_receiver_ranges"),
            "CSG Receiver Ranges"
        )
        : Core::GpuGraphResourceId{}
    ;
    result.csgCutters = csgResources.cutters
        ? importBuffer(
            csgResources.cutters,
            Name("render.deferred.csg_cutters"),
            "CSG Cutters"
        )
        : Core::GpuGraphResourceId{}
    ;
    result.csgClipContextSlots = csgResources.clipContextSlots
        ? importBuffer(
            csgResources.clipContextSlots,
            Name("render.deferred.csg_clip_context_slots"),
            "CSG Clip Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;
    result.csgIntervalSampleState = csgResources.intervalSampleState
        ? importBuffer(
            csgResources.intervalSampleState,
            Name("render.deferred.csg_interval_sample_state"),
            "CSG Interval Sample State"
        )
        : Core::GpuGraphResourceId{}
    ;
    result.bindlessSlots = history
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
    result.currentBindlessSlots =
        !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get()
            ? result.bindlessSlots
            : importCurrentBindlessSlots(
                Name("render.deferred_composite.bindless_slots"),
                "Deferred Bindless Slots"
            )
    ;
    result.materialContextSlots = rayTracingGraphResources.materialContextSlotsBuffer
        ? importBuffer(
            rayTracingGraphResources.materialContextSlotsBuffer,
            Name("render.deferred.material_context_slots"),
            "Ray Trace Material Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;


    // History copy declared after Present; reuse active-lighting identities for copy destinations.
    if(capturesLaggedLightingHistory){
        result.historyCopyShadowVisibility = result.currentShadowVisibility;
        result.historyCopyCausticIrradiance = result.currentCausticIrradiance;
        result.historyCopySurfelIrradiance = history
            ? result.currentSurfelIrradiance
            : result.surfelIrradiance
        ;
        result.historyCopyDestinationShadowVisibility = history
            ? result.shadowVisibility
            : importFirstWriteTexture(
                captureHistory->shadowVisibility,
                Name("render.lagged_history_copy.history_shadow_visibility"),
                "History Shadow Visibility"
            )
        ;
        result.historyCopyDestinationCausticIrradiance = history
            ? result.causticIrradiance
            : importFirstWriteTexture(
                captureHistory->causticIrradiance,
                Name("render.lagged_history_copy.history_caustic_irradiance"),
                "History Caustic Irradiance"
            )
        ;
        result.historyCopyDestinationSurfelIrradiance = history
            ? result.surfelIrradiance
            : importFirstWriteTexture(
                captureHistory->surfelIrradiance,
                Name("render.lagged_history_copy.history_surfel_irradiance"),
                "History Surfel Irradiance"
            )
        ;
    }


    // AVBOIT shares deferred G-buffer and imports; compiler owns state seeds through Lighting/Composite.
    result.avboitLowRaster = importAvboitTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    result.avboitAccumColor = importAvboitTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    result.avboitAccumExtinction = importAvboitTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    result.refractionDepth = importAvboitTexture(
        deferredTargets.avboit.refractionDepth, Name("render.avboit.refractionDepth"), "AVBOIT refractionDepth");
    result.refractionNormalIor = importAvboitTexture(
        deferredTargets.avboit.refractionNormalIor, Name("render.avboit.refractionNormalIor"), "AVBOIT refractionNormalIor");
    result.refractionTintCoverage = importAvboitTexture(
        deferredTargets.avboit.refractionTintCoverage, Name("render.avboit.refractionTintCoverage"), "AVBOIT refractionTintCoverage");
    result.refractionInstance = importAvboitTexture(
        deferredTargets.avboit.refractionInstance, Name("render.avboit.refractionInstance"), "AVBOIT refractionInstance");
    result.refractionSpecularRoughness = importAvboitTexture(
        deferredTargets.avboit.refractionSpecularRoughness, Name("render.avboit.refractionSpecularRoughness"), "AVBOIT Refraction Specular Roughness");
    result.refractionResolve = importAvboitTexture(
        deferredTargets.avboit.refractionResolve, Name("render.avboit.refractionResolve"), "AVBOIT refractionResolve");
    result.avboitForegroundColor = importAvboitTexture(
        deferredTargets.avboit.foregroundAccumColor, Name("render.avboit.avboitForegroundColor"), "AVBOIT avboitForegroundColor");
    result.avboitForegroundExtinction = importAvboitTexture(
        deferredTargets.avboit.foregroundAccumExtinction, Name("render.avboit.avboitForegroundExtinction"), "AVBOIT avboitForegroundExtinction");
    result.avboitTransmittance = importAvboitTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    result.avboitCoverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    result.avboitDepthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    result.avboitControl = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    result.avboitExtinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    result.avboitExtinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    result.avboitMaterialDomain = m_graph.importHazardDomain(
        RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    result.avboitCsgDomain = m_graph.importHazardDomain(
        RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !result.albedo.valid()
        || !result.normal.valid()
        || !result.worldPosition.valid()
        || !result.specularRoughness.valid()
        || !result.refractionSpecularRoughness.valid()
        || !result.depth.valid()
        || !result.csgCapBackNormal.valid()
        || !result.csgIntervalDepth.valid()
        || !result.csgIntervalId.valid()
        || !result.csgReceiverEventData.valid()
        || !result.csgReceiverEventCount.valid()
        || !result.csgReceiverSpanData.valid()
        || !result.csgReceiverSpanCount.valid()
        || !result.csgRemovedIntervalDepth.valid()
        || !result.csgRemovedIntervalCapNormal.valid()
        || !result.csgRemovedIntervalData.valid()
        || !result.csgRemovedIntervalCount.valid()
        || !result.shadowVisibility.valid()
        || !result.causticIrradiance.valid()
        || !result.surfelIrradiance.valid()
        || !result.currentShadowVisibility.valid()
        || !result.currentCausticIrradiance.valid()
        || !result.currentSurfelIrradiance.valid()
        || !result.opaqueColor.valid()
        || !result.sceneShading.valid()
        || !result.lights.valid()
        || !result.meshView.valid()
        || !result.bindlessSlots.valid()
        || !result.currentBindlessSlots.valid()
        || (rayTracingGraphResources.materialContextSlotsBuffer && !result.materialContextSlots.valid())
        || (capturesLaggedLightingHistory && (
            !result.historyCopyShadowVisibility.valid()
            || !result.historyCopyCausticIrradiance.valid()
            || !result.historyCopySurfelIrradiance.valid()
            || !result.historyCopyDestinationShadowVisibility.valid()
            || !result.historyCopyDestinationCausticIrradiance.valid()
            || !result.historyCopyDestinationSurfelIrradiance.valid()
        ))
        || !result.avboitLowRaster.valid()
        || !result.avboitAccumColor.valid()
        || !result.avboitAccumExtinction.valid()
        || !result.avboitTransmittance.valid()
        || !result.avboitCoverage.valid()
        || !result.avboitDepthWarp.valid()
        || !result.avboitControl.valid()
        || !result.avboitExtinction.valid()
        || !result.avboitExtinctionOverflow.valid()
        || !result.avboitMaterialDomain.valid()
        || !result.avboitCsgDomain.valid()
    )
        return MakeUnexpected(Failure{});
    result.csgPeelSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgPeelLayerCount
    );
    result.csgReceiverEventDataSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverEventLayerCount
    );
    result.csgReceiverEventCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    result.csgReceiverSpanDataSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverSpanLayerCount
    );
    result.csgReceiverSpanCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    result.csgRemovedIntervalSubresources = Core::TextureSubresourceSet(
        0u,
        1u,
        0u,
        deferredTargets.csgRemovedIntervalLayerCount
    );
    result.csgRemovedIntervalCountSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

