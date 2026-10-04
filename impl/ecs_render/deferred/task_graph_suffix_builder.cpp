// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/task_graph_suffix_builder.h>

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>

#include <core/graphics/backend_selection/backend.h>

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredGraphSuffixBuilder::DeferredGraphSuffixBuilder(
    Core::GpuTaskGraph& graph,
    RendererDeferredSystem& deferredSystem,
    Core::GraphicsRuntime& graphics,
    Core::IGpuTaskGraphPresentationContributor* presentationContributor,
    Core::IGpuTaskGraphOutputLayerContributor* outputLayerContributor
)
    : m_graph(graph)
    , m_deferredSystem(deferredSystem)
    , m_graphics(graphics)
    , m_presentationContributor(presentationContributor)
    , m_outputLayerContributor(outputLayerContributor){
}

[[nodiscard]] bool DeferredGraphSuffixBuilder::declare(
    const DeferredGraphSuffixInputs& inputs,
    DeferredFrameTargets& targets,
    const ReflectionCompositeInputs& compositeInputs,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    const Core::GpuTaskId& shadowVisibilityTask,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    DeferredGraphSuffixResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = DeferredGraphSuffixResult{};
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
        return false;

    Core::GpuTaskGraphOutputLayer outputLayer;
    if(m_outputLayerContributor && !m_outputLayerContributor->declareTaskGraphOutputLayer(m_graph, outputLayer)){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: output layer did not declare its graph work"));
        return false;
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
        const bool hasColor = outputLayer.color.valid();
        if(
            !declarations.valid()
            || !outputLayer.validShape()
            || (outputLayer.readyTask.valid() && !declarations.validTask(outputLayer.readyTask))
        )
            return false;
        if(hasColor){
            if(!declarations.validResource(outputLayer.color) || !declarations.validResourceVersion(outputLayer.colorVersion))
                return false;
            const Core::GpuTaskGraphResourceVersionView version = declarations.resourceVersionAt(outputLayer.colorVersion.index);
            const Core::Texture* const texture = declarations.textureForResource(outputLayer.color);
            if(!texture || version.resource != outputLayer.color || version.origin != Core::GpuGraphResourceVersionOrigin::TaskProduced)
                return false;
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
                return false;
        }
    }

    const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);
        desc.setInitialState(Core::ResourceStates::Unknown);
        return m_graph.importTexture(texture, desc);
    };
    const Core::GpuGraphResourceId compositeColor = importFirstWriteTexture(
        targets.compositeColor,
        Name("render.deferred_composite.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId compositeBindlessSlots = inputs.currentBindlessSlots;
    if(
        !compositeColor.valid()
        || !compositeBindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return false;
    }

    const Core::GpuTaskResourceUse compositeResourceUses[] = {
        ReadUse(inputs.opaqueColor),
        ReadUse(inputs.reflectionGraph.opaqueRadiance),
        ReadUse(inputs.reflectionGraph.glassRadiance),
        ReadUse(inputs.avboitAccumColor),
        ReadUse(inputs.avboitAccumExtinction),
        ReadUse(inputs.avboitForegroundColor),
        ReadUse(inputs.avboitForegroundExtinction),
        ReadUse(inputs.refractionResolve),
        ReadUse(
            compositeBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Core::GpuTaskCostHint::Medium;
    compositeScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    compositeScheduling.forceSubmissionBoundary = true;
    compositeScheduling.allowPacketMerge = false;
    const Core::GpuTaskId compositeDependencies[] = {
        inputs.lightingTask,
        inputs.avboitFinalTask,
        inputs.refractionResolveTask,
        inputs.reflectionGraph.completion,
    };
    Core::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setScheduling(compositeScheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeResourceUses, LengthOf(compositeResourceUses))
    ;
    // Composite remains a distinct packet joining graph-owned AVBOIT and Lighting.
    outResult.compositeTask = m_deferredSystem.declareDeferredCompositeTask(
        m_graph,
        compositeDesc,
        targets,
        compositeTimingTicket,
        compositeInputs
    );
    if(!outResult.compositeTask.valid()){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return false;
    }

    const Core::AcquiredPresentationFrame& presentationFrame = *inputs.presentationFrame;
    const Core::FramebufferDesc& presentationFramebufferDesc = *inputs.presentationFramebufferDesc;
    const Core::GpuExternalCompletionId backBufferAvailability =
        m_graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc{}
                .setIdentity(Name("render.deferred_present.backbuffer_availability"))
                .setMarkerLabel("Presentation Back Buffer Availability")
                .setToken(presentationFrame.backBuffer.availabilityCompletion)
        )
    ;
    if(!backBufferAvailability.valid()){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not import presentation back-buffer availability"));
        return false;
    }

    Core::GpuGraphResourceDesc backBufferDesc = TextureResourceDesc(
        Name("render.deferred_present.backbuffer"),
        "Presentation Back Buffer"
    );
    backBufferDesc
        .setInitialState(presentationFrame.backBuffer.nativeInitialState)
        .setInitialAvailabilityCompletion(backBufferAvailability)
        .setExternalFinalState(Core::ResourceStates::Present)
    ;
    const Core::GpuGraphResourceId backbuffer = m_graph.importTexture(
        presentationFrame.backBuffer.texture,
        backBufferDesc
    );
    if(!backbuffer.valid()){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return false;
    }

    Core::GpuTaskResourceUse presentResourceUses[4u] = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(
            backbuffer,
            presentationFramebufferDesc.colorAttachments[0].subresources,
            Core::ResourceStates::RenderTarget
        ),
    };
    usize presentResourceUseCount = 3u;
    const Core::GpuTaskResourceVersionUse outputLayerVersionUse{
        .version = outputLayer.colorVersion,
        .role = Core::GpuTaskResourceVersionRole::Consume,
    };
    if(outputLayer.color.valid())
        presentResourceUses[presentResourceUseCount++] = ReadUse(outputLayer.color);

    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    Core::GpuTaskId presentDependencies[3u] = { outResult.compositeTask };
    usize presentDependencyCount = 1u;
    if(inputs.useLaggedLightingHistory)
        presentDependencies[presentDependencyCount++] = inputs.surfelGiTask;
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
    outResult.presentTask = m_graph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &targets,
            .presentationFrame = presentationFrame,
            .backBuffer = backbuffer,
            .outputLayer = outputLayer,
            .outputLayerContributor = m_outputLayerContributor,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &presentTimingTicket,
            .shadowVisibilityTask = &shadowVisibilityTask,
        }
    );
    if(!outResult.presentTask.valid()){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return false;
    }

    // UI/overlay declares before diagnostic tails so timing follows the final contributor.
    outResult.overlayRequired =
        m_presentationContributor
        && m_presentationContributor->hasTaskGraphPresentationWork()
    ;
    if(outResult.overlayRequired){
        outResult.overlayTask = m_presentationContributor->declareTaskGraphPresentation(
            m_graph,
            presentationFrame,
            backbuffer,
            outResult.presentTask
        );
        if(!outResult.overlayTask.valid()){
            NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: presentation contributor did not declare its final graph task"));
            return false;
        }
    }

    const Core::GpuTaskId frameTimingEndDependency = outResult.overlayTask.valid()
        ? outResult.overlayTask
        : outResult.presentTask
    ;
    Core::GpuTaskSchedulingHint frameTimingEndScheduling;
    frameTimingEndScheduling.cost = Core::GpuTaskCostHint::Tiny;
    frameTimingEndScheduling.forceSubmissionBoundary = true;
    frameTimingEndScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc frameTimingEndDesc;
    frameTimingEndDesc
        .setIdentity(Name("render.frame_timing_end"))
        .setMarkerLabel("Frame Timing End")
        .setScheduling(frameTimingEndScheduling)
        .setDependencies(&frameTimingEndDependency, 1u)
    ;
    outResult.frameTimingEndTask = m_graph.addTask<FrameTimingEndGraphTask>(
        frameTimingEndDesc,
        FrameTimingEndGraphTask::Payload{
            .frameTimingTransaction = &frameTimingTransaction,
        }
    );
    if(!outResult.frameTimingEndTask.valid()){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not declare deferred frame-timing endpoint graph task"));
        return false;
    }
    if(!m_graph.declarePresentEndpoint(Core::GpuPresentEndpoint{
        .producer = outResult.frameTimingEndTask,
        .backBuffer = backbuffer,
    })){
        NWB_LOGGER_WARNING(GLOBAL_TEXT("RendererSystem: could not declare deferred graph presentation endpoint"));
        return false;
    }

    outResult.compositeColor = compositeColor;
    outResult.compositeBindlessSlots = compositeBindlessSlots;
    outResult.backbuffer = backbuffer;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

