// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_suffix_builder.h"

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredGraphSuffixBuilder::DeferredGraphSuffixBuilder(
    Core::GpuTaskGraph& graph,
    RendererDeferredSystem& deferredSystem,
    Core::GraphicsRuntime& graphics,
    Core::IGpuTaskGraphPresentationContributor* presentationContributor,
    Core::IGpuTaskGraphOutputLayerContributor* outputLayerContributor
)noexcept
    : m_graph(graph)
    , m_deferredSystem(deferredSystem)
    , m_graphics(graphics)
    , m_presentationContributor(presentationContributor)
    , m_outputLayerContributor(outputLayerContributor){
}

[[nodiscard]] Expected<DeferredGraphSuffixResult> DeferredGraphSuffixBuilder::declare(
    const DeferredGraphSuffixInputs& inputs,
    DeferredFrameTargets& targets,
    const ReflectionCompositeInputs& compositeInputs,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    const Core::GpuTaskId& shadowVisibilityTask,
    Core::GpuTimingFrameTransaction& frameTimingTransaction
){
    using namespace RendererTaskGraphDetail;
    DeferredGraphSuffixResult result{};
    if(
        !inputs.targets
        || !inputs.presentationFrame
        || !inputs.presentationFramebufferDesc
        || !inputs.opaqueColor.valid()
        || !inputs.avboitAccumColor.valid()
        || !inputs.avboitAccumExtinction.valid()
        || !inputs.refractionResolve.valid()
        || !inputs.currentBindlessSlots.valid()
        || !inputs.lightingTask.valid()
        || !inputs.avboitFinalTask.valid()
        || !inputs.refractionResolveTask.valid()
        || !inputs.reflectionGraph.valid()
        || !inputs.presentationFrame->valid()
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
        const bool hasColor = outputLayer.color.valid();
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
            const Core::TextureDesc& outputDesc = inputs.presentationFrame->backBuffer.texture->getDescription();
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
    const auto compositeColorResult = importFirstWriteTexture(targets.compositeColor, Name("render.deferred_composite.composite_color"), "Composite Color");
    const Core::GpuGraphResourceId compositeBindlessSlots = inputs.currentBindlessSlots;
    if(!compositeColorResult || !compositeBindlessSlots.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuGraphResourceId compositeColor = *compositeColorResult;

    const Core::GpuTaskResourceUse compositeResourceUses[] = {
        ReadUse(inputs.opaqueColor),
        ReadUse(inputs.reflectionGraph.opaqueRadiance),
        ReadUse(inputs.reflectionGraph.glassRadiance),
        ReadUse(inputs.avboitAccumColor),
        ReadUse(inputs.avboitAccumExtinction),
        ReadUse(inputs.avboitForegroundColor),
        ReadUse(inputs.avboitForegroundExtinction),
        ReadUse(inputs.refractionResolve),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Core::GpuTaskCostHint::Medium;
    compositeScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    compositeScheduling.forceSubmissionBoundary = false;
    compositeScheduling.allowPacketMerge = true;
    compositeScheduling.mergeWithPrevious = true;
    const Core::GpuTaskId compositeDependencies[] = { inputs.lightingTask, inputs.avboitFinalTask, inputs.refractionResolveTask, inputs.reflectionGraph.completion, };
    Core::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setScheduling(compositeScheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeResourceUses, LengthOf(compositeResourceUses))
    ;
    // Composite may share its compatible serial predecessor while retaining all graph-owned joins.
    const auto resultCompositeTaskResult = m_deferredSystem.declareDeferredCompositeTask(m_graph, compositeDesc, targets, compositeTimingTicket, compositeInputs);
    if(!resultCompositeTaskResult){
        result.compositeTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return MakeUnexpected(Failure{});
    }
    result.compositeTask = *resultCompositeTaskResult;

    const Core::AcquiredPresentationFrame& presentationFrame = *inputs.presentationFrame;
    const Core::FramebufferDesc& presentationFramebufferDesc = *inputs.presentationFramebufferDesc;
    const auto backBufferAvailabilityResult =
        m_graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc{}
                .setIdentity(Name("render.deferred_present.backbuffer_availability"))
                .setMarkerLabel("Presentation Back Buffer Availability")
                .setToken(presentationFrame.backBuffer.availabilityCompletion)
        )
    ;
    if(!backBufferAvailabilityResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import presentation back-buffer availability"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuExternalCompletionId backBufferAvailability = *backBufferAvailabilityResult;

    Core::GpuGraphResourceDesc backBufferDesc = TextureResourceDesc(Name("render.deferred_present.backbuffer"), "Presentation Back Buffer");
    backBufferDesc.setInitialState(presentationFrame.backBuffer.nativeInitialState).setInitialAvailabilityCompletion(backBufferAvailability).setExternalFinalState(Core::ResourceStates::Present);
    const auto backbufferResult = m_graph.importTexture(presentationFrame.backBuffer.texture, backBufferDesc);
    if(!backbufferResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuGraphResourceId backbuffer = *backbufferResult;

    Core::GpuTaskResourceUse presentResourceUses[4u] = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(backbuffer, presentationFramebufferDesc.colorAttachments[0].subresources, Core::ResourceStates::RenderTarget),
    };
    usize presentResourceUseCount = 3u;
    const Core::GpuTaskResourceVersionUse outputLayerVersionUse{ .version = outputLayer.colorVersion, .role = Core::GpuTaskResourceVersionRole::Consume, };
    if(outputLayer.color.valid())
        presentResourceUses[presentResourceUseCount++] = ReadUse(outputLayer.color);

    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    Core::GpuTaskId presentDependencies[4u] = { result.compositeTask };
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
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setScheduling(presentScheduling)
        .setDependencies(presentDependencies, presentDependencyCount)
        .setResourceUses(presentResourceUses, presentResourceUseCount)
        .setResourceVersionUses(&outputLayerVersionUse, outputLayer.color.valid() ? 1u : 0u)
    ;
    const auto resultPresentTaskResult = m_graph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = m_deferredSystem,
            .graphics = m_graphics,
            .targets = targets,
            .presentationFrame = presentationFrame,
            .backBuffer = backbuffer,
            .outputLayer = outputLayer,
            .outputLayerContributor = m_outputLayerContributor,
            .asyncFinalTiming = asyncFinalTiming,
            .timingTicket = presentTimingTicket,
            .shadowVisibilityTask = shadowVisibilityTask,
        }
    );
    if(!resultPresentTaskResult){
        result.presentTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return MakeUnexpected(Failure{});
    }
    result.presentTask = *resultPresentTaskResult;

    // UI/overlay declares before diagnostic tails so timing follows the final contributor.
    result.overlayRequired = m_presentationContributor && m_presentationContributor->hasTaskGraphPresentationWork();
    if(result.overlayRequired){
        const auto resultOverlayTaskResult = m_presentationContributor->declareTaskGraphPresentation(m_graph, presentationFrame, backbuffer, result.presentTask);
        if(!resultOverlayTaskResult){
            result.overlayTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor did not declare its final graph task"));
            return MakeUnexpected(Failure{});
        }
        result.overlayTask = *resultOverlayTaskResult;
    }

    const Core::GpuTaskId frameTimingEndDependency = result.overlayTask.valid() ? result.overlayTask : result.presentTask;
    Core::GpuTaskSchedulingHint frameTimingEndScheduling;
    frameTimingEndScheduling.cost = Core::GpuTaskCostHint::Tiny;
    frameTimingEndScheduling.forceSubmissionBoundary = true;
    frameTimingEndScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc frameTimingEndDesc;
    frameTimingEndDesc.setIdentity(Name("render.frame_timing_end")).setMarkerLabel("Frame Timing End").setScheduling(frameTimingEndScheduling).setDependencies(&frameTimingEndDependency, 1u);
    const auto resultFrameTimingEndTaskResult = m_graph.addTask<FrameTimingEndGraphTask>(frameTimingEndDesc, FrameTimingEndGraphTask::Payload{ .frameTimingTransaction = frameTimingTransaction, });
    if(!resultFrameTimingEndTaskResult){
        result.frameTimingEndTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-timing endpoint graph task"));
        return MakeUnexpected(Failure{});
    }
    result.frameTimingEndTask = *resultFrameTimingEndTaskResult;
    if(!m_graph.declarePresentEndpoint(Core::GpuPresentEndpoint{ .producer = result.frameTimingEndTask, .backBuffer = backbuffer, })){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graph presentation endpoint"));
        return MakeUnexpected(Failure{});
    }

    result.compositeColor = compositeColor;
    result.compositeBindlessSlots = compositeBindlessSlots;
    result.backbuffer = backbuffer;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

