#pragma once


#include "meshlet_ref_decode.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ByteVectorT>
[[nodiscard]] bool AppendMeshletRefDelta(
    ByteVectorT& outBytes,
    const u32 delta,
    const MeshletRefDeltaWidth::Enum width
){
    if(!MeshletRefDeltaFitsWidth(delta, width))
        return false;

    outBytes.push_back(static_cast<u8>(delta & s_MeshletPackedByteMask));
    if(width == MeshletRefDeltaWidth::U8)
        return true;

    outBytes.push_back(static_cast<u8>((delta >> s_MeshletPackedByteBits) & s_MeshletPackedByteMask));
    if(width == MeshletRefDeltaWidth::U16)
        return true;

    outBytes.push_back(static_cast<u8>((delta >> (s_MeshletPackedByteBits * 2u)) & s_MeshletPackedByteMask));
    outBytes.push_back(static_cast<u8>((delta >> (s_MeshletPackedByteBits * 3u)) & s_MeshletPackedByteMask));
    return true;
}

struct MeshletRefEncodeRange{
    u32 minimum = Limit<u32>::s_Max;
    u32 maximum = 0u;
};

struct MeshletAttributeRefEncodeRanges{
    MeshletRefEncodeRange normal;
    MeshletRefEncodeRange tangent;
    MeshletRefEncodeRange uv0;
    MeshletRefEncodeRange color;
};

template<typename RefT>
struct MeshletRefEncodeChannel{
    u32 RefT::* indexMember = nullptr;
    u32 MeshletDesc::* baseMember = nullptr;
    MeshletRefDeltaWidth::Enum width = MeshletRefDeltaWidth::U8;
    const tchar* failureText = nullptr;
};

using MeshletPositionRefEncodeChannel = MeshletRefEncodeChannel<MeshletPositionStreamRef>;
using MeshletAttributeRefEncodeChannel = MeshletRefEncodeChannel<MeshletAttributeStreamRef>;

NWB_INLINE void AddMeshletRefEncodeIndex(MeshletRefEncodeRange& range, const u32 index){
    range.minimum = Min(range.minimum, index);
    range.maximum = Max(range.maximum, index);
}

NWB_INLINE void AddMeshletAttributeRefEncodeRanges(
    MeshletAttributeRefEncodeRanges& ranges,
    const MeshletAttributeStreamRef& ref
){
    AddMeshletRefEncodeIndex(ranges.normal, ref.normal);
    AddMeshletRefEncodeIndex(ranges.tangent, ref.tangent);
    AddMeshletRefEncodeIndex(ranges.uv0, ref.uv0);
    AddMeshletRefEncodeIndex(ranges.color, ref.color);
}

NWB_INLINE void StoreMeshletAttributeRefEncodeBases(
    MeshletDesc& meshlet,
    const MeshletAttributeRefEncodeRanges& ranges
){
    meshlet.normalBase = ranges.normal.minimum;
    meshlet.tangentBase = ranges.tangent.minimum;
    meshlet.uv0Base = ranges.uv0.minimum;
    meshlet.colorBase = ranges.color.minimum;
}

[[nodiscard]] NWB_INLINE MeshletRefDeltaWidth::Enum MeshletRefEncodeRangeWidth(const MeshletRefEncodeRange& range){
    return MeshletRefDeltaWidthForMaxDelta(range.maximum - range.minimum);
}

template<typename ByteVectorT>
[[nodiscard]] bool AppendMeshletRefIndexDelta(
    ByteVectorT& outDeltas,
    const u32 index,
    const u32 base,
    const MeshletRefDeltaWidth::Enum width
){
    return index >= base && AppendMeshletRefDelta(outDeltas, index - base, width);
}

template<typename RefT, typename RefVectorT, typename DeltaVectorT, typename FailureHandlerT>
[[nodiscard]] bool AppendMeshletRefChannelDeltas(
    const RefVectorT& refs,
    const u32 sourceRefOffset,
    const MeshletDesc& meshlet,
    const usize meshletIndex,
    const MeshletRefEncodeChannel<RefT>& channel,
    const u32 refCount,
    DeltaVectorT& outDeltas,
    FailureHandlerT onFailure
){
    for(u32 localRefIndex = 0u; localRefIndex < refCount; ++localRefIndex){
        const RefT& ref = refs[sourceRefOffset + localRefIndex];
        const u32 index = ref.*(channel.indexMember);
        const u32 base = meshlet.*(channel.baseMember);
        if(!AppendMeshletRefIndexDelta(outDeltas, index, base, channel.width))
            return onFailure(meshletIndex, channel.failureText);
    }

    return true;
}

[[nodiscard]] NWB_INLINE bool MeshletDecodedPositionRefMatches(
    const MeshletPositionStreamRef& decoded,
    const MeshletPositionStreamRef& source,
    const bool skinRequired
){
    return decoded.position == source.position
        && decoded.skin == (skinRequired ? source.skin : s_MeshMissingStreamIndex)
    ;
}

