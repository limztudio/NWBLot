// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_payload_packing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr GLB_INLINE bool MeshletRefDeltaWidthValid(const MeshletRefDeltaWidth::Enum width)noexcept{
    return width == MeshletRefDeltaWidth::U8 || width == MeshletRefDeltaWidth::U16 || width == MeshletRefDeltaWidth::U32;
}

[[nodiscard]] constexpr GLB_INLINE u32 MeshletRefDeltaByteWidth(const MeshletRefDeltaWidth::Enum width)noexcept{
    return width == MeshletRefDeltaWidth::U8
        ? 1u
        : width == MeshletRefDeltaWidth::U16
            ? 2u
            : 4u
    ;
}

[[nodiscard]] GLB_INLINE bool MeshletRefDeltaByteCount(
    const u32 refCount,
    const MeshletRefDeltaWidth::Enum width,
    usize& outByteCount
)noexcept{
    outByteCount = 0u;
    if(!MeshletRefDeltaWidthValid(width))
        return false;

    const usize byteWidth = MeshletRefDeltaByteWidth(width);
    if(static_cast<usize>(refCount) > Limit<usize>::s_Max / byteWidth)
        return false;

    outByteCount = static_cast<usize>(refCount) * byteWidth;
    return true;
}

[[nodiscard]] GLB_INLINE bool AddMeshletRefDeltaByteCount(
    usize& inOutByteCount,
    const u32 refCount,
    const MeshletRefDeltaWidth::Enum width
)noexcept{
    usize channelBytes = 0u;
    if(!MeshletRefDeltaByteCount(refCount, width, channelBytes))
        return false;
    if(channelBytes > Limit<usize>::s_Max - inOutByteCount)
        return false;

    inOutByteCount += channelBytes;
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

[[nodiscard]] GLB_INLINE bool AddMeshletRefLayoutChannel(
    const usize baseOffset,
    const u32 refCount,
    const MeshletRefDeltaWidth::Enum width,
    usize& inOutRelativeOffset,
    usize& outChannelByteOffset
)noexcept{
    if(baseOffset > Limit<usize>::s_Max - inOutRelativeOffset)
        return false;

    outChannelByteOffset = baseOffset + inOutRelativeOffset;
    return AddMeshletRefDeltaByteCount(inOutRelativeOffset, refCount, width);
}

[[nodiscard]] GLB_INLINE bool BuildMeshletPositionRefEncodingLayout(
    const MeshletDesc& meshlet,
    const bool skinRequired,
    MeshletPositionRefEncodingLayout& outLayout
)noexcept{
    outLayout = {};
    const u32 positionCount = MeshletPositionCount(meshlet);
    outLayout.positionWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingPositionShift);
    outLayout.skinWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingSkinShift);
    if(
        !AddMeshletRefLayoutChannel(
            meshlet.positionRefOffset,
            positionCount,
            outLayout.positionWidth,
            outLayout.byteCount,
            outLayout.positionByteOffset
        )
    )
        return false;

    if(!skinRequired)
        return true;

    return AddMeshletRefLayoutChannel(
        meshlet.positionRefOffset,
        positionCount,
        outLayout.skinWidth,
        outLayout.byteCount,
        outLayout.skinByteOffset
    );
}

[[nodiscard]] GLB_INLINE bool BuildMeshletAttributeRefEncodingLayout(
    const MeshletDesc& meshlet,
    MeshletAttributeRefEncodingLayout& outLayout
)noexcept{
    outLayout = {};
    const u32 attributeCount = MeshletAttributeCount(meshlet);
    outLayout.normalWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingNormalShift);
    outLayout.tangentWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingTangentShift);
    outLayout.uv0Width = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingUv0Shift);
    outLayout.colorWidth = MeshletRefEncodingWidth(meshlet.encoding, s_MeshletRefEncodingColorShift);
    return
        AddMeshletRefLayoutChannel(
            meshlet.attributeRefOffset,
            attributeCount,
            outLayout.normalWidth,
            outLayout.byteCount,
            outLayout.normalByteOffset
        )
        && AddMeshletRefLayoutChannel(
            meshlet.attributeRefOffset,
            attributeCount,
            outLayout.tangentWidth,
            outLayout.byteCount,
            outLayout.tangentByteOffset
        )
        && AddMeshletRefLayoutChannel(
            meshlet.attributeRefOffset,
            attributeCount,
            outLayout.uv0Width,
            outLayout.byteCount,
            outLayout.uv0ByteOffset
        )
        && AddMeshletRefLayoutChannel(
            meshlet.attributeRefOffset,
            attributeCount,
            outLayout.colorWidth,
            outLayout.byteCount,
            outLayout.colorByteOffset
        )
    ;
}

[[nodiscard]] GLB_INLINE bool MeshletEncodedPositionRefByteCount(
    const MeshletDesc& meshlet,
    const bool skinRequired,
    usize& outByteCount
)noexcept{
    MeshletPositionRefEncodingLayout layout;
    if(!BuildMeshletPositionRefEncodingLayout(meshlet, skinRequired, layout))
        return false;

    outByteCount = layout.byteCount;
    return true;
}

[[nodiscard]] GLB_INLINE bool MeshletEncodedAttributeRefByteCount(const MeshletDesc& meshlet, usize& outByteCount)noexcept{
    MeshletAttributeRefEncodingLayout layout;
    if(!BuildMeshletAttributeRefEncodingLayout(meshlet, layout))
        return false;

    outByteCount = layout.byteCount;
    return true;
}

