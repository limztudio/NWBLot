// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_FrameGraphLegacyPayloadVersion = 1u;
inline constexpr u16 s_FrameGraphPayloadVersion = 2u;
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

namespace FrameGraphQueueClass{
    enum Enum : u8{
        Unknown = 0u,
        Graphics = 1u,
        Compute = 2u,
        Transfer = 3u,

        kCount = 4u,
    };
};

namespace FrameGraphQueueAssignmentReason{
    enum Enum : u8{
        Unknown = 0u,
        RequiredGraphics = 1u,
        PreferredQueue = 2u,
        DedicatedCompute = 3u,
        DedicatedTransfer = 4u,
        Fallback = 5u,
        ConservativeAny = 6u,
        SameClassRouting = 7u,
        CompilerOverride = 8u,
        ScoredAny = 9u,

        kCount = 10u,
    };
};

namespace FrameGraphQueueAssignmentModifier{
    enum Mask : u8{
        None = 0u,
        DirectDependencyAffinity = 1u << 0u,
        SameClassLoadBalance = 1u << 1u,
        NonPrimaryPreference = 1u << 2u,
        DebugTimingOverride = 1u << 3u,
        TimingCalibration = 1u << 4u,
        TimingFeedback = 1u << 5u,

        All = DirectDependencyAffinity
            | SameClassLoadBalance
            | NonPrimaryPreference
            | DebugTimingOverride
            | TimingCalibration
            | TimingFeedback,
    };
};

namespace FrameGraphQueueAssignmentAcceptance{
    enum Enum : u8{
        NotAccepted = 0u,
        First = 1u,
        Unchanged = 2u,
        Changed = 3u,

        kCount = 4u,
    };
};

struct FrameGraphPhysicalQueueId{
    u16 index = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;

    [[nodiscard]] constexpr bool valid()const noexcept{
        return index != Limit<u16>::s_Max && deviceGeneration != 0u;
    }
};
inline constexpr bool operator==(const FrameGraphPhysicalQueueId& lhs, const FrameGraphPhysicalQueueId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.deviceGeneration == rhs.deviceGeneration;
}
inline constexpr bool operator!=(const FrameGraphPhysicalQueueId& lhs, const FrameGraphPhysicalQueueId& rhs)noexcept{
    return !(lhs == rhs);
}

struct FrameGraphQueueAssignmentScore{
    i32 preference = 0;
    i32 overlap = 0;
    i32 queueLoad = 0;
    i32 incomingCrossings = 0;
    i32 outgoingCrossings = 0;
    i32 ownershipTransfers = 0;
    i32 total = 0;
};

struct FrameGraphQueueAssignment{
    FrameGraphPhysicalQueueId initialQueue;
    FrameGraphPhysicalQueueId plannedQueue;
    FrameGraphPhysicalQueueId acceptedQueue;
    FrameGraphPhysicalQueueId previousAcceptedQueue;
    FrameGraphQueueAssignmentScore score;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    FrameGraphQueueAssignmentReason::Enum reason = FrameGraphQueueAssignmentReason::Unknown;
    FrameGraphQueueAssignmentModifier::Mask modifiers = FrameGraphQueueAssignmentModifier::None;
    FrameGraphQueueAssignmentAcceptance::Enum acceptance = FrameGraphQueueAssignmentAcceptance::NotAccepted;
    bool dedicated = false;
    bool present = false;
};

#pragma pack(push, 1)
struct EncodedFrameGraphPayloadHeader{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphLegacyPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
};

struct EncodedFrameGraphPayloadHeaderV2{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
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

struct EncodedFrameGraphPhysicalQueueId{
    u16 index = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;
};

struct EncodedFrameGraphQueueAssignment{
    u32 nodeIndex = 0u;
    EncodedFrameGraphPhysicalQueueId initialQueue;
    EncodedFrameGraphPhysicalQueueId plannedQueue;
    EncodedFrameGraphPhysicalQueueId acceptedQueue;
    EncodedFrameGraphPhysicalQueueId previousAcceptedQueue;
    i32 scorePreference = 0;
    i32 scoreOverlap = 0;
    i32 scoreQueueLoad = 0;
    i32 scoreIncomingCrossings = 0;
    i32 scoreOutgoingCrossings = 0;
    i32 scoreOwnershipTransfers = 0;
    i32 scoreTotal = 0;
    u8 queueClass = FrameGraphQueueClass::Unknown;
    u8 reason = FrameGraphQueueAssignmentReason::Unknown;
    u8 modifiers = FrameGraphQueueAssignmentModifier::None;
    u8 acceptance = FrameGraphQueueAssignmentAcceptance::NotAccepted;
    u8 dedicated = 0u;
    u8 reserved[3u] = {};
};
#pragma pack(pop)
static_assert(sizeof(EncodedFrameGraphPayloadHeader) == 28u, "EncodedFrameGraphPayloadHeader wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeader) == 1u, "EncodedFrameGraphPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV2) == 32u, "EncodedFrameGraphPayloadHeaderV2 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV2) == 1u, "EncodedFrameGraphPayloadHeaderV2 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV2>, "EncodedFrameGraphPayloadHeaderV2 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV2>, "EncodedFrameGraphPayloadHeaderV2 must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphNode) == 72u, "EncodedFrameGraphNode wire layout drifted");
static_assert(alignof(EncodedFrameGraphNode) == 1u, "EncodedFrameGraphNode must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphEdge) == 12u, "EncodedFrameGraphEdge wire layout drifted");
static_assert(alignof(EncodedFrameGraphEdge) == 1u, "EncodedFrameGraphEdge must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPhysicalQueueId) == 4u, "EncodedFrameGraphPhysicalQueueId wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueId) == 1u, "EncodedFrameGraphPhysicalQueueId must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphQueueAssignment) == 56u, "EncodedFrameGraphQueueAssignment wire layout drifted");
static_assert(alignof(EncodedFrameGraphQueueAssignment) == 1u, "EncodedFrameGraphQueueAssignment must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");

struct FrameGraphNodeDesc{
    Name name = NAME_NONE;
    AStringView label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    FrameGraphQueueAssignment queueAssignment;
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
    FrameGraphQueueAssignment queueAssignment;

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
[[nodiscard]] bool IsValidFrameGraphQueueClass(FrameGraphQueueClass::Enum queueClass)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignmentReason(FrameGraphQueueAssignmentReason::Enum reason)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignmentAcceptance(FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignment(const FrameGraphQueueAssignment& assignment)noexcept;
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

