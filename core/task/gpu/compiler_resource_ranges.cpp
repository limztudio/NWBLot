// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <core/graphics/backend_selection/backend.h>

#include <global/integer_rect.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsReadAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Read || access == GpuTaskResourceAccess::ReadWrite;
}

[[nodiscard]] b8 IsWriteAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Write || access == GpuTaskResourceAccess::ReadWrite;
}

// Typed imports keep the Texture descriptor, so planning uses the same finite subresource extent as recording.
// Metadata-only texture declarations intentionally remain symbolic: their dimensions are not known until a later backend import.
[[nodiscard]] Expected<GpuTaskResourceRange> ResolveTextureRangeForPlanning(const Texture* const texture, const GpuTaskResourceRange& range)noexcept{
    GpuTaskResourceRange result = range;
    if(!texture)
        return result;

    result.textureSubresources = range.textureSubresources.resolve(texture->getCreationDescription(), TextureSubresourceMipResolve::Range);
    return result.textureSubresources.hasExtent() ? Expected<GpuTaskResourceRange>(result) : MakeUnexpected(Failure{});
}

[[nodiscard]] Expected<GpuTaskResourceRange> ResolveResourceRangeForPlanning(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& range)noexcept{
    GpuTaskResourceRange result = range;
    if(resource.type == GpuGraphResourceType::Texture)
        return ResolveTextureRangeForPlanning(graph.textureForResource(resource.id), range);
    if(resource.type != GpuGraphResourceType::Buffer)
        return result;
    if(!range.bufferRange.hasExtent())
        return MakeUnexpected(Failure{});
    const Buffer* const buffer = graph.bufferForResource(resource.id);
    if(!buffer)
        return result;

    const BufferDesc& description = buffer->getCreationDescription();
    if(range.bufferRange.byteOffset >= description.byteSize || (range.bufferRange.byteSize != BufferRange::s_AllBytes && range.bufferRange.byteSize > description.byteSize - range.bufferRange.byteOffset))
        return MakeUnexpected(Failure{});
    result.bufferRange = range.bufferRange.resolve(description);
    return result.bufferRange.hasExtent() ? Expected<GpuTaskResourceRange>(result) : MakeUnexpected(Failure{});
}

[[nodiscard]] b8 RangesOverlap(const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& lhs, const GpuTaskResourceRange& rhs)noexcept{
    if(resource.type == GpuGraphResourceType::Texture)
        return lhs.textureSubresources.overlaps(rhs.textureSubresources);
    if(resource.type == GpuGraphResourceType::Buffer)
        return lhs.bufferRange.overlaps(rhs.bufferRange);
    return true;
}

[[nodiscard]] b8 RangeContains(const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& outer, const GpuTaskResourceRange& inner)noexcept{
    if(resource.type == GpuGraphResourceType::Texture)
        return outer.textureSubresources.contains(inner.textureSubresources);
    if(resource.type == GpuGraphResourceType::Buffer)
        return outer.bufferRange.contains(inner.bufferRange);
    return true;
}