[[nodiscard]] GLB_INLINE bool DecodeMeshletRefDelta(
    const u8* const bytes,
    const usize byteCount,
    const usize byteOffset,
    const MeshletRefDeltaWidth::Enum width,
    u32& outDelta
)noexcept{
    outDelta = 0u;
    if(!MeshletRefDeltaWidthValid(width))
        return false;

    const usize byteWidth = MeshletRefDeltaByteWidth(width);
    if(byteOffset > byteCount || byteWidth > byteCount - byteOffset)
        return false;

    outDelta = static_cast<u32>(bytes[byteOffset]);
    if(width == MeshletRefDeltaWidth::U8)
        return true;

    outDelta |= static_cast<u32>(bytes[byteOffset + 1u]) << s_MeshletPackedByteBits;
    if(width == MeshletRefDeltaWidth::U16)
        return true;

    outDelta |= static_cast<u32>(bytes[byteOffset + 2u]) << (s_MeshletPackedByteBits * 2u);
    outDelta |= static_cast<u32>(bytes[byteOffset + 3u]) << (s_MeshletPackedByteBits * 3u);
    return true;
}

[[nodiscard]] GLB_INLINE bool DecodeMeshletRefDeltaAtIndex(
    const u8* const bytes,
    const usize byteCount,
    const usize channelByteOffset,
    const u32 localIndex,
    const MeshletRefDeltaWidth::Enum width,
    u32& outDelta
)noexcept{
    return DecodeMeshletRefDelta(
        bytes,
        byteCount,
        channelByteOffset + static_cast<usize>(localIndex) * MeshletRefDeltaByteWidth(width),
        width,
        outDelta
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

[[nodiscard]] GLB_INLINE bool DecodeMeshletPositionRefChannel(
    const u8* const bytes,
    const usize byteCount,
    const MeshletPositionRefDecodeChannel& channel,
    const u32 localPositionIndex,
    MeshletPositionStreamRef& outRef
)noexcept{
    u32 delta = 0u;
    if(!DecodeMeshletRefDeltaAtIndex(bytes, byteCount, channel.byteOffset, localPositionIndex, channel.width, delta))
        return false;

    outRef.*(channel.indexMember) = channel.base + delta;
    return true;
}

[[nodiscard]] GLB_INLINE bool DecodeMeshletAttributeRefChannel(
    const u8* const bytes,
    const usize byteCount,
    const MeshletAttributeRefDecodeChannel& channel,
    const u32 localAttributeIndex,
    MeshletAttributeStreamRef& outRef
)noexcept{
    u32 delta = 0u;
    if(!DecodeMeshletRefDeltaAtIndex(bytes, byteCount, channel.byteOffset, localAttributeIndex, channel.width, delta))
        return false;

    outRef.*(channel.indexMember) = channel.base + delta;
    return true;
}

[[nodiscard]] GLB_INLINE bool DecodeMeshletPositionRef(
    const u8* const bytes,
    const usize byteCount,
    const MeshletDesc& meshlet,
    const u32 localPositionIndex,
    const bool skinRequired,
    MeshletPositionStreamRef& outRef
)noexcept{
    outRef = {};
    const u32 positionCount = MeshletPositionCount(meshlet);
    if(localPositionIndex >= positionCount)
        return false;

    MeshletPositionRefEncodingLayout layout;
    if(!BuildMeshletPositionRefEncodingLayout(meshlet, skinRequired, layout))
        return false;

    const MeshletPositionRefDecodeChannel positionChannel{
        layout.positionByteOffset,
        layout.positionWidth,
        meshlet.positionBase,
        &MeshletPositionStreamRef::position,
    };
    if(!DecodeMeshletPositionRefChannel(bytes, byteCount, positionChannel, localPositionIndex, outRef))
        return false;

    if(!skinRequired)
        return meshlet.skinBase == s_MeshMissingStreamIndex;

    const MeshletPositionRefDecodeChannel skinChannel{
        layout.skinByteOffset,
        layout.skinWidth,
        meshlet.skinBase,
        &MeshletPositionStreamRef::skin,
    };
    return DecodeMeshletPositionRefChannel(bytes, byteCount, skinChannel, localPositionIndex, outRef);
}

[[nodiscard]] GLB_INLINE bool DecodeMeshletAttributeRef(
    const u8* const bytes,
    const usize byteCount,
    const MeshletDesc& meshlet,
    const u32 localAttributeIndex,
    MeshletAttributeStreamRef& outRef
)noexcept{
    outRef = {};
    const u32 attributeCount = MeshletAttributeCount(meshlet);
    if(localAttributeIndex >= attributeCount)
        return false;

    MeshletAttributeRefEncodingLayout layout;
    if(!BuildMeshletAttributeRefEncodingLayout(meshlet, layout))
        return false;

    const MeshletAttributeRefDecodeChannel channels[] = {
        { layout.normalByteOffset, layout.normalWidth, meshlet.normalBase, &MeshletAttributeStreamRef::normal },
        { layout.tangentByteOffset, layout.tangentWidth, meshlet.tangentBase, &MeshletAttributeStreamRef::tangent },
        { layout.uv0ByteOffset, layout.uv0Width, meshlet.uv0Base, &MeshletAttributeStreamRef::uv0 },
        { layout.colorByteOffset, layout.colorWidth, meshlet.colorBase, &MeshletAttributeStreamRef::color },
    };
    for(const MeshletAttributeRefDecodeChannel& channel : channels){
        if(!DecodeMeshletAttributeRefChannel(bytes, byteCount, channel, localAttributeIndex, outRef))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

