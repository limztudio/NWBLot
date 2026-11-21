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

[[nodiscard]] Expected<GpuTaskResourceRange> ResolveResourceNodeRangeForPlanning(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphDetail::GpuGraphResourceNode& resource, const u32 resourceIndex, const GpuTaskResourceRange& range)noexcept{
    GpuTaskResourceRange result = range;
    if(resource.type == GpuGraphResourceType::Texture)
        return ResolveTextureRangeForPlanning(CompilerGraphAccess::TextureForResourceIndex(graph, resourceIndex), range);
    if(resource.type != GpuGraphResourceType::Buffer)
        return result;
    if(!range.bufferRange.hasExtent())
        return MakeUnexpected(Failure{});
    const Buffer* const buffer = CompilerGraphAccess::BufferForResourceIndex(graph, resourceIndex);
    if(!buffer)
        return result;

    const BufferDesc& description = buffer->getCreationDescription();
    if(range.bufferRange.byteOffset >= description.byteSize || (range.bufferRange.byteSize != BufferRange::s_AllBytes && range.bufferRange.byteSize > description.byteSize - range.bufferRange.byteOffset))
        return MakeUnexpected(Failure{});
    result.bufferRange = range.bufferRange.resolve(description);
    return result.bufferRange.hasExtent() ? Expected<GpuTaskResourceRange>(result) : MakeUnexpected(Failure{});
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
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static b8 HasValidResourceBounds(const GpuGraphResourceType::Enum resourceType, const GpuTaskResourceRange& range)noexcept{
    if(resourceType == GpuGraphResourceType::Buffer)
        return range.bufferRange.hasExtent();
    if(resourceType == GpuGraphResourceType::Texture){
        const TextureSubresourceSet& texture = range.textureSubresources;
        return texture.baseMipLevel < texture.mipEnd() && texture.baseArraySlice < texture.arrayEnd();
    }
    return false;
}

[[nodiscard]] static b8 TryNormalizeSingleResourceRange(const GpuGraphResourceType::Enum resourceType, const GpuTaskResourceRange& range, GpuTaskResourceRange& outRange)noexcept{
    if(resourceType == GpuGraphResourceType::Buffer){
        if(!range.bufferRange.hasExtent())
            return false;
        const u64 xBegin = range.bufferRange.byteOffset;
        const u64 xEnd = range.bufferRange.end();
        if(xBegin >= xEnd)
            return false;
        outRange = GpuTaskResourceRange{
            .bufferRange = BufferRange(xBegin, xEnd == Limit<u64>::s_Max ? BufferRange::s_AllBytes : xEnd - xBegin),
        };
        return true;
    }
    if(resourceType != GpuGraphResourceType::Texture)
        return false;

    const TextureSubresourceSet& texture = range.textureSubresources;
    const u64 xBegin = texture.baseMipLevel;
    const u64 xEnd = texture.mipEnd();
    const u64 yBegin = texture.baseArraySlice;
    const u64 yEnd = texture.arrayEnd();
    if(xBegin >= xEnd || yBegin >= yEnd || xBegin > Limit<MipLevel>::s_Max || yBegin > Limit<ArraySlice>::s_Max)
        return false;

    const u64 mipCount = xEnd == Limit<u64>::s_Max ? TextureSubresourceSet::s_AllMipLevels : xEnd - xBegin;
    const u64 arrayCount = yEnd == Limit<u64>::s_Max ? TextureSubresourceSet::s_AllArraySlices : yEnd - yBegin;
    if(
        mipCount == 0u
        || arrayCount == 0u
        || mipCount > Limit<MipLevel>::s_Max
        || arrayCount > Limit<ArraySlice>::s_Max
        || (xEnd != Limit<u64>::s_Max && mipCount == TextureSubresourceSet::s_AllMipLevels)
        || (yEnd != Limit<u64>::s_Max && arrayCount == TextureSubresourceSet::s_AllArraySlices)
    )
        return false;

    outRange = GpuTaskResourceRange{
        .textureSubresources = TextureSubresourceSet{
            static_cast<MipLevel>(xBegin),
            static_cast<MipLevel>(mipCount),
            static_cast<ArraySlice>(yBegin),
            static_cast<ArraySlice>(arrayCount),
        },
    };
    return true;
}

[[nodiscard]] b8 CollectResourceFirstUseRangesWithinTaskWithScratch(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const TaskResourceUseIndex& useHistory,
    const usize useIndex,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    ResourceRangeScratch& rangeScratch,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
){
    outRanges.clear();
    if(!useHistory.validFor(task) || useIndex >= task.resourceUseCount || !useHistory.validResource(resource.id) || task.resourceUses[useIndex].resource != resource.id)
        return false;

    const usize firstUse = useHistory.first(resource.id);
    if(firstUse >= useIndex){
        GpuTaskResourceRange firstUseRange{};
        if(!TryNormalizeSingleResourceRange(resource.type, range, firstUseRange))
            return false;
        outRanges.push_back(firstUseRange);
        return true;
    }

    const auto requestedBounds = IntegerRectBoundsFrom(resource.type, range);
    if(!requestedBounds)
        return false;

    const auto firstPreviousRange = ResolveResourceRangeForPlanning(graph, resource, task.resourceUses[firstUse].range);
    if(!firstPreviousRange)
        return false;
    const auto firstPreviousBounds = IntegerRectBoundsFrom(resource.type, (*firstPreviousRange));
    if(!firstPreviousBounds)
        return false;

    Vector<IntegerRectBounds, Alloc::ScratchArena>& uncovered = rangeScratch.uncovered;
    Vector<IntegerRectBounds, Alloc::ScratchArena>& remainders = rangeScratch.remainders;
    uncovered.clear();
    remainders.clear();
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

        Swap(uncovered, remainders);
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
    ResourceRangeScratch rangeScratch(scratchArena);
    return CollectResourceFirstUseRangesWithinTaskWithScratch(graph, task, useHistory, useIndex, resource, range, rangeScratch, outRanges);
}

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

        FOUNDATION_ASSERT(discovered[runBegin].state);
        FOUNDATION_ASSERT(runBegin == 0u || discovered[runBegin - 1u].stateIndex > stateIndex);
        for(usize fragmentIndex = runBegin; fragmentIndex < runEnd; ++fragmentIndex)
            outFragments.push_back(discovered[fragmentIndex]);
        runEnd = runBegin;
    }
    for(usize fragmentIndex = trackedEnd; fragmentIndex < discovered.size(); ++fragmentIndex)
        outFragments.push_back(discovered[fragmentIndex]);
}

