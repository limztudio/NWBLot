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

[[nodiscard]] bool IsValidTextureRange(const TextureSubresourceSet& range)noexcept{
    return range.numMipLevels != 0u && range.numArraySlices != 0u;
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
    return IsValidTextureRange(outRange.textureSubresources);
}

[[nodiscard]] bool IsValidBufferRange(const BufferRange& range)noexcept{
    return range.byteSize != 0u
        && (
            range.byteSize == BufferRange::AllBytes
            || range.byteOffset <= Limit<u64>::s_Max - range.byteSize
        )
    ;
}

[[nodiscard]] static u64 RangeEnd(const u32 base, const u32 count, const u32 all)noexcept{
    return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
}

[[nodiscard]] bool RangesOverlap(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& lhs,
    const GpuTaskResourceRange& rhs
)noexcept{
    // Buffers intentionally stay whole-resource in Phase 1. Their declared byte ranges become useful when the
    // compiler grows a tested interval tracker; treating them as independent before then would be unsafe.
    if(resource.type != GpuGraphResourceType::Texture)
        return true;

    const TextureSubresourceSet& left = lhs.textureSubresources;
    const TextureSubresourceSet& right = rhs.textureSubresources;
    const u64 leftMipEnd = RangeEnd(left.baseMipLevel, left.numMipLevels, TextureSubresourceSet::AllMipLevels);
    const u64 rightMipEnd = RangeEnd(right.baseMipLevel, right.numMipLevels, TextureSubresourceSet::AllMipLevels);
    const u64 leftArrayEnd = RangeEnd(left.baseArraySlice, left.numArraySlices, TextureSubresourceSet::AllArraySlices);
    const u64 rightArrayEnd = RangeEnd(right.baseArraySlice, right.numArraySlices, TextureSubresourceSet::AllArraySlices);
    return left.baseMipLevel < rightMipEnd
        && right.baseMipLevel < leftMipEnd
        && left.baseArraySlice < rightArrayEnd
        && right.baseArraySlice < leftArrayEnd;
}