[[nodiscard]] NWB_INLINE bool MeshletDecodedAttributeRefMatches(
    const MeshletAttributeStreamRef& decoded,
    const MeshletAttributeStreamRef& source
){
    return decoded.normal == source.normal
        && decoded.tangent == source.tangent
        && decoded.uv0 == source.uv0
        && decoded.color == source.color
    ;
}

template<
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename PositionDeltaVectorT,
    typename AttributeDeltaVectorT,
    typename FailureHandlerT
>
[[nodiscard]] bool ValidateEncodedMeshletRefDeltas(
    const MeshletDesc& meshlet,
    const usize meshletIndex,
    const u32 sourcePositionRefOffset,
    const u32 sourceAttributeRefOffset,
    const PositionRefVectorT& positionRefs,
    const AttributeRefVectorT& attributeRefs,
    const PositionDeltaVectorT& positionDeltas,
    const AttributeDeltaVectorT& attributeDeltas,
    const bool skinRequired,
    FailureHandlerT onFailure
){
    usize encodedPositionBytes = 0u;
    usize encodedAttributeBytes = 0u;
    if(
        !MeshletEncodedPositionRefByteCount(meshlet, skinRequired, encodedPositionBytes)
        || !MeshletEncodedAttributeRefByteCount(meshlet, encodedAttributeBytes)
    )
        return onFailure(meshletIndex, NWB_TEXT("encoded ref width is invalid"));
    if(
        meshlet.positionRefOffset > positionDeltas.size()
        || encodedPositionBytes != positionDeltas.size() - meshlet.positionRefOffset
        || meshlet.attributeRefOffset > attributeDeltas.size()
        || encodedAttributeBytes != attributeDeltas.size() - meshlet.attributeRefOffset
    )
        return onFailure(meshletIndex, NWB_TEXT("encoded ref byte count mismatch"));

    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletPositionStreamRef& sourceRef = positionRefs[sourcePositionRefOffset + localPositionIndex];
        MeshletPositionStreamRef decodedRef;
        if(
            !DecodeMeshletPositionRef(
                positionDeltas.data(),
                positionDeltas.size(),
                meshlet,
                localPositionIndex,
                skinRequired,
                decodedRef
            )
            || !MeshletDecodedPositionRefMatches(decodedRef, sourceRef, skinRequired)
        )
            return onFailure(meshletIndex, NWB_TEXT("encoded position ref decode mismatch"));
    }

    for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
        const MeshletAttributeStreamRef& sourceRef = attributeRefs[sourceAttributeRefOffset + localAttributeIndex];
        MeshletAttributeStreamRef decodedRef;
        if(
            !DecodeMeshletAttributeRef(
                attributeDeltas.data(),
                attributeDeltas.size(),
                meshlet,
                localAttributeIndex,
                decodedRef
            )
            || !MeshletDecodedAttributeRefMatches(decodedRef, sourceRef)
        )
            return onFailure(meshletIndex, NWB_TEXT("encoded attribute ref decode mismatch"));
    }

    return true;
}

template<
    typename MeshletVectorT,
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename PositionDeltaVectorT,
    typename AttributeDeltaVectorT,
    typename FailureHandlerT
