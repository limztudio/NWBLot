// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_payload_packing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr NWB_INLINE bool MeshletRefDeltaWidthValid(const MeshletRefDeltaWidth::Enum width)noexcept{
    return width == MeshletRefDeltaWidth::U8 || width == MeshletRefDeltaWidth::U16 || width == MeshletRefDeltaWidth::U32;
}

[[nodiscard]] constexpr NWB_INLINE u32 MeshletRefDeltaByteWidth(const MeshletRefDeltaWidth::Enum width)noexcept{
    return width == MeshletRefDeltaWidth::U8
        ? 1u
        : width == MeshletRefDeltaWidth::U16
            ? 2u
            : 4u
    ;
}

[[nodiscard]] NWB_INLINE Expected<usize> MeshletRefDeltaByteCount(
    const u32 refCount,
    const MeshletRefDeltaWidth::Enum width
)noexcept{
    if(!MeshletRefDeltaWidthValid(width))
        return MakeUnexpected(Failure{});

    const usize byteWidth = MeshletRefDeltaByteWidth(width);
    if(static_cast<usize>(refCount) > Limit<usize>::s_Max / byteWidth)
        return MakeUnexpected(Failure{});

    return static_cast<usize>(refCount) * byteWidth;
}

[[nodiscard]] NWB_INLINE bool AddMeshletRefDeltaByteCount(
    usize& inOutByteCount,
    const u32 refCount,
    const MeshletRefDeltaWidth::Enum width
)noexcept{
    const auto channelBytes = MeshletRefDeltaByteCount(refCount, width);
    if(!channelBytes)
        return false;
    if(*channelBytes > Limit<usize>::s_Max - inOutByteCount)
        return false;

    inOutByteCount += *channelBytes;
    return true;
}

struct MeshletPositionRefEncodingLayout{
    MeshletRefDeltaWidth::Enum positionWidth = MeshletRefDeltaWidth::U8;
    MeshletRefDeltaWidth::Enum skinWidth = MeshletRefDeltaWidth::U8;
    usize positionByteOffset = 0u;
    usize skinByteOffset = 0u;
    usize byteCount = 0u;
};

struct MeshletAttributeRefEncodingLayout{
    MeshletRefDeltaWidth::Enum normalWidth = MeshletRefDeltaWidth::U8;
    MeshletRefDeltaWidth::Enum tangentWidth = MeshletRefDeltaWidth::U8;
    MeshletRefDeltaWidth::Enum uv0Width = MeshletRefDeltaWidth::U8;
    MeshletRefDeltaWidth::Enum colorWidth = MeshletRefDeltaWidth::U8;
    usize normalByteOffset = 0u;
    usize tangentByteOffset = 0u;
    usize uv0ByteOffset = 0u;
    usize colorByteOffset = 0u;
    usize byteCount = 0u;
};

[[nodiscard]] NWB_INLINE Expected<usize> AddMeshletRefLayoutChannel(
    const usize baseOffset,
    const u32 refCount,
    const MeshletRefDeltaWidth::Enum width,
    usize& inOutRelativeOffset
)noexcept{
    if(baseOffset > Limit<usize>::s_Max - inOutRelativeOffset)
        return MakeUnexpected(Failure{});

    const usize channelByteOffset = baseOffset + inOutRelativeOffset;
    if(!AddMeshletRefDeltaByteCount(inOutRelativeOffset, refCount, width))
        return MakeUnexpected(Failure{});
    return channelByteOffset;
}

[[nodiscard]] NWB_INLINE Expected<MeshletPositionRefEncodingLayout> BuildMeshletPositionRefEncodingLayout(
    const MeshletDesc& meshlet,
    const bool skinRequired
)noexcept{
    MeshletPositionRefEncodingLayout layout;
    const u32 positionCount = MeshletPositionCount(meshlet);
    layout.positionWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingPositionShift);
    layout.skinWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingSkinShift);
    const auto positionOffset = AddMeshletRefLayoutChannel(meshlet.positionRefOffset, positionCount, layout.positionWidth, layout.byteCount);
    if(!positionOffset)
        return MakeUnexpected(Failure{});
    layout.positionByteOffset = *positionOffset;
    if(!skinRequired)
        return layout;
    const auto skinOffset = AddMeshletRefLayoutChannel(meshlet.positionRefOffset, positionCount, layout.skinWidth, layout.byteCount);
    if(!skinOffset)
        return MakeUnexpected(Failure{});
    layout.skinByteOffset = *skinOffset;
    return layout;
}