[[nodiscard]] const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const GpuPhysicalQueueId& destinationQueue,
    const GpuPhysicalQueueId* const requiredSourceQueue,
    const GpuTaskGraph::DeclarationReadView* const planningGraph
)noexcept{
    if(resource.initialOwnerHandoffSourceCount == 0u || !resource.initialOwnerHandoffSources || (resource.type != GpuGraphResourceType::Texture && resource.type != GpuGraphResourceType::Buffer && resource.type != GpuGraphResourceType::AccelStruct))
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u; sourceIndex < resource.initialOwnerHandoffSourceCount; ++sourceIndex){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        Expected<GpuTaskResourceRange> plannedSourceRange = source.range;
        if(
            (requiredSourceQueue && source.sourceQueue != *requiredSourceQueue)
            || source.destinationQueue != destinationQueue
            || (planningGraph && !(plannedSourceRange = ResolveResourceRangeForPlanning(*planningGraph, resource, source.range)))
            || !RangeContains(resource, (*plannedSourceRange), range)
        )
            continue;
        // One first-use fragment must have one exact external owner and one matching completion token.
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Textures use mip/array rectangles; buffers use byte intervals with a single fixed secondary row.
// The shared partition preserves every independently produced fragment, including symbolic tails in metadata-only graphs.
[[nodiscard]] static Expected<IntegerRectBounds> IntegerRectBoundsFrom(const GpuGraphResourceType::Enum resourceType, const GpuTaskResourceRange& range)noexcept{
    if(resourceType == GpuGraphResourceType::Buffer){
        if(!range.bufferRange.hasExtent())
            return MakeUnexpected(Failure{});
        return IntegerRectBounds{ .xBegin = range.bufferRange.byteOffset, .xEnd = range.bufferRange.end(), .yBegin = 0u, .yEnd = 1u, };
    }
    if(resourceType != GpuGraphResourceType::Texture)
        return MakeUnexpected(Failure{});
    const TextureSubresourceSet& texture = range.textureSubresources;
    const IntegerRectBounds result{ .xBegin = texture.baseMipLevel, .xEnd = texture.mipEnd(), .yBegin = texture.baseArraySlice, .yEnd = texture.arrayEnd(), };
    if(result.xBegin >= result.xEnd || result.yBegin >= result.yEnd)
        return MakeUnexpected(Failure{});
    return result;
}

[[nodiscard]] static Expected<GpuTaskResourceRange> IntegerRectBoundsTo(const GpuGraphResourceType::Enum resourceType, const IntegerRectBounds& bounds)noexcept{
    if(resourceType == GpuGraphResourceType::Buffer){
        if(bounds.xBegin >= bounds.xEnd || bounds.yBegin != 0u || bounds.yEnd != 1u)
            return MakeUnexpected(Failure{});
        return GpuTaskResourceRange{ .bufferRange = BufferRange(bounds.xBegin, bounds.xEnd == Limit<u64>::s_Max ? BufferRange::s_AllBytes : bounds.xEnd - bounds.xBegin), };
    }
    if(resourceType != GpuGraphResourceType::Texture)
        return MakeUnexpected(Failure{});
    if(bounds.xBegin >= bounds.xEnd || bounds.yBegin >= bounds.yEnd || bounds.xBegin > Limit<MipLevel>::s_Max || bounds.yBegin > Limit<ArraySlice>::s_Max)
        return MakeUnexpected(Failure{});

    const u64 mipCount = bounds.xEnd == Limit<u64>::s_Max ? TextureSubresourceSet::s_AllMipLevels : bounds.xEnd - bounds.xBegin;
    const u64 arrayCount = bounds.yEnd == Limit<u64>::s_Max ? TextureSubresourceSet::s_AllArraySlices : bounds.yEnd - bounds.yBegin;
    if(
        mipCount == 0u
        || arrayCount == 0u
        || mipCount > Limit<MipLevel>::s_Max
        || arrayCount > Limit<ArraySlice>::s_Max
        || (bounds.xEnd != Limit<u64>::s_Max && mipCount == TextureSubresourceSet::s_AllMipLevels)
        || (bounds.yEnd != Limit<u64>::s_Max && arrayCount == TextureSubresourceSet::s_AllArraySlices)
    )
        return MakeUnexpected(Failure{});

    return GpuTaskResourceRange{ .textureSubresources = TextureSubresourceSet{ static_cast<MipLevel>(bounds.xBegin), static_cast<MipLevel>(mipCount), static_cast<ArraySlice>(bounds.yBegin), static_cast<ArraySlice>(arrayCount), }, };
}

// One task owns transitions between its own declared subresources. Later ranges may add untouched cells needing a boundary source or initial state.
[[nodiscard]] b8 CollectResourceFirstUseRangesWithinTask(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const TaskResourceUseIndex& useHistory,
    const usize useIndex,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
){
    outRanges.clear();
    if(!useHistory.validFor(task) || useIndex >= task.resourceUseCount || !useHistory.validResource(resource.id) || task.resourceUses[useIndex].resource != resource.id)
        return false;

    const auto requestedBounds = IntegerRectBoundsFrom(resource.type, range);
    if(!requestedBounds)
        return false;

    const usize firstUse = useHistory.first(resource.id);
    if(firstUse >= useIndex){
        const auto firstUseRange = IntegerRectBoundsTo(resource.type, (*requestedBounds));
        if(!firstUseRange)
            return false;
        outRanges.push_back((*firstUseRange));
        return true;
    }

    const auto firstPreviousRange = ResolveResourceRangeForPlanning(graph, resource, task.resourceUses[firstUse].range);
    if(!firstPreviousRange)
        return false;
    const auto firstPreviousBounds = IntegerRectBoundsFrom(resource.type, (*firstPreviousRange));
    if(!firstPreviousBounds)
        return false;

    Vector<IntegerRectBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<IntegerRectBounds, Alloc::ScratchArena> remainders(scratchArena);
    AppendIntegerRectRemainder((*requestedBounds), (*firstPreviousBounds), uncovered);

    for(usize previousUseIndex = useHistory.next(firstUse); previousUseIndex < useIndex && !uncovered.empty(); previousUseIndex = useHistory.next(previousUseIndex)){
        const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];

        const auto previousRange = ResolveResourceRangeForPlanning(graph, resource, previousUse.range);
        if(!previousRange)
            return false;

        const auto previousBounds = IntegerRectBoundsFrom(resource.type, (*previousRange));
        if(!previousBounds)
            return false;

        remainders.clear();
        for(const IntegerRectBounds& uncoveredRange : uncovered)
            AppendIntegerRectRemainder(uncoveredRange, (*previousBounds), remainders);

        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const IntegerRectBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    outRanges.reserve(uncovered.size());
    for(const IntegerRectBounds& uncoveredRange : uncovered){
        const auto firstUseRange = IntegerRectBoundsTo(resource.type, uncoveredRange);
        if(!firstUseRange)
            return false;
        outRanges.push_back((*firstUseRange));
    }
    return true;
}

// Both collectors discover contiguous state runs newest-to-oldest, followed only by uncovered initial-state fragments.
// Reverse the runs, retaining forward discovery order within each run and the initial-state suffix.
static void AppendResourceStateFragmentsInStateOrder(const Span<const TrackedResourceStateFragment> discovered, Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments){
    outFragments.clear();
    outFragments.reserve(discovered.size());

    usize trackedEnd = discovered.size();
    while(trackedEnd > 0u && !discovered[trackedEnd - 1u].state)
        --trackedEnd;

    usize runEnd = trackedEnd;
    while(runEnd > 0u){
        const usize stateIndex = discovered[runEnd - 1u].stateIndex;
        usize runBegin = runEnd - 1u;
        while(runBegin > 0u && discovered[runBegin - 1u].stateIndex == stateIndex)
            --runBegin;

        NWB_ASSERT(discovered[runBegin].state);
        NWB_ASSERT(runBegin == 0u || discovered[runBegin - 1u].stateIndex > stateIndex);
        for(usize fragmentIndex = runBegin; fragmentIndex < runEnd; ++fragmentIndex)
            outFragments.push_back(discovered[fragmentIndex]);
        runEnd = runBegin;
    }
    for(usize fragmentIndex = trackedEnd; fragmentIndex < discovered.size(); ++fragmentIndex)
        outFragments.push_back(discovered[fragmentIndex]);
}

// Walk newest-to-oldest and consume only still-uncovered portions of the requested ranges.
// A selected state owns exactly the terminal cells it produced; remaining cells keep their declared graph initial state.
[[nodiscard]] b8 CollectLatestResourceStateFragments(
    const Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    const Span<const GpuTaskResourceRange> requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    if(!history.validFor(trackedStates) || !history.validResource(resource.id))
        return false;

    const usize newestStateIndex = history.last(resource.id);
    if(requestedRanges.size() == 1u && newestStateIndex != Limit<usize>::s_Max && RangeContains(resource, trackedStates[newestStateIndex].range, requestedRanges.front())){
        Expected<IntegerRectBounds> requestedBounds = IntegerRectBounds{};
        Expected<IntegerRectBounds> stateBounds = IntegerRectBounds{};
        if(!(requestedBounds = IntegerRectBoundsFrom(resource.type, requestedRanges.front())) || !(stateBounds = IntegerRectBoundsFrom(resource.type, trackedStates[newestStateIndex].range)))
            return false;
        const auto fragmentRange = IntegerRectBoundsTo(resource.type, (*requestedBounds));
        if(!fragmentRange)
            return false;
        outFragments.clear();
        outFragments.reserve(1u);
        outFragments.push_back(TrackedResourceStateFragment{ .range = (*fragmentRange), .state = &trackedStates[newestStateIndex], .stateIndex = newestStateIndex, });
        return true;
    }

    Vector<IntegerRectBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<IntegerRectBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> discovered(scratchArena);
    for(const GpuTaskResourceRange& requestedRange : requestedRanges){
        const auto requestedBounds = IntegerRectBoundsFrom(resource.type, requestedRange);
        if(!requestedBounds)
            return false;
        uncovered.push_back((*requestedBounds));
    }

    for(usize stateIndex = history.last(resource.id); stateIndex != Limit<usize>::s_Max && !uncovered.empty(); stateIndex = history.previous(stateIndex)){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex];

        const auto stateBounds = IntegerRectBoundsFrom(resource.type, state.range);
        if(!stateBounds)
            return false;

        remainders.clear();
        for(const IntegerRectBounds& uncoveredRange : uncovered){
            const auto intersection = IntersectIntegerRectBounds(uncoveredRange, (*stateBounds));
            if(!intersection){
                remainders.push_back(uncoveredRange);
                continue;
            }

            const auto fragmentRange = IntegerRectBoundsTo(resource.type, (*intersection));
            if(!fragmentRange)
                return false;
            discovered.push_back(TrackedResourceStateFragment{ .range = (*fragmentRange), .state = &state, .stateIndex = stateIndex, });
            AppendIntegerRectRemainder(uncoveredRange, (*intersection), remainders);
        }
        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const IntegerRectBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    for(const IntegerRectBounds& uncoveredRange : uncovered){
        const auto fragmentRange = IntegerRectBoundsTo(resource.type, uncoveredRange);
        if(!fragmentRange)
            return false;
        discovered.push_back(TrackedResourceStateFragment{ .range = (*fragmentRange), });
    }

    AppendResourceStateFragmentsInStateOrder(discovered, outFragments);
    return true;
}

// Terminal exports have no single requested range. Subtract later declared states from each earlier range, keeping only its surviving portions.
// This uses the same symbolic interval/rectangle partition as inter-task consumer fan-in.
[[nodiscard]] b8 CollectTerminalResourceStateFragments(
    const Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    if(!history.validFor(trackedStates) || !history.validResource(resource.id))
        return false;

    Vector<IntegerRectBounds, Alloc::ScratchArena> covered(scratchArena);
    Vector<IntegerRectBounds, Alloc::ScratchArena> remaining(scratchArena);
    Vector<IntegerRectBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> discovered(scratchArena);

    for(usize stateIndex = history.last(resource.id); stateIndex != Limit<usize>::s_Max; stateIndex = history.previous(stateIndex)){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex];

        const auto stateBounds = IntegerRectBoundsFrom(resource.type, state.range);
        if(!stateBounds)
            return false;

        remaining.clear();
        remaining.push_back((*stateBounds));
        for(const IntegerRectBounds& coveredRange : covered){
            remainders.clear();
            for(const IntegerRectBounds& remainingRange : remaining)
                AppendIntegerRectRemainder(remainingRange, coveredRange, remainders);
            remaining.clear();
            remaining.reserve(remainders.size());
            for(const IntegerRectBounds& remainder : remainders)
                remaining.push_back(remainder);
            if(remaining.empty())
                break;
        }

        for(const IntegerRectBounds& terminalRange : remaining){
            const auto fragmentRange = IntegerRectBoundsTo(resource.type, terminalRange);
            if(!fragmentRange)
                return false;
            discovered.push_back(TrackedResourceStateFragment{ .range = (*fragmentRange), .state = &state, .stateIndex = stateIndex, });
        }
        if(!remaining.empty())
            covered.push_back((*stateBounds));
    }

    AppendResourceStateFragmentsInStateOrder(discovered, outFragments);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