>
[[nodiscard]] bool EncodeMeshletRefDeltas(
    MeshletVectorT& meshlets,
    const PositionRefVectorT& positionRefs,
    const AttributeRefVectorT& attributeRefs,
    PositionDeltaVectorT& outPositionDeltas,
    AttributeDeltaVectorT& outAttributeDeltas,
    const bool skinRequired,
    FailureHandlerT onFailure
){
    outPositionDeltas.clear();
    outAttributeDeltas.clear();
    outPositionDeltas.reserve(positionRefs.size() * sizeof(MeshletPositionStreamRef));
    outAttributeDeltas.reserve(attributeRefs.size() * sizeof(MeshletAttributeStreamRef));

    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        MeshletDesc& meshlet = meshlets[meshletIndex];
        if(MeshletPositionCount(meshlet) == 0u || MeshletAttributeCount(meshlet) == 0u)
            return onFailure(meshletIndex, NWB_TEXT("meshlet cannot encode empty ref ranges"));

        const u32 sourcePositionRefOffset = meshlet.positionRefOffset;
        const u32 sourceAttributeRefOffset = meshlet.attributeRefOffset;
        MeshletRefEncodeRange positionRange;
        MeshletRefEncodeRange skinRange;
        MeshletAttributeRefEncodeRanges attributeRanges;

        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
            const MeshletPositionStreamRef& ref = positionRefs[sourcePositionRefOffset + localPositionIndex];
            AddMeshletRefEncodeIndex(positionRange, ref.position);
            if(skinRequired){
                AddMeshletRefEncodeIndex(skinRange, ref.skin);
            }
            else if(ref.skin != s_MeshMissingStreamIndex){
                return onFailure(meshletIndex, NWB_TEXT("static meshlet cannot encode skin references"));
            }
        }

        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
            const MeshletAttributeStreamRef& ref = attributeRefs[sourceAttributeRefOffset + localAttributeIndex];
            AddMeshletAttributeRefEncodeRanges(attributeRanges, ref);
        }

        meshlet.positionBase = positionRange.minimum;
        meshlet.skinBase = skinRequired ? skinRange.minimum : s_MeshMissingStreamIndex;
        StoreMeshletAttributeRefEncodeBases(meshlet, attributeRanges);
        const MeshletRefDeltaWidth::Enum positionWidth = MeshletRefEncodeRangeWidth(positionRange);
        const MeshletRefDeltaWidth::Enum skinWidth = skinRequired ? MeshletRefEncodeRangeWidth(skinRange) : MeshletRefDeltaWidth::U8;
        const MeshletRefDeltaWidth::Enum normalWidth = MeshletRefEncodeRangeWidth(attributeRanges.normal);
        const MeshletRefDeltaWidth::Enum tangentWidth = MeshletRefEncodeRangeWidth(attributeRanges.tangent);
        const MeshletRefDeltaWidth::Enum uv0Width = MeshletRefEncodeRangeWidth(attributeRanges.uv0);
        const MeshletRefDeltaWidth::Enum colorWidth = MeshletRefEncodeRangeWidth(attributeRanges.color);
        meshlet.encoding = PackMeshletRefEncoding(
            positionWidth,
            skinWidth,
            normalWidth,
            tangentWidth,
            uv0Width,
            colorWidth
        );
        if(
            outPositionDeltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || outAttributeDeltas.size() > static_cast<usize>(Limit<u32>::s_Max)
        )
            return onFailure(meshletIndex, NWB_TEXT("encoded ref byte offset exceeds u32 limits"));

        meshlet.positionRefOffset = static_cast<u32>(outPositionDeltas.size());
        meshlet.attributeRefOffset = static_cast<u32>(outAttributeDeltas.size());

        const MeshletPositionRefEncodeChannel positionChannels[] = {
            { &MeshletPositionStreamRef::position, &MeshletDesc::positionBase, positionWidth, NWB_TEXT("position ref delta cannot be encoded") },
            { &MeshletPositionStreamRef::skin, &MeshletDesc::skinBase, skinWidth, NWB_TEXT("skin ref delta cannot be encoded") },
        };
        const usize positionChannelCount = skinRequired ? 2u : 1u;
        for(usize channelIndex = 0u; channelIndex < positionChannelCount; ++channelIndex){
            if(!AppendMeshletRefChannelDeltas(
                positionRefs,
                sourcePositionRefOffset,
                meshlet,
                meshletIndex,
                positionChannels[channelIndex],
                MeshletPositionCount(meshlet),
                outPositionDeltas,
                onFailure
            ))
                return false;
        }

        const MeshletAttributeRefEncodeChannel attributeChannels[] = {
            { &MeshletAttributeStreamRef::normal, &MeshletDesc::normalBase, normalWidth, NWB_TEXT("normal ref delta cannot be encoded") },
            { &MeshletAttributeStreamRef::tangent, &MeshletDesc::tangentBase, tangentWidth, NWB_TEXT("tangent ref delta cannot be encoded") },
            { &MeshletAttributeStreamRef::uv0, &MeshletDesc::uv0Base, uv0Width, NWB_TEXT("uv0 ref delta cannot be encoded") },
            { &MeshletAttributeStreamRef::color, &MeshletDesc::colorBase, colorWidth, NWB_TEXT("color ref delta cannot be encoded") },
        };
        for(const MeshletAttributeRefEncodeChannel& channel : attributeChannels){
            if(!AppendMeshletRefChannelDeltas(
                attributeRefs,
                sourceAttributeRefOffset,
                meshlet,
                meshletIndex,
                channel,
                MeshletAttributeCount(meshlet),
                outAttributeDeltas,
                onFailure
            ))
                return false;
        }

        if(!ValidateEncodedMeshletRefDeltas(
            meshlet,
            meshletIndex,
            sourcePositionRefOffset,
            sourceAttributeRefOffset,
            positionRefs,
            attributeRefs,
            outPositionDeltas,
            outAttributeDeltas,
            skinRequired,
            onFailure
        ))
            return false;
    }

    if(
        outPositionDeltas.size() > static_cast<usize>(Limit<u32>::s_Max)
        || outAttributeDeltas.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return onFailure(meshlets.size(), NWB_TEXT("encoded ref byte count exceeds u32 limits"));

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