[[nodiscard]] NWB_INLINE Expected<MeshletAttributeRefEncodingLayout> BuildMeshletAttributeRefEncodingLayout(
    const MeshletDesc& meshlet
)noexcept{
    MeshletAttributeRefEncodingLayout layout;
    const u32 attributeCount = MeshletAttributeCount(meshlet);
    layout.normalWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingNormalShift);
    layout.tangentWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingTangentShift);
    layout.uv0Width = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingUv0Shift);
    layout.colorWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingColorShift);
    const auto normalOffset = AddMeshletRefLayoutChannel(meshlet.attributeRefOffset, attributeCount, layout.normalWidth, layout.byteCount);
    if(!normalOffset)
        return MakeUnexpected(Failure{});
    layout.normalByteOffset = *normalOffset;
    const auto tangentOffset = AddMeshletRefLayoutChannel(meshlet.attributeRefOffset, attributeCount, layout.tangentWidth, layout.byteCount);
    if(!tangentOffset)
        return MakeUnexpected(Failure{});
    layout.tangentByteOffset = *tangentOffset;
    const auto uv0Offset = AddMeshletRefLayoutChannel(meshlet.attributeRefOffset, attributeCount, layout.uv0Width, layout.byteCount);
    if(!uv0Offset)
        return MakeUnexpected(Failure{});
    layout.uv0ByteOffset = *uv0Offset;
    const auto colorOffset = AddMeshletRefLayoutChannel(meshlet.attributeRefOffset, attributeCount, layout.colorWidth, layout.byteCount);
    if(!colorOffset)
        return MakeUnexpected(Failure{});
    layout.colorByteOffset = *colorOffset;
    return layout;
}

[[nodiscard]] NWB_INLINE Expected<usize> MeshletEncodedPositionRefByteCount(
    const MeshletDesc& meshlet,
    const bool skinRequired
)noexcept{
    const auto layout = BuildMeshletPositionRefEncodingLayout(meshlet, skinRequired);
    if(!layout)
        return MakeUnexpected(Failure{});

    return layout->byteCount;
}

[[nodiscard]] NWB_INLINE Expected<usize> MeshletEncodedAttributeRefByteCount(const MeshletDesc& meshlet)noexcept{
    const auto layout = BuildMeshletAttributeRefEncodingLayout(meshlet);
    if(!layout)
        return MakeUnexpected(Failure{});

    return layout->byteCount;
}

[[nodiscard]] NWB_INLINE Expected<u32> DecodeMeshletRefDelta(
    const u8* const bytes,
    const usize byteCount,
    const usize byteOffset,
    const MeshletRefDeltaWidth::Enum width
)noexcept{
    if(!MeshletRefDeltaWidthValid(width))
        return MakeUnexpected(Failure{});

    const usize byteWidth = MeshletRefDeltaByteWidth(width);
    if(byteOffset > byteCount || byteWidth > byteCount - byteOffset)
        return MakeUnexpected(Failure{});

    u32 delta = static_cast<u32>(bytes[byteOffset]);
    if(width == MeshletRefDeltaWidth::U8)
        return delta;

    delta |= static_cast<u32>(bytes[byteOffset + 1u]) << s_MeshletPackedByteBits;
    if(width == MeshletRefDeltaWidth::U16)
        return delta;

    delta |= static_cast<u32>(bytes[byteOffset + 2u]) << (s_MeshletPackedByteBits * 2u);
    delta |= static_cast<u32>(bytes[byteOffset + 3u]) << (s_MeshletPackedByteBits * 3u);
    return delta;
}

[[nodiscard]] NWB_INLINE Expected<u32> DecodeMeshletRefDeltaAtIndex(
    const u8* const bytes,
    const usize byteCount,
    const usize channelByteOffset,
    const u32 localIndex,
    const MeshletRefDeltaWidth::Enum width
)noexcept{
    return DecodeMeshletRefDelta(
        bytes,
        byteCount,
        channelByteOffset + static_cast<usize>(localIndex) * MeshletRefDeltaByteWidth(width),
        width
    );
}

struct MeshletAttributeRefDecodeChannel{
    usize byteOffset = 0u;
    MeshletRefDeltaWidth::Enum width = MeshletRefDeltaWidth::U8;
    u32 base = 0u;
    u32 MeshletAttributeStreamRef::* indexMember = nullptr;
};

