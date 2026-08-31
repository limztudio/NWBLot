// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_resource_versions{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 BufferRangeEnd(const BufferRange& range)noexcept{
    return range.byteSize == BufferRange::AllBytes ? Limit<u64>::s_Max : range.byteOffset + range.byteSize;
}

[[nodiscard]] static bool ResolvePhysicalRange(
    const GpuTaskGraph& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept{
    using namespace GpuTaskGraphCompilerDetail;

    outRange = range;
    switch(resource.type){
    case GpuGraphResourceType::Texture:
        if(!ResolveTextureRangeForPlanning(graph.textureForResource(resource.id), range, outRange))
            return false;
        return IsValidTextureRange(outRange.textureSubresources);
    case GpuGraphResourceType::Buffer:
        if(!IsValidBufferRange(range.bufferRange))
            return false;
        if(const Buffer* const buffer = graph.bufferForResource(resource.id))
            outRange.bufferRange = range.bufferRange.resolve(buffer->getCreationDescription());
        return IsValidBufferRange(outRange.bufferRange);
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool ResolveVersionRange(
    const GpuTaskGraph& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept{
    if(!ResolvePhysicalRange(graph, resource, range, outRange))
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:
    {
        const Texture* const texture = graph.textureForResource(resource.id);
        if(!texture)
            return true;

        const TextureSubresourceSet& requested = range.textureSubresources;
        const TextureSubresourceSet& resolved = outRange.textureSubresources;
        return requested.baseMipLevel == resolved.baseMipLevel
            && (
                requested.numMipLevels == TextureSubresourceSet::AllMipLevels
                || requested.numMipLevels == resolved.numMipLevels
            )
            && requested.baseArraySlice == resolved.baseArraySlice
            && (
                requested.numArraySlices == TextureSubresourceSet::AllArraySlices
                || requested.numArraySlices == resolved.numArraySlices
            )
        ;
    }
    case GpuGraphResourceType::Buffer:
    {
        const BufferRange& requested = range.bufferRange;
        if(BufferRangeEnd(requested) <= requested.byteOffset)
            return false;
        if(!graph.bufferForResource(resource.id))
            return true;

        const BufferRange& resolved = outRange.bufferRange;
        return requested.byteOffset == resolved.byteOffset
            && (requested.byteSize == BufferRange::AllBytes || requested.byteSize == resolved.byteSize)
        ;
    }
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return range.textureSubresources == s_AllSubresources && range.bufferRange == s_EntireBuffer;
    default:
        return false;
    }
}

[[nodiscard]] static bool RangeContainsVersion(
    const GpuTaskGraph& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& outer,
    const GpuTaskResourceRange& inner
)noexcept{
    GpuTaskResourceRange resolvedOuter;
    GpuTaskResourceRange resolvedInner;
    if(
        !ResolvePhysicalRange(graph, resource, outer, resolvedOuter)
        || !ResolveVersionRange(graph, resource, inner, resolvedInner)
    )
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:
        return GpuTaskGraphCompilerDetail::RangeContains(resource, resolvedOuter, resolvedInner);
    case GpuGraphResourceType::Buffer:
        return resolvedOuter.bufferRange.byteOffset <= resolvedInner.bufferRange.byteOffset
            && BufferRangeEnd(resolvedOuter.bufferRange) >= BufferRangeEnd(resolvedInner.bufferRange)
        ;
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool RangeOverlapsVersion(
    const GpuTaskGraph& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& physicalRange,
    const GpuTaskResourceRange& versionRange
)noexcept{
    GpuTaskResourceRange resolvedPhysical;
    GpuTaskResourceRange resolvedVersion;
    if(
        !ResolvePhysicalRange(graph, resource, physicalRange, resolvedPhysical)
        || !ResolveVersionRange(graph, resource, versionRange, resolvedVersion)
    )
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:
        return GpuTaskGraphCompilerDetail::RangesOverlap(resource, resolvedPhysical, resolvedVersion);
    case GpuGraphResourceType::Buffer:
        return resolvedPhysical.bufferRange.byteOffset < BufferRangeEnd(resolvedVersion.bufferRange)
            && resolvedVersion.bufferRange.byteOffset < BufferRangeEnd(resolvedPhysical.bufferRange)
        ;
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool HasCoveringPhysicalUse(
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task,
    const GpuTaskGraphResourceVersionView& version,
    const GpuTaskResourceVersionRole::Enum role
)noexcept{
    const GpuTaskGraphResourceView resource = graph.resourceAt(version.resource.index);
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& physicalUse = task.resourceUses[useIndex];
        if(
            physicalUse.resource != version.resource
            || (
                role == GpuTaskResourceVersionRole::Produce
                && !GpuTaskGraphCompilerDetail::IsWriteAccess(physicalUse.access)
            )
            || (
                role == GpuTaskResourceVersionRole::Consume
                && !GpuTaskGraphCompilerDetail::IsReadAccess(physicalUse.access)
            )
        )
            continue;
        if(RangeContainsVersion(graph, resource, physicalUse.range, version.range))
            return true;
    }
    return false;
}

[[nodiscard]] static bool HasOverlappingPhysicalWrite(
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task,
    const GpuTaskGraphResourceVersionView& version
)noexcept{
    const GpuTaskGraphResourceView resource = graph.resourceAt(version.resource.index);
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(
            use.resource == version.resource
            && GpuTaskGraphCompilerDetail::IsWriteAccess(use.access)
            && RangeOverlapsVersion(graph, resource, use.range, version.range)
        )
            return true;
    }
    return false;
}

[[nodiscard]] static bool TaskConsumesVersion(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceVersionId version
)noexcept{
    for(usize useIndex = 0u; useIndex < task.resourceVersionUseCount; ++useIndex){
        const GpuTaskResourceVersionUse& use = task.resourceVersionUses[useIndex];
        if(use.version == version && use.role == GpuTaskResourceVersionRole::Consume)
            return true;
    }
    return false;
}

[[nodiscard]] static bool BuildDependencyReachability(
    const Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& edges,
    const usize taskCount,
    Vector<u64, Alloc::ScratchArena>& outReachability,
    usize& outWordCount,
    Alloc::ScratchArena& scratchArena
){
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    outWordCount = taskCount == 0u ? 0u : (taskCount - 1u) / s_BitsPerWord + 1u;
    if(outWordCount != 0u && taskCount > Limit<usize>::s_Max / outWordCount)
        return false;

    outReachability.clear();
    outReachability.resize(taskCount * outWordCount, 0u);
    if(taskCount == 0u)
        return true;

    Vector<usize, Alloc::ScratchArena> outgoingOffsets(taskCount + 1u, scratchArena);
    for(usize taskIndex = 0u; taskIndex <= taskCount; ++taskIndex)
        outgoingOffsets[taskIndex] = 0u;
    for(const GpuTaskDependencyEdge& edge : edges)
        ++outgoingOffsets[edge.producer.index + 1u];
    for(usize taskIndex = 1u; taskIndex <= taskCount; ++taskIndex)
        outgoingOffsets[taskIndex] += outgoingOffsets[taskIndex - 1u];

    Vector<usize, Alloc::ScratchArena> writeOffsets(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        writeOffsets[taskIndex] = outgoingOffsets[taskIndex];

    Vector<usize, Alloc::ScratchArena> outgoingTasks(edges.size(), scratchArena);
    for(const GpuTaskDependencyEdge& edge : edges)
        outgoingTasks[writeOffsets[edge.producer.index]++] = edge.consumer.index;

    Vector<usize, Alloc::ScratchArena> pending(scratchArena);
    pending.reserve(taskCount);
    for(usize sourceIndex = 0u; sourceIndex < taskCount; ++sourceIndex){
        const usize rowOffset = sourceIndex * outWordCount;
        const usize sourceWord = rowOffset + sourceIndex / s_BitsPerWord;
        outReachability[sourceWord] |= static_cast<u64>(1u) << (sourceIndex % s_BitsPerWord);
        pending.clear();
        pending.push_back(sourceIndex);

        for(usize pendingIndex = 0u; pendingIndex < pending.size(); ++pendingIndex){
            const usize producerIndex = pending[pendingIndex];
            for(
                usize edgeIndex = outgoingOffsets[producerIndex];
                edgeIndex < outgoingOffsets[producerIndex + 1u];
                ++edgeIndex
            ){
                const usize consumerIndex = outgoingTasks[edgeIndex];
                const usize consumerWord = rowOffset + consumerIndex / s_BitsPerWord;
                const u64 consumerMask = static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
                if((outReachability[consumerWord] & consumerMask) != 0u)
                    continue;
                outReachability[consumerWord] |= consumerMask;
                pending.push_back(consumerIndex);
            }
        }
    }
    return true;
}

[[nodiscard]] static bool IsDependencyReachable(
    const Vector<u64, Alloc::ScratchArena>& reachability,
    const usize wordCount,
    const GpuTaskId source,
    const GpuTaskId destination
)noexcept{
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    const usize wordIndex = source.index * wordCount + destination.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (destination.index % s_BitsPerWord);
    return (reachability[wordIndex] & mask) != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildResourceVersionDependencyEdges(
    const GpuTaskGraph& graph,
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& outEdges,
    GpuTaskGraphAnalysisDiagnostic& outDiagnostic,
    Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_gpu_task_resource_versions;

    outEdges.clear();
    outDiagnostic = GpuTaskGraphAnalysisDiagnostic{};
    const auto fail = [&](
        const GpuTaskGraphAnalysisStatus::Enum status,
        const GpuTaskId task,
        const GpuTaskId relatedTask,
        const GpuGraphResourceId resource,
        const GpuGraphResourceVersionId version
    ){
        outDiagnostic.status = status;
        outDiagnostic.task = task;
        outDiagnostic.relatedTask = relatedTask;
        outDiagnostic.resource = resource;
        outDiagnostic.resourceVersion = version;
        return false;
    };

    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex){
        const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(versionIndex);
        if(!graph.validResource(version.resource) || version.origin >= GpuGraphResourceVersionOrigin::kCount){
            return fail(
                GpuTaskGraphAnalysisStatus::InvalidResourceVersion,
                {},
                {},
                version.resource,
                version.id
            );
        }

        const GpuTaskGraphResourceView resource = graph.resourceAt(version.resource.index);
        GpuTaskResourceRange resolvedRange;
        if(!ResolveVersionRange(graph, resource, version.range, resolvedRange)){
            return fail(
                GpuTaskGraphAnalysisStatus::InvalidResourceVersion,
                {},
                {},
                version.resource,
                version.id
            );
        }
    }

    Vector<GpuTaskId, Alloc::ScratchArena> producers(graph.resourceVersionCount(), scratchArena);
    usize resourceVersionUseCount = 0u;
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        if(task.resourceVersionUseCount > Limit<usize>::s_Max - resourceVersionUseCount)
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id, {}, {}, {});
        resourceVersionUseCount += task.resourceVersionUseCount;
        for(usize useIndex = 0u; useIndex < task.resourceVersionUseCount; ++useIndex){
            const GpuTaskResourceVersionUse& use = task.resourceVersionUses[useIndex];
            if(!graph.validResourceVersion(use.version)){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse,
                    task.id,
                    {},
                    {},
                    use.version
                );
            }
            const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(use.version.index);
            if(use.role >= GpuTaskResourceVersionRole::kCount){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse,
                    task.id,
                    {},
                    version.resource,
                    use.version
                );
            }

            for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                if(task.resourceVersionUses[previousUseIndex].version == use.version){
                    return fail(
                        GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse,
                        task.id,
                        {},
                        version.resource,
                        use.version
                    );
                }
            }
            if(!HasCoveringPhysicalUse(graph, task, version, use.role)){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse,
                    task.id,
                    {},
                    version.resource,
                    use.version
                );
            }
            if(use.role != GpuTaskResourceVersionRole::Produce)
                continue;
            if(version.origin != GpuGraphResourceVersionOrigin::TaskProduced){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse,
                    task.id,
                    {},
                    version.resource,
                    use.version
                );
            }
            if(producers[use.version.index].valid()){
                return fail(
                    GpuTaskGraphAnalysisStatus::DuplicateResourceVersionProducer,
                    task.id,
                    producers[use.version.index],
                    version.resource,
                    use.version
                );
            }
            producers[use.version.index] = task.id;
        }
    }

    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex){
        const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(versionIndex);
        if(version.origin == GpuGraphResourceVersionOrigin::TaskProduced && !producers[versionIndex].valid()){
            return fail(
                GpuTaskGraphAnalysisStatus::MissingResourceVersionProducer,
                {},
                {},
                version.resource,
                version.id
            );
        }
    }
    if(graph.resourceVersionCount() == 0u)
        return true;
    outEdges.reserve(resourceVersionUseCount);

    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceVersionUseCount; ++useIndex){
            const GpuTaskResourceVersionUse& use = task.resourceVersionUses[useIndex];
            if(use.role != GpuTaskResourceVersionRole::Consume)
                continue;

            const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(use.version.index);
            if(version.origin == GpuGraphResourceVersionOrigin::ImportedRoot)
                continue;
            outEdges.push_back(GpuTaskDependencyEdge{
                .producer = producers[use.version.index],
                .consumer = task.id,
                .resource = version.resource,
                .resourceVersion = version.id,
                .hazard = GpuTaskHazardType::VersionDependency,
            });
        }
    }

    usize semanticEdgeCount = outEdges.size();
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        if(task.dependencyCount > Limit<usize>::s_Max - semanticEdgeCount)
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id, {}, {}, {});
        semanticEdgeCount += task.dependencyCount;
    }

    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena> semanticEdges(scratchArena);
    semanticEdges.reserve(semanticEdgeCount);
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            semanticEdges.push_back(GpuTaskDependencyEdge{
                .producer = task.dependencies[dependencyIndex],
                .consumer = task.id,
                .resource = {},
                .resourceVersion = {},
                .hazard = GpuTaskHazardType::Explicit,
            });
        }
    }
    for(const GpuTaskDependencyEdge& edge : outEdges)
        semanticEdges.push_back(edge);

    Vector<GpuTaskId, Alloc::ScratchArena> consumers(scratchArena);
    consumers.reserve(graph.taskCount());

    // Imported roots precede every graph task, so their consumers must finish before any other overlapping writer.
    // These constraints are unconditional and participate in produced-version reachability regardless of version
    // declaration order.
    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex){
        const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(versionIndex);
        if(version.origin != GpuGraphResourceVersionOrigin::ImportedRoot)
            continue;

        consumers.clear();
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            if(TaskConsumesVersion(task, version.id))
                consumers.push_back(task.id);
        }
        if(consumers.empty())
            continue;

        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView writer = graph.taskAt(taskIndex);
            if(!HasOverlappingPhysicalWrite(graph, writer, version))
                continue;

            for(const GpuTaskId consumer : consumers){
                if(consumer == writer.id)
                    continue;
                const GpuTaskDependencyEdge edge{
                    .producer = consumer,
                    .consumer = writer.id,
                    .resource = version.resource,
                    .resourceVersion = version.id,
                    .hazard = GpuTaskHazardType::VersionLifetime,
                };
                outEdges.push_back(edge);
                semanticEdges.push_back(edge);
            }
        }
    }

    Vector<u64, Alloc::ScratchArena> semanticReachability(scratchArena);
    usize semanticReachabilityWordCount = 0u;
    if(!BuildDependencyReachability(
        semanticEdges,
        graph.taskCount(),
        semanticReachability,
        semanticReachabilityWordCount,
        scratchArena
    ))
        return fail(GpuTaskGraphAnalysisStatus::InvalidTask, {}, {}, {}, {});

    // A produced version permits an overlapping writer before its producer only when the declared semantic graph or
    // an imported-root lifetime already proves that order. Otherwise all version consumers precede the writer. Do
    // not feed these selected produced-version constraints back into later classification: doing so would guess an
    // order between independent produced values from version declaration order rather than requiring task intent.
    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex){
        const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(versionIndex);
        if(version.origin != GpuGraphResourceVersionOrigin::TaskProduced)
            continue;

        consumers.clear();
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            if(TaskConsumesVersion(task, version.id))
                consumers.push_back(task.id);
        }
        if(consumers.empty())
            continue;

        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView writer = graph.taskAt(taskIndex);
            if(
                writer.id == producers[versionIndex]
                || !HasOverlappingPhysicalWrite(graph, writer, version)
                || IsDependencyReachable(
                    semanticReachability,
                    semanticReachabilityWordCount,
                    writer.id,
                    producers[versionIndex]
                )
            )
                continue;

            for(const GpuTaskId consumer : consumers){
                if(consumer == writer.id)
                    continue;
                outEdges.push_back(GpuTaskDependencyEdge{
                    .producer = consumer,
                    .consumer = writer.id,
                    .resource = version.resource,
                    .resourceVersion = version.id,
                    .hazard = GpuTaskHazardType::VersionLifetime,
                });
            }
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