[[nodiscard]] bool RangeContains(
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& outer,
    const GpuTaskResourceRange& inner
)noexcept{
    if(resource.type != GpuGraphResourceType::Texture)
        return true;

    const TextureSubresourceSet& outerTexture = outer.textureSubresources;
    const TextureSubresourceSet& innerTexture = inner.textureSubresources;
    return outerTexture.baseMipLevel <= innerTexture.baseMipLevel
        && RangeEnd(
            outerTexture.baseMipLevel,
            outerTexture.numMipLevels,
            TextureSubresourceSet::AllMipLevels
        ) >= RangeEnd(
            innerTexture.baseMipLevel,
            innerTexture.numMipLevels,
            TextureSubresourceSet::AllMipLevels
        )
        && outerTexture.baseArraySlice <= innerTexture.baseArraySlice
        && RangeEnd(
            outerTexture.baseArraySlice,
            outerTexture.numArraySlices,
            TextureSubresourceSet::AllArraySlices
        ) >= RangeEnd(
            innerTexture.baseArraySlice,
            innerTexture.numArraySlices,
            TextureSubresourceSet::AllArraySlices
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture state tracking is subresource-granular, but graph declarations can name rectangles that straddle several
// independently produced regions. Keep the interval endpoints symbolic so metadata-only graphs retain the same
// correct partition as typed textures whose physical mip/array bounds are only known at recording time.
struct TextureRangeBounds{
    u64 mipBegin = 0u;
    u64 mipEnd = 0u;
    u64 arrayBegin = 0u;
    u64 arrayEnd = 0u;
};

[[nodiscard]] static bool TextureRangeBoundsFrom(
    const GpuTaskResourceRange& range,
    TextureRangeBounds& outBounds
)noexcept{
    const TextureSubresourceSet& texture = range.textureSubresources;
    outBounds = TextureRangeBounds{
        .mipBegin = texture.baseMipLevel,
        .mipEnd = RangeEnd(texture.baseMipLevel, texture.numMipLevels, TextureSubresourceSet::AllMipLevels),
        .arrayBegin = texture.baseArraySlice,
        .arrayEnd = RangeEnd(texture.baseArraySlice, texture.numArraySlices, TextureSubresourceSet::AllArraySlices),
    };
    return outBounds.mipBegin < outBounds.mipEnd && outBounds.arrayBegin < outBounds.arrayEnd;
}

[[nodiscard]] static bool TextureRangeBoundsTo(
    const TextureRangeBounds& bounds,
    GpuTaskResourceRange& outRange
)noexcept{
    if(
        bounds.mipBegin >= bounds.mipEnd
        || bounds.arrayBegin >= bounds.arrayEnd
        || bounds.mipBegin > Limit<MipLevel>::s_Max
        || bounds.arrayBegin > Limit<ArraySlice>::s_Max
    )
        return false;

    const u64 mipCount = bounds.mipEnd == Limit<u64>::s_Max
        ? TextureSubresourceSet::AllMipLevels
        : bounds.mipEnd - bounds.mipBegin
    ;
    const u64 arrayCount = bounds.arrayEnd == Limit<u64>::s_Max
        ? TextureSubresourceSet::AllArraySlices
        : bounds.arrayEnd - bounds.arrayBegin
    ;
    if(
        mipCount == 0u
        || arrayCount == 0u
        || mipCount > Limit<MipLevel>::s_Max
        || arrayCount > Limit<ArraySlice>::s_Max
        || (bounds.mipEnd != Limit<u64>::s_Max && mipCount == TextureSubresourceSet::AllMipLevels)
        || (bounds.arrayEnd != Limit<u64>::s_Max && arrayCount == TextureSubresourceSet::AllArraySlices)
    )
        return false;

    outRange = GpuTaskResourceRange{
        .textureSubresources = TextureSubresourceSet{
            static_cast<MipLevel>(bounds.mipBegin),
            static_cast<MipLevel>(mipCount),
            static_cast<ArraySlice>(bounds.arrayBegin),
            static_cast<ArraySlice>(arrayCount),
        },
    };
    return true;
}

[[nodiscard]] static bool IntersectTextureRangeBounds(
    const TextureRangeBounds& lhs,
    const TextureRangeBounds& rhs,
    TextureRangeBounds& outIntersection
)noexcept{
    outIntersection = TextureRangeBounds{
        .mipBegin = lhs.mipBegin > rhs.mipBegin ? lhs.mipBegin : rhs.mipBegin,
        .mipEnd = lhs.mipEnd < rhs.mipEnd ? lhs.mipEnd : rhs.mipEnd,
        .arrayBegin = lhs.arrayBegin > rhs.arrayBegin ? lhs.arrayBegin : rhs.arrayBegin,
        .arrayEnd = lhs.arrayEnd < rhs.arrayEnd ? lhs.arrayEnd : rhs.arrayEnd,
    };
    return outIntersection.mipBegin < outIntersection.mipEnd
        && outIntersection.arrayBegin < outIntersection.arrayEnd
    ;
}

static void AppendTextureRangeRemainder(
    const TextureRangeBounds& outer,
    const TextureRangeBounds& cut,
    Vector<TextureRangeBounds, Alloc::ScratchArena>& outRanges
){
    TextureRangeBounds intersection;
    if(!IntersectTextureRangeBounds(outer, cut, intersection)){
        outRanges.push_back(outer);
        return;
    }

    if(outer.mipBegin < intersection.mipBegin){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = outer.mipBegin,
            .mipEnd = intersection.mipBegin,
            .arrayBegin = outer.arrayBegin,
            .arrayEnd = outer.arrayEnd,
        });
    }
    if(intersection.mipEnd < outer.mipEnd){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = intersection.mipEnd,
            .mipEnd = outer.mipEnd,
            .arrayBegin = outer.arrayBegin,
            .arrayEnd = outer.arrayEnd,
        });
    }
    if(outer.arrayBegin < intersection.arrayBegin){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = intersection.mipBegin,
            .mipEnd = intersection.mipEnd,
            .arrayBegin = outer.arrayBegin,
            .arrayEnd = intersection.arrayBegin,
        });
    }
    if(intersection.arrayEnd < outer.arrayEnd){
        outRanges.push_back(TextureRangeBounds{
            .mipBegin = intersection.mipBegin,
            .mipEnd = intersection.mipEnd,
            .arrayBegin = intersection.arrayEnd,
            .arrayEnd = outer.arrayEnd,
        });
    }
}

// One task owns transitions between its own commands, but only for subresources it already declared earlier in that
// task. A later overlapping range can also introduce previously untouched cells, which still need the graph's
// packet-boundary state source or declared initial state before native task recording begins.
[[nodiscard]] bool CollectTextureFirstUseRangesWithinTask(
    const GpuTaskGraphTaskView& task,
    const usize useIndex,
    const GpuGraphResourceId& resource,
    const Texture* const texture,
    const GpuTaskResourceRange& range,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& outRanges
){
    outRanges.clear();

    TextureRangeBounds requestedBounds;
    if(!TextureRangeBoundsFrom(range, requestedBounds))
        return false;

    Vector<TextureRangeBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    uncovered.push_back(requestedBounds);

    for(usize previousUseIndex = 0u; previousUseIndex < useIndex && !uncovered.empty(); ++previousUseIndex){
        const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];
        if(previousUse.resource != resource)
            continue;

        GpuTaskResourceRange previousRange;
        if(!ResolveTextureRangeForPlanning(texture, previousUse.range, previousRange))
            return false;

        TextureRangeBounds previousBounds;
        if(!TextureRangeBoundsFrom(previousRange, previousBounds))
            return false;

        remainders.clear();
        for(const TextureRangeBounds& uncoveredRange : uncovered)
            AppendTextureRangeRemainder(uncoveredRange, previousBounds, remainders);

        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const TextureRangeBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    outRanges.reserve(uncovered.size());
    for(const TextureRangeBounds& uncoveredRange : uncovered){
        GpuTaskResourceRange firstUseRange;
        if(!TextureRangeBoundsTo(uncoveredRange, firstUseRange))
            return false;
        outRanges.push_back(firstUseRange);
    }
    return true;
}

