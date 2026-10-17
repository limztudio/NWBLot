// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clear_chain_builder.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitClearChainBuilder::AvboitClearChainBuilder(Core::GpuTaskGraph& graph, RendererAvboitSystem& avboitSystem)noexcept
    : m_graph(graph)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<AvboitClearChainResult> AvboitClearChainBuilder::declare(const AvboitClearChainInputs& inputs, GraphClearTimingRecordState& clearTimingState){
    AvboitClearChainResult result{};
    if(!inputs.uploadTask.valid())
        return MakeUnexpected(Failure{});

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
            return MakeUnexpected(Failure{});

        Core::GpuTaskSchedulingHint avboitClearScheduling;
        avboitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        avboitClearScheduling.forceSubmissionBoundary = false;
        avboitClearScheduling.allowPacketMerge = true;
        avboitClearScheduling.mergeWithPrevious = true;
        // Clear chain and Occupancy share one AVBOIT Pre timing packet across consumer frontiers.
        avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto makeAvboitClearTaskDesc = [&avboitClearScheduling](const Name identity, const AStringView markerLabel, const Core::GpuTaskId& dependency)noexcept{
            Core::GpuTaskDesc clearDesc;
            clearDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(avboitClearScheduling).setDependencies(&dependency, 1u);
            return clearDesc;
        };
        const auto makeAvboitFloatClearDesc = [](const Core::GpuGraphResourceId destination, const Core::Color& value, const Core::GpuClearTextureTaskRecordHooks& recordHooks = {})noexcept{
            Core::GpuClearTextureTaskDesc clearDesc;
            clearDesc.destination = destination;
            clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
            clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
            clearDesc.floatValue = value;
            clearDesc.recordHooks = recordHooks;
            return clearDesc;
        };
        const Core::GpuClearTextureTaskRecordHooks avboitClearBeginHooks{ .context = &clearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
        const Core::GpuClearTextureTaskRecordHooks avboitClearEndHooks{ .context = &clearTimingState, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
        const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
        const auto avboitClearTaskResult = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(Name("render.avboit.clear.low_raster"), "AVBOIT Clear Low Raster", inputs.uploadTask),
            makeAvboitFloatClearDesc(inputs.lowRaster, transparentBlack, avboitClearBeginHooks)
        );
        if(!avboitClearTaskResult){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult;
        m_avboitSystem.taskGraphStage().m_clearFirstTask = avboitClearTask;
        const auto avboitClearTaskResult2 = m_graph.addClearTextureTask(makeAvboitClearTaskDesc(Name("render.avboit.clear.accum_color"), "AVBOIT Clear Accumulation Color", avboitClearTask), makeAvboitFloatClearDesc(inputs.accumColor, transparentBlack));
        if(!avboitClearTaskResult2){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult2;
        const auto avboitClearTaskResult3 = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(Name("render.avboit.clear.accum_extinction"), "AVBOIT Clear Accumulation Extinction", avboitClearTask),
            makeAvboitFloatClearDesc(inputs.accumExtinction, transparentBlack)
        );
        if(!avboitClearTaskResult3){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult3;
        const Core::GpuGraphResourceId foregroundClearTargets[] = { inputs.foregroundColor, inputs.foregroundExtinction };
        for(const auto target : foregroundClearTargets){
            const auto avboitClearTaskResult4 = m_graph.addClearTextureTask(
                makeAvboitClearTaskDesc(target == inputs.foregroundColor ? Name("render.avboit.clear.foreground_color") : Name("render.avboit.clear.foreground_extinction"), "AVBOIT Clear Foreground", avboitClearTask),
                makeAvboitFloatClearDesc(target, transparentBlack)
            );
            if(!avboitClearTaskResult4){
                avboitClearTask = {};
                return MakeUnexpected(Failure{});
            }
            avboitClearTask = *avboitClearTaskResult4;
        }
        const auto appendAvboitBufferClear = [&](const Name identity, const AStringView markerLabel, const Core::GpuGraphResourceId destination, const u32 value){
            const auto producedTask = m_graph.addClearBufferTask(makeAvboitClearTaskDesc(identity, markerLabel, avboitClearTask), Core::GpuClearBufferTaskDesc{ .destination = destination, .clearValue = value, });
            if(!producedTask){
                avboitClearTask = {};
                return false;
            }
            avboitClearTask = *producedTask;
            return true;
        };
        if(
            !appendAvboitBufferClear(Name("render.avboit.clear.coverage"), "AVBOIT Clear Coverage", inputs.coverage, 0u)
            || !appendAvboitBufferClear(Name("render.avboit.clear.depth_warp"), "AVBOIT Clear Depth Warp", inputs.depthWarp, 0u)
            || !appendAvboitBufferClear(Name("render.avboit.clear.control"), "AVBOIT Clear Control", inputs.control, 0u)
            || !appendAvboitBufferClear(Name("render.avboit.clear.extinction"), "AVBOIT Clear Extinction", inputs.extinction, 0u)
            || !appendAvboitBufferClear(Name("render.avboit.clear.extinction_overflow"), "AVBOIT Clear Extinction Overflow", inputs.extinctionOverflow, NWB_AVBOIT_OVERFLOW_INVALID)
        )
            return MakeUnexpected(Failure{});
        const auto avboitClearTaskResult5 = m_graph.addClearTextureTask(
            makeAvboitClearTaskDesc(Name("render.avboit.clear.transmittance"), "AVBOIT Clear Transmittance", avboitClearTask),
            makeAvboitFloatClearDesc(inputs.transmittance, Core::Color(1.f, 1.f, 1.f, 1.f), avboitClearEndHooks)
        );
        if(!avboitClearTaskResult5){
            avboitClearTask = {};
            return MakeUnexpected(Failure{});
        }
        avboitClearTask = *avboitClearTaskResult5;
        m_avboitSystem.taskGraphStage().m_clearTask = avboitClearTask;
    }
    result.clearTask = avboitClearTask;
    result.clearFirstTask = inputs.clearTargets ? m_avboitSystem.taskGraphStage().m_clearFirstTask : Core::GpuTaskId{};
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

