// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_FrameGraphPayloadVersion = 1u;
inline constexpr u32 s_FrameGraphPayloadMagic = 0x4E574647u; // NWFG

namespace FrameGraphNodeKind{
    enum Enum : u8{
        Unknown,
        Pass,
        Resource,
        External,
    };
};

namespace FrameGraphEdgeKind{
    enum Enum : u8{
        Unknown,
        Reads,
        Writes,
        DependsOn,
    };
};

#pragma pack(push, 1)
struct EncodedFrameGraphPayloadHeader{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
};

struct EncodedFrameGraphNode{
    NameHash nameHash = {};
    u32 labelOffset = 0u;
    u8 kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    u16 reserved = 0u;
};

struct EncodedFrameGraphEdge{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    u8 kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
    u16 reserved = 0u;
};
#pragma pack(pop)
static_assert(sizeof(EncodedFrameGraphPayloadHeader) == 28u, "EncodedFrameGraphPayloadHeader wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeader) == 1u, "EncodedFrameGraphPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphNode) == 72u, "EncodedFrameGraphNode wire layout drifted");
static_assert(alignof(EncodedFrameGraphNode) == 1u, "EncodedFrameGraphNode must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphEdge) == 12u, "EncodedFrameGraphEdge wire layout drifted");
static_assert(alignof(EncodedFrameGraphEdge) == 1u, "EncodedFrameGraphEdge must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");

struct FrameGraphNodeDesc{
    Name name = NAME_NONE;
    AStringView label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
};

struct FrameGraphEdgeDesc{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

struct FrameGraphNodePayload{
    Name name = NAME_NONE;
    AString<TelemetryArena> label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;

    explicit FrameGraphNodePayload(TelemetryArena& arena)
        : label(arena)
    {}
};

struct FrameGraphEdgePayload{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

struct FrameGraphPayload{
    u64 frameIndex = 0u;
    Vector<FrameGraphNodePayload, TelemetryArena> nodes;
    Vector<FrameGraphEdgePayload, TelemetryArena> edges;

    explicit FrameGraphPayload(TelemetryArena& arena)
        : nodes(arena)
        , edges(arena)
    {}
};

using FrameGraphNodeDescs = Vector<FrameGraphNodeDesc, TelemetryArena>;
using FrameGraphEdgeDescs = Vector<FrameGraphEdgeDesc, TelemetryArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsValidFrameGraphNodeKind(FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] bool IsValidFrameGraphEdgeKind(FrameGraphEdgeKind::Enum kind)noexcept;
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool ParseFrameGraphPayload(TelemetryArena& arena, const void* payload, usize payloadBytes, FrameGraphPayload& outPayload);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    u32 streamId = 0u
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
