// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/packet_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;
struct RendererAvboitTaskGraphValidation;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// GPU timing tickets, measures, and clear-timing bindings for one deferred frame. Members point at each other, so the state stays in place.
struct FrameExecuteTimingState final : NoCopy{
    static constexpr usize s_GraphicsPrefixTimingTicketCount = static_cast<usize>(ECSRenderDetail::DeferredGraphicsPrefixTimingSlot::kCount);

    // Prefix stages start separate; shared packets rebind to one ticket.
    Core::GpuTimingSubmissionTicket shadowPrepareTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixMeshViewSetupTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixSceneShadingSetupTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixDeferredClearTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixGbufferTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixCsgReceiverSpanTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixCsgIntervalCombineTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixCsgIntervalSampleTimingTicket;
    Core::GpuTimingSubmissionTicket graphicsPrefixNormalizeTimingTicket;
    Core::GpuTimingSubmissionTicket* graphicsPrefixTimingTickets[s_GraphicsPrefixTimingTicketCount];
    Core::GpuTimingSubmissionTicket* const graphicsPrefixOwnedTimingTickets[s_GraphicsPrefixTimingTicketCount];
    // AsyncPrefix is valid only when the endpoints share one submission.
    bool asyncPrefixTimingSpansOnePacket = true;
    Optional<Core::GpuTimingMeasure> asyncPrefixTiming;
    Optional<Core::GpuTimingMeasure> deferredClearTiming;
    GraphClearTimingRecordState deferredClearTimingState;
    Optional<Core::GpuTimingMeasure> opaqueCsgIntervalClearTiming;
    GraphClearTimingRecordState opaqueCsgIntervalClearTimingState;
    // Keep this measurement alive like CSG intervals.
    Optional<Core::GpuTimingMeasure> opaqueRegularSharedComputeEmulationTiming;
    // Interval-sample compute/raster may span two callbacks.
    Optional<Core::GpuTimingMeasure> opaqueCsgIntervalSampleComputeEmulationTiming;
    Core::GpuTimingSubmissionTicket shadowVisibilityTimingTicket;
    // The terminal fold still closes the Shadow Visibility range.
    Optional<Core::GpuTimingMeasure> shadowVisibilityAsyncTiming;
    Optional<Core::GpuTimingMeasure> shadowVisibilityTiming;
    Optional<Core::GpuTimingMeasure> opaqueSoftResolveTiming;
    Optional<Core::GpuTimingMeasure> transparentSoftResolveTiming;
    Core::GpuTimingSubmissionTicket softwareCausticsTimingTicket;
    Core::GpuTimingSubmissionTicket surfelGiTimingTicket;
    // Age/free opens it; the GI callback closes it.
    Optional<Core::GpuTimingMeasure> surfelGiAsyncTiming;
    Core::GpuTimingSubmissionTicket hardwareCausticsTimingTicket;
    // Decay opens it; the photon producer closes it.
    Optional<Core::GpuTimingMeasure> causticPhotonTiming;
    // Downsample opens it; wavelet resolve closes it.
    Optional<Core::GpuTimingMeasure> causticResolveTiming;
    Core::GpuTimingSubmissionTicket avboitPreTimingTicket;
    Core::GpuTimingSubmissionTicket avboitDepthWarpTimingTicket;
    Core::GpuTimingSubmissionTicket avboitExtinctionTimingTicket;
    Core::GpuTimingSubmissionTicket avboitIntegrationTimingTicket;
    Core::GpuTimingSubmissionTicket avboitAccumulationTimingTicket;
    Optional<Core::GpuTimingMeasure> avboitClearTiming;
    GraphClearTimingRecordState avboitClearTimingState;
    Optional<Core::GpuTimingMeasure> transparentCsgIntervalClearTiming;
    GraphClearTimingRecordState transparentCsgIntervalClearTimingState;
    // Pre opens it; Combine closes it.
    Optional<Core::GpuTimingMeasure> transparentCsgIntervalsTiming;
    // Producer opens these; the raster consumer closes them.
    Optional<Core::GpuTimingMeasure> avboitOccupancyComputeEmulationTiming;
    Optional<Core::GpuTimingMeasure> avboitExtinctionComputeEmulationTiming;
    Optional<Core::GpuTimingMeasure> avboitAccumulationComputeEmulationTiming;
    Core::GpuTimingSubmissionTicket deferredLightingTimingTicket;
    Core::GpuTimingSubmissionTicket deferredCompositeTimingTicket;
    Core::GpuTimingSubmissionTicket deferredPresentTimingTicket;
    // Publish the endpoint only after Present accepts.
    Core::GpuTimingFrameTransaction frameTimingTransaction;
    Optional<Core::GpuTimingMeasure> asyncFinalTiming;


public:
    explicit FrameExecuteTimingState(Core::GraphicsRuntime& graphics)noexcept;


public:
    void discardGraphicsPrefixTickets();
    void discardTickets();
    // Drops every open measure, the frame transaction, and all tickets.
    void discardAll();

    // Binds each normal-execution task to its ticket; prefix tasks that share a packet use the first task's ticket. Returns the binding count.
    [[nodiscard]] Expected<usize> bindNormalTimingTickets(
        const RendererFramePipeline& renderer,
        const Core::GpuCompiledGraph::ReadView& compiledPlan,
        const Core::GpuTaskId (&graphicsPrefixTimingTasks)[s_GraphicsPrefixTimingTicketCount],
        usize graphicsPrefixUniquePacketCount,
        const RendererAvboitTaskGraphValidation& avboitValidation,
        bool hardwareShadowSupported,
        usize ticketCapacity,
        Core::GpuTaskGraphTaskTimingTicket* tickets
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