static void AppendTextureStateFragmentsInStateOrder(
    const Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& discovered,
    const usize stateCount,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
){
    outFragments.clear();
    outFragments.reserve(discovered.size());
    for(usize stateIndex = 0u; stateIndex < stateCount; ++stateIndex){
        for(const TrackedTextureStateFragment& fragment : discovered){
            if(fragment.stateIndex == stateIndex)
                outFragments.push_back(fragment);
        }
    }
    for(const TrackedTextureStateFragment& fragment : discovered){
        if(!fragment.state)
            outFragments.push_back(fragment);
    }
}

// Walk newest-to-oldest and consume only still-uncovered portions of the requested ranges. A selected state
// therefore owns exactly the terminal cells that it actually produced; the remaining cells retain their declared
// graph initial state rather than inheriting an unrelated adjacent producer.
[[nodiscard]] bool CollectLatestTextureStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuGraphResourceId& resource,
    const Vector<GpuTaskResourceRange, Alloc::ScratchArena>& requestedRanges,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
){
    Vector<TextureRangeBounds, Alloc::ScratchArena> uncovered(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena> discovered(scratchArena);
    for(const GpuTaskResourceRange& requestedRange : requestedRanges){
        TextureRangeBounds requestedBounds;
        if(!TextureRangeBoundsFrom(requestedRange, requestedBounds))
            return false;
        uncovered.push_back(requestedBounds);
    }

    for(usize stateIndex = trackedStates.size(); stateIndex > 0u && !uncovered.empty(); --stateIndex){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex - 1u];
        if(state.resource != resource)
            continue;

        TextureRangeBounds stateBounds;
        if(!TextureRangeBoundsFrom(state.range, stateBounds))
            return false;

        remainders.clear();
        for(const TextureRangeBounds& uncoveredRange : uncovered){
            TextureRangeBounds intersection;
            if(!IntersectTextureRangeBounds(uncoveredRange, stateBounds, intersection)){
                remainders.push_back(uncoveredRange);
                continue;
            }

            GpuTaskResourceRange fragmentRange;
            if(!TextureRangeBoundsTo(intersection, fragmentRange))
                return false;
            discovered.push_back(TrackedTextureStateFragment{
                .range = fragmentRange,
                .state = &state,
                .stateIndex = stateIndex - 1u,
            });
            AppendTextureRangeRemainder(uncoveredRange, intersection, remainders);
        }
        uncovered.clear();
        uncovered.reserve(remainders.size());
        for(const TextureRangeBounds& remainder : remainders)
            uncovered.push_back(remainder);
    }

    for(const TextureRangeBounds& uncoveredRange : uncovered){
        GpuTaskResourceRange fragmentRange;
        if(!TextureRangeBoundsTo(uncoveredRange, fragmentRange))
            return false;
        discovered.push_back(TrackedTextureStateFragment{
            .range = fragmentRange,
        });
    }

    AppendTextureStateFragmentsInStateOrder(discovered, trackedStates.size(), outFragments);
    return true;
}

// Terminal graph-to-external exports have no one requested range. Subtract the union of every later declared
// texture state from each earlier range, leaving only the portions whose final state snapshot still belongs to that
// earlier task. This uses the same symbolic rectangle representation as inter-task consumer fan-in.
[[nodiscard]] bool CollectTerminalTextureStateFragments(
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedStates,
    const GpuGraphResourceId& resource,
    Alloc::ScratchArena& scratchArena,
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& outFragments
){
    Vector<TextureRangeBounds, Alloc::ScratchArena> covered(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remaining(scratchArena);
    Vector<TextureRangeBounds, Alloc::ScratchArena> remainders(scratchArena);
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena> discovered(scratchArena);

    for(usize stateIndex = trackedStates.size(); stateIndex > 0u; --stateIndex){
        const TrackedCompiledResourceState& state = trackedStates[stateIndex - 1u];
        if(state.resource != resource)
            continue;

        TextureRangeBounds stateBounds;
        if(!TextureRangeBoundsFrom(state.range, stateBounds))
            return false;

        remaining.clear();
        remaining.push_back(stateBounds);
        for(const TextureRangeBounds& coveredRange : covered){
            remainders.clear();
            for(const TextureRangeBounds& remainingRange : remaining)
                AppendTextureRangeRemainder(remainingRange, coveredRange, remainders);
            remaining.clear();
            remaining.reserve(remainders.size());
            for(const TextureRangeBounds& remainder : remainders)
                remaining.push_back(remainder);
            if(remaining.empty())
                break;
        }

        for(const TextureRangeBounds& terminalRange : remaining){
            GpuTaskResourceRange fragmentRange;
            if(!TextureRangeBoundsTo(terminalRange, fragmentRange))
                return false;
            discovered.push_back(TrackedTextureStateFragment{
                .range = fragmentRange,
                .state = &state,
                .stateIndex = stateIndex - 1u,
            });
        }
        covered.push_back(stateBounds);
    }

    AppendTextureStateFragmentsInStateOrder(discovered, trackedStates.size(), outFragments);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

