// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_ref_decode.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MeshletPackedByte2Shift = s_MeshletPackedByteBits * 2u;
inline constexpr u32 s_MeshletPackedByte3Shift = s_MeshletPackedByteBits * 3u;
inline constexpr usize s_StaticMeshletPositionChannelCount = 1u;
inline constexpr usize s_SkinnedMeshletPositionChannelCount = 2u;
inline constexpr TStringView s_InvalidEncodedRefWidthMessage = NWB_TEXT("encoded ref width is invalid");
inline constexpr TStringView s_EncodedRefByteCountMismatchMessage = NWB_TEXT("encoded ref byte count mismatch");
inline constexpr TStringView s_EncodedPositionRefDecodeMismatchMessage = NWB_TEXT("encoded position ref decode mismatch");
inline constexpr TStringView s_EncodedAttributeRefDecodeMismatchMessage = NWB_TEXT("encoded attribute ref decode mismatch");
inline constexpr TStringView s_EmptyMeshletRefRangeMessage = NWB_TEXT("meshlet cannot encode empty ref ranges");
inline constexpr TStringView s_StaticMeshletSkinRefMessage = NWB_TEXT("static meshlet cannot encode skin references");
inline constexpr TStringView s_EncodedRefByteOffsetLimitMessage = NWB_TEXT("encoded ref byte offset exceeds u32 limits");
inline constexpr TStringView s_EncodedRefByteCountLimitMessage = NWB_TEXT("encoded ref byte count exceeds u32 limits");

template<typename ByteVectorT>
void AppendMeshletRefDelta(ByteVectorT& outBytes, const u32 delta, const MeshletRefDeltaWidth::Enum width)noexcept(noexcept(outBytes.push_back(u8{}))){
    NWB_ASSERT(MeshletRefDeltaFitsWidth(delta, width));
    outBytes.push_back(static_cast<u8>(delta & s_MeshletPackedByteMask));
    if(width == MeshletRefDeltaWidth::U8)
        return;

    outBytes.push_back(static_cast<u8>((delta >> s_MeshletPackedByteBits) & s_MeshletPackedByteMask));
    if(width == MeshletRefDeltaWidth::U16)
        return;

    outBytes.push_back(static_cast<u8>((delta >> s_MeshletPackedByte2Shift) & s_MeshletPackedByteMask));
    outBytes.push_back(static_cast<u8>((delta >> s_MeshletPackedByte3Shift) & s_MeshletPackedByteMask));
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
};

using MeshletPositionRefEncodeChannel = MeshletRefEncodeChannel<MeshletPositionStreamRef>;
using MeshletAttributeRefEncodeChannel = MeshletRefEncodeChannel<MeshletAttributeStreamRef>;

NWB_INLINE void AddMeshletRefEncodeIndex(MeshletRefEncodeRange& range, const u32 index)noexcept{
    range.minimum = Min(range.minimum, index);
    range.maximum = Max(range.maximum, index);
}

NWB_INLINE void AddMeshletAttributeRefEncodeRanges(MeshletAttributeRefEncodeRanges& ranges, const MeshletAttributeStreamRef& ref)noexcept{
    AddMeshletRefEncodeIndex(ranges.normal, ref.normal);
    AddMeshletRefEncodeIndex(ranges.tangent, ref.tangent);
    AddMeshletRefEncodeIndex(ranges.uv0, ref.uv0);
    AddMeshletRefEncodeIndex(ranges.color, ref.color);
}

NWB_INLINE void StoreMeshletAttributeRefEncodeBases(MeshletDesc& meshlet, const MeshletAttributeRefEncodeRanges& ranges)noexcept{
    meshlet.normalBase = ranges.normal.minimum;
    meshlet.tangentBase = ranges.tangent.minimum;
    meshlet.uv0Base = ranges.uv0.minimum;
    meshlet.colorBase = ranges.color.minimum;
}

[[nodiscard]] NWB_INLINE MeshletRefDeltaWidth::Enum MeshletRefEncodeRangeWidth(const MeshletRefEncodeRange& range)noexcept{
    return MeshletRefDeltaWidthForMaxDelta(range.maximum - range.minimum);
}

// The channel base and width come from these same refs, so every delta is in range.
template<typename RefT, typename DeltaVectorT>
void AppendMeshletRefChannelDeltas(const Span<const RefT> refs, const u32 sourceRefOffset, const MeshletDesc& meshlet, const MeshletRefEncodeChannel<RefT>& channel, const u32 refCount, DeltaVectorT& outDeltas)noexcept(
    noexcept(AppendMeshletRefDelta(outDeltas, u32{}, channel.width))
){
    const u32 base = meshlet.*(channel.baseMember);
    for(u32 localRefIndex = 0u; localRefIndex < refCount; ++localRefIndex){
        const u32 index = refs[sourceRefOffset + localRefIndex].*(channel.indexMember);
        NWB_ASSERT(index >= base);
        AppendMeshletRefDelta(outDeltas, index - base, channel.width);
    }
}

