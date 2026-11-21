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
inline constexpr u32 s_InvalidHazardIndex = Limit<u32>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct HazardDomainReaderNode{
    u32 taskIndex = s_InvalidHazardIndex;
    u32 next = s_InvalidHazardIndex;
};

struct HazardDomainAccessState{
    u32 lastWriter = s_InvalidHazardIndex;
    u32 readerHead = s_InvalidHazardIndex;
    u32 readerTail = s_InvalidHazardIndex;
};

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

    void retireCovered(Vector<TrackedResourceAccess, Alloc::ScratchArena>& accesses, const GpuGraphResourceType::Enum resourceType, const GpuTaskResourceRange& range)noexcept{
        if(resourceType == GpuGraphResourceType::HazardDomain || resourceType == GpuGraphResourceType::AccelStruct){
            first = s_InvalidAccess;
            last = s_InvalidAccess;
            return;
        }
        usize previous = s_InvalidAccess;
        for(usize index = first; index != s_InvalidAccess; ){
            const usize next = accesses[index].next;
            if(RangeContainsForType(resourceType, range, accesses[index].range)){
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
    const usize potentialAccessCount = CompilerGraphAccess::TotalResourceUseCount(graph);
    if(potentialAccessCount == 0u)
        return true;

    const usize resourceCount = CompilerGraphAccess::ResourceCount(graph);
    const u64 generation = CompilerGraphAccess::Generation(graph);
    Vector<HazardDomainAccessState, Alloc::ScratchArena> hazardDomainStates(resourceCount, scratchArena);
    Vector<HazardDomainReaderNode, Alloc::ScratchArena> hazardDomainReaders(scratchArena);
    hazardDomainReaders.reserve(potentialAccessCount);
    Vector<TrackedResourceAccessLists, Alloc::ScratchArena> resourceAccesses(scratchArena);
    Vector<TrackedResourceAccess, Alloc::ScratchArena> writers(scratchArena);
    Vector<TrackedResourceAccess, Alloc::ScratchArena> readers(scratchArena);

    for(const GpuTaskId taskID : orderedTasks){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskID.index);
        const Span<const GpuTaskResourceUse> resourceUses = CompilerGraphAccess::TaskResourceUses(graph, task);
        for(const GpuTaskResourceUse& use : resourceUses){
            const GpuGraphResourceType::Enum resourceType = CompilerGraphAccess::ResourceNode(graph, use.resource.index).type;
            if(resourceType == GpuGraphResourceType::HazardDomain){
                HazardDomainAccessState& state = hazardDomainStates[use.resource.index];
                const b8 isRead = IsReadAccess(use.access);
                const b8 isWrite = IsWriteAccess(use.access);
                if(isRead && state.lastWriter != s_InvalidHazardIndex && state.lastWriter != taskID.index){
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{
                        .producer = GpuTaskId{ .generation = generation, .index = state.lastWriter },
                        .consumer = taskID,
                        .resource = use.resource,
                        .resourceVersion = {},
                        .hazard = GpuTaskHazardType::ReadAfterWrite,
                    });
                }
                if(isWrite){
                    if(state.lastWriter != s_InvalidHazardIndex && state.lastWriter != taskID.index){
                        edgeCollector.appendInferred(GpuTaskDependencyEdge{
                            .producer = GpuTaskId{ .generation = generation, .index = state.lastWriter },
                            .consumer = taskID,
                            .resource = use.resource,
                            .resourceVersion = {},
                            .hazard = GpuTaskHazardType::WriteAfterWrite,
                        });
                    }
                    for(u32 readerIndex = state.readerHead; readerIndex != s_InvalidHazardIndex; readerIndex = hazardDomainReaders[readerIndex].next){
                        const u32 readerTask = hazardDomainReaders[readerIndex].taskIndex;
                        if(readerTask != taskID.index){
                            edgeCollector.appendInferred(GpuTaskDependencyEdge{
                                .producer = GpuTaskId{ .generation = generation, .index = readerTask },
                                .consumer = taskID,
                                .resource = use.resource,
                                .resourceVersion = {},
                                .hazard = GpuTaskHazardType::WriteAfterRead,
                            });
                        }
                    }
                    state.lastWriter = taskID.index;
                    state.readerHead = s_InvalidHazardIndex;
                    state.readerTail = s_InvalidHazardIndex;
                }
                else if(isRead && state.lastWriter != taskID.index){
                    FOUNDATION_FATAL_ASSERT_MSG(hazardDomainReaders.size() < s_InvalidHazardIndex, "HazardDomain reader table overflow");
                    const u32 nodeIndex = static_cast<u32>(hazardDomainReaders.size());
                    hazardDomainReaders.push_back(HazardDomainReaderNode{ .taskIndex = taskID.index, .next = s_InvalidHazardIndex });
                    if(state.readerTail == s_InvalidHazardIndex)
                        state.readerHead = nodeIndex;
                    else
                        hazardDomainReaders[state.readerTail].next = nodeIndex;
                    state.readerTail = nodeIndex;
                }
                continue;
            }

            if(resourceAccesses.empty()){
                resourceAccesses.resize(resourceCount);
                writers.reserve(potentialAccessCount);
                readers.reserve(potentialAccessCount);
            }
            Expected<GpuTaskResourceRange> plannedRange = use.range;
            if(resourceType == GpuGraphResourceType::Buffer){
                const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, use.resource.index);
                if(!(plannedRange = ResolveResourceNodeRangeForPlanning(graph, resource, use.resource.index, use.range))){
                    invalidTask = taskID;
                    invalidResource = use.resource;
                    return false;
                }
            }
            TrackedResourceAccessLists& accesses = resourceAccesses[use.resource.index];
            const auto overlaps = [&](const TrackedResourceAccess& access)noexcept{
                return access.task != taskID && RangesOverlapForType(resourceType, access.range, (*plannedRange));
            };

            if(IsReadAccess(use.access)){
                for(usize index = accesses.writers.first; index != s_InvalidAccess; index = writers[index].next){
                    const TrackedResourceAccess& writer = writers[index];
                    if(!overlaps(writer))
                        continue;
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{ .producer = writer.task, .consumer = taskID, .resource = use.resource, .resourceVersion = {}, .hazard = GpuTaskHazardType::ReadAfterWrite, });
                }
            }
            if(IsWriteAccess(use.access)){
                for(usize index = accesses.writers.first; index != s_InvalidAccess; index = writers[index].next){
                    const TrackedResourceAccess& writer = writers[index];
                    if(!overlaps(writer))
                        continue;
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{ .producer = writer.task, .consumer = taskID, .resource = use.resource, .resourceVersion = {}, .hazard = GpuTaskHazardType::WriteAfterWrite, });
                }
                for(usize index = accesses.readers.first; index != s_InvalidAccess; index = readers[index].next){
                    const TrackedResourceAccess& reader = readers[index];
                    if(!overlaps(reader))
                        continue;
                    edgeCollector.appendInferred(GpuTaskDependencyEdge{ .producer = reader.task, .consumer = taskID, .resource = use.resource, .resourceVersion = {}, .hazard = GpuTaskHazardType::WriteAfterRead, });
                }
                accesses.writers.retireCovered(writers, resourceType, (*plannedRange));
                accesses.readers.retireCovered(readers, resourceType, (*plannedRange));
                accesses.writers.append(writers, taskID, (*plannedRange));
            }
            else if(IsReadAccess(use.access)){
                b8 alreadyWrittenByTask = false;
                for(usize index = accesses.writers.first; index != s_InvalidAccess; index = writers[index].next){
                    const TrackedResourceAccess& writer = writers[index];
                    if(writer.task == taskID && RangeContainsForType(resourceType, writer.range, (*plannedRange))){
                        alreadyWrittenByTask = true;
                        break;
                    }
                }
                if(!alreadyWrittenByTask)
                    accesses.readers.append(readers, taskID, (*plannedRange));
            }
        }
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

