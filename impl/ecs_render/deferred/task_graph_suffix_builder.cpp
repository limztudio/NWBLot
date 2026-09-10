// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/task_graph_suffix_builder.h>

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredGraphSuffixBuilder::DeferredGraphSuffixBuilder(
    Core::GpuTaskGraph& graph,
    RendererDeferredSystem& deferredSystem,
    Core::GraphicsRuntime& graphics,
    Core::IGpuTaskGraphPresentationContributor* presentationContributor
)
    : m_graph(graph)
    , m_deferredSystem(deferredSystem)
    , m_graphics(graphics)
    , m_presentationContributor(presentationContributor){
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
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
        .setQueue(ComputeQueueRequest())
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import presentation back-buffer availability"));
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return false;
    }

    const Core::GpuTaskResourceUse presentResourceUses[] = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(
            backbuffer,
            presentationFramebufferDesc.colorAttachments[0].subresources,
            Core::ResourceStates::RenderTarget
        ),
    };
    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = inputs.useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    const Core::GpuTaskId presentDependencies[] = {
        outResult.compositeTask,
        inputs.surfelGiTask,
    };
    const usize presentDependencyCount = inputs.useLaggedLightingHistory ? LengthOf(presentDependencies) : 1u;
    Core::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(presentScheduling)
        .setDependencies(presentDependencies, presentDependencyCount)
        .setResourceUses(presentResourceUses, LengthOf(presentResourceUses))
    ;
    outResult.presentTask = m_graph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &targets,
            .presentationFrame = presentationFrame,
            .backBuffer = backbuffer,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &presentTimingTicket,
            .shadowVisibilityTask = &shadowVisibilityTask,
        }
    );
    if(!outResult.presentTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
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
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: presentation contributor did not declare its final graph task"));
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
        .setQueue(GraphicsQueueRequest())
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-timing endpoint graph task"));
        return false;
    }
    if(!m_graph.declarePresentEndpoint(Core::GpuPresentEndpoint{
        .producer = outResult.frameTimingEndTask,
        .backBuffer = backbuffer,
    })){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred graph presentation endpoint"));
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

