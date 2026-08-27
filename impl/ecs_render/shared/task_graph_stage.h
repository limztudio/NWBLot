// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared-output compute emulation alternates one generate and one raster phase for every retained regular
// draw. Keep the supported range in the task-graph contract so task-owning domains do not depend on RendererSystem.
inline constexpr usize s_SharedComputeEmulationPhasesPerDraw = 2u;
inline constexpr usize s_SharedComputeEmulationMinimumDrawCount = 2u;
inline constexpr usize s_SharedComputeEmulationMaximumDrawCount = 5u;
inline constexpr usize s_SharedComputeEmulationMinimumPhaseCount =
    s_SharedComputeEmulationMinimumDrawCount * s_SharedComputeEmulationPhasesPerDraw;
inline constexpr usize s_SharedComputeEmulationMaximumPhaseCount =
    s_SharedComputeEmulationMaximumDrawCount * s_SharedComputeEmulationPhasesPerDraw;

[[nodiscard]] inline constexpr bool IsSupportedSharedComputeEmulationDrawCount(const usize drawCount)noexcept{
    return drawCount >= s_SharedComputeEmulationMinimumDrawCount
        && drawCount <= s_SharedComputeEmulationMaximumDrawCount;
}

[[nodiscard]] inline constexpr usize SharedComputeEmulationPhaseCountForDrawCount(const usize drawCount)noexcept{
    return drawCount * s_SharedComputeEmulationPhasesPerDraw;
}

[[nodiscard]] inline constexpr bool IsSupportedSharedComputeEmulationPhaseCount(const usize phaseCount)noexcept{
    return phaseCount >= s_SharedComputeEmulationMinimumPhaseCount
        && phaseCount <= s_SharedComputeEmulationMaximumPhaseCount
        && (phaseCount % s_SharedComputeEmulationPhasesPerDraw) == 0u;
}

// Immutable frame facts used while declaring the graph. Queue assignment remains a compiler result; this
// carries only the features and external-history availability that change which semantic tasks exist.
struct RendererFrameGraphFeatures{
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryReadReady = false;
    // A prior Transfer history-copy tail still reads the live producer targets. The next shadow/caustic writers
    // must wait for it even when current-frame Lighting does not sample history.
    bool laggedLightingHistoryWriterWaitPending = false;
    bool hasTransparentRenderers = false;
    bool hardwareCaustics = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A domain publishes this narrow boundary only after it has declared its internal task topology. Downstream
// consumers can depend on the semantic stage without becoming coupled to domain-local task identifiers.
struct RendererTaskGraphTransparencyStage{
    Core::GpuTaskId firstTask;
    Core::GpuTaskId completionTask;
    bool hasTransparentTasks = false;

    [[nodiscard]] bool valid()const noexcept{ return firstTask.valid() && completionTask.valid(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

