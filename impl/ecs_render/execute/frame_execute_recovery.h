// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Recovers a partly accepted deferred frame: runs the graph-owned recovery packet, then rejects the remaining normal packets.
class FrameExecuteRecovery final : NoCopy{
public:
    FrameExecuteRecovery(RendererFramePipeline& renderer, Core::GpuTimingFrameTransaction& frameTimingTransaction, const Core::GpuCompiledGraph::ReadView& compiledPlan)noexcept
        : m_renderer(renderer)
        , m_frameTimingTransaction(frameTimingTransaction)
        , m_compiledPlan(compiledPlan)
    {}


public:
    [[nodiscard]] bool discardUnacceptedGraphPackets();
    [[nodiscard]] bool executeFrameRecoveryTask();
    [[nodiscard]] bool recoverPendingFrameSubmission();
    [[nodiscard]] bool recoverPendingFrameThenDiscardUnaccepted();
    void failFrameRenderRecovery();


private:
    RendererFramePipeline& m_renderer;
    Core::GpuTimingFrameTransaction& m_frameTimingTransaction;
    const Core::GpuCompiledGraph::ReadView& m_compiledPlan;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