[[nodiscard]] NWB_INLINE bool MeshletDecodedPositionRefMatches(const MeshletPositionStreamRef& decoded, const MeshletPositionStreamRef& source, const bool skinRequired)noexcept{
    return decoded.position == source.position && decoded.skin == (skinRequired ? source.skin : s_MeshMissingStreamIndex);
}

[[nodiscard]] NWB_INLINE bool MeshletDecodedAttributeRefMatches(const MeshletAttributeStreamRef& decoded, const MeshletAttributeStreamRef& source)noexcept{
    return decoded.normal == source.normal && decoded.tangent == source.tangent && decoded.uv0 == source.uv0 && decoded.color == source.color;
}

template<typename FailureHandlerT>
[[nodiscard]] bool ValidateEncodedMeshletRefDeltas(
    const MeshletDesc& meshlet,
    const usize meshletIndex,
    const u32 sourcePositionRefOffset,
    const u32 sourceAttributeRefOffset,
    const Span<const MeshletPositionStreamRef> positionRefs,
    const Span<const MeshletAttributeStreamRef> attributeRefs,
    const Span<const u8> positionDeltas,
    const Span<const u8> attributeDeltas,
    const bool skinRequired,
    FailureHandlerT onFailure
)noexcept(
    IsNothrowDestructible_V<FailureHandlerT>
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(meshletIndex, s_InvalidEncodedRefWidthMessage)))
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(meshletIndex, s_EncodedRefByteCountMismatchMessage)))
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(meshletIndex, s_EncodedPositionRefDecodeMismatchMessage)))
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(meshletIndex, s_EncodedAttributeRefDecodeMismatchMessage)))
){
    const auto encodedPositionBytes = MeshletEncodedPositionRefByteCount(meshlet, skinRequired);
    const auto encodedAttributeBytes = MeshletEncodedAttributeRefByteCount(meshlet);
    if(!encodedPositionBytes || !encodedAttributeBytes)
        return onFailure(meshletIndex, s_InvalidEncodedRefWidthMessage);
    if(
        meshlet.positionRefOffset > positionDeltas.size()
        || *encodedPositionBytes != positionDeltas.size() - meshlet.positionRefOffset
        || meshlet.attributeRefOffset > attributeDeltas.size()
        || *encodedAttributeBytes != attributeDeltas.size() - meshlet.attributeRefOffset
    )
        return onFailure(meshletIndex, s_EncodedRefByteCountMismatchMessage);

    for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
        const MeshletPositionStreamRef& sourceRef = positionRefs[sourcePositionRefOffset + localPositionIndex];
        const auto decodedRef = DecodeMeshletPositionRef(positionDeltas.data(), positionDeltas.size(), meshlet, localPositionIndex, skinRequired);
        if(!decodedRef || !MeshletDecodedPositionRefMatches(*decodedRef, sourceRef, skinRequired))
            return onFailure(meshletIndex, s_EncodedPositionRefDecodeMismatchMessage);
    }

    for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
        const MeshletAttributeStreamRef& sourceRef = attributeRefs[sourceAttributeRefOffset + localAttributeIndex];
        const auto decodedRef = DecodeMeshletAttributeRef(attributeDeltas.data(), attributeDeltas.size(), meshlet, localAttributeIndex);
        if(!decodedRef || !MeshletDecodedAttributeRefMatches(*decodedRef, sourceRef))
            return onFailure(meshletIndex, s_EncodedAttributeRefDecodeMismatchMessage);
    }

    return true;
}

template<
    typename PositionDeltaVectorT,
    typename AttributeDeltaVectorT,
    typename FailureHandlerT
>
[[nodiscard]] bool EncodeMeshletRefDeltas(
    const Span<const MeshletPositionStreamRef> positionRefs,
    const Span<const MeshletAttributeStreamRef> attributeRefs,
    const bool skinRequired,
    FailureHandlerT onFailure,
    const Span<MeshletDesc> meshlets,
    PositionDeltaVectorT& outPositionDeltas,
    AttributeDeltaVectorT& outAttributeDeltas
)noexcept(
    IsNothrowDestructible_V<FailureHandlerT>
    && noexcept(outPositionDeltas.clear())
    && noexcept(outAttributeDeltas.clear())
    && noexcept(outPositionDeltas.reserve(positionRefs.size() * sizeof(MeshletPositionStreamRef)))
    && noexcept(outAttributeDeltas.reserve(attributeRefs.size() * sizeof(MeshletAttributeStreamRef)))
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(DeclVal<usize&>(), s_EmptyMeshletRefRangeMessage)))
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(DeclVal<usize&>(), s_StaticMeshletSkinRefMessage)))
    && noexcept(static_cast<bool>(outPositionDeltas.size() > static_cast<usize>(Limit<u32>::s_Max) || outAttributeDeltas.size() > static_cast<usize>(Limit<u32>::s_Max)))
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(DeclVal<usize&>(), s_EncodedRefByteOffsetLimitMessage)))
    && noexcept(static_cast<u32>(outPositionDeltas.size()))
    && noexcept(static_cast<u32>(outAttributeDeltas.size()))
    && noexcept(AppendMeshletRefChannelDeltas(positionRefs, DeclVal<const u32&>(), DeclVal<const MeshletDesc&>(), DeclVal<const MeshletPositionRefEncodeChannel&>(), u32{}, outPositionDeltas))
    && noexcept(AppendMeshletRefChannelDeltas(attributeRefs, DeclVal<const u32&>(), DeclVal<const MeshletDesc&>(), DeclVal<const MeshletAttributeRefEncodeChannel&>(), u32{}, outAttributeDeltas))
