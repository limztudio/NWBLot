// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_hazard_tracker.h"
#include "compiler_analysis_internal.h"
#include "compiler_internal.h"
#include "inferred_dependency_collector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


namespace __hidden_gpu_resource_hazard_tracker{
using namespace GpuTaskGraphCompilerDetail;

inline constexpr usize s_InvalidAccess = Limit<usize>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TrackedResourceAccess{
    GpuTaskId task;
    GpuTaskResourceRange range;
    usize next = s_InvalidAccess;
};

struct TrackedResourceAccessList{
    usize first = s_InvalidAccess;
    usize last = s_InvalidAccess;


public:
    void append(Vector<TrackedResourceAccess, Alloc::ScratchArena>& accesses, const GpuTaskId task, const GpuTaskResourceRange& range){
        const usize index = accesses.size();
        accesses.push_back(TrackedResourceAccess{ .task = task, .range = range });
        if(last == s_InvalidAccess)
            first = index;
        else
            accesses[last].next = index;
        last = index;
    }

    void retireCovered(Vector<TrackedResourceAccess, Alloc::ScratchArena>& accesses, const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& range)noexcept{
        usize previous = s_InvalidAccess;
        for(usize index = first; index != s_InvalidAccess; ){
            const usize next = accesses[index].next;
            if(RangeContains(resource, range, accesses[index].range)){
                if(previous == s_InvalidAccess)
                    first = next;
                else
                    accesses[previous].next = next;
                if(last == index)
                    last = previous;
            }
            else
                previous = index;
            index = next;
        }
    }
};

struct TrackedResourceAccessLists{
    TrackedResourceAccessList writers;
    TrackedResourceAccessList readers;
};

};


b8 GpuResourceHazardTracker::Append(const GpuTaskGraph::DeclarationReadView& graph, const Span<const GpuTaskId> orderedTasks, Alloc::ScratchArena& scratchArena, GpuInferredDependencyCollector& edgeCollector, GpuTaskId& invalidTask, GpuGraphResourceId& invalidResource){
    using namespace __hidden_gpu_resource_hazard_tracker;
    using namespace GpuTaskGraphCompilerDetail;
    // Process physical resource use in the semantic stable order.
    // Explicit and version edges outrank declaration order; per-resource state emits only the nearest RAW/WAR/WAW deps.
    usize potentialAccessCount = 0u;
    for(const GpuTaskId task : orderedTasks)
        potentialAccessCount += graph.taskAt(task.index).resourceUseCount;
    // Resource-local lists retain active accesses in their original order and unlink overwritten ranges.
    Vector<TrackedResourceAccessLists, Alloc::ScratchArena> resourceAccesses(potentialAccessCount == 0u ? 0u : graph.resourceCount(), scratchArena);
    Vector<TrackedResourceAccess, Alloc::ScratchArena> writers(scratchArena);
    Vector<TrackedResourceAccess, Alloc::ScratchArena> readers(scratchArena);
    writers.reserve(potentialAccessCount);
    readers.reserve(potentialAccessCount);

    for(const GpuTaskId taskID : orderedTasks){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            Expected<GpuTaskResourceRange> plannedRange = use.range;
            if(resource.type == GpuGraphResourceType::Buffer && !(plannedRange = ResolveResourceRangeForPlanning(graph, resource, use.range))){
                invalidTask = task.id;
                invalidResource = use.resource;
                return false;
            }
            TrackedResourceAccessLists& accesses = resourceAccesses[use.resource.index];
            const auto overlaps = [&](const TrackedResourceAccess& access)noexcept{
                return access.task != task.id && RangesOverlap(resource, access.range, (*plannedRange));
            };

            if(IsReadAccess(use.access)){
                for(usize index = accesses.writers.first; index != s_InvalidAccess; index = writers[index].next){
                    const TrackedResourceAccess& writer = writers[index];
                    if(!overlaps(writer))
                        continue;
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{ .producer = writer.task, .consumer = task.id, .resource = use.resource, .resourceVersion = {}, .hazard = GpuTaskHazardType::ReadAfterWrite, });
                }
            }
            if(IsWriteAccess(use.access)){
                for(usize index = accesses.writers.first; index != s_InvalidAccess; index = writers[index].next){
                    const TrackedResourceAccess& writer = writers[index];
                    if(!overlaps(writer))
                        continue;
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{ .producer = writer.task, .consumer = task.id, .resource = use.resource, .resourceVersion = {}, .hazard = GpuTaskHazardType::WriteAfterWrite, });
                }
                for(usize index = accesses.readers.first; index != s_InvalidAccess; index = readers[index].next){
                    const TrackedResourceAccess& reader = readers[index];
                    if(!overlaps(reader))
                        continue;
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{ .producer = reader.task, .consumer = task.id, .resource = use.resource, .resourceVersion = {}, .hazard = GpuTaskHazardType::WriteAfterRead, });
                }
                accesses.writers.retireCovered(writers, resource, (*plannedRange));
                accesses.readers.retireCovered(readers, resource, (*plannedRange));
                accesses.writers.append(writers, task.id, (*plannedRange));
            }
            else if(IsReadAccess(use.access)){
                b8 alreadyWrittenByTask = false;
                for(usize index = accesses.writers.first; index != s_InvalidAccess; index = writers[index].next){
                    const TrackedResourceAccess& writer = writers[index];
                    if(writer.task == task.id && RangeContains(resource, writer.range, (*plannedRange))){
                        alreadyWrittenByTask = true;
                        break;
                    }
                }
                if(!alreadyWrittenByTask)
                    accesses.readers.append(readers, task.id, (*plannedRange));
            }
        }
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

