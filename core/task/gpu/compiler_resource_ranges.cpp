// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsReadAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Read || access == GpuTaskResourceAccess::ReadWrite;
}

[[nodiscard]] bool IsWriteAccess(const GpuTaskResourceAccess::Enum access)noexcept{
    return access == GpuTaskResourceAccess::Write || access == GpuTaskResourceAccess::ReadWrite;
}

// Typed imports retain the physical Texture descriptor, so compile-time state planning must use the same finite
// subresource extent as native recording. Metadata-only texture declarations intentionally remain symbolic: their
// dimensions are not known until a later backend import.
[[nodiscard]] bool ResolveTextureRangeForPlanning(
    const Texture* const texture,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept{
    outRange = range;
    if(!texture)
        return true;

    outRange.textureSubresources = range.textureSubresources.resolve(
        texture->getCreationDescription(),
        TextureSubresourceMipResolve::Range
    );
    return outRange.textureSubresources.hasExtent();
}

[[nodiscard]] bool ResolveResourceRangeForPlanning(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuTaskResourceRange& outRange
)noexcept{
    outRange = range;
    if(resource.type == GpuGraphResourceType::Texture)
        return ResolveTextureRangeForPlanning(graph.textureForResource(resource.id), range, outRange);
    if(resource.type != GpuGraphResourceType::Buffer)
        return true;
    if(!range.bufferRange.hasExtent())
        return false;
    const Buffer* const buffer = graph.bufferForResource(resource.id);
    if(!buffer)
        return true;

    const BufferDesc& description = buffer->getCreationDescription();
    if(
        range.bufferRange.byteOffset >= description.byteSize
        || (
            range.bufferRange.byteSize != BufferRange::AllBytes
            && range.bufferRange.byteSize > description.byteSize - range.bufferRange.byteOffset
        )
    )
        return false;
    outRange.bufferRange = range.bufferRange.resolve(description);
    return outRange.bufferRange.hasExtent();
}

[[nodiscard]] bool RangesOverlap(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& lhs,
    const GpuTaskResourceRange& rhs
)noexcept{
    if(resource.type == GpuGraphResourceType::Texture)
        return lhs.textureSubresources.overlaps(rhs.textureSubresources);
    if(resource.type == GpuGraphResourceType::Buffer)
        return lhs.bufferRange.overlaps(rhs.bufferRange);
    return true;
}

[[nodiscard]] bool RangeContains(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& outer,
    const GpuTaskResourceRange& inner
)noexcept{
    if(resource.type == GpuGraphResourceType::Texture)
        return outer.textureSubresources.contains(inner.textureSubresources);
    if(resource.type == GpuGraphResourceType::Buffer)
        return outer.bufferRange.contains(inner.bufferRange);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Textures use mip/array rectangles; buffers use byte intervals with a single fixed secondary row. The shared
// partition preserves every independently produced fragment, including symbolic tails in metadata-only graphs.
struct ResourceRangeBounds{
    u64 xBegin = 0u;
    u64 xEnd = 0u;
    u64 yBegin = 0u;
    u64 yEnd = 0u;
};

[[nodiscard]] static bool ResourceRangeBoundsFrom(
    const GpuGraphResourceType::Enum resourceType,
    const GpuTaskResourceRange& range,
    ResourceRangeBounds& outBounds
)noexcept{
    if(resourceType == GpuGraphResourceType::Buffer){
        if(!range.bufferRange.hasExtent())
            return false;
        outBounds = ResourceRangeBounds{
            .xBegin = range.bufferRange.byteOffset,
            .xEnd = range.bufferRange.end(),
            .yBegin = 0u,
            .yEnd = 1u,
        };
        return true;
    }
    if(resourceType != GpuGraphResourceType::Texture)
        return false;
    const TextureSubresourceSet& texture = range.textureSubresources;
    outBounds = ResourceRangeBounds{
        .xBegin = texture.baseMipLevel,
        .xEnd = texture.mipEnd(),
        .yBegin = texture.baseArraySlice,
        .yEnd = texture.arrayEnd(),
    };
    return outBounds.xBegin < outBounds.xEnd && outBounds.yBegin < outBounds.yEnd;
}

[[nodiscard]] static bool ResourceRangeBoundsTo(
    const GpuGraphResourceType::Enum resourceType,
    const ResourceRangeBounds& bounds,
    GpuTaskResourceRange& outRange
)noexcept{
    if(resourceType == GpuGraphResourceType::Buffer){
        if(bounds.xBegin >= bounds.xEnd || bounds.yBegin != 0u || bounds.yEnd != 1u)
            return false;
        outRange = GpuTaskResourceRange{
            .bufferRange = BufferRange(
                bounds.xBegin,
                bounds.xEnd == Limit<u64>::s_Max ? BufferRange::AllBytes : bounds.xEnd - bounds.xBegin
            ),
        };
        return true;
    }
    if(resourceType != GpuGraphResourceType::Texture)
        return false;
    if(
        bounds.xBegin >= bounds.xEnd
        || bounds.yBegin >= bounds.yEnd
        || bounds.xBegin > Limit<MipLevel>::s_Max
        || bounds.yBegin > Limit<ArraySlice>::s_Max
    )
        return false;

    const u64 mipCount = bounds.xEnd == Limit<u64>::s_Max
        ? TextureSubresourceSet::AllMipLevels
        : bounds.xEnd - bounds.xBegin
    ;
    const u64 arrayCount = bounds.yEnd == Limit<u64>::s_Max
        ? TextureSubresourceSet::AllArraySlices
        : bounds.yEnd - bounds.yBegin
    ;
    if(
        mipCount == 0u
        || arrayCount == 0u
        || mipCount > Limit<MipLevel>::s_Max
        || arrayCount > Limit<ArraySlice>::s_Max
        || (bounds.xEnd != Limit<u64>::s_Max && mipCount == TextureSubresourceSet::AllMipLevels)
        || (bounds.yEnd != Limit<u64>::s_Max && arrayCount == TextureSubresourceSet::AllArraySlices)
    )
        return false;

    outRange = GpuTaskResourceRange{
        .textureSubresources = TextureSubresourceSet{
            static_cast<MipLevel>(bounds.xBegin),
            static_cast<MipLevel>(mipCount),
            static_cast<ArraySlice>(bounds.yBegin),
            static_cast<ArraySlice>(arrayCount),
        },
    };
    return true;
}

[[nodiscard]] static bool IntersectResourceRangeBounds(
    const ResourceRangeBounds& lhs,
    const ResourceRangeBounds& rhs,
    ResourceRangeBounds& outIntersection
)noexcept{
    outIntersection = ResourceRangeBounds{
        .xBegin = lhs.xBegin > rhs.xBegin ? lhs.xBegin : rhs.xBegin,
        .xEnd = lhs.xEnd < rhs.xEnd ? lhs.xEnd : rhs.xEnd,
        .yBegin = lhs.yBegin > rhs.yBegin ? lhs.yBegin : rhs.yBegin,
        .yEnd = lhs.yEnd < rhs.yEnd ? lhs.yEnd : rhs.yEnd,
    };
    return outIntersection.xBegin < outIntersection.xEnd
        && outIntersection.yBegin < outIntersection.yEnd
    ;
}

static void AppendResourceRangeRemainder(
    const ResourceRangeBounds& outer,
    const ResourceRangeBounds& cut,
    Vector<ResourceRangeBounds, Alloc::ScratchArena>& outRanges
){
    ResourceRangeBounds intersection;
    if(!IntersectResourceRangeBounds(outer, cut, intersection)){
        outRanges.push_back(outer);
        return;
    }

    if(outer.xBegin < intersection.xBegin){
        outRanges.push_back(ResourceRangeBounds{
            .xBegin = outer.xBegin,
            .xEnd = intersection.xBegin,
            .yBegin = outer.yBegin,
            .yEnd = outer.yEnd,
        });
    }
    if(intersection.xEnd < outer.xEnd){
        outRanges.push_back(ResourceRangeBounds{
            .xBegin = intersection.xEnd,
            .xEnd = outer.xEnd,
            .yBegin = outer.yBegin,
            .yEnd = outer.yEnd,
        });
    }
    if(outer.yBegin < intersection.yBegin){
        outRanges.push_back(ResourceRangeBounds{
            .xBegin = intersection.xBegin,
            .xEnd = intersection.xEnd,
            .yBegin = outer.yBegin,
            .yEnd = intersection.yBegin,
        });
    }
    if(intersection.yEnd < outer.yEnd){
        outRanges.push_back(ResourceRangeBounds{
            .xBegin = intersection.xBegin,
            .xEnd = intersection.xEnd,
            .yBegin = intersection.yEnd,
            .yEnd = outer.yEnd,
        });
    }
}

// One task owns transitions between its own commands, but only for subresources it already declared earlier in that
// task. A later overlapping range can also introduce previously untouched cells, which still need the graph's
// packet-boundary state source or declared initial state before native task recording begins.
[[nodiscard]] bool CollectResourceFirstUseRangesWithinTask(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const usize useIndex,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
){
    outRanges.clear();

    ResourceRangeBounds requestedBounds;
    if(!ResourceRangeBoundsFrom(resource.type, range, requestedBounds))
        return false;

    Vector<ResourceRangeBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<ResourceRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    uncovered.push_back(requestedBounds);

    for(usize previousUseIndex = 0u; previousUseIndex < useIndex && !uncovered.empty(); ++previousUseIndex){
        const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];
        if(previousUse.resource != resource.id)
            continue;

        GpuTaskResourceRange previousRange;
        if(!ResolveResourceRangeForPlanning(graph, resource, previousUse.range, previousRange))
            return false;

        ResourceRangeBounds previousBounds;
        if(!ResourceRangeBoundsFrom(resource.type, previousRange, previousBounds))
            return false;

        remainders.clear();
        for(const ResourceRangeBounds& uncoveredRange : uncovered)
            AppendResourceRangeRemainder(uncoveredRange, previousBounds, remainders);

        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const ResourceRangeBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    outRanges.reserve(uncovered.size());
    for(const ResourceRangeBounds& uncoveredRange : uncovered){
        GpuTaskResourceRange firstUseRange;
        if(!ResourceRangeBoundsTo(resource.type, uncoveredRange, firstUseRange))
            return false;
        outRanges.push_back(firstUseRange);
    }
    return true;
}

// Both collectors discover contiguous state runs newest-to-oldest, followed only by uncovered initial-state fragments.
// Reverse the runs, retaining forward discovery order within each run and the initial-state suffix.
static void AppendResourceStateFragmentsInStateOrder(
    const Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& discovered,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments){
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

// Walk newest-to-oldest and consume only still-uncovered portions of the requested ranges. A selected state
// therefore owns exactly the terminal cells that it actually produced; the remaining cells retain their declared
// graph initial state rather than inheriting an unrelated adjacent producer.
[[nodiscard]] bool CollectLatestResourceStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuTaskGraphResourceView& resource,
    const Vector<GpuTaskResourceRange, Alloc::ScratchArena>& requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    Vector<ResourceRangeBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<ResourceRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> discovered(scratchArena);
    for(const GpuTaskResourceRange& requestedRange : requestedRanges){
        ResourceRangeBounds requestedBounds;
        if(!ResourceRangeBoundsFrom(resource.type, requestedRange, requestedBounds))
            return false;
        uncovered.push_back(requestedBounds);
    }

    for(usize stateIndex = trackedStates.size(); stateIndex > 0u && !uncovered.empty(); --stateIndex){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex - 1u];
        if(state.resource != resource.id)
            continue;

        ResourceRangeBounds stateBounds;
        if(!ResourceRangeBoundsFrom(resource.type, state.range, stateBounds))
            return false;

        remainders.clear();
        for(const ResourceRangeBounds& uncoveredRange : uncovered){
            ResourceRangeBounds intersection;
            if(!IntersectResourceRangeBounds(uncoveredRange, stateBounds, intersection)){
                remainders.push_back(uncoveredRange);
                continue;
            }

            GpuTaskResourceRange fragmentRange;
            if(!ResourceRangeBoundsTo(resource.type, intersection, fragmentRange))
                return false;
            discovered.push_back(TrackedResourceStateFragment{
                .range = fragmentRange,
                .state = &state,
                .stateIndex = stateIndex - 1u,
            });
            AppendResourceRangeRemainder(uncoveredRange, intersection, remainders);
        }
        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const ResourceRangeBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    for(const ResourceRangeBounds& uncoveredRange : uncovered){
        GpuTaskResourceRange fragmentRange;
        if(!ResourceRangeBoundsTo(resource.type, uncoveredRange, fragmentRange))
            return false;
        discovered.push_back(TrackedResourceStateFragment{
            .range = fragmentRange,
        });
    }

    AppendResourceStateFragmentsInStateOrder(discovered, outFragments);
    return true;
}

// Terminal graph-to-external exports have no one requested range. Subtract the union of every later declared
// resource state from each earlier range, leaving only the portions whose final state snapshot still belongs to that
// earlier task. This uses the same symbolic interval/rectangle partition as inter-task consumer fan-in.
[[nodiscard]] bool CollectTerminalResourceStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuTaskGraphResourceView& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& outFragments
){
    Vector<ResourceRangeBounds, Alloc::ScratchArena> covered(scratchArena);
    Vector<ResourceRangeBounds, Alloc::ScratchArena> remaining(scratchArena);
    Vector<ResourceRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> discovered(scratchArena);

    for(usize stateIndex = trackedStates.size(); stateIndex > 0u; --stateIndex){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex - 1u];
        if(state.resource != resource.id)
            continue;

        ResourceRangeBounds stateBounds;
        if(!ResourceRangeBoundsFrom(resource.type, state.range, stateBounds))
            return false;

        remaining.clear();
        remaining.push_back(stateBounds);
        for(const ResourceRangeBounds& coveredRange : covered){
            remainders.clear();
            for(const ResourceRangeBounds& remainingRange : remaining)
                AppendResourceRangeRemainder(remainingRange, coveredRange, remainders);
            remaining.clear();
            remaining.reserve(remainders.size());
            for(const ResourceRangeBounds& remainder : remainders)
                remaining.push_back(remainder);
            if(remaining.empty())
                break;
        }

        for(const ResourceRangeBounds& terminalRange : remaining){
            GpuTaskResourceRange fragmentRange;
            if(!ResourceRangeBoundsTo(resource.type, terminalRange, fragmentRange))
                return false;
            discovered.push_back(TrackedResourceStateFragment{
                .range = fragmentRange,
                .state = &state,
                .stateIndex = stateIndex - 1u,
            });
        }
        covered.push_back(stateBounds);
    }

    AppendResourceStateFragmentsInStateOrder(discovered, outFragments);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

