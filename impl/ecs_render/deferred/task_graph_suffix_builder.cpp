// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_suffix_builder.h"
#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/deferred/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/deferred/import_types.adhd.h>
#include <core/graphics/backend_selection/backend.h>
#include "task_graph_gbuffer_task.h"
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/deferred/format_private.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <core/graphics/backend_selection.h>
#include "post_gbuffer_normalize_builder.h"
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/shared/frame_push_constants.adhd.h>
#include <core/common/log.h>
#include "lighting_stage_builder.h"
#include <impl/ecs_render/shadow/rt_shadow_state.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include "frame_tail_builder.h"
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredGraphSuffixResult> DeferredGraphSuffixBuilder::declare(
    const DeferredGraphSuffixInputs& inputs,
    const Core::GpuTaskId& shadowVisibilityTask,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingFrameTransaction& frameTimingTransaction
){
    using namespace RendererTaskGraphDetail;
    DeferredGraphSuffixResult result{};
    if(
        !inputs.opaqueColor.valid()
        || !inputs.avboitAccumColor.valid()
        || !inputs.avboitAccumExtinction.valid()
        || !inputs.refractionResolve.valid()
        || !inputs.refractionDepth.valid()
        || !inputs.currentBindlessSlots.valid()
        || !inputs.lightingTask.valid()
        || !inputs.avboitFinalTask.valid()
        || !inputs.refractionResolveTask.valid()
        || !inputs.reflectionGraph.valid()
        || !inputs.presentationFrame.valid()
    )
        return MakeUnexpected(Failure{});

    Core::GpuTaskGraphOutputLayer outputLayer;
    if(m_outputLayerContributor){
        const auto declaredLayer = m_outputLayerContributor->declareTaskGraphOutputLayer(m_graph);
        if(!declaredLayer){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: output layer did not declare its graph work"));
            return MakeUnexpected(Failure{});
        }
        outputLayer = *declaredLayer;
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
        const b8 hasColor = outputLayer.color.valid();
        if(!declarations.valid() || !outputLayer.validShape() || (outputLayer.readyTask.valid() && !declarations.validTask(outputLayer.readyTask)))
            return MakeUnexpected(Failure{});
        if(hasColor){
            if(!declarations.validResource(outputLayer.color) || !declarations.validResourceVersion(outputLayer.colorVersion))
                return MakeUnexpected(Failure{});
            const Core::GpuTaskGraphResourceVersionView version = declarations.resourceVersionAt(outputLayer.colorVersion.index);
            const Core::Texture* const texture = declarations.textureForResource(outputLayer.color);
            if(!texture || version.resource != outputLayer.color || version.origin != Core::GpuGraphResourceVersionOrigin::TaskProduced)
                return MakeUnexpected(Failure{});
            const Core::TextureDesc& desc = texture->getDescription();
            const Core::TextureDesc& outputDesc = inputs.presentationFrame.backBuffer.texture->getDescription();
            if(
                outputLayer.sampledImage.descriptorClass() != Core::GpuDescriptorClass::SampledImage
                || desc.width != outputDesc.width
                || desc.height != outputDesc.height
                || desc.dimension != Core::TextureDimension::Texture2D
                || desc.format != Core::Format::RGBA16_FLOAT
                || desc.sampleCount != 1u
                || desc.mipLevels != 1u
                || desc.arraySize != 1u
                || !version.range.textureSubresources.isEntireTexture(desc)
            )
                return MakeUnexpected(Failure{});
        }
    }

    const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);
        desc.setInitialState(Core::ResourceStates::Unknown);
        return m_graph.importTexture(texture, desc);
    };
    const auto compositeColorResult = importFirstWriteTexture(inputs.targets.compositeColor, Name(s_RenderNameRenderDeferredCompositeCompositeColor), "Composite Color");
    const Core::GpuGraphResourceId compositeBindlessSlots = inputs.currentBindlessSlots;
    if(!compositeColorResult || !compositeBindlessSlots.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuGraphResourceId compositeColor = *compositeColorResult;

    const Array<Core::GpuTaskResourceUse, 11u> compositeResourceUses = {
        ReadUse(inputs.opaqueColor),
        ReadUse(inputs.reflectionGraph.opaqueRadiance),
        ReadUse(inputs.reflectionGraph.glassRadiance),
        ReadUse(inputs.avboitAccumColor),
        ReadUse(inputs.avboitAccumExtinction),
        ReadUse(inputs.avboitForegroundColor),
        ReadUse(inputs.avboitForegroundExtinction),
        ReadUse(inputs.refractionResolve),
        ReadUse(inputs.refractionDepth),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Core::GpuTaskCostHint::Medium;
    compositeScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    compositeScheduling.forceSubmissionBoundary = false;
    compositeScheduling.allowPacketMerge = true;
    compositeScheduling.mergeWithPrevious = true;
    const Array<Core::GpuTaskId, 4u> compositeDependencies = { inputs.lightingTask, inputs.avboitFinalTask, inputs.refractionResolveTask, inputs.reflectionGraph.completion, };
    Core::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name(s_RenderNameRenderDeferredComposite))
        .setMarkerLabel(RendererGpuPassText::s_DeferredCompositeLabel)
        .setScheduling(compositeScheduling)
        .setDependencies(compositeDependencies)
        .setResourceUses(compositeResourceUses)
    ;
    // Composite may share its compatible serial predecessor while retaining all graph-owned joins.
    const auto compositeTaskResult = m_deferredSystem.declareDeferredCompositeTask(m_graph, compositeDesc, inputs.reflectionCompositeInputs, inputs.targets, compositeTimingTicket);
    if(!compositeTaskResult){
        result.compositeTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return MakeUnexpected(Failure{});
    }
    result.compositeTask = *compositeTaskResult;

    const Core::AcquiredPresentationFrame& presentationFrame = inputs.presentationFrame;
    const Core::FramebufferDesc& presentationFramebufferDesc = inputs.presentationFramebufferDesc;
    const auto backBufferAvailabilityResult =
        m_graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc{}
                .setIdentity(Name(s_RenderNameRenderDeferredPresentBackbufferAvailability))
                .setMarkerLabel("Presentation Back Buffer Availability")
                .setToken(presentationFrame.backBuffer.availabilityCompletion)
        )
    ;
    if(!backBufferAvailabilityResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import presentation back-buffer availability"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuExternalCompletionId backBufferAvailability = *backBufferAvailabilityResult;

    Core::GpuGraphResourceDesc backBufferDesc = TextureResourceDesc(Name(s_RenderNameRenderDeferredPresentBackbuffer), "Presentation Back Buffer");
    backBufferDesc.setInitialState(presentationFrame.backBuffer.nativeInitialState).setInitialAvailabilityCompletion(backBufferAvailability).setExternalFinalState(Core::ResourceStates::Present);
    const auto backbufferResult = m_graph.importTexture(presentationFrame.backBuffer.texture, backBufferDesc);
    if(!backbufferResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuGraphResourceId backbuffer = *backbufferResult;

    Array<Core::GpuTaskResourceUse, s_PresentMaxResourceUseCount> presentResourceUses = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(backbuffer, presentationFramebufferDesc.colorAttachments[0].subresources, Core::ResourceStates::RenderTarget),
    };
    usize presentResourceUseCount = s_PresentBaseResourceUseCount;
    const Core::GpuTaskResourceVersionUse outputLayerVersionUse{ .version = outputLayer.colorVersion, .role = Core::GpuTaskResourceVersionRole::Consume, };
    if(outputLayer.color.valid())
        presentResourceUses[presentResourceUseCount++] = ReadUse(outputLayer.color);

    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    Array<Core::GpuTaskId, s_PresentMaxDependencyCount> presentDependencies = { result.compositeTask };
    usize presentDependencyCount = 1u;
    if(inputs.useLaggedLightingHistory){
        presentDependencies[presentDependencyCount++] = inputs.surfelGiTask;
        // Lagged lighting omits the current HW caustic join; drain photon geometry reads before next-frame Graphics skinning.
        if(inputs.hardwareCausticsTask.valid())
            presentDependencies[presentDependencyCount++] = inputs.hardwareCausticsTask;
    }
    if(outputLayer.readyTask.valid())
        presentDependencies[presentDependencyCount++] = outputLayer.readyTask;
    Core::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name(s_RenderNameRenderDeferredPresent))
        .setMarkerLabel(RendererGpuPassText::s_DeferredPresentLabel)
        .setScheduling(presentScheduling)
        .setDependencies({ presentDependencies.data(), presentDependencyCount })
        .setResourceUses({ presentResourceUses.data(), presentResourceUseCount })
        .setResourceVersionUses({ &outputLayerVersionUse, outputLayer.color.valid() ? 1u : 0u })
    ;
    const auto presentTaskResult = m_graph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = m_deferredSystem,
            .graphics = m_graphics,
            .targets = inputs.targets,
            .presentationFrame = presentationFrame,
            .backBuffer = backbuffer,
            .outputLayer = outputLayer,
            .outputLayerContributor = m_outputLayerContributor,
            .asyncFinalTiming = asyncFinalTiming,
            .timingTicket = presentTimingTicket,
            .shadowVisibilityTask = shadowVisibilityTask,
        });
    if(!presentTaskResult){
        result.presentTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return MakeUnexpected(Failure{});
    }
    result.presentTask = *presentTaskResult;

    // UI/overlay declares before diagnostic tails so timing follows the final contributor.
    result.overlayRequired = m_presentationContributor && m_presentationContributor->hasTaskGraphPresentationWork();
    if(result.overlayRequired){
        const auto overlayTaskResult = m_presentationContributor->declareTaskGraphPresentation(m_graph, presentationFrame, backbuffer, result.presentTask);
        if(!overlayTaskResult){
            result.overlayTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor did not declare its final graph task"));
            return MakeUnexpected(Failure{});
        }
        result.overlayTask = *overlayTaskResult;
    }

    const Core::GpuTaskId frameTimingEndDependency = result.overlayTask.valid() ? result.overlayTask : result.presentTask;
    Core::GpuTaskSchedulingHint frameTimingEndScheduling;
    frameTimingEndScheduling.cost = Core::GpuTaskCostHint::Tiny;
    frameTimingEndScheduling.forceSubmissionBoundary = true;
    frameTimingEndScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc frameTimingEndDesc;
    frameTimingEndDesc.setIdentity(Name(s_RenderNameRenderFrameTimingEnd)).setMarkerLabel("Frame Timing End").setScheduling(frameTimingEndScheduling).setDependencies({ &frameTimingEndDependency, 1u });
    const auto frameTimingEndTaskResult = m_graph.addTask<FrameTimingEndGraphTask>(frameTimingEndDesc, FrameTimingEndGraphTask::Payload{ .frameTimingTransaction = frameTimingTransaction, });
    if(!frameTimingEndTaskResult){
        result.frameTimingEndTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-timing endpoint graph task"));
        return MakeUnexpected(Failure{});
    }
    result.frameTimingEndTask = *frameTimingEndTaskResult;
    if(!m_graph.declarePresentEndpoint(Core::GpuPresentEndpoint{ .producer = result.frameTimingEndTask, .backBuffer = backbuffer, })){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graph presentation endpoint"));
        return MakeUnexpected(Failure{});
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GbufferGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.opaqueDrawSnapshot.captured)
        return false;

    Core::GraphicsRuntime& graphics = payload.graphics;
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    const MaterialPassDrawItemPartitionsView opaqueDrawItems = frameSetupReady ? payload.opaqueDrawSnapshot.drawItemsView() : MaterialPassDrawItemPartitionsView{};
    const CsgFrameGpuDataView csgFrameData = frameSetupReady ? payload.opaqueDrawSnapshot.csgFrameDataView() : CsgFrameGpuDataView{};

    const b8 hasDeferredDrawItems = !opaqueDrawItems.empty();
    const b8 deferredResourcesReady = hasDeferredDrawItems && payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.opaqueDrawSnapshot.instanceCount, payload.opaqueDrawSnapshot.materialTypedByteCount);
    const b8 regularDrawResourcesReady = deferredResourcesReady && materialSystem.drawRecorder().materialPassDrawResourcesReady(opaqueDrawItems.regular, payload.frameBindings);
    const MaterialPassDrawItemsView regularDrawItemsForGbuffer = payload.regularSharedComputeEmulationDrawsGraphOwned ? MaterialPassDrawItemsView{ opaqueDrawItems.regular.meshDrawItems, opaqueDrawItems.regular.indexedDrawItems, {} } : opaqueDrawItems.regular;
    const b8 csgResourcesReady = deferredResourcesReady && (!csgFrameData.hasWork() || (payload.csgFrameBuffersUploaded && payload.csgResources.frameReady(csgFrameData) && payload.frameBindings.bindingValid()));
    const b8 csgReceiverSurfaceDrawResourcesReady = csgResourcesReady && (opaqueDrawItems.csgReceiverSurface.empty() || materialSystem.drawRecorder().materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings));
    if(regularDrawResourcesReady && payload.regularSharedComputeEmulationDrawsGraphOwned && (!payload.regularSharedComputeEmulationTiming || payload.regularSharedComputeEmulationTiming->has_value()))
        return false;

    const b8 csgSampleStateReady = csgResourcesReady;
    if(deferredResourcesReady && csgSampleStateReady && csgFrameData.hasWork()){
        // Graph uploads are immutable; native recording consumes them as-is.
        csgSystem.dispatchCsgIntervalPeels(commandList, deferredTargets, csgFrameData, payload.csgResources, payload.frameBindings);
    }

    // Clears begin the first segment; continuations resume with LOAD.
    Core::RenderPassParameters renderPassParameters;
    renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_BASE_COLOR_LOCATION] = ECSRenderDetail::s_ClearColor;
    renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_NORMAL_LOCATION] = ECSRenderDetail::s_GBufferNormalClearColor;
    renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION] = ECSRenderDetail::s_GBufferWorldPositionClearColor;
    renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_SPECULAR_ROUGHNESS_LOCATION] = ECSRenderDetail::s_GBufferSpecularRoughnessClearColor;
    for(u32 attachmentIndex = 0u; attachmentIndex < NWB_MESH_GBUFFER_TARGET_COUNT; ++attachmentIndex)
        renderPassParameters.colorAttachmentActions[attachmentIndex].loadAction = Core::RenderPassLoadAction::Clear;
    renderPassParameters.depthAttachmentActions.loadAction = Core::RenderPassLoadAction::Clear;
    commandList.beginRenderPass(*deferredTargets.framebuffer, renderPassParameters);

    if(deferredResourcesReady){
        const MaterialPassDrawContext opaqueDrawContext{
            commandList,
            deferredTargets.framebuffer.get(),
            nullptr,
            deferredViewportState,
            nullptr,
            payload.frameBindings,
            MaterialPipelinePass::Opaque,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.regularComputeEmulationOutputStatesGraphOwned
        };
        if(regularDrawResourcesReady && (payload.regularSharedComputeEmulationDrawsGraphOwned || !regularDrawItemsForGbuffer.empty())){
            if(payload.regularSharedComputeEmulationDrawsGraphOwned){
                payload.regularSharedComputeEmulationTiming->emplace(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueRegular, graphics.getDevice(), commandList);
                // Close the marker before finalizing this command list.
                if(!Core::FinishSplitGpuTimingMarker(payload.regularSharedComputeEmulationTiming))
                    return false;
                if(!regularDrawItemsForGbuffer.empty())
                    materialSystem.drawRecorder().renderMaterialPassDrawItems(opaqueDrawContext, regularDrawItemsForGbuffer);
            }
            else{
                Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueRegular, graphics.getDevice(), commandList);

                materialSystem.drawRecorder().renderMaterialPassDrawItems(opaqueDrawContext, regularDrawItemsForGbuffer);
            }
        }

        Core::ViewportState csgIntervalViewportState;
        csgIntervalViewportState.addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport()).addScissorRect(ToCoreRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height)));
        const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
            commandList,
            deferredTargets.framebuffer.get(),
            nullptr,
            csgIntervalViewportState,
            &payload.csgResources,
            payload.frameBindings,
            MaterialPipelinePass::CsgReceiverSurface,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.csgReceiverComputeEmulationOutputStatesGraphOwned
        };
        if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
            Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsgReceiverSurface, graphics.getDevice(), commandList);

            materialSystem.drawRecorder().renderMaterialPassDrawItems(csgReceiverSurfaceDrawContext, opaqueDrawItems.csgReceiverSurface);
        }
    }
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> PostGbufferNormalizeBuilder::declare(const PostGbufferNormalizeInputs& inputs, Core::Alloc::ScratchArena& scratchArena){
    using namespace RendererTaskGraphDetail;

    const b8 shadowTraceGeometryStatesGraphOwned = inputs.shadowTraceGeometrySet.valid();
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> normalizeResourceUses{ scratchArena };
    normalizeResourceUses.reserve(s_NormalizeResourceUseCapacity + (shadowTraceGeometryStatesGraphOwned ? 0u : inputs.shadowTraceGeometryResources.size()));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.normal, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.worldPosition, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.specularRoughness, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.depth, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.sceneShading, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.lights, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(inputs.resources.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(const Core::GpuGraphResourceId resource : inputs.shadowTraceGeometryResources){
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        if(!shadowTraceGeometryStatesGraphOwned)
            normalizeResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowTraceGeometrySetUse{ .resourceSet = inputs.shadowTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    normalizeScheduling.forceSubmissionBoundary = false;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name(s_FrameGraphNameRenderGraphicsPrefixNormalize))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setScheduling(normalizeScheduling)
        .setDependencies({ &inputs.gbufferCompletionTask, 1u })
        .setResourceUses(normalizeResourceUses)
        .setResourceSetUses({ shadowTraceGeometryStatesGraphOwned ? &shadowTraceGeometrySetUse : nullptr, shadowTraceGeometryStatesGraphOwned ? 1u : 0u })
    ;
    const auto graphicsPrefixTaskResult = m_graph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{
            .raytracingSystem = m_raytracingSystem,
            .asyncPrefixTiming = &inputs.asyncPrefixTiming,
            .timingTicket = inputs.timingTicket,
            .shadowVisibilityTask = inputs.shadowVisibilityTask,
        }
    );
    if(!graphicsPrefixTaskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare post-G-buffer normalization task"));
        return MakeUnexpected(Failure{});
    }
    return *graphicsPrefixTaskResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredLightingStageResult> DeferredLightingStageBuilder::declare(const DeferredLightingStageInputs& inputs, Core::GpuTimingSubmissionTicket& timingTicket){
    using namespace RendererTaskGraphDetail;
    DeferredLightingStageResult result{};
    if(
        !inputs.albedo.valid()
        || !inputs.normal.valid()
        || !inputs.worldPosition.valid()
        || !inputs.depth.valid()
        || !inputs.shadowVisibility.valid()
        || !inputs.causticIrradiance.valid()
        || !inputs.surfelIrradiance.valid()
        || !inputs.sceneShading.valid()
        || !inputs.lights.valid()
        || !inputs.bindlessSlots.valid()
        || !inputs.opaqueColor.valid()
        || !inputs.graphicsPrefixTask.valid()
        || !inputs.avboitFinalTask.valid()
        || !inputs.shadowVisibilityTask.valid()
        || !inputs.surfelGiTask.valid()
        || (inputs.declaresHardwareCaustics && !inputs.hardwareCausticsTask.valid())
        || (!inputs.declaresHardwareCaustics && !inputs.softwareCausticsTask.valid())
    )
        return MakeUnexpected(Failure{});

    DeferredFrameTargets& deferredTargets = inputs.targets;
    const b8 useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const DeferredLaggedLightingHistoryResources* const history = inputs.history;
    if(useLaggedLightingHistory && !history)
        return MakeUnexpected(Failure{});
    const b8 hasTransparentRenderers = inputs.hasTransparentRenderers;

    // Live Lighting joins current producers via graph edges; lagged Lighting reads history.
    const Array<Core::GpuTaskId, 4u> hardwareLightingDependencies = { inputs.shadowVisibilityTask, inputs.surfelGiTask, inputs.avboitFinalTask, inputs.hardwareCausticsTask, };
    const Array<Core::GpuTaskId, 4u> softwareLightingDependencies = { inputs.shadowVisibilityTask, inputs.softwareCausticsTask, inputs.surfelGiTask, inputs.avboitFinalTask, };
    // Lagged Lighting reads prefix inputs plus history; finalizer restores depth layout first.
    const Array<Core::GpuTaskId, 2u> laggedLightingDependencies = { inputs.graphicsPrefixTask, inputs.avboitFinalTask, };
    const usize laggedLightingDependencyCount = hasTransparentRenderers ? s_TransparentLaggedLightingDependencyCount : 1u;
    const Array<Core::GpuTaskId, 1u> laggedLightingSelectorUploadDependencies = { inputs.graphicsPrefixTask };
    const b8 laggedBindlessSlotsGraphOwned = useLaggedLightingHistory && !history->slotsUploaded;
    Core::GpuTaskId historySlotsUploadTask;
    if(laggedBindlessSlotsGraphOwned){
        const auto laggedBindlessSlotsBlobResult = m_graph.copyUploadData(&history->slots, sizeof(history->slots), alignof(DeferredBindlessResourceSlots));
        if(!laggedBindlessSlotsBlobResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId laggedBindlessSlotsBlob = *laggedBindlessSlotsBlobResult;

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name(s_RenderNameRenderLaggedLightingBindlessSlotsUpload))
            .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
            .setScheduling(uploadScheduling)
            .setDependencies(laggedLightingSelectorUploadDependencies)
        ;
        const auto historySlotsUploadTaskResult = m_graph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = laggedBindlessSlotsBlob,
                .destination = inputs.bindlessSlots,
                // Selector buffers publish Common; Deferred Lighting owns the ConstantBuffer transition in this same packet.
                .finalState = Core::ResourceStates::Common,
            });
        if(!historySlotsUploadTaskResult){
            historySlotsUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        historySlotsUploadTask = *historySlotsUploadTaskResult;
    }
    const Array<Core::GpuTaskId, 3u> laggedLightingWithSelectorDependencies = { inputs.graphicsPrefixTask, historySlotsUploadTask, inputs.avboitFinalTask, };
    const Core::GpuTaskId* const lightingDependencies = inputs.declaresHardwareCaustics
        ? (useLaggedLightingHistory ? laggedLightingDependencies.data() : hardwareLightingDependencies.data())
        : (useLaggedLightingHistory ? laggedLightingDependencies.data() : softwareLightingDependencies.data())
    ;
    const Core::GpuTaskId* const resolvedLightingDependencies = laggedBindlessSlotsGraphOwned ? laggedLightingWithSelectorDependencies.data() : lightingDependencies;
    const usize lightingDependencyCount = laggedBindlessSlotsGraphOwned
        ? (hasTransparentRenderers ? s_TransparentLaggedSelectorDependencyCount : s_OpaqueLaggedSelectorDependencyCount)
        : (useLaggedLightingHistory ? laggedLightingDependencyCount : hardwareLightingDependencies.size())
    ;

    const Array<Core::GpuExternalCompletionId, 1u> laggedLightingExternalDependencies = { inputs.historyReadReadyCompletion, };
    const Core::GpuExternalCompletionId* const lightingExternalDependencies = useLaggedLightingHistory ? laggedLightingExternalDependencies.data() : nullptr;
    const usize lightingExternalDependencyCount = useLaggedLightingHistory ? laggedLightingExternalDependencies.size() : 0u;

    // Active lagged Lighting receives compiler-owned state seeds for its shared prefix inputs while it reads history.
    // Transparent depth is the exception: the finalizer above orders its temporary DepthRead layout before Lighting samples it.
    const Array<Core::GpuTaskResourceUse, 11u> resourceUses = {
        ReadTextureUse(inputs.albedo, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.normal, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(inputs.causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(inputs.surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer),
        ReadUse(inputs.lights, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(inputs.opaqueColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = !laggedBindlessSlotsGraphOwned;
    scheduling.allowPacketMerge = laggedBindlessSlotsGraphOwned;
    scheduling.mergeWithPrevious = laggedBindlessSlotsGraphOwned;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name(s_RenderNameRenderDeferredLighting))
        .setMarkerLabel(RendererGpuPassText::s_DeferredLightingLabel)
        .setScheduling(scheduling)
        .setDependencies({ resolvedLightingDependencies, lightingDependencyCount })
        .setExternalDependencies({ lightingExternalDependencies, lightingExternalDependencyCount })
        .setResourceUses(resourceUses)
    ;
    const auto lightingTaskResult = m_deferredSystem.declareDeferredLightingTask(m_graph, desc, useLaggedLightingHistory, deferredTargets, timingTicket);
    if(!lightingTaskResult){
        result.lightingTask = {};
        return MakeUnexpected(Failure{});
    }
    result.lightingTask = *lightingTaskResult;
    result.historySlotsUploadTask = historySlotsUploadTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeferredPresentGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(context, payload.shadowVisibilityTask);
    const b8 shadowVisibilityRunsOnCompute = shadowVisibilityQueue && shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute;
    if(!payload.presentationFrame.valid() || !payload.backBuffer.valid() || !shadowVisibilityQueue)
        return false;
    const Core::Framebuffer& presentationFramebuffer = *payload.presentationFrame.framebuffer;
    const Core::FramebufferDesc& presentationFramebufferDesc = presentationFramebuffer.getDescription();
    if(
        presentationFramebufferDesc.colorAttachments.size() != 1u
        || presentationFramebufferDesc.colorAttachments[0].texture != payload.presentationFrame.backBuffer.texture.get()
        || context.declarations.textureForResource(payload.backBuffer) != payload.presentationFrame.backBuffer.texture.get()
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(shadowVisibilityRunsOnCompute){
        payload.asyncFinalTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncFinal, payload.graphics.getDevice(), commandList);
        if(!Core::FinishSplitGpuTimingMarker(&payload.asyncFinalTiming))
            return false;
    }

    const b8 presentRecorded = payload.deferredSystem.renderDeferredPresent(commandList, payload.targets, payload.presentationFrame, payload.outputLayer.sampledImage);
    if(shadowVisibilityRunsOnCompute && presentRecorded && payload.asyncFinalTiming.has_value()){
        payload.asyncFinalTiming.value().finishTiming(commandList);
        payload.asyncFinalTiming.reset();
    }
    return presentRecorded;
}

void DeferredPresentGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    AcceptOutputLayer(payload.outputLayerContributor, payload.outputLayer.frameGeneration, token);
}

void DeferredPresentGraphTask::AcceptOutputLayer(Core::IGpuTaskGraphOutputLayerContributor* const contributor, const u64 frameGeneration, const Core::QueueSubmissionToken& token){
    if(contributor && frameGeneration != 0u && token.valid())
        contributor->acceptTaskGraphOutputLayer(frameGeneration, token);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredFrameTailResult> DeferredFrameTailBuilder::declare(const DeferredFrameTailInputs& inputs){
    using namespace RendererTaskGraphDetail;
    DeferredFrameTailResult result{};
    if(!inputs.terminalPresentationTask.valid())
        return MakeUnexpected(Failure{});

    if(inputs.capturesLaggedLightingHistory){
        // The built-in derives whole-resource copy declarations and retains imports; slices stay explicit only in the copy body.
        // Caustic and surfel irradiance copy regions follow the shadow visibility slots.
        Array<Core::GpuCopyTextureTaskRegion, NWB_SCENE_SHADOW_SLOT_COUNT + s_IrradianceHistoryCopyRegionCount> historyCopyRegions = {};
        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::GpuCopyTextureTaskRegion& region = historyCopyRegions[shadowSlot];
            region.source = inputs.historyCopyShadowVisibility;
            region.destination = inputs.historyCopyDestinationShadowVisibility;
            region.sourceSlice.setArraySlice(shadowSlot);
            region.destinationSlice.setArraySlice(shadowSlot);
        }
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].source = inputs.historyCopyCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].destination = inputs.historyCopyDestinationCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].source = inputs.historyCopySurfelIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].destination = inputs.historyCopyDestinationSurfelIrradiance;
        Core::GpuTaskSchedulingHint historyCopyScheduling;
        historyCopyScheduling.cost = Core::GpuTaskCostHint::Medium;
        historyCopyScheduling.forceSubmissionBoundary = true;
        historyCopyScheduling.allowPacketMerge = false;
        const Array<Core::GpuTaskId, 1> historyCopyDependencies = { inputs.terminalPresentationTask };
        Core::GpuTaskDesc historyCopyDesc;
        historyCopyDesc.setIdentity(Name(s_RenderNameRenderLaggedHistoryCopy)).setMarkerLabel("Lagged Lighting History Copy").setScheduling(historyCopyScheduling).setDependencies(historyCopyDependencies);
        const auto historyCopyTaskResult = m_graph.addCopyTextureTask(historyCopyDesc, Core::GpuCopyTextureTaskDesc{ .regions = historyCopyRegions.data(), .regionCount = historyCopyRegions.size(), .acceptedToken = &inputs.historyCopySubmissionToken, });
        if(!historyCopyTaskResult){
            result.historyCopyTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred lagged-lighting history-copy task"));
            return MakeUnexpected(Failure{});
        }
        result.historyCopyTask = *historyCopyTaskResult;
    }

    // Recovery is a late independent Graphics tail.
    // It deliberately has no packet dependency on normal work: a rejected suffix must not prevent it from retiring the accepted frame prefix.
    // Its compiled packet asks the graph transaction to join every accepted non-Graphics physical queue at submit time.
    const auto recoveryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(Name(s_RenderNameRenderFrameRecoveryTiming), "Frame Recovery Timing"));
    if(!recoveryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred frame-recovery graph resources"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuGraphResourceId recoveryDomain = *recoveryDomainResult;

    const Array<Core::GpuTaskResourceUse, 1> recoveryResourceUses = { ReadWriteUse(recoveryDomain, Core::ResourceStates::Common), };
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Core::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc.setIdentity(Name(s_RenderNameRenderFrameRecovery)).setMarkerLabel("Frame Recovery").setScheduling(recoveryScheduling).setResourceUses(recoveryResourceUses);
    const auto recoveryTaskResult = m_graph.addTask<ECSRenderDetail::FrameRecoveryGraphTask>(
        recoveryDesc,
        ECSRenderDetail::FrameRecoveryGraphTask::Payload{ .frameTimingTransaction = inputs.frameTimingTransaction, .armed = inputs.recoveryArmed, .retiresFrameTiming = inputs.recoveryRetiresFrameTiming, });
    if(!recoveryTaskResult){
        result.recoveryTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-recovery graph task"));
        return MakeUnexpected(Failure{});
    }
    result.recoveryTask = *recoveryTaskResult;

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
