// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_FrameGraphPayloadVersion = 9u;
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
    static constexpr u8 s_FrameGraphQueueClassUnknownBase = 0u;
    static constexpr u8 s_FrameGraphQueueClassCountValue = 4u;
    enum Enum : u8{
        Unknown = s_FrameGraphQueueClassUnknownBase,
        Graphics,
        Compute,
        Transfer,

        kCount = s_FrameGraphQueueClassCountValue,
    };
};

namespace FrameGraphQueueAssignmentReason{
    static constexpr u8 s_FrameGraphQueueAssignmentReasonUnknownBase = 0u;
    static constexpr u8 s_FrameGraphQueueAssignmentReasonCountValue = 4u;
    enum Enum : u8{
        Unknown = s_FrameGraphQueueAssignmentReasonUnknownBase,
        RequiredGraphics,
        Conservative,
        Scored,

        kCount = s_FrameGraphQueueAssignmentReasonCountValue,
    };
};

namespace FrameGraphQueueAssignmentModifier{
    static constexpr u8 s_FrameGraphQueueAssignmentModifierNoneBase = 0u;
    enum Mask : u8{
        None = s_FrameGraphQueueAssignmentModifierNoneBase,
        DirectDependencyAffinity = 1u << 0u,
        SameClassLoadBalance = 1u << 1u,
        NonPrimaryRouting = 1u << 2u,
        DiagnosticTimingQueueOverride = 1u << 3u,
        TimingCalibration = 1u << 4u,
        TimingFeedback = 1u << 5u,
        DiagnosticQueueOverride = 1u << 6u,

        All = DirectDependencyAffinity
            | SameClassLoadBalance
            | NonPrimaryRouting
            | DiagnosticTimingQueueOverride
            | TimingCalibration
            | TimingFeedback
            | DiagnosticQueueOverride,
    };
};

namespace FrameGraphQueueAssignmentAcceptance{
    static constexpr u8 s_FrameGraphQueueAssignmentAcceptanceNotAcceptedBase = 0u;
    static constexpr u8 s_FrameGraphQueueAssignmentAcceptanceCountValue = 4u;
    enum Enum : u8{
        NotAccepted = s_FrameGraphQueueAssignmentAcceptanceNotAcceptedBase,
        First,
        Unchanged,
        Changed,

        kCount = s_FrameGraphQueueAssignmentAcceptanceCountValue,
    };
};

namespace FrameGraphTaskPacketizationDecision{
    static constexpr u8 s_FrameGraphTaskPacketizationDecisionUnknownBase = 0u;
    static constexpr u8 s_FrameGraphTaskPacketizationDecisionCountValue = 12u;
    enum Enum : u8{
        Unknown = s_FrameGraphTaskPacketizationDecisionUnknownBase,
        FirstTask,
        MergeNotRequested,
        TaskForcesBoundary,
        QueueChanged,
        PrecedingTaskForcesBoundary,
        ScoredMergeIneligible,
        MergeRequiresExplicitImmediateDependency,
        CrossQueueConsumerFrontier,
        MergedExplicit,
        MergedFrontierScored,
        ScoredMergeDomainMismatch,

        kCount = s_FrameGraphTaskPacketizationDecisionCountValue,
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

struct FrameGraphCompiledTask{
    u64 planGeneration = 0u;
    u32 packetIndex = Limit<u32>::s_Max;
    FrameGraphTaskPacketizationDecision::Enum packetizationDecision = FrameGraphTaskPacketizationDecision::Unknown;
    bool present = false;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

