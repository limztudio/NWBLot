// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/clear_chain_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitClearChainBuilder::AvboitClearChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererAvboitSystem& avboitSystem
)
    : m_graph(graph)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitClearChainBuilder::declare(
    const AvboitClearChainInputs& inputs,
    GraphClearTimingRecordState& clearTimingState,
    AvboitClearChainResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = AvboitClearChainResult{};
    if(!inputs.uploadTask.valid())
        return false;

    // Keep native order: uploads, serial target clears, then occupancy; graph owns all CopyDest ops.
    Core::GpuTaskId avboitClearTask = inputs.uploadTask;
    if(inputs.clearTargets){
        if(
            !inputs.lowRaster.valid()
            || !inputs.accumColor.valid()
            || !inputs.accumExtinction.valid()
            || !inputs.foregroundColor.valid()
            || !inputs.foregroundExtinction.valid()
            || !inputs.transmittance.valid()
            || !inputs.coverage.valid()
            || !inputs.depthWarp.valid()
            || !inputs.control.valid()
            || !inputs.extinction.valid()
            || !inputs.extinctionOverflow.valid()
        )
            return false;

        Core::GpuTaskSchedulingHint avboitClearScheduling;
        avboitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        avboitClearScheduling.forceSubmissionBoundary = false;
        avboitClearScheduling.allowPacketMerge = true;
        avboitClearScheduling.mergeWithPrevious = true;
        // Clear chain and Occupancy share one AVBOIT Pre timing packet across consumer frontiers.
        avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto makeAvboitClearTaskDesc = [&avboitClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(avboitClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const auto makeAvboitFloatClearDesc = [](
            const Core::GpuGraphResourceId destination,
            const Core::Color& value,
            const Core::GpuClearTextureTaskRecordHooks& recordHooks = {}
        ){
            Core::GpuClearTextureTaskDesc clearDesc;
            clearDesc.destination = destination;
            clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
            clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
            clearDesc.floatValue = value;
            clearDesc.recordHooks = recordHooks;
            return clearDesc;
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearBeginHooks{
            .context = &clearTimingState,
            .beforeClear = &BeginGraphClearTimingRecord,
            .discarded = &DiscardGraphClearTimingRecord,
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearEndHooks{
            .context = &clearTimingState,
            .afterClear = &EndGraphClearTimingRecord,
            .discarded = &DiscardGraphClearTimingRecord,
        };
        const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
        avboitClearTask = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.low_raster"),
                "AVBOIT Clear Low Raster",
                inputs.uploadTask
            ),
            makeAvboitFloatClearDesc(inputs.lowRaster, transparentBlack, avboitClearBeginHooks)
        );
        if(!avboitClearTask.valid())
            return false;
        m_avboitSystem.taskGraphStage().m_clearFirstTask = avboitClearTask;
        avboitClearTask = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.accum_color"),
                "AVBOIT Clear Accumulation Color",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(inputs.accumColor, transparentBlack)
        );
        if(!avboitClearTask.valid())
            return false;
        avboitClearTask = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.accum_extinction"),
                "AVBOIT Clear Accumulation Extinction",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(inputs.accumExtinction, transparentBlack)
        );
        if(!avboitClearTask.valid())
            return false;
        const Core::GpuGraphResourceId foregroundClearTargets[] = { inputs.foregroundColor, inputs.foregroundExtinction };
        for(const auto target : foregroundClearTargets){
            avboitClearTask = m_graph.addClearTextureTask(
                makeAvboitClearTaskDesc(target == inputs.foregroundColor
                    ? Name("render.avboit.clear.foreground_color") : Name("render.avboit.clear.foreground_extinction"),
                    "AVBOIT Clear Foreground", avboitClearTask),
                makeAvboitFloatClearDesc(target, transparentBlack));
            if(!avboitClearTask.valid())
                return false;
        }
        const auto appendAvboitBufferClear = [&](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuGraphResourceId destination,
            const u32 value
        ){
            avboitClearTask = m_graph.addClearBufferTask(
                makeAvboitClearTaskDesc(identity, markerLabel, avboitClearTask),
                Core::GpuClearBufferTaskDesc{
                    .destination = destination,
                    .clearValue = value,
                }
            );
            return avboitClearTask.valid();
        };
        if(
            !appendAvboitBufferClear(
                Name("render.avboit.clear.coverage"),
                "AVBOIT Clear Coverage",
                inputs.coverage,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.depth_warp"),
                "AVBOIT Clear Depth Warp",
                inputs.depthWarp,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.control"),
                "AVBOIT Clear Control",
                inputs.control,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.extinction"),
                "AVBOIT Clear Extinction",
                inputs.extinction,
                0u
            )
            || !appendAvboitBufferClear(
                Name("render.avboit.clear.extinction_overflow"),
                "AVBOIT Clear Extinction Overflow",
                inputs.extinctionOverflow,
                NWB_AVBOIT_OVERFLOW_INVALID
            )
        )
            return false;
        avboitClearTask = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(
                Name("render.avboit.clear.transmittance"),
                "AVBOIT Clear Transmittance",
                avboitClearTask
            ),
            makeAvboitFloatClearDesc(
                inputs.transmittance,
                Core::Color(1.f, 1.f, 1.f, 1.f),
                avboitClearEndHooks
            )
        );
        if(!avboitClearTask.valid())
            return false;
        m_avboitSystem.taskGraphStage().m_clearTask = avboitClearTask;
    }
    outResult.clearTask = avboitClearTask;
    outResult.clearFirstTask = inputs.clearTargets
        ? m_avboitSystem.taskGraphStage().m_clearFirstTask
        : Core::GpuTaskId{}
    ;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
