// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_resource_versions{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<GpuTaskResourceRange> ResolvePhysicalRange(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range
)noexcept{
    using namespace GpuTaskGraphCompilerDetail;

    GpuTaskResourceRange result = range;
    switch(resource.type){
    case GpuGraphResourceType::Texture:
    {
        const auto resolved = ResolveTextureRangeForPlanning(graph.textureForResource(resource.id), range);
        if(!resolved || !resolved->textureSubresources.hasExtent())
            return MakeUnexpected(Failure{});
        return *resolved;
    }
    case GpuGraphResourceType::Buffer:
        if(!range.bufferRange.hasExtent())
            return MakeUnexpected(Failure{});
        if(const Buffer* const buffer = graph.bufferForResource(resource.id))
            result.bufferRange = range.bufferRange.resolve(buffer->getCreationDescription());
        if(!result.bufferRange.hasExtent())
            return MakeUnexpected(Failure{});
        return result;
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return result;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] static Expected<GpuTaskResourceRange> ResolveVersionRange(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range
)noexcept{
    const auto result = ResolvePhysicalRange(graph, resource, range);
    if(!result)
        return MakeUnexpected(Failure{});

    switch(resource.type){
    case GpuGraphResourceType::Texture:
    {
        const Texture* const texture = graph.textureForResource(resource.id);
        if(!texture)
            return *result;

        const TextureSubresourceSet& requested = range.textureSubresources;
        const TextureSubresourceSet& resolved = result->textureSubresources;
        if(
            requested.baseMipLevel != resolved.baseMipLevel
            || (requested.numMipLevels != TextureSubresourceSet::s_AllMipLevels && requested.numMipLevels != resolved.numMipLevels)
            || requested.baseArraySlice != resolved.baseArraySlice
            || (requested.numArraySlices != TextureSubresourceSet::s_AllArraySlices && requested.numArraySlices != resolved.numArraySlices)
        )
            return MakeUnexpected(Failure{});
        return *result;
    }
    case GpuGraphResourceType::Buffer:
    {
        const BufferRange& requested = range.bufferRange;
        if(!graph.bufferForResource(resource.id))
            return *result;

        const BufferRange& resolved = result->bufferRange;
        if(
            requested.byteOffset != resolved.byteOffset
            || (requested.byteSize != BufferRange::s_AllBytes && requested.byteSize != resolved.byteSize)
        )
            return MakeUnexpected(Failure{});
        return *result;
    }
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        if(range.textureSubresources != s_AllSubresources || range.bufferRange != s_EntireBuffer)
            return MakeUnexpected(Failure{});
        return *result;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] static bool RangeContainsVersion(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& outer,
    const GpuTaskResourceRange& inner
)noexcept{
    Expected<GpuTaskResourceRange> resolvedOuter = GpuTaskResourceRange{};
    Expected<GpuTaskResourceRange> resolvedInner = GpuTaskResourceRange{};
    if(
        !(resolvedOuter = ResolvePhysicalRange(graph, resource, outer))
        || !(resolvedInner = ResolveVersionRange(graph, resource, inner))
    )
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:
    case GpuGraphResourceType::Buffer:
        return GpuTaskGraphCompilerDetail::RangeContains(resource, (*resolvedOuter), (*resolvedInner));
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool RangeOverlapsVersion(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& physicalRange,
    const GpuTaskResourceRange& versionRange
)noexcept{
    Expected<GpuTaskResourceRange> resolvedPhysical = GpuTaskResourceRange{};
    Expected<GpuTaskResourceRange> resolvedVersion = GpuTaskResourceRange{};
    if(
        !(resolvedPhysical = ResolvePhysicalRange(graph, resource, physicalRange))
        || !(resolvedVersion = ResolveVersionRange(graph, resource, versionRange))
    )
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:
    case GpuGraphResourceType::Buffer:
        return GpuTaskGraphCompilerDetail::RangesOverlap(resource, (*resolvedPhysical), (*resolvedVersion));
    case GpuGraphResourceType::AccelStruct:
    case GpuGraphResourceType::HazardDomain:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool HasCoveringPhysicalUse(
    const GpuTaskGraph::DeclarationReadView& graph,
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
    const GpuTaskGraph::DeclarationReadView& graph,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrozenResourceVersionReachability final : NoCopy{
private:
    static constexpr usize s_BitsPerWord = sizeof(u64) * 8u;


public:
    explicit FrozenResourceVersionReachability(Alloc::ScratchArena& scratchArena)
        : m_incomingOffsets(scratchArena)
        , m_incomingProducers(scratchArena)
        , m_rowIndices(scratchArena)
        , m_rows(scratchArena)
        , m_pending(scratchArena)
        , m_scratchArena(scratchArena)
    {}


public:
    [[nodiscard]] bool build(
        const Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& edges,
        const usize taskCount,
        const usize maximumRowCount
    ){
        m_wordsPerRow = taskCount == 0u ? 0u : (taskCount - 1u) / s_BitsPerWord + 1u;
        if(m_wordsPerRow != 0u && taskCount > Limit<usize>::s_Max / m_wordsPerRow)
            return false;

        m_incomingOffsets.clear();
        m_incomingOffsets.resize(taskCount + 1u, 0u);
        for(const GpuTaskDependencyEdge& edge : edges)
            ++m_incomingOffsets[edge.consumer.index + 1u];
        for(usize taskIndex = 1u; taskIndex <= taskCount; ++taskIndex)
            m_incomingOffsets[taskIndex] += m_incomingOffsets[taskIndex - 1u];

        Vector<usize, Alloc::ScratchArena> writeOffsets(taskCount, m_scratchArena);
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
            writeOffsets[taskIndex] = m_incomingOffsets[taskIndex];
        m_incomingProducers.resize(edges.size());
        for(const GpuTaskDependencyEdge& edge : edges)
            m_incomingProducers[writeOffsets[edge.consumer.index]++] = edge.producer.index;

        m_rowIndices.clear();
        m_rowIndices.resize(taskCount, Limit<usize>::s_Max);
        m_rows.clear();
        m_rows.reserve(Min(taskCount, maximumRowCount));
        m_pending.clear();
        m_pending.reserve(taskCount);
        return true;
    }

    [[nodiscard]] bool reaches(const GpuTaskId source, const GpuTaskId destination){
        usize& rowIndex = m_rowIndices[destination.index];
        if(rowIndex == Limit<usize>::s_Max){
            const usize newRowIndex = m_rows.size();
            m_rows.emplace_back(m_scratchArena);
            Vector<u64, Alloc::ScratchArena>& row = m_rows.back();
            row.resize(m_wordsPerRow, 0u);
            row[destination.index / s_BitsPerWord] |= static_cast<u64>(1u) << (destination.index % s_BitsPerWord);
            m_pending.clear();
            m_pending.push_back(destination.index);
            for(usize pendingIndex = 0u; pendingIndex < m_pending.size(); ++pendingIndex){
                const u32 consumerIndex = m_pending[pendingIndex];
                for(
                    usize edgeIndex = m_incomingOffsets[consumerIndex];
                    edgeIndex < m_incomingOffsets[consumerIndex + 1u];
                    ++edgeIndex
                ){
                    const u32 producerIndex = m_incomingProducers[edgeIndex];
                    const usize producerWord = producerIndex / s_BitsPerWord;
                    const u64 producerMask = static_cast<u64>(1u) << (producerIndex % s_BitsPerWord);
                    if((row[producerWord] & producerMask) != 0u)
                        continue;
                    row[producerWord] |= producerMask;
                    m_pending.push_back(producerIndex);
                }
            }
            rowIndex = newRowIndex;
        }
        const Vector<u64, Alloc::ScratchArena>& row = m_rows[rowIndex];
        const u64 mask = static_cast<u64>(1u) << (source.index % s_BitsPerWord);
        return (row[source.index / s_BitsPerWord] & mask) != 0u;
    }


private:
    Vector<usize, Alloc::ScratchArena> m_incomingOffsets;
    Vector<u32, Alloc::ScratchArena> m_incomingProducers;
    Vector<usize, Alloc::ScratchArena> m_rowIndices;
    Vector<Vector<u64, Alloc::ScratchArena>, Alloc::ScratchArena> m_rows;
    Vector<u32, Alloc::ScratchArena> m_pending;
    Alloc::ScratchArena& m_scratchArena;
    usize m_wordsPerRow = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, GpuTaskGraphAnalysisDiagnostic> BuildResourceVersionDependencyEdges(
    const GpuTaskGraph::DeclarationReadView& graph,
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena>& outEdges,
    Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_gpu_task_resource_versions;

    outEdges.clear();
    GpuTaskGraphAnalysisDiagnostic diagnostic;
    const auto fail = [&](
        const GpuTaskGraphAnalysisStatus::Enum status,
        const GpuTaskId task,
        const GpuTaskId relatedTask,
        const GpuGraphResourceId resource,
        const GpuGraphResourceVersionId version
    ){
        diagnostic.status = status;
        diagnostic.task = task;
        diagnostic.relatedTask = relatedTask;
        diagnostic.resource = resource;
        diagnostic.resourceVersion = version;
        return MakeUnexpected(diagnostic);
    };

    bool hasTaskProducedVersions = false;
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
        const auto resolvedRange = ResolveVersionRange(graph, resource, version.range);
        if(!resolvedRange){
            return fail(
                GpuTaskGraphAnalysisStatus::InvalidResourceVersion,
                {},
                {},
                version.resource,
                version.id
            );
        }
        hasTaskProducedVersions = hasTaskProducedVersions || version.origin == GpuGraphResourceVersionOrigin::TaskProduced;
    }

    constexpr usize s_LinearDuplicateCheckUseLimit = 8u;
    Vector<GpuTaskId, Alloc::ScratchArena> producers(hasTaskProducedVersions ? graph.resourceVersionCount() : 0u, scratchArena);
    Optional<Vector<u32, Alloc::ScratchArena>> lastVersionUseTasks;
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

            if(task.resourceVersionUseCount > s_LinearDuplicateCheckUseLimit){
                if(!lastVersionUseTasks.has_value())
                    lastVersionUseTasks.emplace(graph.resourceVersionCount(), Limit<u32>::s_Max, scratchArena);
                u32& lastUseTask = (*lastVersionUseTasks)[use.version.index];
                if(lastUseTask == task.id.index){
                    return fail(
                        GpuTaskGraphAnalysisStatus::InvalidResourceVersionUse,
                        task.id,
                        {},
                        version.resource,
                        use.version
                    );
                }
                lastUseTask = task.id.index;
            }
            else{
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

    if(hasTaskProducedVersions){
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
    }
    if(graph.resourceVersionCount() == 0u)
        return {};
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

    // Retain consumer task order and distinct writer task order without rediscovering them for every version.
    Vector<usize, Alloc::ScratchArena> consumerOffsets(graph.resourceVersionCount() + 1u, 0u, scratchArena);
    Vector<usize, Alloc::ScratchArena> writerOffsets(graph.resourceCount() + 1u, 0u, scratchArena);
    Vector<u32, Alloc::ScratchArena> lastWriterTasks(graph.resourceCount(), Limit<u32>::s_Max, scratchArena);
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceVersionUseCount; ++useIndex){
            const GpuTaskResourceVersionUse& use = task.resourceVersionUses[useIndex];
            if(use.role == GpuTaskResourceVersionRole::Consume)
                ++consumerOffsets[use.version.index + 1u];
        }
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(
                !IsWriteAccess(use.access)
                || !graph.validResource(use.resource)
                || lastWriterTasks[use.resource.index] == task.id.index
            )
                continue;
            lastWriterTasks[use.resource.index] = task.id.index;
            ++writerOffsets[use.resource.index + 1u];
        }
    }
    for(usize versionIndex = 1u; versionIndex <= graph.resourceVersionCount(); ++versionIndex)
        consumerOffsets[versionIndex] += consumerOffsets[versionIndex - 1u];
    for(usize resourceIndex = 1u; resourceIndex <= graph.resourceCount(); ++resourceIndex)
        writerOffsets[resourceIndex] += writerOffsets[resourceIndex - 1u];

    Vector<GpuTaskId, Alloc::ScratchArena> consumerTasks(consumerOffsets.back(), scratchArena);
    Vector<GpuTaskId, Alloc::ScratchArena> writerTasks(writerOffsets.back(), scratchArena);
    Vector<usize, Alloc::ScratchArena> writeOffsets(Max(graph.resourceVersionCount(), graph.resourceCount()), scratchArena);
    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex)
        writeOffsets[versionIndex] = consumerOffsets[versionIndex];
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceVersionUseCount; ++useIndex){
            const GpuTaskResourceVersionUse& use = task.resourceVersionUses[useIndex];
            if(use.role == GpuTaskResourceVersionRole::Consume)
                consumerTasks[writeOffsets[use.version.index]++] = task.id;
        }
    }
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        writeOffsets[resourceIndex] = writerOffsets[resourceIndex];
        lastWriterTasks[resourceIndex] = Limit<u32>::s_Max;
    }
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(
                !IsWriteAccess(use.access)
                || !graph.validResource(use.resource)
                || lastWriterTasks[use.resource.index] == task.id.index
            )
                continue;
            lastWriterTasks[use.resource.index] = task.id.index;
            writerTasks[writeOffsets[use.resource.index]++] = task.id;
        }
    }

    // Imported roots precede every graph task, so their consumers must finish before any other overlapping writer.
    // These constraints are unconditional and participate in produced-version reachability regardless of version
    // declaration order.
    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex){
        const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(versionIndex);
        if(version.origin != GpuGraphResourceVersionOrigin::ImportedRoot)
            continue;

        const usize consumerBegin = consumerOffsets[versionIndex];
        const usize consumerEnd = consumerOffsets[versionIndex + 1u];
        if(consumerBegin == consumerEnd)
            continue;

        for(
            usize writerIndex = writerOffsets[version.resource.index];
            writerIndex < writerOffsets[version.resource.index + 1u];
            ++writerIndex
        ){
            const GpuTaskGraphTaskView writer = graph.taskAt(writerTasks[writerIndex].index);
            if(!HasOverlappingPhysicalWrite(graph, writer, version))
                continue;

            for(usize consumerIndex = consumerBegin; consumerIndex < consumerEnd; ++consumerIndex){
                const GpuTaskId consumer = consumerTasks[consumerIndex];
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

    FrozenResourceVersionReachability semanticReachability(scratchArena);
    bool semanticReachabilityBuilt = false;

    // Overlapping pre-producer writers need a proven semantic/imported order; else consumers precede the writer.
    // Never feed these constraints back: declaration order must not imply intent.
    for(usize versionIndex = 0u; versionIndex < graph.resourceVersionCount(); ++versionIndex){
        const GpuTaskGraphResourceVersionView version = graph.resourceVersionAt(versionIndex);
        if(version.origin != GpuGraphResourceVersionOrigin::TaskProduced)
            continue;

        const usize consumerBegin = consumerOffsets[versionIndex];
        const usize consumerEnd = consumerOffsets[versionIndex + 1u];
        if(consumerBegin == consumerEnd)
            continue;

        for(
            usize writerIndex = writerOffsets[version.resource.index];
            writerIndex < writerOffsets[version.resource.index + 1u];
            ++writerIndex
        ){
            const GpuTaskGraphTaskView writer = graph.taskAt(writerTasks[writerIndex].index);
            if(writer.id == producers[versionIndex] || !HasOverlappingPhysicalWrite(graph, writer, version))
                continue;
            if(!semanticReachabilityBuilt){
                if(!semanticReachability.build(semanticEdges, graph.taskCount(), graph.resourceVersionCount()))
                    return fail(GpuTaskGraphAnalysisStatus::InvalidTask, {}, {}, {}, {});
                semanticReachabilityBuilt = true;
            }
            if(semanticReachability.reaches(writer.id, producers[versionIndex]))
                continue;

            for(usize consumerIndex = consumerBegin; consumerIndex < consumerEnd; ++consumerIndex){
                const GpuTaskId consumer = consumerTasks[consumerIndex];
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
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

