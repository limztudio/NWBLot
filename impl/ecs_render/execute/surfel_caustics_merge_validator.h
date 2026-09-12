// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel-GI plus caustics merge validation owns packet-merge checks for GI tasks.
struct SurfelCausticsMergeValidationResult{
    const Core::GpuPhysicalQueueInfo* hardwareCausticsQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiPreparationQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiSnapshotCopyQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiCounterReadbackQueue = nullptr;
    bool surfelGiOutputClearMergedIntoGiPacket = false;
    bool surfelGiPreparedPrefixMergedIntoGiPacket = false;
    bool surfelGiInitializationLifecycleMergedIntoPreparationPacket = false;
    bool causticPhotonMergedIntoCausticsPacket = false;
    bool causticGeometryMergedIntoCausticsPacket = false;
    bool causticResolvePrepareMergedIntoCausticsPacket = false;
    bool causticResolveWaveletMergedIntoCausticsPacket = false;
    bool causticResolveSecondWaveletMergedIntoCausticsPacket = false;
    bool causticResolveThirdWaveletMergedIntoCausticsPacket = false;
    bool causticResolveFourthWaveletMergedIntoCausticsPacket = false;
    bool causticResolveFifthWaveletMergedIntoCausticsPacket = false;
    bool causticResolveUpsampleMergedIntoCausticsPacket = false;
    bool causticIrradianceClearMergedIntoCausticsPacket = false;
    bool causticAccumulatorNonTemporalClearMergedIntoCausticsPacket = false;
    bool causticAccumulatorBootstrapClearMergedIntoCausticsPacket = false;
    bool causticAccumulatorDecayMergedIntoCausticsPacket = false;
    bool surfelGiSnapshotCopyAndTimingPacketsAreDistinct = false;
    bool surfelCounterReadbackFollowsPresentation = false;
    bool laggedLightingHistoryFollowsPresentation = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SurfelCausticsMergeValidator final : NoCopy{
public:
    explicit SurfelCausticsMergeValidator(NotNull<RendererFramePipeline*> pipeline);


public:
    void validate(
        const Core::GpuCompiledGraph::ReadView& compiledPlan,
        Core::GpuTaskId causticsTask,
        Core::GpuTaskId terminalPresentationTask,
        bool captureLaggedLightingHistory,
        SurfelCausticsMergeValidationResult& outResult
    )const;


private:
    NotNull<RendererFramePipeline*> m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
