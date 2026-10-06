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
    Core::GpuTaskDesc& desc,
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
    const Core::GpuTaskResourceSetUse* const resourceSetUses,
    const usize resourceSetUseCount){
    desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setScheduling(SharedComputeEmulationChainScheduling())
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses, resourceSetUseCount)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

