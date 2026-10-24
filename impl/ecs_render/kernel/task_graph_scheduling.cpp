// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scheduling.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EnableSameFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling, const bool preserveDirectDependency)noexcept{
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    scheduling.preserveSameClassQueueWithDirectDependency = preserveDirectDependency;
}

[[nodiscard]] Core::GpuTaskSchedulingHint SharedComputeEmulationChainScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    return scheduling;
}

void MakeSharedComputeEmulationPhaseTaskDesc(
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses,
    Core::GpuTaskDesc& desc
)noexcept{
    desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setScheduling(SharedComputeEmulationChainScheduling())
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses.empty() ? nullptr : resourceSetUses.data(), resourceSetUses.size())
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