#if NWB_OCCUR_ASSERT
    && noexcept(ValidateEncodedMeshletRefDeltas(
        DeclVal<const MeshletDesc&>(),
        DeclVal<usize&>(),
        DeclVal<const u32&>(),
        DeclVal<const u32&>(),
        positionRefs,
        attributeRefs,
        { outPositionDeltas.data(), outPositionDeltas.size() },
        { outAttributeDeltas.data(), outAttributeDeltas.size() },
        skinRequired,
        onFailure
    ))
#endif
    && noexcept(DeclVal<void(*)(bool)noexcept>()(onFailure(meshlets.size(), s_EncodedRefByteCountLimitMessage)))
){
    outPositionDeltas.clear();
    outAttributeDeltas.clear();
    outPositionDeltas.reserve(positionRefs.size() * sizeof(MeshletPositionStreamRef));
    outAttributeDeltas.reserve(attributeRefs.size() * sizeof(MeshletAttributeStreamRef));

    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        MeshletDesc& meshlet = meshlets[meshletIndex];
        if(MeshletPositionCount(meshlet) == 0u || MeshletAttributeCount(meshlet) == 0u)
            return onFailure(meshletIndex, s_EmptyMeshletRefRangeMessage);

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
                return onFailure(meshletIndex, s_StaticMeshletSkinRefMessage);
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
        meshlet.encoding = PackMeshletRefEncoding(positionWidth, skinWidth, normalWidth, tangentWidth, uv0Width, colorWidth);
        if(outPositionDeltas.size() > static_cast<usize>(Limit<u32>::s_Max) || outAttributeDeltas.size() > static_cast<usize>(Limit<u32>::s_Max))
            return onFailure(meshletIndex, s_EncodedRefByteOffsetLimitMessage);

        meshlet.positionRefOffset = static_cast<u32>(outPositionDeltas.size());
        meshlet.attributeRefOffset = static_cast<u32>(outAttributeDeltas.size());

        const MeshletPositionRefEncodeChannel positionChannels[] = {
            { &MeshletPositionStreamRef::position, &MeshletDesc::positionBase, positionWidth },
            { &MeshletPositionStreamRef::skin, &MeshletDesc::skinBase, skinWidth },
        };
        const usize positionChannelCount = skinRequired ? s_SkinnedMeshletPositionChannelCount : s_StaticMeshletPositionChannelCount;
        for(usize channelIndex = 0u; channelIndex < positionChannelCount; ++channelIndex)
            AppendMeshletRefChannelDeltas(positionRefs, sourcePositionRefOffset, meshlet, positionChannels[channelIndex], MeshletPositionCount(meshlet), outPositionDeltas);

        const MeshletAttributeRefEncodeChannel attributeChannels[] = {
            { &MeshletAttributeStreamRef::normal, &MeshletDesc::normalBase, normalWidth },
            { &MeshletAttributeStreamRef::tangent, &MeshletDesc::tangentBase, tangentWidth },
            { &MeshletAttributeStreamRef::uv0, &MeshletDesc::uv0Base, uv0Width },
            { &MeshletAttributeStreamRef::color, &MeshletDesc::colorBase, colorWidth },
        };
        for(const MeshletAttributeRefEncodeChannel& channel : attributeChannels)
            AppendMeshletRefChannelDeltas(attributeRefs, sourceAttributeRefOffset, meshlet, channel, MeshletAttributeCount(meshlet), outAttributeDeltas);

        NWB_ASSERT(ValidateEncodedMeshletRefDeltas(meshlet, meshletIndex, sourcePositionRefOffset, sourceAttributeRefOffset, positionRefs, attributeRefs, { outPositionDeltas.data(), outPositionDeltas.size() }, { outAttributeDeltas.data(), outAttributeDeltas.size() }, skinRequired, onFailure));
    }

    if(outPositionDeltas.size() > static_cast<usize>(Limit<u32>::s_Max) || outAttributeDeltas.size() > static_cast<usize>(Limit<u32>::s_Max))
        return onFailure(meshlets.size(), s_EncodedRefByteCountLimitMessage);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