[[nodiscard]] b8 CollectLatestResourceStateFragmentsWithScratch(
    const Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    const Span<const GpuTaskResourceRange> requestedRanges,
    ResourceRangeScratch& rangeScratch,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    if(!history.validFor(trackedStates) || !history.validResource(resource.id))
        return false;

    const usize newestStateIndex = history.last(resource.id);
    if(newestStateIndex == Limit<usize>::s_Max){
        outFragments.clear();
        outFragments.reserve(requestedRanges.size());
        for(const GpuTaskResourceRange& requestedRange : requestedRanges){
            GpuTaskResourceRange fragmentRange{};
            if(!TryNormalizeSingleResourceRange(resource.type, requestedRange, fragmentRange))
                return false;
            outFragments.push_back(TrackedResourceStateFragment{ .range = fragmentRange, });
        }
        return true;
    }
    if(requestedRanges.size() == 1u && RangeContains(resource, trackedStates[newestStateIndex].range, requestedRanges.front())){
        GpuTaskResourceRange fragmentRange{};
        if(!TryNormalizeSingleResourceRange(resource.type, requestedRanges.front(), fragmentRange) || !HasValidResourceBounds(resource.type, trackedStates[newestStateIndex].range))
            return false;
        outFragments.clear();
        outFragments.reserve(1u);
        outFragments.push_back(TrackedResourceStateFragment{ .range = fragmentRange, .state = &trackedStates[newestStateIndex], .stateIndex = newestStateIndex, });
        return true;
    }

    Vector<IntegerRectBounds, Alloc::ScratchArena>& uncovered = rangeScratch.uncovered;
    Vector<IntegerRectBounds, Alloc::ScratchArena>& remainders = rangeScratch.remainders;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& discovered = rangeScratch.discovered;
    uncovered.clear();
    remainders.clear();
    discovered.clear();
    for(const GpuTaskResourceRange& requestedRange : requestedRanges){
        const auto requestedBounds = IntegerRectBoundsFrom(resource.type, requestedRange);
        if(!requestedBounds)
            return false;
        uncovered.push_back((*requestedBounds));
    }

    for(usize stateIndex = newestStateIndex; stateIndex != Limit<usize>::s_Max && !uncovered.empty(); stateIndex = history.previous(stateIndex)){
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
        Swap(uncovered, remainders);
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

[[nodiscard]] b8 CollectLatestResourceStateFragments(
    const Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    const Span<const GpuTaskResourceRange> requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    ResourceRangeScratch rangeScratch(scratchArena);
    return CollectLatestResourceStateFragmentsWithScratch(trackedStates, history, resource, requestedRanges, rangeScratch, outFragments);
}

[[nodiscard]] b8 CollectTerminalResourceStateFragmentsWithScratch(
    const Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    ResourceRangeScratch& rangeScratch,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments,
    const GpuTaskResourceRange* const fullResolvedRange
){
    if(!history.validFor(trackedStates) || !history.validResource(resource.id))
        return false;

    Optional<IntegerRectBounds> fullBounds;
    if(fullResolvedRange){
        if(const auto resolvedBounds = IntegerRectBoundsFrom(resource.type, *fullResolvedRange))
            fullBounds = *resolvedBounds;
    }

    Vector<IntegerRectBounds, Alloc::ScratchArena>& covered = rangeScratch.covered;
    Vector<IntegerRectBounds, Alloc::ScratchArena>& remaining = rangeScratch.remaining;
    Vector<IntegerRectBounds, Alloc::ScratchArena>& remainders = rangeScratch.remainders;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& discovered = rangeScratch.discovered;
    covered.clear();
    remaining.clear();
    remainders.clear();
    discovered.clear();

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
            Swap(remaining, remainders);
            if(remaining.empty())
                break;
        }

        for(const IntegerRectBounds& terminalRange : remaining){
            const auto fragmentRange = IntegerRectBoundsTo(resource.type, terminalRange);
            if(!fragmentRange)
                return false;
            discovered.push_back(TrackedResourceStateFragment{ .range = (*fragmentRange), .state = &state, .stateIndex = stateIndex, });
        }
        const b8 coversFullResource = (
            fullBounds
            && stateBounds->xBegin == fullBounds->xBegin
            && stateBounds->xEnd == fullBounds->xEnd
            && stateBounds->yBegin == fullBounds->yBegin
            && stateBounds->yEnd == fullBounds->yEnd
        ) || (
            stateBounds->xBegin == 0u
            && stateBounds->xEnd == Limit<u64>::s_Max
            && stateBounds->yBegin == 0u
            && (stateBounds->yEnd == Limit<u64>::s_Max || (resource.type == GpuGraphResourceType::Buffer && stateBounds->yEnd == 1u))
        );
        if(coversFullResource)
            break;
        if(!remaining.empty())
            covered.push_back((*stateBounds));
    }

    AppendResourceStateFragmentsInStateOrder(discovered, outFragments);
    return true;
}

[[nodiscard]] b8 CollectTerminalResourceStateFragments(
    const Span<const TrackedCompiledResourceState> trackedStates,
    const TrackedResourceStateHistory& history,
    const GpuTaskGraphResourceView& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    ResourceRangeScratch rangeScratch(scratchArena);
    return CollectTerminalResourceStateFragmentsWithScratch(trackedStates, history, resource, rangeScratch, outFragments, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