struct MeshletPositionRefDecodeChannel{
    usize byteOffset = 0u;
    MeshletRefDeltaWidth::Enum width = MeshletRefDeltaWidth::U8;
    u32 base = 0u;
    u32 MeshletPositionStreamRef::* indexMember = nullptr;
};

[[nodiscard]] NWB_INLINE bool DecodeMeshletPositionRefChannel(
    const u8* const bytes,
    const usize byteCount,
    const MeshletPositionRefDecodeChannel& channel,
    const u32 localPositionIndex,
    MeshletPositionStreamRef& inOutRef
)noexcept{
    const auto delta = DecodeMeshletRefDeltaAtIndex(bytes, byteCount, channel.byteOffset, localPositionIndex, channel.width);
    if(!delta)
        return false;

    inOutRef.*(channel.indexMember) = channel.base + *delta;
    return true;
}

[[nodiscard]] NWB_INLINE bool DecodeMeshletAttributeRefChannel(
    const u8* const bytes,
    const usize byteCount,
    const MeshletAttributeRefDecodeChannel& channel,
    const u32 localAttributeIndex,
    MeshletAttributeStreamRef& inOutRef
)noexcept{
    const auto delta = DecodeMeshletRefDeltaAtIndex(bytes, byteCount, channel.byteOffset, localAttributeIndex, channel.width);
    if(!delta)
        return false;

    inOutRef.*(channel.indexMember) = channel.base + *delta;
    return true;
}

[[nodiscard]] NWB_INLINE Expected<MeshletPositionStreamRef> DecodeMeshletPositionRef(
    const u8* const bytes,
    const usize byteCount,
    const MeshletDesc& meshlet,
    const u32 localPositionIndex,
    const bool skinRequired
)noexcept{
    MeshletPositionStreamRef ref;
    const u32 positionCount = MeshletPositionCount(meshlet);
    if(localPositionIndex >= positionCount)
        return MakeUnexpected(Failure{});

    const auto layout = BuildMeshletPositionRefEncodingLayout(meshlet, skinRequired);
    if(!layout)
        return MakeUnexpected(Failure{});

    const MeshletPositionRefDecodeChannel positionChannel{
        layout->positionByteOffset,
        layout->positionWidth,
        meshlet.positionBase,
        &MeshletPositionStreamRef::position,
    };
    if(!DecodeMeshletPositionRefChannel(bytes, byteCount, positionChannel, localPositionIndex, ref))
        return MakeUnexpected(Failure{});

    if(!skinRequired){
        if(meshlet.skinBase != s_MeshMissingStreamIndex)
            return MakeUnexpected(Failure{});
        return ref;
    }

    const MeshletPositionRefDecodeChannel skinChannel{
        layout->skinByteOffset,
        layout->skinWidth,
        meshlet.skinBase,
        &MeshletPositionStreamRef::skin,
    };
    if(!DecodeMeshletPositionRefChannel(bytes, byteCount, skinChannel, localPositionIndex, ref))
        return MakeUnexpected(Failure{});
    return ref;
}

[[nodiscard]] NWB_INLINE Expected<MeshletAttributeStreamRef> DecodeMeshletAttributeRef(
    const u8* const bytes,
    const usize byteCount,
    const MeshletDesc& meshlet,
    const u32 localAttributeIndex
)noexcept{
    MeshletAttributeStreamRef ref;
    const u32 attributeCount = MeshletAttributeCount(meshlet);
    if(localAttributeIndex >= attributeCount)
        return MakeUnexpected(Failure{});

    const auto layout = BuildMeshletAttributeRefEncodingLayout(meshlet);
    if(!layout)
        return MakeUnexpected(Failure{});

    const MeshletAttributeRefDecodeChannel channels[] = {
        { layout->normalByteOffset, layout->normalWidth, meshlet.normalBase, &MeshletAttributeStreamRef::normal },
        { layout->tangentByteOffset, layout->tangentWidth, meshlet.tangentBase, &MeshletAttributeStreamRef::tangent },
        { layout->uv0ByteOffset, layout->uv0Width, meshlet.uv0Base, &MeshletAttributeStreamRef::uv0 },
        { layout->colorByteOffset, layout->colorWidth, meshlet.colorBase, &MeshletAttributeStreamRef::color },
    };
    for(const MeshletAttributeRefDecodeChannel& channel : channels){
        if(!DecodeMeshletAttributeRefChannel(bytes, byteCount, channel, localAttributeIndex, ref))
            return MakeUnexpected(Failure{});
    }

    return ref;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

